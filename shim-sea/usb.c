/*
 * sea USB probe 1.  Everything is printed BEFORE the access that could hang the bus
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
	w8(MUSB + 0x01, 0x60);			/* SOFTCONN | HSENAB */
}

static void watch(unsigned secs)
{
	uint64_t end = cnt() + frq() * secs;
	uint32_t seen = 0, resets = 0, setups = 0;
	int hs = 0, i;
	while (cnt() < end) {
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
	con_str("A", "read-only dump; last line = where it hung");
	con_str("INFRA", "reading clock-gate status");
	con_hex2("CG", "90=", r32(INFRA + 0x90), "94=", r32(INFRA + 0x94));
	con_hex2("CG", "ac=", r32(INFRA + 0xac), "c8=", r32(INFRA + 0xc8));
	con_str("PHY", "reading 0x11ca0300");
	dump_phy("before");
	con_str("MUSB", "reading 0x11200000");
	dump_musb("before");
	con_skip();
	con_str("NEXT", "B in 25 s - plug the PC USB cable now");
	hold(25000);

	con_clear(BLUE);
	con_str("SHIM", "sea usb probe1  B: soft-connect, PHY untouched");
	musb_connect();
	hold(300);
	dump_musb("after connect");
	watch(20);
	con_skip();
	con_str("NEXT", "C in 20 s");
	hold(20000);

	con_clear(BLUE);
	con_str("SHIM", "sea usb probe1  C: tphy init + power_on");
	w8(MUSB + 0x01, 0);			/* drop SOFTCONN */
	hold(200);
	phy_init_power_on();
	dump_phy("after init");
	hold(100);
	musb_connect();
	hold(300);
	dump_musb("after connect");
	watch(30);
}
