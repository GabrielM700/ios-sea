# Port para o Redmi Note 12S ("sea", Helio G96 / MT6781, 8 GiB)

Repositório derivado do branch `fleur`: mesmo SoC MT6781, mesmo kernel SWEET **sem alteração**. Só mudam os
endereços do ADT sintético (`tools/mkadt.py --sea`) e o shim (`shim-sea/`). Tudo abaixo foi medido
no aparelho com probes (EL1, MMU 16K, RAM acima de 4 GiB, GICv3, 8 cores por PSCI, UFS 3.1 HCI).

## Mapa do aparelho
| Item | Valor |
|---|---|
| RAM do XNU | `0x140000000` + `0xCD808000` (3,2 GiB livres; log de panic de 1 MiB no topo) |
| Framebuffer (frame 1 do LK) | `0x7c606000`, 1080x2400, pitch 4320 B, a8r8g8b8 |
| GICD / GICR | `0x0C000000` / `0x0C040000` (8 x 0x20000) |
| UFS HCI | `0x11270000` (LK deixa em PowerDown; START STOP UNIT na W-LUN acorda). GPT na **LUN 2** (`ufslun=2`) |
| Timer | 13 MHz, timer virtual INTID 27 |
| Cores | 0-5 Cortex-A55, 6-7 Cortex-A76; PSCI CPU_ON com alvo `i<<8` |
| Boot | LK carrega a imagem em `0x40080000`, entra em EL1, MMU/cache off, x0 = FDT (`0x4BC80000`) |

## Etapa 1 — testar a cadeia sem kernel (já validada no aparelho)
    cd shim-sea/fakexnu && ./build.sh
    python3 tools/mkadt.py sea.adt --sea
    python3 tools/mkpayload.py shim-sea/fakexnu/out/fakexnu.macho sea.adt payload-fake.bin --cmdline "fake=1"
    STOCK=/caminho/boot_original.img shim-sea/build-xnu.sh payload-fake.bin sea-xnu-fake.img
    fastboot flash boot_b sea-xnu-fake.img
Precisa de `clang` + `ld.lld` + `llvm-objcopy` (alvo aarch64) e python3. O `boot_original.img` é o **seu**
boot stock (cabeçalho v3); não vai para o repositório.

## Etapa 2 — kernel de verdade
1. Crie um repositório público no GitHub com este conteúdo (push do `main`); em *Actions* habilite os workflows e rode `xnu` na branch `main`
   (Padrões 13.5 / DEVELOPMENT / SWEET, ~1 h, runner `macos-14`
   (esse label sai do ar em 2026-11-02).
2. Baixe o artefato `xnu-kernel` -> `kernel.development.sweet`.
3. (userland) `research/ipsw-x/d22ap.adt` = nó /product do DeviceTree.d22ap do IPSW do iPhone X (ver topo de `tools/mkadt.py`).
4. Monte:
       python3 tools/mkadt.py sea.adt --sea
       python3 tools/mkpayload.py kernel.development.sweet sea.adt payload-sea.bin \
         --cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0"
       STOCK=/caminho/boot_original.img shim-sea/build-xnu.sh payload-sea.bin sea-xnu.img
   Primeiro marco (como no fleur): panic em `IOFindBSDRoot: ... md0 ... has not been configured`.
5. Ler a GPT pelo UFS: `rd=ufsroot0 ufsroot=userdata ufslun=2` (patch 0082).

## Root do iOS
Imagem HFS+ de 16 GiB. No fleur ela vai para `userdata` (apaga os dados do Android); no sea `cust` (2 GiB)
e `super` (7 GiB) não comportam. Nada grava em `userdata` sem decisão explícita.

## Splash
Antes de saltar para o XNU o shim mostra 6 s de log em texto e depois 6 s de um cartão (imagem + texto, gerado por
`shim-sea/splash/make-splash.py` a partir de `patrick.png`; `splash.bin.gz` já vem pronto). Se o cartão aparece, o
carregador terminou de copiar kernel/ADT/boot_args; se depois ele é substituído pelas linhas brancas do kprintf, o XNU está rodando.

## Testes no host
`shim-sea/test/` valida a varredura/realinhamento do ramdisk (`lt.c`) e `loader_boot` inteiro contra um payload
(`lb.c`, mapeia RAM em 0x140000000 com mmap). Ver cabeçalho dos arquivos.

## Créditos
Base: dipa-studio/ios-sweet e Electron-py/ios-sweet (branch fleur), MIT.

## USB loader (fastboot-compatible) e root mínimo em RAM

- `shim-sea/build-usbfb.sh` gera a imagem do loader USB (MUSB do MT6781; o VBUS tem de ser
  forçado no PHY, o sea não o liga ao PHY). Grave uma vez na `boot_b`; depois:
  `fastboot stage payload.bin && fastboot oem go` (o payload vai para 0xC0100000, até ~2 GiB).
- `tools/mkpayload.py KERNEL sea.adt out.bin --ramdisk root.img --cmdline '... rd=md0'` embute o root.
- `tools/mkminroot.sh OS.dmg SYSTEMOS.dmg root.img` monta, no seu PC, um HFS+ mínimo do iOS 16.7.16
  (launchd, dyld, shared cache, jobs de demonstração ps/df/mount). Boot-args sugeridos:
  `debug=0xc serial=3 cpus=8 keepsyms=1 -noprogress rd=md0 rootrw=1 launchd_unsecure_cache=1 ignition_level=1 wdt=-1 wdt_test=no_user_checkin`.
