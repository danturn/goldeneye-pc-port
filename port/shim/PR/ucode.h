/*
 * PC port shim for PR/ucode.h.
 *
 * Pass-through to the real header, plus — because ucode.h is the LAST include
 * in <ultra64.h> (after libaudio.h, whose partial-parse poisoning is the
 * reason pc_protos.h cannot anchor earlier, e.g. in gbi.h) — the D38
 * prototype header for implicitly declared game functions.
 *
 * Inert in the N64 build (port/shim is not on its include path).
 */
#ifndef _PORT_SHIM_UCODE_H_
#define _PORT_SHIM_UCODE_H_

#if defined(PORT)
#    include "include/PR/ucode.h"
#    /* D402 (Linux build): a TU that includes <bondconstants.h> before
#     * <ultra64.h> (e.g. port/src/optionsoverlay.c) sets src/bondconstants.h's
#     * include-guard, then that header's own top `#include <ultra64.h>` reaches
#     * this pc_protos.h -> bondtypes.h -> bondconstants.h chain while
#     * src/bondconstants.h is still mid-parse (its ITEM_IDS/ACT_TYPE/... are
#     * defined only later in the file). glibc GCC rejects the half-defined
#     * types hard (MinGW tolerated it, so this only surfaced on Linux). The
#     * bondconstants.h shim sets _PORT_DEFER_PC_PROTOS around that pass so the
#     * D38 prototypes are pulled in only once, after the constants are whole. */
#    if !defined(_PORT_DEFER_PC_PROTOS)
#        include "pc_protos.h"
#    endif
#else
#    include <PR/ucode.h>
#endif

#endif /* _PORT_SHIM_UCODE_H_ */
