/*
 * Minimal text console on the LK framebuffer (mirrored to all LK buffers). With the MMU off
 * every access is Device memory, so stay aligned (-mstrict-align).
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

/* Integers only: no absolute-pointer relocations in the position-independent image. */
#define NFB 1
static const uintptr_t fb_base[NFB] = { FB_BASE };
static uint32_t con_bg;
static int row;

void con_px(int x, int y, uint32_t c)
{
	for (int i = 0; i < NFB; i++)
		((volatile uint32_t *)fb_base[i])[y * (FB_STRIDE / 4) + x] = c;
}

void con_clear(uint32_t bg)
{
	for (int i = 0; i < NFB; i++)
		for (int y = 0; y < FB_H; y++) {
			volatile uint32_t *line = (volatile uint32_t *)fb_base[i] + y * (FB_STRIDE / 4);
			for (int x = 0; x < FB_W; x++)
				line[x] = bg;
		}
	con_bg = bg;
	row = 0;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	int x0 = MARGIN + col * CH_W, y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > FB_W || y0 + CH_H > FB_H)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			if (!(g[gy] & (0x80 >> gx)))
				continue;	/* background already cleared */
			uint32_t c = fg;
			for (int sy = 0; sy < SCALE; sy++)
				for (int sx = 0; sx < SCALE; sx++)
					con_px(x0 + gx * SCALE + sx, y0 + gy * SCALE + sy, c);
		}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}

int con_row(void)
{
	return row;
}

static int fmt_dec(char *buf, uint32_t v)
{
	char t[11];
	int n = 0, k = 0;
	do {
		t[n++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (n)
		buf[k++] = t[--n];
	buf[k] = 0;
	return k;
}

/* "name  LLLLLLLL  <decimal> MiB": GPT line, 16-char names fit in 44 columns */
void con_part(const char *name, uint32_t lba, uint32_t mib)
{
	char hx[9], dec[11];
	put_str(0, name, YELLOW);
	fmt_hex(hx, lba, 8);
	put_str(17, hx, WHITE);
	fmt_dec(dec, mib);
	int col = put_str(26, dec, WHITE);
	put_str(col + 1, "M", WHITE);
	row++;
}
