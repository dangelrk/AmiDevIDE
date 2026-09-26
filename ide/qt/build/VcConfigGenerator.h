#pragma once

#include "ToolchainConfig.h"

class Project;

// Writes <project>/build/vc.cfg: compile-only vc profile (-cc/-as/-ul/-cf
// -ml=1000 -hunkdebug -amiga-softfloat, +-sd for smallData), mirroring
// config/aos68k* but WITHOUT the -ld/-l2 linker directives, since linking is
// done directly with vlink from the generated Makefile (plan section 6a).
namespace VcConfigGenerator {
QString generate(const ToolchainConfig &toolchain, const Project &project);

// Writes the generated text to <project>/build/vc.cfg. Returns false on I/O error.
bool writeToProject(const ToolchainConfig &toolchain, const Project &project);
}
