cmake_minimum_required(VERSION 3.21)
project(AmiDevIDE NONE)

# Superbuild for the whole AmigaOS cross-development environment:
#
#   toolchain/   generated -- vbcc, vasm, vlink, ACE, the SDKs, the configs
#   ide/         the IDE, Qt on the host and MUI on the Amiga
#   compilers/   the patch series for ACE BASIC; upstream is fetched
#
# Nothing here is checked in prebuilt, and no third-party source is checked
# in at all.  `cmake --build build --target acd-toolchain` goes from an empty
# checkout to a working m68k compiler.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------- options

set(ACD_TOOLCHAIN_PREFIX "${CMAKE_SOURCE_DIR}/toolchain" CACHE PATH
    "Generated toolchain root -- this is what \$VBCC points at")
set(ACD_VAR_DIR "${CMAKE_SOURCE_DIR}/var" CACHE PATH
    "Downloads, unpacked sources and emulator images")

option(ACD_BOOTSTRAP        "Fetch sources and build the toolchain"        ON)
option(ACD_BUILD_IDE_QT     "Build the Qt IDE (auto-disabled without Qt6)" ON)
option(ACD_BUILD_IDE_AMIGA  "Cross-build the MUI IDE for m68k"             OFF)
option(ACD_BUILD_TESTS      "Build and register the IDE test suite"        ON)
option(ACD_OFFLINE          "Never use the network; use var/downloads/"    OFF)

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(ACD_PY ${Python3_EXECUTABLE})
set(ACD_BOOTSTRAP_DIR "${CMAKE_SOURCE_DIR}/bootstrap")

# Every bootstrap script honours these, so an out-of-tree toolchain needs no
# further plumbing.
set(ACD_ENV
    "ACD_TOOLCHAIN=${ACD_TOOLCHAIN_PREFIX}"
    "ACD_VAR=${ACD_VAR_DIR}")

function(acd_script_target name script)
    cmake_parse_arguments(ARG "" "COMMENT" "ARGS;DEPENDS" ${ARGN})
    add_custom_target(${name}
        COMMAND ${CMAKE_COMMAND} -E env ${ACD_ENV}
                ${ACD_PY} "${ACD_BOOTSTRAP_DIR}/${script}" ${ARG_ARGS}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "${ARG_COMMENT}"
        USES_TERMINAL
        VERBATIM)
    if(ARG_DEPENDS)
        add_dependencies(${name} ${ARG_DEPENDS})
    endif()
endfunction()

# acd-sdk unpacks .lha archives, and nothing ships a reader for them. Without
# this check the gap shows only after the download and the vbcc build. Once
# the SDKs are installed no extractor is needed, so that case only warns.
find_program(ACD_LHA NAMES lha lhasa 7z 7zz)
if(NOT ACD_LHA)
    execute_process(COMMAND ${ACD_PY} -c "import lhafile"
        RESULT_VARIABLE _acd_lhafile OUTPUT_QUIET ERROR_QUIET)
    if(NOT _acd_lhafile EQUAL 0)
        set(_acd_lha_hint
            "No LHA extractor found (lha, lhasa, 7z, 7zz or the Python module "
            "lhafile for ${ACD_PY}). Install one of:\n"
            "  brew install lhasa                 (macOS)\n"
            "  sudo apt install lhasa             (Debian/Ubuntu)\n"
            "  pacman -S mingw-w64-x86_64-p7zip   (MSYS2)\n"
            "  ${ACD_PY} -m pip install lhafile\n"
            "then run cmake again.")
        if(EXISTS "${ACD_TOOLCHAIN_PREFIX}/targets/m68k-amigaos/lib"
           AND EXISTS "${ACD_TOOLCHAIN_PREFIX}/sdk/NDK_3.2"
           AND EXISTS "${ACD_TOOLCHAIN_PREFIX}/sdk/MUI_3.8")
            message(WARNING ${_acd_lha_hint}
                "\nThe SDKs are already installed, so this only matters for "
                "acd-sdk --force.")
        else()
            message(FATAL_ERROR ${_acd_lha_hint})
        endif()
    endif()
endif()

# ------------------------------------------------------------- bootstrap

if(ACD_OFFLINE)
    set(ACD_FETCH_ARGS --offline)
else()
    set(ACD_FETCH_ARGS)
endif()

acd_script_target(acd-fetch acd_fetch.py
    ARGS ${ACD_FETCH_ARGS}
    COMMENT "Fetching external sources")

acd_script_target(acd-update-check acd_fetch.py
    ARGS --check-updates
    COMMENT "Checking upstream for newer releases")

acd_script_target(acd-tools acd_build_tools.py
    COMMENT "Building vbcc, vasm and vlink from source")

acd_script_target(acd-sdk acd_install_sdk.py
    COMMENT "Installing the SDKs")

acd_script_target(acd-seed acd_seed_from_legacy.py
    COMMENT "Seeding SDKs and emulator images from a previous installation")

acd_script_target(acd-ace-src acd_ace.py
    COMMENT "Unpacking ACE BASIC and applying compilers/acebasic/patches")

acd_script_target(acd-config acd_gen_config.py
    COMMENT "Generating vc configs, setenv and the FS-UAE machine")

acd_script_target(acd-doctor acd_doctor.py
    COMMENT "Checking the toolchain")

# ACE BASIC builds with its own Makefile and installs into the same root.
# The tree is upstream plus our patches, assembled by acd-ace-src under var/.
# Passing VBCC explicitly is what makes the toolchain relocatable.
find_program(ACD_MAKE NAMES gmake make REQUIRED)

add_custom_target(acd-ace
    COMMAND ${ACD_MAKE} -f Makefile-host "VBCC=${ACD_TOOLCHAIN_PREFIX}"
    COMMAND ${ACD_MAKE} -f Makefile-host install "VBCC=${ACD_TOOLCHAIN_PREFIX}"
    WORKING_DIRECTORY "${ACD_VAR_DIR}/src/acebasic/src/make"
    COMMENT "Building and installing ACE BASIC"
    USES_TERMINAL
    VERBATIM)

# The one target that takes an empty checkout to a working compiler.  The
# steps are strictly ordered, so they are chained rather than listed.
add_dependencies(acd-tools  acd-fetch)
add_dependencies(acd-sdk    acd-fetch)
add_dependencies(acd-ace-src acd-fetch)
add_dependencies(acd-ace    acd-tools acd-ace-src)
add_dependencies(acd-config acd-sdk acd-ace)

add_custom_target(acd-toolchain
    COMMENT "Toolchain ready -- run acd-doctor to check it")
add_dependencies(acd-toolchain acd-config)

# ------------------------------------------------------------------- IDE

if(ACD_BUILD_IDE_QT)
    # Qt6's config pulls in FindThreads, which refuses to run before a
    # compiler language is enabled -- so enable them before probing, not
    # after. The project itself starts as NONE so a toolchain-only build on
    # a machine without a C++ compiler still configures.
    enable_language(C)
    enable_language(CXX)
    find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
    if(NOT Qt6_FOUND)
        message(STATUS
            "Qt6 not found -- skipping the Qt IDE. "
            "Point CMAKE_PREFIX_PATH at a Qt installation to build it.")
        set(ACD_BUILD_IDE_QT OFF)
    endif()
endif()

if(ACD_BUILD_IDE_QT)
    # At the top level too, or ctest run from the build root finds nothing:
    # ide/ registers the tests, but only the top-level project writes the
    # CTestTestfile.cmake that ctest starts from.
    if(ACD_BUILD_TESTS)
        enable_testing()
    endif()
    set(AMIGAIDE_TOOLCHAIN_ROOT "${ACD_TOOLCHAIN_PREFIX}" CACHE PATH
        "Toolchain root the IDE defaults to" FORCE)
    set(AMIGAIDE_BUILD_TESTS ${ACD_BUILD_TESTS} CACHE BOOL "" FORCE)
    add_subdirectory(ide)
endif()

# The MUI frontend is built by vbcc through its own Makefile: CMake has no
# m68k-amigaos toolchain file and does not need one for a flat, 40-object
# build with no configure step.
if(ACD_BUILD_IDE_AMIGA)
    add_custom_target(amigaide-m68k
        COMMAND ${ACD_MAKE} -f amiga/Makefile.amiga-cross app
                "VBCC=${ACD_TOOLCHAIN_PREFIX}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/ide"
        COMMENT "Cross-building the MUI IDE for m68k-amigaos"
        USES_TERMINAL
        VERBATIM)

    # Amiga archive + Aminet files (install/amiga/, tools/release/):
    # build/release/AmiDevIDE-<version>.lha/.readme for GitHub and
    # build/release/aminet-<version>/ without the version for Aminet.
    add_custom_target(amiga-release
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/release/amiga_release.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Packing the MUI IDE for Aminet"
        USES_TERMINAL
        VERBATIM)
    add_dependencies(amiga-release amigaide-m68k)
endif()

# ---------------------------------------------------------------- summary

message(STATUS "")
message(STATUS "AmiDevIDE")
message(STATUS "  toolchain root : ${ACD_TOOLCHAIN_PREFIX}")
message(STATUS "  working data   : ${ACD_VAR_DIR}")
message(STATUS "  bootstrap      : ${ACD_BOOTSTRAP}  (offline: ${ACD_OFFLINE})")
message(STATUS "  Qt IDE         : ${ACD_BUILD_IDE_QT}")
message(STATUS "  MUI IDE (m68k) : ${ACD_BUILD_IDE_AMIGA}")
message(STATUS "")
message(STATUS "  first run: cmake --build ${CMAKE_BINARY_DIR} --target acd-toolchain")
message(STATUS "")
