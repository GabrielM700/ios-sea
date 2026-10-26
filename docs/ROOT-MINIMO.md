# ios-sea: root mínimo do iOS 16 em RAM, via USB (Redmi Note 12S "sea")

Adaptação do "On the phone" do HakiOS ao nosso caso. Diferenças: o loader é o shim (não UEFI),
o kernel é o SWEET xnu-8796.141.3 (HFS+/mockfs, sem APFS), não há `xl` nem firmware a puxar
(sem GPU, Wi-Fi, áudio, toque ainda), e nada é gravado no `userdata`.

## No PC (Linux x86-64, ~15 GB livres)
Pacotes: `apfs-fuse` (build de 2021 ou mais nova, com LZBITMAP), `hfsprogs`, `rsync`, `p7zip-full`,
`python3`, `fastboot`.

```sh
git clone https://github.com/GabrielM700/ios-sea.git && cd ios-sea
# 1. kernel: artefato "xnu-kernel" do workflow xnu.yml -> kernel.development.sweet
# 2. IPSW do iPhone10,3 16.7.16 (20H392): só dois membros (nomes em BuildManifest.plist)
python3 tools/remotezip.py IPSW_URL 087-86698-044 ipsw     # OS (o rootfs)
python3 tools/remotezip.py IPSW_URL 087-86593-047 ipsw     # Cryptex1,SystemOS (shared cache)
# 3. root mínimo (HFS+, ~2 GB)
tools/mkminroot.sh ipsw/<OS>.dmg ipsw/<SystemOS>.dmg root.img
# 4. árvore de dispositivos e payload (kernel + ADT + root)
python3 tools/mkadt.py sea.adt --sea
python3 tools/mkpayload.py kernel.development.sweet sea.adt payload.bin --ramdisk root.img \
  --cmdline 'debug=0xc serial=3 cpus=8 keepsyms=1 -noprogress rd=md0 rootrw=1 launchd_unsecure_cache=1 ignition_level=1 wdt=-1 wdt_test=no_user_checkin'
```

## No aparelho (uma vez por sessão)
```sh
# loader USB (grava só a boot_b; uma vez, ou ao mudar o loader)
fastboot flash boot_b sea-usb-fb4.img && fastboot reboot
# a tela azul mostra "waiting for the host"; então:
fastboot stage payload.bin && fastboot oem go     # ~6 min para 2 GB (USB 2.0, PIO)
```
Voltar ao fastboot do LK: segure Power + Vol- (ou `fastboot reboot` enquanto o loader USB responde).

## O que esperar
`BSD root: md0` -> `hfs: mounted iOSRoot` -> launchd (PID 1) -> saída de `ps -A`, `df`, `mount` no console.
Limite: payload <= 0x7FE00000 (~2 GiB). Se o shared cache for maior, cortar subcaches.
Não incluído: `userdata` (intocado), firmware do sea, toque/GPU/rede.
