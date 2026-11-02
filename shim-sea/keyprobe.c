/*
 * sea key probe (read only): which PWRAP register layout the MT6781 uses, can we read the
 * MT6358/MT6366 PMIC through it, and what do power / vol+ / vol- change?
 *
 *   PWRAP 0x1000d000   PMIC wrapper (WACS2 channel)
 *   PMIC  0x008 HWCID  0x00a SWCID  0x028 TOPSTATUS (MT6358: PWRKEY_DEB bit1, HOMEKEY_DEB bit3, 0 = pressed)
 *   KP    0x10010000   keypad matrix (STA 0x0, MEM1..5 0x4..0x14, DEBOUNCE 0x18)
 *
 * Layouts come from Linux mtk-pmic-wrap.c: mt6768/mt6779 (legacy command word, FSM in RDATA[18:16])
 * and mt6873/mt6789 (arbiter: command = address, data through SWINF_2, FSM in RDATA[3:1]).
 * Only read transactions are issued and a variant is only tried when its INIT_DONE register reads 1.
 */
#include <stdint.h>
#include "console.h"

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

#define PWRAP	0x1000d000UL
#define KP	0x10010000UL

struct pwv {
	uint16_t init, cmd, rdata, fsm, vld;
	uint8_t arb;
};

/* 0: mt6768  1: mt6779  2: mt6873  3: mt6789(=mt8186 style offsets) */
static const struct pwv pwv[4] = {
	{ 0x094, 0xC20, 0xC24, 0xC24, 0xC28, 0 },
	{ 0x0A0, 0xC20, 0xC24, 0xC24, 0xC28, 0 },
	{ 0x000, 0xC80, 0xC94, 0xCA8, 0xCA4, 1 },
	{ 0x000, 0x880, 0x894, 0x8A8, 0x8A4, 1 },
};

static uint64_t tnow(void)
{
	__asm__ volatile("isb" ::: "memory");
	return rsr("cntvct_el0");
}

static void wait_ms(unsigned ms)
{
	uint64_t f = rsr("cntfrq_el0");
	uint64_t end = tnow() + (f ? f : 13000000UL) / 1000 * ms;
	while (tnow() < end)
		;
}

static unsigned fsm_of(const struct pwv *v)
{
	uint32_t r = rd32(PWRAP + v->fsm);
	return v->arb ? (r >> 1) & 7 : (r >> 16) & 7;
}

/* 0 ok, 1 never idle, 2 no valid data */
static int pw_read(const struct pwv *v, uint32_t adr, uint32_t *out)
{
	uint64_t f = rsr("cntfrq_el0") / 1000000;
	uint64_t end;
	if (!f)
		f = 13;
	end = tnow() + 5000 * f;
	while (fsm_of(v) != 0) {
		if (tnow() > end) {
			wr32(PWRAP + v->vld, 1);
			return 1;
		}
	}
	wr32(PWRAP + v->cmd, v->arb ? adr : ((adr >> 1) << 16));
	end = tnow() + 5000 * f;
	while (fsm_of(v) != 6) {
		if (tnow() > end)
			return 2;
	}
	*out = rd32(PWRAP + v->rdata) & 0xffff;
	wr32(PWRAP + v->vld, 1);
	return 0;
}

static int sel = -1;

static void vname(char *b, int i)
{
	b[0] = 'V';
	b[1] = (char)('0' + i);
	b[2] = 0;
}

static int pmic_ok(uint32_t cid)
{
	return (cid >> 8) == 0x58 || (cid >> 8) == 0x66 || (cid >> 8) == 0x59 || (cid >> 8) == 0x57;
}

static void find_pwrap(void)
{
	int i;
	char vn[8];
	con_str("PWRAP", "V0 6768 V1 6779 V2 6873 V3 6789");
	for (i = 0; i < 4; i++) {
		const struct pwv *v = &pwv[i];
		uint32_t init = rd32(PWRAP + v->init);
		uint32_t fsmw = rd32(PWRAP + v->fsm);
		uint32_t cid = 0, rc;
		vname(vn, i);
		con_hex2(vn, "init=", init, "fsmw=", fsmw);
		if (init != 1 || fsm_of(v) != 0) {
			con_str("  skip", "init!=1 or fsm busy");
			continue;
		}
		rc = (uint32_t)pw_read(v, 0x008, &cid);
		con_hex2("  CID", "rc=", rc, "val=", cid);
		if (!rc && pmic_ok(cid)) {
			sel = i;
			con_str("  FOUND", "pmic answers through this layout");
			break;
		}
	}
	if (sel < 0)
		con_str("PWRAP", "no layout answered");
}

/* the event log for the live screen */
#define NEV 12
static uint32_t ev_id[NEV], ev_old[NEV], ev_new[NEV];
static unsigned ev_n;

static void ev_add(uint32_t id, uint32_t o, uint32_t n)
{
	unsigned i = ev_n % NEV;
	ev_id[i] = id;
	ev_old[i] = o;
	ev_new[i] = n;
	ev_n++;
}

static void label(char *b, uint32_t id)
{
	if (id == 0xff) {
		b[0] = 'T'; b[1] = 'O'; b[2] = 'P'; b[3] = 0;
	} else if (id == 0xfe) {
		b[0] = 'S'; b[1] = 'T'; b[2] = 'A'; b[3] = 0;
	} else {
		b[0] = 'K'; b[1] = 'P'; b[2] = (char)('0' + id); b[3] = 0;
	}
}

static void draw(uint32_t top, uint32_t cid, uint32_t *kp, int kp_on, unsigned left)
{
	unsigned i, n;
	char lb[8];
	con_clear(BLUE);
	con_str("SHIM", "sea key probe  press keys");
	con_hex2("PMIC", "cid=", cid, "top=", top);
	con_hex2("TOP", "pwr(b1)=", (top >> 1) & 1, "home(b3)=", (top >> 3) & 1);
	if (kp_on) {
		con_hex2("KP", "sta=", kp[0], "mem1=", kp[1]);
		con_hex2("KP", "mem2=", kp[2], "mem3=", kp[3]);
		con_hex2("KP", "mem4=", kp[4], "mem5=", kp[5]);
	} else {
		con_str("KP", "not read");
	}
	con_hex("LEFT s", left);
	con_str("LOG", "newest first: old -> new");
	n = ev_n < NEV ? ev_n : NEV;
	for (i = 0; i < n; i++) {
		unsigned k = (ev_n - 1 - i) % NEV;
		label(lb, ev_id[k]);
		con_hex2(lb, "old=", ev_old[k], "new=", ev_new[k]);
	}
}

void key_probe(void)
{
	uint32_t top = 0, cid = 0, ktop = 0xffffffffU;
	uint32_t kp[6] = { 0, 0, 0, 0, 0, 0 }, kpo[6] = { 0, 0, 0, 0, 0, 0 };
	int i, kp_on = 0, first = 1;
	unsigned t;

	find_pwrap();
	con_skip();
	con_str("NEXT", "live keys in 20 s");
	wait_ms(20000);
	if (sel < 0) {
		con_str("STOP", "no pmic, nothing to watch");
		return;
	}
	{
		const struct pwv *v = &pwv[sel];
		pw_read(v, 0x008, &cid);
	}
	/* KP is read last: if its clock is gated the bus may hang and the watchdog reboots us. */
	con_clear(BLUE);
	con_str("KP", "reading keypad regs next");
	con_str("KP", "reboot here = clock gated");
	wait_ms(4000);
	for (i = 0; i < 6; i++)
		kpo[i] = kp[i] = rd32(KP + 4 * i);
	kp_on = 1;

	for (t = 0; t < 90 * 10; t++) {
		const struct pwv *v = &pwv[sel];
		int changed = first;
		if (pw_read(v, 0x028, &top) == 0 && top != ktop) {
			if (!first)
				ev_add(0xff, ktop, top);
			ktop = top;
			changed = 1;
		}
		for (i = 0; i < 6; i++) {
			uint32_t x = rd32(KP + 4 * i);
			if (x != kp[i]) {
				ev_add(i == 0 ? 0xfe : (uint32_t)i, kp[i], x);
				kp[i] = x;
				changed = 1;
			}
		}
		if (changed || (t % 100) == 0) {
			draw(top, cid, kp, kp_on, 90 - t / 10);
			first = 0;
		}
		wait_ms(100);
	}
}
