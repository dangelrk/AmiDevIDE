cmake_minimum_required(VERSION 3.21)
project(AmigaIDE LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Root of the generated vbcc/vasm/vlink tree ($VBCC).  The AmiDevIDE
# superbuild passes this in; the fallback keeps `cmake -B build` inside ide/
# working on its own, for anyone who only wants the IDE.
if(NOT DEFINED AMIGAIDE_TOOLCHAIN_ROOT)
  set(AMIGAIDE_TOOLCHAIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../toolchain"
      CACHE PATH "Root of the vbcc/vasm/vlink toolchain tree")
endif()
get_filename_component(AMIGAIDE_TOOLCHAIN_ROOT "${AMIGAIDE_TOOLCHAIN_ROOT}" ABSOLUTE)

# Where ide/ itself lives.  CMAKE_SOURCE_DIR cannot be used for this: under
# the AmiDevIDE superbuild it points at the repository root, and every
# path below that used to say CMAKE_SOURCE_DIR silently resolved one level
# too high.
set(AMIGAIDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${AMIGAIDE_ROOT}/cmake")

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt" "/opt/homebrew")

# Build stamp, regenerated on every build into the build tree (never into
# the source tree, so a rebuild cannot dirty the repository). The script
# only rewrites the header when its contents actually change, so an
# unchanged stamp does not force a recompile.
set(AMIGAIDE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AMIGAIDE_GENERATED_DIR}")
add_custom_target(amigaide_buildstamp ALL
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${AMIGAIDE_ROOT}
            -DOUT=${AMIGAIDE_GENERATED_DIR}/AmigaIDEBuildStamp.h
            -P ${AMIGAIDE_ROOT}/cmake/BuildStamp.cmake
    COMMENT "Updating build stamp"
    VERBATIM)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
qt_standard_project_setup()

include(cmake/CompilerWarnings.cmake)
include(cmake/Deploy.cmake)

add_subdirectory(core)
add_subdirectory(qt)

option(AMIGAIDE_BUILD_TESTS "Build AmigaIDE Qt Test suite" ON)
if(AMIGAIDE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
