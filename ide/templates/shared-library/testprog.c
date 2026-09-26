/*
 * Opens the library this project builds, calls one function through it and
 * closes it again. Build the library first, copy it to LIBS: (or the
 * current directory, which exec also searches), then run this.
 *
 * This is the part that cannot be checked on the host: linking proves the
 * romtag and the vector table are well formed, but only exec actually
 * opening the library proves the init table, the negative-size
 * calculation and the LVO offsets are right.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <stdio.h>

struct Library *ExampleBase;

/* LVO -30 is the first user function after Open/Close/Expunge/Reserved.
 * Declared the way vbcc calls a library: base in a6, argument in d0. */
LONG Triple(__reg("d0") LONG n) =
    "\tmove.l\t_ExampleBase,a6\n"
    "\tjsr\t-30(a6)\n";

int main(void)
{
    LONG result;

    ExampleBase = OpenLibrary("example.library", 1);
    if (ExampleBase == NULL) {
        printf("Could not open example.library v1.\n");
        printf("Copy it to LIBS: or to the current directory first.\n");
        return 20;
    }

    result = Triple(7);
    printf("Triple(7) = %ld (expected 21)\n", result);

    CloseLibrary(ExampleBase);
    return result == 21 ? 0 : 20;
}
