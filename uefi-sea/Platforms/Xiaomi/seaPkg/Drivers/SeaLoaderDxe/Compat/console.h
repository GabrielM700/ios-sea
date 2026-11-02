#pragma once
/* the shim's console calls, over EDK2's DebugLib (DEBUG builds print on the screen) and a colour bar on the GOP */
#include <stdint.h>
#define BLUE 0x1
void con_clear(unsigned colour);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
void con_hex2(const char *l1, const char *l2, uint64_t v1, const char *l3, uint64_t v2);
void con_skip(void);
int  con_row(void);
