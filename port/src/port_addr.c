/*
 * port_addr.c — the N64 address-space window (see port/include/port_addr.h).
 *
 * On macOS/arm64 the whole window is reserved PROT_NONE up front so that the
 * fixed-address carves (cart, DRAM V1/V2, game-thread stacks, in dram.c /
 * romdata.c / libultra.c) land inside a range the process already owns and
 * cannot collide with libmalloc or the dyld shared cache. Reserving is cheap:
 * PROT_NONE pages are not committed.
 *
 * On Windows/Linux PORT_ADDR_BASE is 0 and this is a no-op beyond recording
 * the image base.
 */

#include <stdint.h>

#include "platform.h"
#include "system.h"
#include "port_addr.h"

#if defined(PLATFORM_MACOS) && defined(PLATFORM_ARM)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

uintptr_t g_portImageBase = 0;
int g_portUseImageRel = 0;

/* Defined by port/src/dram_syms*.s; must equal PORT_ADDR_BASE + 0x70000000. */
extern unsigned char cfb_16[];

/* ROM-free verification of the address model: window round-trips for each
 * region, the absolute-symbol base, and the image-relative encoding. Logs
 * PASS/FAIL for each so a run without a ROM still exercises the core. */
static int portAddrCheck(const char *what, int ok)
{
    sysLogPrintf(ok ? LOG_INFO : LOG_ERROR, "portAddr: %-26s %s",
                 what, ok ? "PASS" : "FAIL");
    return ok;
}

static void portAddrSelfTest(void)
{
    const uintptr_t B = (uintptr_t)PORT_ADDR_BASE;
    int ok = 1;
    ok &= portAddrCheck("host->n64 DRAM V1",
                        portHostToN64((void *)(B + 0x70001234u)) == 0x70001234u);
    ok &= portAddrCheck("n64->host DRAM V1",
                        portN64ToHost(0x70001234u) == (void *)(B + 0x70001234u));
    ok &= portAddrCheck("host->n64 DRAM V2",
                        portHostToN64((void *)(B + 0x80005678u)) == 0x80005678u);
    ok &= portAddrCheck("host->n64 cart",
                        portHostToN64((void *)(B + 0x10456789u)) == 0x10456789u);
    ok &= portAddrCheck("n64->host cart",
                        portN64ToHost(0x10456789u) == (void *)(B + 0x10456789u));
    ok &= portAddrCheck("cfb_16 at base+DRAM_V1",
                        (uintptr_t)cfb_16 == B + 0x70000000u);
    if (g_portUseImageRel) {
        ok &= portAddrCheck("host->n64 image",
                            portHostToN64((void *)(g_portImageBase + 0x1234u)) == 0x40001234u);
        ok &= portAddrCheck("n64->host image",
                            portN64ToHost(0x40001234u) == (void *)(g_portImageBase + 0x1234u));
    }
    sysLogPrintf(ok ? LOG_INFO : LOG_ERROR, "portAddr: selftest %s",
                 ok ? "ALL PASS" : "FAILURES");
}

void portAddrInit(void)
{
    g_portImageBase = sysImageBase();
    /* The image needs the 0x40000000 encoding iff it lies outside the window,
     * i.e. its address does not survive a u32 truncation. False on Linux
     * (image at 0x20000000, inside the window), true on Windows (0x140000000)
     * and macOS (0x1_00xxxxxx + slide). */
    g_portUseImageRel = (g_portImageBase >= (uintptr_t)PORT_ADDR_WINDOW) ? 1 : 0;

#if PORT_ADDR_BASE != 0
    {
        /* Non-clobbering: VM_FLAGS_FIXED without VM_FLAGS_OVERWRITE fails with
         * KERN_NO_SPACE if the range is already in use, rather than silently
         * replacing a live mapping (mmap MAP_FIXED on macOS does the latter). */
        mach_vm_address_t addr = (mach_vm_address_t)PORT_ADDR_BASE;
        kern_return_t kr = mach_vm_map(mach_task_self(), &addr,
                                       (mach_vm_size_t)PORT_ADDR_WINDOW, 0,
                                       VM_FLAGS_FIXED, MEMORY_OBJECT_NULL, 0, FALSE,
                                       VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_DEFAULT);
        if (kr != KERN_SUCCESS || addr != (mach_vm_address_t)PORT_ADDR_BASE) {
            sysFatalError("portAddr: cannot reserve the 4 GiB N64 window at "
                          "0x%llx (%s). See docs/dev/MACOS-ARM64-PLAN.md sec 2.",
                          (unsigned long long)PORT_ADDR_BASE, mach_error_string(kr));
        }
    }
#endif

    /* The committed .darwin.s absolute symbols must have been generated with
     * the same --base; cfb_16 is a region-independent sentinel for that. */
    if ((uintptr_t)cfb_16 != (uintptr_t)PORT_ADDR_BASE + 0x70000000ULL) {
        sysFatalError("portAddr: dram_syms base mismatch (cfb_16=%p, expected 0x%llx). "
                      "Regenerate with: scripts/gen_romassets.py <region> --darwin "
                      "--base 0x%llx --rom-size <size> --out ... --dram-out ...",
                      (void *)cfb_16,
                      (unsigned long long)((uintptr_t)PORT_ADDR_BASE + 0x70000000ULL),
                      (unsigned long long)PORT_ADDR_BASE);
    }

    sysLogPrintf(LOG_INFO, "portAddr: window base=0x%llx image=0x%llx image_rel=%d",
                 (unsigned long long)PORT_ADDR_BASE,
                 (unsigned long long)g_portImageBase, g_portUseImageRel);

    portAddrSelfTest();
}
