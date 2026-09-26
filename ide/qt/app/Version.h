#pragma once

// The version constants live in core/ so the MUI frontend can use them too
// -- see aide_version.h. This header stays for the Qt side's existing
// includes and adds the generated build stamp, which only the CMake build
// produces.
#if defined(__has_include)
#  if __has_include("AmigaIDEBuildStamp.h")
#    include "AmigaIDEBuildStamp.h"
#  endif
#endif

#include "aide/aide_version.h"
