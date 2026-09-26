# aide_core: portable C99 logic shared by qt/ and amiga/. No Qt, no MUI.
# Built as a plain C static
# library so both the Qt build (linked in from C++) and the vbcc
# cross-build (amiga/Makefile.amiga-cross) can consume the same sources.
#
# The file list lives in core/sources.mk so that the two Amiga Makefiles can
# include the very same list -- it was written out three times before and had
# already drifted apart.
include(CoreSources)
acd_read_make_list("${CMAKE_CURRENT_SOURCE_DIR}/sources.mk"
                   AIDE_CORE_SOURCES AIDE_CORE_SOURCES)
acd_read_make_list("${CMAKE_CURRENT_SOURCE_DIR}/sources.mk"
                   AIDE_CORE_SOURCES_POSIX AIDE_CORE_PLATFORM_SOURCES)

add_library(aide_core STATIC ${AIDE_CORE_SOURCES} ${AIDE_CORE_PLATFORM_SOURCES})
target_include_directories(aide_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
set_target_properties(aide_core PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)
