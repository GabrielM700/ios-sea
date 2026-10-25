#pragma once
#include <stdint.h>

/*
 * sea, from the probe13 binary (which drew readable text): the scanned buffer is
 * LK "framebuffer" frame 1 at 0x7c606000 and the pitch is 4320 B (1080 px * 4),
 * NOT the 4352 B (1088 px) of the fleur sibling.  Frames 0 and 2 are not shown.
 */
#define FB_BASE		0x7c606000UL
#define FB_W		1080
#define FB_H		2400
#define FB_STRIDE	(1080 * 4)	/* a8r8g8b8 */

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

/* single pixel, mirrored to every frame buffer */
void con_px(int x, int y, uint32_t c);

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
int con_row(void);
/* GPT line: name, first LBA (hex), size in MiB (decimal) */
void con_part(const char *name, uint32_t lba, uint32_t mib);
