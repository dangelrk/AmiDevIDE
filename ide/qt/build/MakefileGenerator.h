#pragma once

#include "ToolchainConfig.h"

#include "aide/aide_makegen.h"

class Project;

// Generates <project>/build/Makefile: a complete, self-standing GNU Makefile
// (plan section 6b) driven purely by the project's active SDKs (via
// SdkRegistry) and linker/compiler settings -- no hardcoded SDK paths outside
// the registry. Also works headless: `make -f build/Makefile VBCC=<root>`.
namespace MakefileGenerator {
QString generate(const ToolchainConfig &toolchain, const Project &project);
bool writeToProject(const ToolchainConfig &toolchain, const Project &project);

// Test-only: generate() always emits aide_makegen_host_dialect(), which is
// always AIDE_MAKE_DIALECT_POSIX on this (non-m68k) host -- there is no other
// way to golden-test the AmigaDOS dialect without real Amiga hardware. See
// tests/tst_makefilegenerator.cpp.
QString generateForDialect(const ToolchainConfig &toolchain, const Project &project, AideMakeDialect dialect);

// Absolute path of the binary the generated Makefile will produce.
QString targetBinaryPath(const Project &project);

// True if the project needs a generated build/__stack.c for its stack size
// (CLI + standard startup.o only; WB programs get their stack from the icon,
// other startups don't support the __stack mechanism -- see the vbcc manual, section 14.5.3).
bool needsStackObject(const Project &project);
QString stackObjectSource(int stackSize);
}
