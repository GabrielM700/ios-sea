/*
 * sea USB DMA probe: does the MUSB at 0x11200000 have the Mentor "HSDMA" block (0x200..) and/or
 * MediaTek's QMU (0x800..), how many address bits does it have, and what is already programmed?
 *
 * Reads only, except one harmless test: ADDR/COUNT of channel 7 are written with a pattern and read back
 * (CONTROL is never touched, so the channel stays disabled), then restored.
 *   HSDMA: INTR 0x200 (8 bit), channel n at 0x200 + 16*n: CNTL +4 (16 bit), ADDR +8, COUNT +0xC
 */
#include <stdint.h>
#include "console.h"

#define MUSB 0x11200000UL
static inline uint32_t r32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint8_t r8(uintptr_t a) { return *(volatile uint8_t *)a; }
static inline void w32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static void wait_ms(unsigned ms)
{
	uint64_t f, now, end;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(now));
	end = now + (f ? f : 13000000UL) / 1000 * ms;
	do {
		__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(now));
	} while (now < end);
}

static void hexlabel(char *b, uint32_t v)
{
	static const char h[] = "0123456789ABCDEF";
	b[0] = h[(v >> 8) & 15];
	b[1] = h[(v >> 4) & 15];
	b[2] = h[v & 15];
	b[3] = 0;
}

static void dump(uintptr_t base, unsigned off, unsigned words)
{
	unsigned i;
	char lb[8];
	for (i = 0; i < words; i += 2) {
		hexlabel(lb, off + 4 * i);
		con_hex2(lb, "a=", r32(base + off + 4 * i), "b=", r32(base + off + 4 * i + 4));
	}
}

void dma_probe(void)
{
	uintptr_t ch7 = MUSB + 0x200 + 7 * 16;
	uint32_t a0, c0, a1, c1, a2, c2;

	con_clear(BLUE);
	con_str("DMA", "1/4 identity + HSDMA 200..27F");
	con_hex2("VER", "hwvers=", r32(MUSB + 0x6c) & 0xffff, "epinfo=", r8(MUSB + 0x78));
	con_hex2("RAM", "raminfo=", r8(MUSB + 0x79), "devctl=", r8(MUSB + 0x60));
	con_hex2("L1", "l1s=", r32(MUSB + 0xa0), "l1m=", r32(MUSB + 0xa4));
	dump(MUSB, 0x200, 32);
	wait_ms(25000);

	con_clear(BLUE);
	con_str("DMA", "2/4 HSDMA 280..2FF");
	dump(MUSB, 0x280, 32);
	wait_ms(25000);

	con_clear(BLUE);
	con_str("DMA", "3/4 address width (ch7)");
	a0 = r32(ch7 + 8);
	c0 = r32(ch7 + 12);
	w32(ch7 + 8, 0xFFFFFFFFU);
	w32(ch7 + 12, 0xFFFFFFFFU);
	a1 = r32(ch7 + 8);
	c1 = r32(ch7 + 12);
	w32(ch7 + 8, 0x5A5A5A5AU);
	w32(ch7 + 12, 0x00A5A5A5U);
	a2 = r32(ch7 + 8);
	c2 = r32(ch7 + 12);
	w32(ch7 + 8, a0);
	w32(ch7 + 12, c0);
	con_hex2("ADDR", "before=", a0, "ones=", a1);
	con_hex("ADDR 5A", a2);
	con_hex2("COUNT", "before=", c0, "ones=", c1);
	con_hex("COUNT A5", c2);
	con_hex2("CNTL", "ch7=", r32(ch7 + 4), "intr=", r8(MUSB + 0x200));
	wait_ms(25000);

	con_clear(BLUE);
	con_str("DMA", "4/4 QMU / MTK area 800..87F");
	dump(MUSB, 0x800, 32);
	wait_ms(25000);
}
