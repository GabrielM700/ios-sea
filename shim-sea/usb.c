/*
 * sea USB probe 2 (probe 1: PHY init alone gave no RESET; DEVCTL stayed 0x80 = no VBUS/session).  Everything is printed BEFORE the access that could hang the bus
 * (clock-gated MUSB), so the last line on the photo says where it stopped.
 * Phase A: read-only dump.  B: soft-connect without touching the PHY.
 * C: Linux tphy init+power_on sequence, then soft-connect.  B and C log RESET and SETUP packets.
 */
#include <stdint.h>
#include "console.h"
#include "usb.h"

#define MUSB	0x11200000UL
#define PHY	0x11ca0300UL
#define INFRA	0x10001000UL

static inline uint8_t  r8(uintptr_t a)  { return *(volatile uint8_t *)a; }
static inline uint16_t r16(uintptr_t a) { return *(volatile uint16_t *)a; }
static inline uint32_t r32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void w8(uintptr_t a, uint8_t v)   { *(volatile uint8_t *)a = v; }
static inline void w16(uintptr_t a, uint16_t v) { *(volatile uint16_t *)a = v; }
static inline void w32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static inline uint64_t cnt(void) { uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline uint64_t frq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }

static void set32(uintptr_t a, uint32_t m) { w32(a, r32(a) | m); }
static void clr32(uintptr_t a, uint32_t m) { w32(a, r32(a) & ~m); }

static void dump_phy(const char *tag)
{
	con_str("PHY", tag);
	con_hex2("ACR0/1", "00=", r32(PHY + 0x00), "04=", r32(PHY + 0x04));
	con_hex2("ACR2/5", "08=", r32(PHY + 0x08), "14=", r32(PHY + 0x14));
	con_hex2("ACR6/A4", "18=", r32(PHY + 0x18), "20=", r32(PHY + 0x20));
	con_hex2("DCR0/DTM0", "60=", r32(PHY + 0x60), "68=", r32(PHY + 0x68));
	con_hex2("DTM1/BC", "6c=", r32(PHY + 0x6c), "80=", r32(PHY + 0x80));
}

static void dump_musb(const char *tag)
{
	con_str("MUSB", tag);
	con_hex2("PWR/DEV", "power=", r8(MUSB + 0x01), "devctl=", r8(MUSB + 0x60));
	con_hex2("FADDR/INDEX", "faddr=", r8(MUSB + 0x00), "index=", r8(MUSB + 0x0e));
	con_hex2("IE", "txe=", r16(MUSB + 0x06), "rxe=", r16(MUSB + 0x08));
	con_hex2("USBE/INFO", "usbe=", r8(MUSB + 0x0b), "epinfo=", r8(MUSB + 0x78));
	con_hex2("RAM/L1", "raminfo=", r8(MUSB + 0x79), "l1m=", r32(MUSB + 0xa4));
	con_hex2("L1S/CSR0", "l1s=", r32(MUSB + 0xa0), "csr0=", r16(MUSB + 0x102));
}

static void phy_init_power_on(void)
{
	/* u2_phy_instance_init */
	clr32(PHY + 0x68, (1u << 26) | (1u << 18));			/* FORCE_UART_EN | FORCE_SUSPENDM */
	clr32(PHY + 0x68, (3u << 4) | (0xfu << 10) |
	      (1u << 23) | (1u << 21) | (1u << 20) | (1u << 19) | (1u << 17) |
	      (1u << 7) | (1u << 6) | (1u << 2));			/* XCVRSEL, DATAIN, PART_MASK */
	clr32(PHY + 0x6c, 1u << 16);					/* RG_UART_EN */
	set32(PHY + 0x00, 1u << 5);					/* USB20_INTR_EN */
	clr32(PHY + 0x14, 1u << 11);					/* HS_100U_U3_EN */
	clr32(PHY + 0x20, (1u << 9) | (1u << 8));			/* GPIO_CTR */
	clr32(PHY + 0x18, 1u << 23);					/* BC11_SW_EN */
	{ uint32_t t = r32(PHY + 0x18); t = (t & ~0xfu) | 2; w32(PHY + 0x18, t); }	/* SQTH=2 */
	/* u2_phy_instance_power_on */
	set32(PHY + 0x18, 1u << 20);					/* OTG_VBUSCMP_EN */
	set32(PHY + 0x6c, (1u << 5) | (1u << 2));			/* VBUSVALID | AVALID */
	clr32(PHY + 0x6c, 1u << 4);					/* SESSEND */
	/* set_mode(DEVICE) */
	set32(PHY + 0x6c, (1u << 9) | (1u << 1));			/* FORCE_IDDIG | IDDIG */
}


/* VBUS is not wired to the PHY on sea (charger IC owns it): force the UTMI valid signals, like the vendor kernel. */
static void phy_force_vbus(void)
{
	uint32_t t = r32(PHY + 0x6c);
	t |= (1u << 13) | (1u << 12) | (1u << 11) | (1u << 10) | (1u << 9);	/* FORCE_VBUSVALID/SESSEND/BVALID/AVALID/IDDIG */
	t |= (1u << 5) | (1u << 3) | (1u << 2) | (1u << 1);			/* VBUSVALID, BVALID, AVALID, IDDIG(device) */
	t &= ~(1u << 4);							/* SESSEND = 0 */
	w32(PHY + 0x6c, t);
}

static void phy_pll_force(void)
{
	set32(PHY + 0x00, 1u << 15);	/* ACR0 U2PLL_FORCE_ON */
	set32(PHY + 0x08, 1u << 18);	/* ACR2 SIF_U2PLL_FORCE_EN */
	set32(PHY + 0x60, 1u << 24);	/* DCR0 SIF_U2PLL_FORCE_ON */
}

static void hold(unsigned ms)
{
	uint64_t end = cnt() + frq() * ms / 1000;
	while (cnt() < end)
		;
}

static void musb_connect(void)
{
	w16(MUSB + 0x86, 0xffff);		/* TXTOGEN */
	w16(MUSB + 0x82, 0xffff);		/* RXTOGEN */
	w8(MUSB + 0x00, 0);			/* FADDR */
	w32(MUSB + 0xa4, 0xf);			/* L1INTM: TX|RX|USBCOM|DMA */
	w16(MUSB + 0x06, 0xffff);		/* INTRTXE */
	w16(MUSB + 0x08, 0xfffe);		/* INTRRXE */
	w8(MUSB + 0x0b, 0xff);			/* INTRUSBE */
	w8(MUSB + 0x0f, 0);			/* TESTMODE */
	w8(MUSB + 0x01, 0xa0);			/* ISOUPDATE | HSENAB */
	w8(MUSB + 0x60, 0x01);			/* DEVCTL.SESSION (musb_start does this without VBUS) */
	hold(100);
	w8(MUSB + 0x01, 0xe0);			/* + SOFTCONN */
}

static void watch(unsigned secs)
{
	uint64_t end = cnt() + frq() * secs;
	uint32_t seen = 0, resets = 0, setups = 0;
	int hs = 0, i, chg = 0;
	uint32_t ld = 0xffff, lp = 0xffff, ll = 0xffff;
	while (cnt() < end) {
		uint32_t d = r8(MUSB + 0x60), p = r8(MUSB + 0x01), l = r32(MUSB + 0xa0);
		if ((d != ld || p != lp || l != ll) && chg < 10) {
			con_hex2("CHG", "dev=", d, "pwr=", p);
			con_hex("L1S", l);
			chg++;
		}
		ld = d; lp = p; ll = l;
		uint8_t u = r8(MUSB + 0x0a);		/* INTRUSB (clear on read) */
		if (u) {
			seen |= u;
			if (u & 4) {
				resets++;
				hs |= (r8(MUSB + 0x01) >> 4) & 1;
				if (resets == 1)
					con_hex2("RESET", "power=", r8(MUSB + 0x01), "intrusb=", u);
			}
		}
		(void)r16(MUSB + 0x02);			/* INTRTX (clear on read) */
		(void)r16(MUSB + 0x04);
		uint16_t csr = r16(MUSB + 0x102);
		if (csr & 1) {				/* RXPKTRDY */
			uint8_t b[8] = { 0 };
			uint16_t n = r16(MUSB + 0x108);
			for (i = 0; i < 8; i++)
				b[i] = r8(MUSB + 0x20);	/* FIFO0 */
			if (setups < 4)
				con_hex2("SETUP", "w0=", b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24,
					 "w1=", b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24);
			if (setups == 0)
				con_hex("COUNT0", n);
			setups++;
			w16(MUSB + 0x102, 0x48);	/* SERVICED_RXPKTRDY | DATAEND */
		}
	}
	con_hex2("SUMMARY", "intrusb=", seen, "resets=", resets);
	con_hex2("SUMMARY", "setups=", setups, "hs=", hs);
	con_hex2("END", "power=", r8(MUSB + 0x01), "devctl=", r8(MUSB + 0x60));
}

void usb_probe(void)
{
	con_skip();
	con_str("A", "read-only dump (cable already plugged)");
	con_hex2("CG", "90=", r32(INFRA + 0x90), "94=", r32(INFRA + 0x94));
	dump_phy("before");
	dump_musb("before");
	con_skip();
	con_str("NEXT", "B in 15 s");
	hold(15000);

	con_clear(BLUE);
	con_str("SHIM", "sea usb probe2  B: phy init + forced VBUS + session");
	phy_init_power_on();
	phy_force_vbus();
	hold(100);
	dump_phy("after init+force");
	musb_connect();
	hold(300);
	dump_musb("after connect");
	watch(25);
	con_skip();
	con_str("NEXT", "C in 15 s");
	hold(15000);

	con_clear(BLUE);
	con_str("SHIM", "sea usb probe2  C: B + PLL forced on");
	w8(MUSB + 0x01, 0xa0);			/* drop SOFTCONN */
	hold(300);
	phy_pll_force();
	hold(100);
	dump_phy("after pll force");
	w8(MUSB + 0x01, 0xe0);
	hold(300);
	dump_musb("after reconnect");
	watch(30);
}

/* ======================================================================
 * Mini fastboot gadget: enough of the fastboot protocol for
 *   fastboot stage payload.bin && fastboot oem go
 * VID:PID 18d1:4ee0, class ff/42/03, bulk EP1 IN/OUT, polled, PIO.
 * ==================================================================== */

#define CSR0		(MUSB + 0x102)
#define EP1_TXMAXP	(MUSB + 0x110)
#define EP1_TXCSR	(MUSB + 0x112)
#define EP1_RXMAXP	(MUSB + 0x114)
#define EP1_RXCSR	(MUSB + 0x116)
#define EP1_RXCOUNT	(MUSB + 0x118)
#define EP1_FIFO	(MUSB + 0x24)
#define EP0_FIFO	(MUSB + 0x20)

#define STAGE_ADDR	0xC0100000UL
#define STAGE_MAX	0x7FE00000UL

static uint8_t g_cfg;
static uint32_t g_tx_pre, g_tx_post;
static int g_tx_diag;
static int g_bus_reset;

static const uint8_t dev_desc[18] = {
	18, 1, 0x00, 0x02, 0, 0, 0, 64, 0xd1, 0x18, 0xe0, 0x4e, 0x00, 0x01, 1, 2, 3, 1
};
static const uint8_t cfg_desc[32] = {
	9, 2, 32, 0, 1, 1, 0, 0xc0, 0x32,
	9, 4, 0, 0, 2, 0xff, 0x42, 0x03, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x01, 2, 0x00, 0x02, 0
};
static const uint8_t qual_desc[10] = { 10, 6, 0x00, 0x02, 0, 0, 0, 64, 1, 0 };

static uint64_t deadline(unsigned ms) { return cnt() + frq() * ms / 1000; }

static void ep1_fifo_cfg(void)
{
	w8(MUSB + 0x0e, 1);		/* INDEX */
	w8(MUSB + 0x62, 6);		/* TXFIFOSZ 512 */
	w16(MUSB + 0x64, 64 / 8);	/* TXFIFOADD */
	w8(MUSB + 0x63, 6);		/* RXFIFOSZ 512 */
	w16(MUSB + 0x66, (64 + 512) / 8);	/* RXFIFOADD */
	w8(MUSB + 0x0e, 0);
}

static void ep1_enable(void)
{
	w16(EP1_TXMAXP, 512);
	w16(EP1_TXCSR, 0x2000 | 0x40 | 0x08);	/* MODE_TX | CLRDATATOG | FLUSHFIFO */
	w16(EP1_RXMAXP, 512);
	w16(EP1_RXCSR, 0x80 | 0x10);		/* CLRDATATOG | FLUSHFIFO */
	w16(EP1_RXCSR, 0x80 | 0x10);
}

static int g_en_diag;

static void ep1_dump(void)
{
	if (g_en_diag++ >= 2)
		return;
	con_hex2("EP1 TX", "maxp=", r16(EP1_TXMAXP), "csr=", r16(EP1_TXCSR));
	con_hex2("EP1 RX", "maxp=", r16(EP1_RXMAXP), "csr=", r16(EP1_RXCSR));
	w8(MUSB + 0x0e, 1);
	con_hex2("EP1 FIFO", "tsz/add=", r8(MUSB + 0x62) << 16 | r16(MUSB + 0x64),
		 "rsz/add=", r8(MUSB + 0x63) << 16 | r16(MUSB + 0x66));
	w8(MUSB + 0x0e, 0);
}

static void ep1_cleartog(void)
{
	w16(EP1_TXCSR, 0x2000 | 0x40);
	w16(EP1_RXCSR, 0x80);
}

static void bus_reset(void)
{
	w8(MUSB + 0x00, 0);	/* FADDR */
	g_cfg = 0;
	g_bus_reset = 1;
}

/* INTRUSB/INTRTX/INTRRX are write-1-to-clear on this glue */
static void poll_bus(void)
{
	uint8_t u = r8(MUSB + 0x0a);
	if (u) {
		w8(MUSB + 0x0a, u);
		if (u & 4)
			bus_reset();
	}
	uint16_t t = r16(MUSB + 0x02);
	if (t)
		w16(MUSB + 0x02, t);
	uint16_t rr = r16(MUSB + 0x04);
	if (rr)
		w16(MUSB + 0x04, rr);
}

static int ep0_wait_tx(void)
{
	uint64_t end = deadline(200);
	while (r16(CSR0) & 2) {
		if (r16(CSR0) & 0x10)	/* SETUPEND */
			return -1;
		if (cnt() > end)
			return -1;
	}
	return 0;
}

static void ep0_ack(void) { w16(CSR0, 0x40 | 0x08); }		/* SERVICED_RXPKTRDY | DATAEND */
static void ep0_stall(void) { w16(CSR0, 0x40 | 0x20); }		/* SERVICED_RXPKTRDY | SENDSTALL */

static void ep0_send(const uint8_t *d, unsigned n, unsigned wlen)
{
	unsigned off = 0, c, i;
	if (n > wlen)
		n = wlen;
	w16(CSR0, 0x40);	/* SERVICED_RXPKTRDY */
	for (;;) {
		c = n - off;
		if (c > 64)
			c = 64;
		for (i = 0; i < c; i++)
			w8(EP0_FIFO, d[off + i]);
		off += c;
		int last = (c < 64) || (off == n && n == wlen);
		w16(CSR0, last ? 0x0a : 0x02);	/* TXPKTRDY (| DATAEND) */
		if (ep0_wait_tx() < 0)
			return;
		if (last)
			return;
	}
}

static unsigned put_str(uint8_t *o, const char *s)
{
	unsigned n = 2;
	while (*s) {
		o[n++] = (uint8_t)*s++;
		o[n++] = 0;
	}
	o[0] = n;
	o[1] = 3;
	return n;
}

static void handle_setup(void)
{
	uint8_t b[8];
	uint8_t buf[64];
	int i;
	for (i = 0; i < 8; i++)
		b[i] = r8(EP0_FIFO);
	unsigned wval = b[2] | b[3] << 8, wlen = b[6] | b[7] << 8;

	if ((b[0] & 0x60) != 0) {	/* class / vendor: not used */
		ep0_stall();
		return;
	}
	switch (b[1]) {
	case 0:	/* GET_STATUS */
		buf[0] = buf[1] = 0;
		ep0_send(buf, 2, wlen);
		return;
	case 1:	/* CLEAR_FEATURE (ENDPOINT_HALT on the bulk pipe) */
		if ((b[0] & 0x1f) == 2)
			ep1_cleartog();
		ep0_ack();
		return;
	case 5: {	/* SET_ADDRESS */
		uint8_t a = wval & 0x7f;
		ep0_ack();
		uint64_t end = deadline(20);
		while (!(r16(MUSB + 0x02) & 1) && cnt() < end)
			;
		w16(MUSB + 0x02, 1);
		w8(MUSB + 0x00, a);
		return;
	}
	case 6:	/* GET_DESCRIPTOR */
		switch (wval >> 8) {
		case 1: ep0_send(dev_desc, 18, wlen); return;
		case 2: ep0_send(cfg_desc, 32, wlen); return;
		case 6: ep0_send(qual_desc, 10, wlen); return;
		case 3: {
			unsigned n;
			switch (wval & 0xff) {
			case 0: buf[0] = 4; buf[1] = 3; buf[2] = 9; buf[3] = 4; n = 4; break;
			case 1: n = put_str(buf, "ios-sea"); break;
			case 2: n = put_str(buf, "sea usb loader"); break;
			case 3: n = put_str(buf, "SEA0001"); break;
			default: ep0_stall(); return;
			}
			ep0_send(buf, n, wlen);
			return;
		}
		default:
			ep0_stall();
			return;
		}
	case 8:	/* GET_CONFIGURATION */
		buf[0] = g_cfg;
		ep0_send(buf, 1, wlen);
		return;
	case 9:	/* SET_CONFIGURATION */
		g_cfg = wval & 0xff;
		if (g_cfg) {
			ep1_enable();
			ep1_dump();
		}
		ep0_ack();
		return;
	case 10:	/* GET_INTERFACE */
		buf[0] = 0;
		ep0_send(buf, 1, wlen);
		return;
	case 11:	/* SET_INTERFACE */
		ep0_ack();
		return;
	default:
		ep0_stall();
	}
}

static void service_ep0(void)
{
	uint16_t c = r16(CSR0);
	if (c & 0x10)
		w16(CSR0, 0x80);		/* SERVICED_SETUPEND */
	if (c & 0x04)
		w16(CSR0, c & ~0x04);		/* SENTSTALL */
	if (c & 0x01)
		handle_setup();
}

static void service(void)
{
	poll_bus();
	service_ep0();
}

/* ---- bulk ---- */

static unsigned rx_read(uint8_t *dst)
{
	unsigned n = r16(EP1_RXCOUNT), i = 0;
	if (!((uintptr_t)dst & 3)) {
		for (; i + 4 <= n; i += 4)
			*(volatile uint32_t *)(dst + i) = r32(EP1_FIFO);
	}
	for (; i < n; i++)
		dst[i] = r8(EP1_FIFO);
	w16(EP1_RXCSR, r16(EP1_RXCSR) & ~1);
	return n;
}

static int tx_send_raw(const char *s);
static int tx_send(const char *s)
{
	int r = tx_send_raw(s);
	if (g_tx_diag < 6) {
		g_tx_diag++;
		con_hex2(r ? "TX FAIL" : "TX ok", "pre=", g_tx_pre, "post=", g_tx_post);
	}
	return r;
}

static int tx_send_raw(const char *s)
{
	unsigned n = 0;
	while (s[n] && n < 64) {
		w8(EP1_FIFO, (uint8_t)s[n]);
		n++;
	}
	g_tx_pre = r16(EP1_TXCSR);
	w16(EP1_TXCSR, 0x2000 | 0x01);		/* MODE_TX | TXPKTRDY (bit 0; bit 1 is FIFONOTEMPTY) */
	uint64_t end = deadline(2000);
	while (r16(EP1_TXCSR) & 0x01) {
		if (cnt() > end) {
			g_tx_post = r16(EP1_TXCSR);
			return -1;
		}
		poll_bus();
	}
	g_tx_post = r16(EP1_TXCSR);
	return 0;
}

static int starts(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return 1;
}

static void hex8(char *o, uint32_t v)
{
	for (int i = 0; i < 8; i++)
		o[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
}

static void scpy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
}

static void cat(char *d, const char *s)
{
	while (*d)
		d++;
	scpy(d, s);
}

static const char *getvar(const char *n)
{
	if (starts(n, "max-download-size")) return "0x7fe00000";
	if (starts(n, "product")) return "sea";
	if (starts(n, "version-bootloader")) return "ios-sea-usb";
	if (starts(n, "version")) return "0.4";
	if (starts(n, "serialno")) return "SEA0001";
	if (starts(n, "secure")) return "no";
	if (starts(n, "is-userspace")) return "no";
	if (starts(n, "current-slot")) return "b";
	return 0;
}

static void header(void)
{
	con_clear(BLUE);
	con_str("SHIM", "sea usb loader 18d1:4ee0 - fastboot stage + oem go");
}

static void say(const char *l, const char *s)
{
	if (con_row() > 46)
		header();
	con_str(l, s);
}

/* returns 0 = boot payload, 1 = reboot to LK fastboot */
int usb_fastboot(uint64_t *out_addr, uint64_t *out_len)
{
	uint8_t cmd[68] __attribute__((aligned(8)));
	char resp[72];
	uint64_t dl_total = 0, dl_got = 0;
	uint8_t *dl_ptr = 0;
	int state = 0, go = 0, rc = 0;

	header();
	say("USB", "PHY init + forced VBUS");
	phy_init_power_on();
	phy_force_vbus();
	hold(100);
	w8(MUSB + 0x01, 0);
	hold(100);
	ep1_fifo_cfg();
	musb_connect();
	g_cfg = 0;
	say("USB", "connected, waiting for the host");

	uint64_t last = cnt();
	for (;;) {
		service();
		if (cnt() - last > frq() * 120) {
			say("USB", "idle 120 s: back to fastboot");
			hold(2000);
			w8(MUSB + 0x01, 0);
			return 1;
		}
		if (g_bus_reset) {
			g_bus_reset = 0;
			if (state == 1) {
				say("USB", "bus reset during download");
				state = 0;
			}
		}
		if (!g_cfg || !(r16(EP1_RXCSR) & 1))
			continue;

		last = cnt();
		if (state == 1) {
			unsigned n = r16(EP1_RXCOUNT);
			if (n > dl_total - dl_got)
				n = dl_total - dl_got;
			unsigned rd = rx_read(dl_ptr + dl_got);
			(void)rd;
			uint64_t prev = dl_got;
			dl_got += n;
			if ((prev * 4 / dl_total) != (dl_got * 4 / dl_total) && dl_got != dl_total)
				con_hex("GOT", dl_got);
			if (dl_got >= dl_total) {
				con_hex("DONE", dl_got);
				state = 0;
				tx_send("OKAY");
			}
			continue;
		}

		unsigned n = rx_read(cmd);
		if (n > 64)
			n = 64;
		cmd[n] = 0;
		say("CMD", (const char *)cmd);

		if (starts((char *)cmd, "getvar:")) {
			const char *name = (char *)cmd + 7;
			if (starts(name, "all")) {
				tx_send("INFOproduct: sea");
				tx_send("INFOmax-download-size: 0x7fe00000");
				tx_send("INFOversion-bootloader: ios-sea-usb");
				tx_send("OKAY");
			} else {
				const char *v = getvar(name);
				if (v) {
					scpy(resp, "OKAY");
					cat(resp, v);
				} else {
					scpy(resp, "FAILunknown variable");
				}
				tx_send(resp);
			}
		} else if (starts((char *)cmd, "download:")) {
			uint32_t sz = 0;
			for (int i = 9; i < 17 && cmd[i]; i++) {
				char c = cmd[i];
				sz = sz << 4 | (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
			}
			if (sz == 0 || sz > STAGE_MAX) {
				tx_send("FAILbad size");
			} else {
				dl_total = sz;
				dl_got = 0;
				dl_ptr = (uint8_t *)STAGE_ADDR;
				state = 1;
				con_hex("SIZE", sz);
				scpy(resp, "DATA");
				hex8(resp + 4, sz);
				resp[12] = 0;
				tx_send(resp);
			}
		} else if (starts((char *)cmd, "boot") || starts((char *)cmd, "continue") ||
			   starts((char *)cmd, "oem go")) {
			if (dl_total < 64) {
				tx_send("FAILstage a payload first");
			} else {
				tx_send("OKAY");
				go = 1;
			}
		} else if (starts((char *)cmd, "reboot")) {
			tx_send("OKAY");
			rc = 1;
			go = 1;
		} else {
			tx_send("FAILunknown command");
		}
		if (go)
			break;
	}
	hold(300);
	w8(MUSB + 0x01, 0);		/* soft-disconnect */
	*out_addr = STAGE_ADDR;
	*out_len = dl_total;
	return rc;
}
