#!/bin/bash
# Build the sea's UEFI (Project Silicium's Mu-Silicium + this directory) and pack it as boot.img v3.
#   uefi-sea/build.sh MU_SILICIUM_DIR STOCK_BOOT.IMG [RELEASE|DEBUG]  -> out/sea-uefi-<mode>.img
# MU_SILICIUM_DIR: a clone of https://github.com/Project-Silicium/Mu-Silicium with its submodules
#   (Mu_Basecore, Common/Mu, Common/Mu_OEM_Sample, Silicium-ACPI, the openssl one; in Mu_Basecore also the brotli,
#   libfdt, mipisyst and libspdm ones). Needs clang, lld, nasm, python3 and make; this script runs EDK2's own `build`
#   (no stuart), so no pip packages are needed.  DEBUG prints the log on the screen (FrameBufferSerialPortLib).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mu=$(realpath "${1:?Mu-Silicium checkout}") stock=$(realpath "${2:?stock boot.img}") mode=${3:-DEBUG}
tools=$here/../tools
cp "$here/../shim-sea/usb.c" "$here/Platforms/Xiaomi/seaPkg/Drivers/SeaLoaderDxe/usb.c"   # the shim's USB gadget, unchanged
cp -r "$here/Platforms" "$here/Silicon" "$here/Resources" "$mu/"
export WORKSPACE=$mu
export PACKAGES_PATH=$mu/Platforms/Xiaomi:$mu/Common/Mu:$mu/Common/Mu_OEM_Sample:$mu/Mu_Basecore:$mu/Silicon/MediaTek:$mu/Silicon/Silicium:$mu/Silicium-ACPI
export EDK_TOOLS_PATH=$mu/Mu_Basecore/BaseTools PYTHONPATH=$mu/Mu_Basecore/BaseTools/Source/Python CONF_PATH=$mu/Conf
export PATH=$EDK_TOOLS_PATH/BinWrappers/PosixLike:$PATH BASE_TOOLS_PATH=$EDK_TOOLS_PATH
[ -x "$mu/Mu_Basecore/BaseTools/Source/C/bin/GenFv" ] || make -C "$mu/Mu_Basecore/BaseTools" -j"$(nproc)"
mkdir -p "$mu/Conf"
for f in build_rule target tools_def; do [ -f "$mu/Conf/$f.txt" ] || cp "$mu/Mu_Basecore/BaseTools/Conf/$f.template" "$mu/Conf/$f.txt"; done
cd "$mu"
build -a AARCH64 -t CLANGPDB -b "$mode" -p seaPkg/sea.dsc -n "$(nproc)" \
  -D FD_BASE=0x40080000 -D FD_SIZE=0x200000 -D FD_BLOCKS=0x200 -D DEVICE_MODEL=0 -D ENABLE_SECUREBOOT=0 \
  -D BUILDID_STRING=ios-sea -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE
fd=$mu/Build/seaPkg/${mode}_CLANGPDB/FV/SILICIUM_UEFI.fd
out=$here/out; mkdir -p "$out"
python3 "$tools/mkgz_stock.py" "$fd" "$out/uefi-$mode.fd.gz"
python3 "$tools/pack_boot_v3.py" "$stock" "$out/uefi-$mode.fd.gz" "$out/sea-uefi-$mode.img"
cp "$fd" "$out/SILICIUM_UEFI-$mode.fd"
sha256sum "$out/sea-uefi-$mode.img"
