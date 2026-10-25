#pragma once
#include <stdint.h>

/* sea: scanned buffer = LK framebuffer frame 1 (pitch 4320); mapped Normal non-cacheable, page granular */
#define FB_LK_LO	0x7c606000UL
#define FB_LK_HI	0x7d000000UL

struct mmu_info {
	uint64_t img_lo, img_hi, ttbr;
	uint64_t sctlr_before, sctlr_after;
	int n_l3;
};

int mmu_enable(struct mmu_info *info);
uint64_t mmu_probe(uintptr_t va);
