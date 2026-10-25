/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

/*
 * LK hands us vendor_boot's ramdisk and boot's ramdisk back to back in
 * [initrd-start, initrd-end) with no promise about where ours starts or how it is
 * aligned.  Find the SWEETXNU magic at any byte offset, then (if needed) slide the
 * payload down to an 8-byte boundary in place so the header can be read with the
 * MMU off.  Returns the aligned payload address, or 0.
 */
/* SCTLR_EL1 as LK left it; shim_main sets it before turning the I-cache on to speed up the scan. */
uint64_t loader_sctlr0 = ~0ULL;

#define MAGIC64 0x554E585445455753ULL	/* "SWEETXNU" little endian */

static uintptr_t scan_payload(uint64_t lo, uint64_t hi, uint64_t *where)
{
	volatile const uint64_t *w = (volatile const uint64_t *)(lo & ~7UL);
	uint64_t n = ((hi + 7) & ~7UL) / 8 - (lo & ~7UL) / 8;
	uint64_t prev = 0;
	for (uint64_t i = 0; i < n; i++) {
		uint64_t cur = w[i];
		if (cur == MAGIC64) { *where = (uintptr_t)&w[i]; return 1; }
		if (i) {
#define TRY(k) if (((prev >> (8 * (k))) | (cur << (64 - 8 * (k)))) == MAGIC64) { *where = (uintptr_t)&w[i - 1] + (k); return 1; }
			TRY(1) TRY(2) TRY(3) TRY(4) TRY(5) TRY(6) TRY(7)
		}
		prev = cur;
	}
	return 0;
}

/* Move [a, end) down by (a & 7) bytes using aligned words only. */
static uintptr_t realign(uintptr_t a, uint64_t end)
{
	unsigned k = a & 7;
	if (!k)
		return a;
	volatile uint64_t *w = (volatile uint64_t *)(a & ~7UL);
	uint64_t n = (end - (a & ~7UL) + 7) / 8;
	uint64_t cur = w[0];
	for (uint64_t i = 0; i < n; i++) {
		uint64_t nxt = w[i + 1];
		w[i] = (cur >> (8 * k)) | (nxt << (64 - 8 * k));
		cur = nxt;
	}
	return a & ~7UL;
}

static uintptr_t locate(uintptr_t dtb, uint64_t *rd_end_out)
{
	uint64_t lo, hi, where;
	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &lo) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &hi))
		return 0;
	con_hex("INITLO", lo);
	con_hex("INITHI", hi);
	if (!scan_payload(lo, hi, &where)) {
		con_str("SCAN", "no SWEETXNU in initrd");
		return 0;
	}
	con_hex("FOUND", where);
	con_hex("MOD8", where & 7);
	*rd_end_out = hi;
	return where;
}

int
loader_payload_present(uintptr_t dtb)
{
	uint64_t hi;
	uintptr_t a = locate(dtb, &hi);
	if (!a)
		return 0;
	const struct payload *p = (const struct payload *)realign(a, hi);
	return magic_ok(p->magic) && p->version == 1;
}

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_end;
	uintptr_t a = locate(dtb, &rd_end);
	if (!a)
		return "no SWEETXNU payload";
	uint64_t rd_start = realign(a, rd_end);
	con_hex("ALIGNED", rd_start);

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic))
		return "no SWEETXNU payload";
	if (p->version != 1 || (p->adt_off & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex("PHYSBASE", p->phys_base);
	con_hex("MEMSIZE", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex("SEGS", p->nsegs);
	con_hex("KEND", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex("BOOTARGS", ba_pa);
	con_hex("TOPKDATA", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping in 8 s (fbcon clears screen)");
	{
		uint64_t f, t0, t;
		__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
		if (f < 1000000)
			f = 13000000;
		__asm__ volatile("mrs %0, cntvct_el0" : "=r"(t0));
		do
			__asm__ volatile("mrs %0, cntvct_el0" : "=r"(t));
		while (t - t0 < 8 * f);
	}

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	if (loader_sctlr0 != ~0ULL)
		__asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(loader_sctlr0) : "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
