/* Stand-in for xnu: proves the SWEETXNU loader hand-off on the sea (MMU off, x0 = boot_args PA). */
#include <stdint.h>
#include "console.h"

struct boot_video { uint64_t base, display, rowBytes, width, height, depth; };
struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[608];
	uint64_t bootFlags, memSizeActual;
};

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }

static void delay_s(unsigned s)
{
	uint64_t f = rsr("cntfrq_el0");
	if (f < 1000000) f = 13000000;
	uint64_t t0 = rsr("cntvct_el0");
	while (rsr("cntvct_el0") - t0 < (uint64_t)s * f) ;
}

static void __attribute__((noreturn)) reboot_bootloader(void)
{
	wr32(0x10007024UL, (rd32(0x10007024UL) & ~0xfU) | 3);
	register uint64_t x0 __asm__("x0") = 0x84000009;	/* PSCI SYSTEM_RESET */
	__asm__ volatile("dsb sy; smc #0" :: "r"(x0) : "memory");
	for (;;) __asm__ volatile("wfe");
}

static int ram_ok(uintptr_t a)
{
	volatile uint64_t *p = (volatile uint64_t *)a;
	uint64_t o = p[0], pat = 0x5555aaaa5555aaaaUL ^ a;
	p[0] = pat;
	__asm__ volatile("dsb sy" ::: "memory");
	int ok = p[0] == pat;
	p[0] = o;
	return ok;
}

void fx_main(struct boot_args *ba)
{
	con_clear(0xff004000);
	con_str("FAKE XNU", "entered from the loader");
	con_skip();
	con_hex("X0", (uintptr_t)ba);
	con_hex("SCTLR", rsr("sctlr_el1"));
	con_hex("EL", rsr("currentel") >> 2);
	con_hex("PC", (uintptr_t)&fx_main);
	con_hex("REV|VER", ((uint32_t)ba->Revision << 16) | ba->Version);
	con_hex("VIRT", ba->virtBase);
	con_hex("PHYS", ba->physBase);
	con_hex("MEMSIZE", ba->memSize);
	con_hex("TOPKDATA", ba->topOfKernelData);
	con_hex("VBASE", ba->Video.base);
	con_hex("VROW", ba->Video.rowBytes);
	con_hex("VW|VH", (ba->Video.width << 16) | ba->Video.height);
	con_hex("DTREE VA", ba->deviceTreeP);
	con_hex("DTLEN", ba->deviceTreeLength);
	uintptr_t dt = ba->deviceTreeP + (ba->physBase - ba->virtBase);	/* VA -> PA */
	con_hex("DTREE PA", dt);
	/* ADT root node header: u32 nprops, u32 nchildren */
	con_hex("ROOT np", ((volatile uint32_t *)dt)[0]);
	con_hex("ROOT nc", ((volatile uint32_t *)dt)[1]);
	con_str("CMDLINE", ba->CommandLine);
	con_skip();
	con_hex("RAM LO", ram_ok(ba->physBase + 0x100000));
	con_hex("RAM TOP", ram_ok(ba->physBase + ba->memSize - 0x1000));
	con_hex("PANIC", ram_ok(ba->physBase + ba->memSize + 0x1000));
	con_str("PAYLOAD", "loader chain OK");
	con_str("NEXT", "back to fastboot in 60 s");
	delay_s(60);
	reboot_bootloader();
}
