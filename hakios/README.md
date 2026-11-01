# HakiOS (oosexgang) layout for the sea

`hakios-device-sea/` is the Redmi Note 12S as a HakiOS device repository (device.conf with measured status, defconfig, mt6781 SoC layer),
made with `tools/hakios-device.py new sea --soc mt6781` from hakios-xnu-kernel and checked with `check sea`, `make sea_defconfig && make config`.

To publish it and get it listed upstream:

1. Create `hakios-device-sea` on GitHub and push the contents of this directory to it.
2. In a pull request to oosexgang/hakios-xnu-kernel, add to `devices/manifest.conf` (`tools/hakios-device.py lock sea` writes the revision):

```
device sea
	repository  https://github.com/GabrielM700/hakios-device-sea.git
	revision    <commit>
```

This does not move the sea's kernel patches (xnu/patches/0001-00xx), shim or tools into hakios: sea boots through a LK shim, not UEFI,
and HakiOS's kernel is one patch (xnu/hakios.patch) for Qualcomm. Merging those is a separate, larger job.
