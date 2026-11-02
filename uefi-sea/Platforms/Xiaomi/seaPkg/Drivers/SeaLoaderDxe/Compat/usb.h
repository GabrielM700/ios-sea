#pragma once
#include <stdint.h>
void usb_probe(void);
/* mini fastboot gadget; 0 = payload staged at *addr (len *len), 1 = reboot */
int usb_fastboot(uint64_t *addr, uint64_t *len);
