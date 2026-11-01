# UEFI do Redmi Note 12S (sea)

Plataforma do [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) (`Platforms/Xiaomi/seaPkg` + `Silicon/MediaTek/MT6781Pkg`), escrita
só com o que foi **medido no aparelho** (`docs/SEA.md`, `NOTES.md`). O UEFI sai como `boot.img` v3 (kernel gzip no slot do kernel, como o
shim), então grava-se do mesmo jeito. Os fontes estão em `uefi-sea/`; o dispositivo no formato HakiOS está em `hakios/hakios-device-sea/`.

**Estado: compila (RELEASE e DEBUG), não foi testado no aparelho.** Nada aqui toca `userdata`.

## O que a plataforma declara

| Item | Valor (de onde vem) |
|---|---|
| FD / entrada | `0x40080000`, 2 MiB (LK carrega o kernel do boot.img aí, EL1, MMU/cache off) |
| Pilha / heap do DXE | `0x40000000` (256 KiB) / `0x40280000` (60 MiB), mesmo desenho dos outros MediaTek do Mu-Silicium |
| Framebuffer | `0x7C606000`, 1080x2400, pitch 4320 B (= 1080 x 4), a8r8g8b8 (frame 1 da LK) |
| RAM | `0xC0100000..0x13FFFF000` e `0x140000000..0x20D808000` (lê/escreve, probe 17); a página DRAMC `0x13FFFF000` e o bloco reservado acima de `0x20D808000` ficam de fora; os 3 GiB baixos (ATF, modem, ADSP, SCP, SSPM) não são declarados |
| GICv3 | GICD `0x0C000000`, GICR `0x0C040000` (8 x 0x20000) |
| Timer | **virtual** (INTID 27, verificado no sea); o timer físico não foi testado |
| CPUs | 8: 0-5 Cortex-A55, 6-7 Cortex-A76 (`PcdCoreCount 8`) |
| Watchdog | desligado no SEC (`0x10007000`, chave `0x22000000`): sem isso o aparelho reinicia sozinho (visto nos primeiros probes, ~60 s) |
| MMIO mapeado | WDT, I2C0 `0x11007000`, MUSB `0x11200000`, UFS HCI `0x11270000`, PHY USB `0x11CA0000` |

## O que NÃO tem (de propósito)

- **Clock, GPIO, PMIC e MSDC**: os drivers do Mu-Silicium são escritos para os registradores do MT6768/MT6789; nada disso foi medido no MT6781.
  A LK deixa os clocks ligados (visto no probe do USB).
- **Armazenamento**: o sea usa UFS (LUN 2 tem a GPT). Falta um driver UEFI de UFS que faça o START STOP UNIT na W-LUN (o shim faz isso em `shim-sea/ufs.c`).
- **USB**: falta o driver do MUSB + PHY (forçar VBUS, `shim-sea/usb.c` já faz).
- **Teclas**: `KeypadDeviceLib` é um stub (nenhuma tecla). Power/volume ficam no PMIC/GPIO.
- **ACPI**: sem tabelas; **RamManager**: sem (a RAM é estática no `MemoryMapLib`).

Ou seja: o primeiro UEFI sobe, desenha no framebuffer (DEBUG imprime o log na tela) e fica no menu/console. É o degrau para o resto:
UFS e USB (portando o que o shim já faz), teclas e o carregador do XNU (`hakios-uefi/files/XnuLoader.c`).

## Compilar

```sh
git clone --recursive https://github.com/Project-Silicium/Mu-Silicium     # ~3 GB; precisa de clang, lld, nasm, python3
uefi-sea/build.sh Mu-Silicium /caminho/boot.img DEBUG      # -> uefi-sea/out/sea-uefi-DEBUG.img
uefi-sea/build.sh Mu-Silicium /caminho/boot.img RELEASE
```

`boot.img` é o **seu** boot stock (v3). O script usa o `build` do próprio EDK2 (sem `stuart`/pip). Submódulos extras que o `build` precisa
dentro de `Mu_Basecore`: brotli (2), libfdt, mipisyst e libspdm.

## Testar (sem apagar nada)

```sh
fastboot flash boot_b sea-uefi-DEBUG.img     # slot de teste, como com o shim; o stock fica em boot_a
```

O que esperar com DEBUG: o log do UEFI na tela (`FrameBufferSerialPortLib`). Se reiniciar em ~1 min o watchdog não foi desligado (mande foto);
se a tela ficar preta, o framebuffer ou a pilha estão errados. Para voltar ao shim, grave `sea-usb-fb6.img` na mesma partição.
