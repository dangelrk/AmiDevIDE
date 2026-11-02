# The one list of core/ sources, shared by all three build systems:
#
#   core/CMakeLists.txt        parses it (see cmake/CoreSources.cmake)
#   amiga/Makefile.amiga-cross includes it
#   amiga/Makefile.amiga       includes it
#
# It used to be written out three times, and had already drifted: the native
# Amiga Makefile was still missing aide_basicparser.c and aide_basic_keywords.c
# months after ACE BASIC support landed, because adding a file meant
# remembering three places.  Add a source here and every build picks it up.
#
# Paths are relative to core/.  Keep one file per line, in build order, with a
# trailing backslash on every line but the last -- CMake's parser is
# deliberately simple and this file has to stay valid GNU make syntax.

AIDE_CORE_SOURCES = \
	vendor/parson.c \
	src/aide_str.c \
	src/aide_regex_common.c \
	src/aide_hash.c \
	src/aide_symbol.c \
	src/aide_issue.c \
	src/aide_diag.c \
	src/aide_project.c \
	src/aide_sdk.c \
	src/aide_toolchain.c \
	src/aide_vccfg.c \
	src/aide_makegen.c \
	src/aide_logtail.c \
	src/aide_hunk.c \
	src/aide_disasm.c \
	src/aide_icon.c \
	src/aide_structlayout.c \
	src/aide_cparser.c \
	src/aide_asmparser.c \
	src/aide_basicparser.c \
	src/aide_basic_keywords.c \
	src/aide_includegraph.c \
	src/aide_sfd.c \
	src/aide_autodoc.c \
	src/aide_search.c \
	src/aide_theme.c \
	src/aide_keywords.c \
	src/aide_reindent.c \
	src/aide_emulator.c \
	src/aide_vcs.c

# Platform backends.  Exactly one set is compiled; the choice is made by the
# build system, not at runtime -- see aide_fs.h and aide_regex.h.
AIDE_CORE_SOURCES_POSIX = \
	platform/aide_regex_posix.c \
	platform/aide_fs_posix.c

# The Amiga backends live under amiga/src/mui/ rather than core/platform/,
# so they are listed by the Amiga Makefiles themselves, not here.
