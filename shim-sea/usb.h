#pragma once
/* sea USB (MediaTek MUSB @0x11200000 + tphy @0x11ca0300): probe, later the fastboot gadget. */
void usb_probe(void);
/* mini fastboot gadget; 0 = payload staged at *addr (len *len), 1 = reboot to LK fastboot */
int usb_fastboot(uint64_t *addr, uint64_t *len);
