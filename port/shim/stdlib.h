/*
 * PC port shim for <stdlib.h>.
 *
 * The N64 decomp ships include/stdlib.h (a libc stub: lldiv_t/ldiv_t typedefs
 * + a couple of prototypes). C game code gets it, exactly as on the console.
 *
 * In C++ TUs (fast3d) it must NOT be used: MinGW's own headers pull in the
 * host <stdlib.h> through their include chains (sec_api/stdlib_s.h does
 * `#include <stdlib.h>`, which resolves to the N64 header via -I), and the
 * lldiv_t/ldiv_t typedefs then conflict with the host's. The shim routes C++
 * TUs to the host header by absolute path (generated hoststdlib.h, same
 * trick as port/shim/string.h — see docs/internals.md §11 D12).
 *
 * Inert in the N64 build (port/shim is not on its include path).
 */
#if defined(__cplusplus)
#include "hoststdlib.h"
#else
#include "include/stdlib.h"
/* D324: the N64 stub declares nothing else, and -- unlike getenv/realloc
 * below -- MinGW's leaked CRT headers do NOT declare these in the port TUs
 * that use them. An implicit `int f()` declaration makes the caller read a
 * double return from eax instead of xmm0 (silent garbage) and truncates
 * pointer/64-bit-long returns to 32 bits (latent on LP64 for strtol).
 * Declared unconditionally; K&R form is compatible with any real prototype.
 * Game TUs that never include <stdlib.h> get these from pc_protos.h. */
extern double atof();
extern double strtod();
extern long int strtol();
extern int atexit();
/* D324: getenv was previously declared only on non-Windows, on the (wrong)
 * assumption that MinGW's other CRT headers leak a prototype -- config.c gets
 * none, so its implicit `int f()` declaration truncated the pointer return to
 * 32 bits. Declare unconditionally; K&R form is compatible with any leaked or
 * real prototype. */
extern char *getenv();
#if !defined(_WIN32)
/* The N64 stub declares only lldiv_t/ldiv_t + lldiv/ldiv, and a strict host
 * GCC (Linux/macOS) leaks no CRT prototypes at all, so realloc's pointer
 * return would be assumed int and truncated (crashed configGetFrameDump).
 * (malloc/calloc/free are in port/include/pc_protos.h and a few port TUs'
 * own local decls — not repeated here to avoid conflicts.) */
extern void *realloc();
#endif
#endif
