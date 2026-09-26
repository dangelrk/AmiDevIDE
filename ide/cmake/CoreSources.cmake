# Read a variable out of core/sources.mk, the single source-file list shared
# with the two Amiga Makefiles.
#
# The file is valid GNU make: a variable assignment whose value is continued
# with trailing backslashes.  That is a small enough grammar to parse here,
# and keeping make as the canonical format means the Amiga side can simply
# `include` it rather than have anything generated for it.
#
#   acd_read_make_list(<makefile> <variable-name> <out-var>)

function(acd_read_make_list makefile varname outvar)
    if(NOT EXISTS "${makefile}")
        message(FATAL_ERROR "acd_read_make_list: no such file: ${makefile}")
    endif()

    file(STRINGS "${makefile}" lines)
    set(collected "")
    set(in_list FALSE)

    foreach(line IN LISTS lines)
        string(REGEX REPLACE "#.*$" "" line "${line}")
        string(STRIP "${line}" line)
        if(line STREQUAL "")
            continue()
        endif()

        if(NOT in_list)
            if(line MATCHES "^${varname}[ \t]*=[ \t]*(.*)$")
                set(in_list TRUE)
                set(line "${CMAKE_MATCH_1}")
            else()
                continue()
            endif()
        endif()

        set(continues FALSE)
        if(line MATCHES "\\\\$")
            set(continues TRUE)
            string(REGEX REPLACE "\\\\$" "" line "${line}")
        endif()

        string(STRIP "${line}" line)
        if(NOT line STREQUAL "")
            separate_arguments(pieces UNIX_COMMAND "${line}")
            list(APPEND collected ${pieces})
        endif()

        if(NOT continues)
            set(in_list FALSE)
        endif()
    endforeach()

    if(collected STREQUAL "")
        message(FATAL_ERROR
            "acd_read_make_list: ${varname} is empty or missing in ${makefile}")
    endif()

    set(${outvar} "${collected}" PARENT_SCOPE)
endfunction()
