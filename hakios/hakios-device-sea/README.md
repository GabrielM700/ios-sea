# Redmi Note 12S (sea)

A HakiOS device repository for the Redmi Note 12S (Xiaomi "sea", MediaTek Helio G96, MT6781, 8 GiB RAM). It is the first MediaTek
device here, so what differs from the Qualcomm phones is written down below. The kernel, the drivers and the tools are the common
repository's; the mt6781 family's facts are in `soc/`. How the three fit:
[docs/porting/repositories.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/repositories.md).

## What is different from a Qualcomm phone

| | Qualcomm devices here | sea |
|---|---|---|
| Firmware that starts the loader | UEFI (Mu-Silicium, `hakios-uefi`) | **none**: MediaTek's LK ("kaeru") loads `boot.img` (v3) at `0x40080000`, EL1, MMU and caches off. Mu-Silicium has no MT6781 port (it has MT6768 and MT6789 layers for other MediaTek chips, a possible base), so ios-sea carries one of its own (`uefi-sea/`, `docs/UEFI-SEA.md`: compiles, not yet booted on the phone) next to the shim |
| Loader | HakiOS's XNU loader inside UEFI | a bare-metal arm64 **shim** in the `boot.img` kernel slot (wakes UFS, runs a USB gadget, copies the payload, jumps to XNU) |
| Payload to RAM | `fastboot boot` | `fastboot stage` + `fastboot oem addr` + `fastboot oem go` against the shim's own gadget (VID:PID `18d1:4ee0`); the root image is staged straight to its final address, in 1 GiB pieces |
| SoC | SC7280 family | MT6781, 6x A55 + 2x A76, GICv3 at `0x0c000000`, PSCI 1.1 |

The shim, the kernel patches and the tools live in <https://github.com/GabrielM700/ios-sea> (`shim-sea/`, `xnu/patches/`, `tools/`):
this repository is the device's record in HakiOS's own layout, not a second copy of that code.

## What was measured

Every line is `status` in `device.conf`; the evidence is in ios-sea's `NOTES.md` and photos of the phone. In short: XNU
(xnu-8796) boots on the phone with all 8 cores, an in-RAM HFS+ root holding iOS 16.7.16's own `launchd`, dyld shared cache and
daemons (`logd`, `notifyd`, `configd`) runs, and nothing is written to `userdata`. No display driver beyond the LK framebuffer, no touch,
no GPU yet.

## Files

| File | What |
|---|---|
| `device.conf` | what the device is, and how far each part of it works |
| `configs/sea_defconfig` | the common kernel, the SoC's fragment, this device's: `make sea_defconfig` |
| `configs/sea.config` | every phone driver of the common repository is left out: their addresses and pins are the phone they were written for |
| `soc/` | the mt6781 layer: `soc.conf`, `configs/mt6781.config` |

Check with `tools/hakios-device.py check sea`.
