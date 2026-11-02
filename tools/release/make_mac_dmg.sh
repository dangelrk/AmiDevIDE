#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# AmigaIDE (Qt) for macOS as a .dmg: Release build of the Qt IDE alone
# (no toolchain bootstrap, no tests, no MUI build), macdeployqt (Qt
# frameworks into the bundle), ad-hoc signature (an arm64 program does not
# start without one; no Developer ID), image with a link to /Applications.
#
# The toolchain (vbcc, vasm, vlink, NDK) is not in the image -- the NDK may
# not be redistributed. The IDE finds a toolchain it is placed in
# (<toolchain>/bin/AmigaIDE.app) or takes it from Options > Settings.
#
# Usage:  tools/release/make_mac_dmg.sh   -> build/release/AmigaIDE-<version>-macOS.dmg
# Needs:  Qt 6 (Homebrew), CMake, Xcode command line tools.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build/mac-release"
STAGE="$ROOT/build/mac-dmg"
OUTDIR="$ROOT/build/release"
VERSION=$(sed -n 's/^#define AMIGAIDE_VERSION_STRING "\([^"]*\)".*/\1/p' \
          "$ROOT/ide/core/include/aide/aide_version.h")
DMG="$OUTDIR/AmigaIDE-$VERSION-macOS.dmg"

[ "$(uname -s)" = Darwin ] || { echo "macOS only" >&2; exit 1; }
command -v macdeployqt >/dev/null || PATH="/opt/homebrew/opt/qt/bin:$PATH"
command -v macdeployqt >/dev/null || { echo "macdeployqt missing (Qt 6)" >&2; exit 1; }

# 1. Release build of the Qt IDE. The toolchain prefix points into the
#    build tree: the post-build step copies the bundle there, and a
#    developer's own toolchain stays untouched. (ACD_TOOLCHAIN_PREFIX, not
#    AMIGAIDE_TOOLCHAIN_ROOT: the superbuild forces the latter from it.)
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew \
      -DACD_BOOTSTRAP=OFF -DACD_BUILD_TESTS=OFF -DAMIGAIDE_BUILD_TESTS=OFF \
      -DACD_BUILD_IDE_AMIGA=OFF -DACD_BUILD_IDE_QT=ON \
      -DACD_TOOLCHAIN_PREFIX="$BUILD/toolchain" >/dev/null
cmake --build "$BUILD" --target amigaide -j 8
APP=$(find "$BUILD" -name amigaide.app -type d -path '*/ide/qt/*' | head -1)
[ -n "$APP" ] || { echo "amigaide.app not found in $BUILD" >&2; exit 1; }

# 2. own copy for the image, Qt into it, signed
rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUTDIR"
cp -R "$APP" "$STAGE/AmigaIDE.app"
macdeployqt "$STAGE/AmigaIDE.app" -always-overwrite 2>&1 | grep -v "QtPdf\|QtSvg\|using QList" || true
rm -f "$STAGE/AmigaIDE.app/Contents/PlugIns/imageformats/libqsvg.dylib" \
      "$STAGE/AmigaIDE.app/Contents/PlugIns/imageformats/libqpdf.dylib" \
      "$STAGE/AmigaIDE.app/Contents/PlugIns/iconengines/libqsvgicon.dylib"
rmdir "$STAGE/AmigaIDE.app/Contents/PlugIns/iconengines" 2>/dev/null || true
# macdeployqt copies indirect dependencies (libbrotlicommon via
# libbrotlidec) but leaves their own install name pointing to Homebrew
for lib in "$STAGE/AmigaIDE.app/Contents/Frameworks/"*.dylib; do
    [ -f "$lib" ] || continue
    case $(otool -D "$lib" | sed -n 2p) in
        /opt/homebrew/*|/usr/local/*)
            install_name_tool -id "@executable_path/../Frameworks/$(basename "$lib")" \
                "$lib" 2>/dev/null ;;
    esac
done
# nothing may point to Homebrew any more (it would not start on a Mac
# without it)
if find "$STAGE/AmigaIDE.app" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
     xargs -0 otool -L 2>/dev/null | grep -q '/opt/homebrew\|/usr/local/opt'; then
    echo "bundle still refers to Homebrew:" >&2
    find "$STAGE/AmigaIDE.app" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
        xargs -0 otool -L 2>/dev/null | grep '/opt/homebrew\|/usr/local/opt' >&2
    exit 1
fi
codesign --force --deep --sign - "$STAGE/AmigaIDE.app"
codesign --verify --deep --strict "$STAGE/AmigaIDE.app"

# 3. image: program, link to Applications, licence
ln -s /Applications "$STAGE/Programme"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.md"
rm -f "$DMG"
hdiutil create -volname "AmigaIDE $VERSION" -srcfolder "$STAGE" -ov -format UDZO \
    -fs HFS+ "$DMG" >/dev/null
rm -rf "$STAGE"
echo "$DMG"
