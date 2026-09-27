#!/usr/bin/env bash
# All-in-one host install: INSTALL.md section 6.2 to 6.8 in one go.
#
# Checks the host prerequisites, finds Qt 6, configures with the Qt IDE and
# the MUI IDE enabled, builds the toolchain, runs acd-doctor, builds and tests
# the Qt IDE and cross-builds the MUI IDE. The emulator is left out.
#
# Every step is idempotent, so after fixing a failure just run it again.
# macOS is the verified host; on Linux it runs the same steps but installs
# nothing itself.
#
# Written for the bash 3.2 that macOS ships: no associative arrays, no
# mapfile, no ${var,,}.

set -u -o pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
LOGDIR="$ROOT/var/log/install"

QT_PREFIX=""
ASSUME_YES=0

usage() {
    cat <<EOF
Aufruf: ./install.sh [--qt PFAD] [--yes]

Richtet AmiDevIDE auf dem Host komplett ein (INSTALL.md §6.2 bis §6.8):
Toolchain, Prüfung, Qt-IDE mit Tests und die MUI-IDE für den Amiga.
Der Emulator (§7) gehört nicht dazu.

  --qt PFAD   Qt-6-Installation, z. B. ~/Qt/6.11.1/macos. Ohne Angabe wird
              gesucht und bei Bedarf nachgefragt.
  --yes       fehlende Pakete ohne Rückfrage per Homebrew installieren
  --help      diese Hilfe

Protokolle der einzelnen Schritte: var/log/install/
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --qt)      [ $# -ge 2 ] || { echo "--qt braucht einen Pfad" >&2; exit 2; }
                   QT_PREFIX="$2"; shift 2 ;;
        --qt=*)    QT_PREFIX="${1#--qt=}"; shift ;;
        --yes|-y)  ASSUME_YES=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *)         echo "Unbekannte Option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# ------------------------------------------------------------------ output

if [ -t 1 ]; then
    C_BOLD=$'\033[1m'; C_RED=$'\033[31m'; C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'; C_OFF=$'\033[0m'
else
    C_BOLD=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_OFF=""
fi

STEP=0
STEPS=8
step()  { STEP=$((STEP + 1)); printf '\n%s[%d/%d] %s%s\n' "$C_BOLD" "$STEP" "$STEPS" "$1" "$C_OFF"; }
ok()    { printf '    %sOK%s  %s\n' "$C_GREEN" "$C_OFF" "$1"; }
note()  { printf '    %s\n' "$1"; }
warn()  { printf '    %s!%s   %s\n' "$C_YELLOW" "$C_OFF" "$1"; }

# die MESSAGE [HINT_LINE...] -- stop with a message and what to do about it
die() {
    printf '\n%sFEHLER:%s %s\n' "$C_RED" "$C_OFF" "$1" >&2
    shift
    for line in "$@"; do printf '        %s\n' "$line" >&2; done
    printf '\nNach der Behebung einfach ./install.sh erneut starten;\n' >&2
    printf 'bereits erledigte Schritte laufen dann schnell durch.\n' >&2
    exit 1
}

interactive() { [ -t 0 ]; }

# ask QUESTION -> 0 for yes. --yes answers yes; without a terminal, no.
ask() {
    [ "$ASSUME_YES" = 1 ] && return 0
    interactive || return 1
    local answer
    read -r -p "    $1 [J/n] " answer
    case "$answer" in ""|j|J|ja|Ja|y|Y|yes) return 0 ;; *) return 1 ;; esac
}

# run LOGNAME CMD... -- show the output and keep it in var/log/install/
run() {
    local log="$LOGDIR/$1.log"
    shift
    mkdir -p "$LOGDIR"
    printf '    $ %s\n' "$*"
    "$@" 2>&1 | tee "$log"
    return "${PIPESTATUS[0]}"
}

# version_ge HAVE NEED -- dotted numeric comparison
version_ge() {
    local IFS=.
    # shellcheck disable=SC2206
    local a=($1) b=($2) i
    for i in 0 1 2; do
        local x="${a[$i]:-0}" y="${b[$i]:-0}"
        x="${x%%[!0-9]*}"; y="${y%%[!0-9]*}"
        [ "${x:-0}" -gt "${y:-0}" ] && return 0
        [ "${x:-0}" -lt "${y:-0}" ] && return 1
    done
    return 0
}

have() { command -v "$1" >/dev/null 2>&1; }

OS="$(uname -s)"
case "$OS" in
    Darwin) JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ;;
    *)      JOBS="$(nproc 2>/dev/null || echo 4)" ;;
esac

# ------------------------------------------------------- 1. prerequisites

check_prerequisites() {
    step "Voraussetzungen prüfen"

    case "$ROOT" in
        *" "*) die "Der Pfad des Repositorys enthält ein Leerzeichen: $ROOT" \
                   "vc ruft seine Tools direkt auf, nicht über eine Shell (INSTALL.md §6.1)." \
                   "Klone AmiDevIDE in einen Pfad ohne Leerzeichen." ;;
    esac

    if [ "$OS" = Darwin ]; then
        if ! xcode-select -p >/dev/null 2>&1; then
            die "Die Xcode Command Line Tools fehlen (clang, make, git)." \
                "Installieren mit:  xcode-select --install" \
                "Das öffnet einen Systemdialog; danach dieses Skript erneut starten."
        fi
        ok "Xcode Command Line Tools"
    fi

    # Collect what is missing, then install it in one go.
    local missing="" hints=""

    local cmake_ver=""
    if have cmake; then
        cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
    fi
    if [ -n "$cmake_ver" ] && version_ge "$cmake_ver" 3.21; then
        ok "CMake $cmake_ver"
    else
        [ -n "$cmake_ver" ] && warn "CMake $cmake_ver ist zu alt, gebraucht wird 3.21"
        missing="$missing cmake"
        hints="$hints|CMake >= 3.21:  sudo apt install cmake  (bzw. dnf/pacman)"
    fi

    local py_ver=""
    if have python3; then
        py_ver="$(python3 -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])' 2>/dev/null)"
    fi
    if [ -n "$py_ver" ] && version_ge "$py_ver" 3.11; then
        ok "Python $py_ver"
    else
        [ -n "$py_ver" ] && warn "Python $py_ver ist zu alt, gebraucht wird 3.11 (tomllib)"
        missing="$missing python"
        hints="$hints|Python >= 3.11:  sudo apt install python3"
    fi

    local tool
    for tool in git make cc; do
        if have "$tool"; then
            ok "$tool"
        else
            missing="$missing $tool"
            hints="$hints|$tool:  sudo apt install build-essential git"
        fi
    done

    if have curl || have wget; then
        ok "curl/wget"
    else
        missing="$missing curl"
        hints="$hints|curl:  sudo apt install curl"
    fi

    local lha=""
    for tool in lha lhasa 7z 7zz; do
        if have "$tool"; then lha="$tool"; break; fi
    done
    if [ -z "$lha" ] && have python3 && python3 -c "import lhafile" 2>/dev/null; then
        lha="Python-Modul lhafile"
    fi
    if [ -n "$lha" ]; then
        ok "LHA-Entpacker ($lha)"
    else
        missing="$missing lhasa"
        hints="$hints|LHA-Entpacker:  sudo apt install lhasa"
    fi

    [ -z "$missing" ] && return 0

    note ""
    warn "Es fehlt:$missing"

    if [ "$OS" != Darwin ]; then
        local IFS='|'
        # shellcheck disable=SC2086
        die "Fehlende Voraussetzungen, bitte installieren:" $hints
    fi

    if ! have brew; then
        die "Es fehlt:$missing -- und Homebrew, um es zu installieren." \
            "Homebrew installieren (https://brew.sh):" \
            '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"' \
            "Danach dieses Skript erneut starten; es installiert den Rest selbst."
    fi

    # Homebrew formula names for what is missing. git, make and cc come with
    # the Command Line Tools, checked above, so they cannot be missing here.
    local formulae="" m
    for m in $missing; do
        case "$m" in
            cmake)  formulae="$formulae cmake" ;;
            python) formulae="$formulae python@3.12" ;;
            lhasa)  formulae="$formulae lhasa" ;;
            curl)   formulae="$formulae curl" ;;
        esac
    done

    if ! ask "Mit Homebrew installieren:$formulae?"; then
        die "Fehlende Voraussetzungen:$missing" \
            "Installieren mit:  brew install$formulae" \
            "oder dieses Skript mit --yes starten."
    fi
    # shellcheck disable=SC2086
    run brew-install brew install $formulae \
        || die "brew install$formulae ist fehlgeschlagen." \
               "Protokoll: var/log/install/brew-install.log"
    hash -r
    ok "installiert:$formulae"
}

# ------------------------------------------------------------------- 2. Qt

# qt_prefix_of PATH -> the prefix that holds lib/cmake/Qt6, or nothing.
# Accepts the prefix itself, its bin/qmake, or the lib/cmake/Qt6 directory.
qt_prefix_of() {
    local p="${1/#\~/$HOME}"
    p="${p%/}"
    [ -n "$p" ] || return 1
    case "$p" in
        */bin/qmake|*/bin/qmake6) p="$(dirname "$(dirname "$p")")" ;;
        */lib/cmake/Qt6)          p="$(dirname "$(dirname "$(dirname "$p")")")" ;;
    esac
    [ -f "$p/lib/cmake/Qt6/Qt6Config.cmake" ] || return 1
    (cd "$p" && pwd)
}

# qt_missing_modules PREFIX -> the Qt modules this build needs that are absent
qt_missing_modules() {
    local m out=""
    for m in Widgets Concurrent Test; do
        [ -f "$1/lib/cmake/Qt6$m/Qt6${m}Config.cmake" ] || out="$out Qt6$m"
    done
    printf '%s' "$out"
}

find_qt() {
    local c

    # 1. an earlier run of this script or of cmake -B build
    if [ -f "$BUILD/CMakeCache.txt" ]; then
        c="$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' "$BUILD/CMakeCache.txt" | head -1)"
        qt_prefix_of "$c" && return 0
        c="$(sed -n 's/^Qt6_DIR:[A-Z]*=//p' "$BUILD/CMakeCache.txt" | head -1)"
        qt_prefix_of "$c" && return 0
    fi

    # 2. the environment
    for c in "${QTDIR:-}" "${Qt6_DIR:-}" "${CMAKE_PREFIX_PATH:-}"; do
        qt_prefix_of "$c" && return 0
    done

    # 3. the Qt online installer, newest version first
    if [ -d "$HOME/Qt" ]; then
        local sub=macos
        [ "$OS" = Darwin ] || sub=gcc_64
        for c in $(ls -d "$HOME"/Qt/6.*/"$sub" 2>/dev/null \
                   | awk -F/ '{print $(NF-1) "\t" $0}' \
                   | sort -t. -k1,1nr -k2,2nr -k3,3nr | cut -f2); do
            qt_prefix_of "$c" && return 0
        done
    fi

    # 4. Homebrew
    if have brew; then
        c="$(brew --prefix qt 2>/dev/null)"
        qt_prefix_of "$c" && return 0
    fi

    # 5. a qmake in PATH
    for c in qmake6 qmake; do
        if have "$c"; then
            qt_prefix_of "$("$c" -query QT_INSTALL_PREFIX 2>/dev/null)" && return 0
        fi
    done
    return 1
}

select_qt() {
    step "Qt 6 suchen"

    local prefix=""
    if [ -n "$QT_PREFIX" ]; then
        prefix="$(qt_prefix_of "$QT_PREFIX")" \
            || die "Unter --qt $QT_PREFIX liegt kein Qt 6 (lib/cmake/Qt6/Qt6Config.cmake fehlt)." \
                   "Gemeint ist das Verzeichnis mit bin/ und lib/, z. B. ~/Qt/6.11.1/macos."
    else
        prefix="$(find_qt)" || prefix=""
    fi

    if [ -z "$prefix" ]; then
        if ! interactive; then
            die "Qt 6 wurde nicht gefunden." \
                "Pfad angeben:  ./install.sh --qt ~/Qt/6.11.1/macos" \
                "Qt installieren: https://www.qt.io/download-qt-installer-oss" \
                "                 oder: brew install qt   (ungeprüft)"
        fi
        warn "Qt 6 wurde nicht automatisch gefunden."
        note "Gib das Qt-Verzeichnis an, das bin/ und lib/ enthält,"
        note "z. B. ~/Qt/6.11.1/macos. Leere Eingabe bricht ab."
    fi
    while [ -z "$prefix" ]; do
        local answer
        read -r -p "    Qt-Pfad: " answer
        [ -n "$answer" ] || die "Abgebrochen: ohne Qt 6 lässt sich die Qt-IDE nicht bauen." \
                                "Qt installieren: https://www.qt.io/download-qt-installer-oss"
        prefix="$(qt_prefix_of "$answer")" \
            || { warn "Dort liegt kein Qt 6 (lib/cmake/Qt6/Qt6Config.cmake fehlt)."; prefix=""; }
    done

    local absent
    absent="$(qt_missing_modules "$prefix")"
    [ -z "$absent" ] || die "Dem Qt unter $prefix fehlen Module:$absent" \
                            "Im Qt Maintenance Tool nachinstallieren (Qt 6 → Desktop)."

    local ver
    ver="$(sed -n 's/.*PACKAGE_VERSION "\([0-9.]*\)".*/\1/p' \
             "$prefix/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake" 2>/dev/null | head -1)"
    ok "Qt ${ver:-6} in $prefix"
    QT_PREFIX="$prefix"
}

# ------------------------------------------------------------- 3. configure

configure() {
    step "Konfigurieren (Qt-IDE, MUI-IDE, Tests)"
    run configure cmake -S "$ROOT" -B "$BUILD" \
        "-DCMAKE_PREFIX_PATH=$QT_PREFIX" \
        -DACD_BUILD_IDE_QT=ON -DACD_BUILD_IDE_AMIGA=ON -DACD_BUILD_TESTS=ON \
        || die "cmake -B build ist fehlgeschlagen." \
               "Die Meldung steht direkt darüber, das Protokoll in var/log/install/configure.log."
    # The top-level CMakeLists switches the Qt IDE off quietly if find_package
    # fails, so check what it settled on rather than trusting -D.
    grep -q "Qt IDE *: ON" "$LOGDIR/configure.log" \
        || die "CMake hat die Qt-IDE abgeschaltet: Qt 6 unter $QT_PREFIX wurde nicht angenommen." \
               "Siehe var/log/install/configure.log; ggf. mit --qt einen anderen Pfad angeben."
    ok "build/ konfiguriert"
}

# -------------------------------------------------------------- 4.-8. build

build_toolchain() {
    step "Toolchain bauen (vbcc, vasm, vlink, SDKs, ACE BASIC)"
    run toolchain cmake --build "$BUILD" --target acd-toolchain \
        || die "acd-toolchain ist fehlgeschlagen." \
               "Protokoll: var/log/install/toolchain.log" \
               "Scheitert ein Download, lassen sich die Archive von Hand nach" \
               "var/downloads/ legen (docs/SOURCES.html, INSTALL.md §6.4)."
    ok "Toolchain in toolchain/"
}

run_doctor() {
    step "Toolchain prüfen (acd-doctor)"
    run doctor cmake --build "$BUILD" --target acd-doctor \
        || die "acd-doctor meldet Fehler." \
               "Die Zeilen mit ERR darüber nennen das Problem; Protokoll: var/log/install/doctor.log"
    ok "Toolchain ist benutzbar"
}

build_qt_ide() {
    step "Qt-IDE bauen"
    run qt-ide cmake --build "$BUILD" -j "$JOBS" \
        || die "Der Build der Qt-IDE ist fehlgeschlagen." \
               "Protokoll: var/log/install/qt-ide.log"
    ok "Qt-IDE gebaut"
}

run_tests() {
    step "Tests"
    run tests ctest --test-dir "$BUILD" --output-on-failure \
        || die "Nicht alle Tests sind bestanden." \
               "Die fehlgeschlagenen stehen darüber; Protokoll: var/log/install/tests.log"
    ok "alle Tests bestanden"
}

build_mui_ide() {
    step "MUI-IDE für den Amiga cross-kompilieren"
    run mui-ide cmake --build "$BUILD" --target amigaide-m68k \
        || die "Der Cross-Build der MUI-IDE ist fehlgeschlagen." \
               "Protokoll: var/log/install/mui-ide.log"
    local bin="$ROOT/ide/amiga/bin/amigaide"
    file "$bin" 2>/dev/null | grep -q "AmigaOS loadseg" \
        || die "ide/amiga/bin/amigaide ist kein AmigaOS-Programm." \
               "file ide/amiga/bin/amigaide sollte 'AmigaOS loadseg()ble executable' melden."
    ok "ide/amiga/bin/amigaide"
}

summary() {
    printf '\n%sFertig.%s\n\n' "$C_GREEN$C_BOLD" "$C_OFF"
    cat <<EOF
  Qt-IDE starten:      toolchain/bin/amigaide
  Kommandozeile:       source toolchain/setenv.sh
                       vc +aos68k -c99 -O2 hello.c -o hello
                       bas hello.b
  MUI-IDE (Amiga):     ide/amiga/bin/amigaide
  Protokolle:          var/log/install/

  Nicht eingerichtet: der Emulator (INSTALL.md §7) und die SDKs ohne
  Download-Adresse (regex, Roadshow, AmiSSL, SQLite3, PosixLib; §5).
EOF
}

cd "$ROOT" || exit 1
printf '%sAmiDevIDE-Installation%s  (%s, %s)\n' "$C_BOLD" "$C_OFF" "$ROOT" "$OS"
[ "$OS" = Darwin ] || warn "Nur macOS ist end-to-end geprüft; auf $OS installiert das Skript nichts selbst."

check_prerequisites
select_qt
configure
build_toolchain
run_doctor
build_qt_ide
run_tests
build_mui_ide
summary
