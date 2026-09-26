#ifndef AIDE_EMULATOR_H
#define AIDE_EMULATOR_H

#include <stddef.h>

/* Locating and probing FS-UAE.
 *
 * Both answers used to be inline in qt/build/EmulatorRunner.cpp, and both were
 * wrong off macOS: the config path was picked from a per-OS subdirectory that
 * no longer exists (the machine description is generated once now, not kept in
 * three hand-edited copies), and the "already running" probe shelled out to
 * pgrep, which returned a hard false on Windows -- so Run always launched a
 * second emulator there.
 *
 * The Amiga build compiles this too. Nothing here applies when the IDE is
 * itself running on AmigaOS, so the functions degrade to honest negatives
 * rather than being #ifdef'd out of the build: see the notes on each. */

#ifdef __cplusplus
extern "C" {
#endif

/* Writes the path of an FS-UAE executable into `out` (NUL-terminated) and
 * returns 1, or returns 0 and leaves `out` empty when none is found.
 *
 * Looks in the usual per-platform install location first, then falls back to
 * PATH. On AmigaOS this always returns 0 -- there is no emulator to run when
 * you are the thing being emulated. */
int aide_emulator_find_binary(char *out, size_t out_size);

/* 1 if a process whose executable is named `exe_name` (no directory part) is
 * currently running, 0 if not or if it cannot be determined.
 *
 * Implemented with pgrep on Unix and tasklist on Windows. A 0 from "cannot
 * determine" is indistinguishable from "not running" by design: the only
 * caller uses this to decide between launching the emulator and printing
 * "already running, restart the program from the Amiga shell", and launching
 * is the safer wrong answer of the two. */
int aide_emulator_is_running(const char *exe_name);

/* Fills `out` with <toolchain_root>/fs-uae/development.fs-uae, the machine
 * description acd_gen_config.py generates. Returns 1 on success, 0 if the
 * result would not fit. The file is the same on every platform now; the paths
 * inside it are what differ, and the bootstrap bakes those in. */
int aide_emulator_default_config_path(const char *toolchain_root,
                                      char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_EMULATOR_H */
