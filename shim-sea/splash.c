/* Draw the boot card (black screen, picture + text centred) on the LK framebuffer, MMU off. */
#include <stdint.h>
#include "console.h"

extern const uint8_t splash_bin[], splash_bin_end[];

void splash_draw(uintptr_t fb, uint32_t stride, uint32_t fw, uint32_t fh)
{
	const uint32_t *hdr = (const uint32_t *)splash_bin;
	uint32_t w = hdr[0], h = hdr[1];

	if (!w || !h || (w & 1) || w > fw || h > fh ||
	    (uint64_t)(splash_bin_end - splash_bin) < 8 + (uint64_t)w * h * 4)
		return;
	volatile uint64_t *p = (volatile uint64_t *)fb;
	for (uint64_t i = 0; i < (uint64_t)(stride / 8) * fh; i++)
		p[i] = 0xff000000ff000000ULL;			/* opaque black */
	uint32_t x0 = ((fw - w) / 2) & ~1U, y0 = (fh - h) / 2;
	const uint64_t *src = (const uint64_t *)(splash_bin + 8);
	for (uint32_t y = 0; y < h; y++) {
		volatile uint64_t *d = (volatile uint64_t *)(fb + (uint64_t)(y0 + y) * stride + x0 * 4UL);
		for (uint32_t x = 0; x < w / 2; x++)
			d[x] = src[(uint64_t)y * (w / 2) + x];
	}
}
