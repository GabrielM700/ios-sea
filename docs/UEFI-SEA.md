# UEFI do Redmi Note 12S (sea)

Plataforma do [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) (`Platforms/Xiaomi/seaPkg` + `Silicon/MediaTek/MT6781Pkg`), escrita
só com o que foi **medido no aparelho** (`docs/SEA.md`, `NOTES.md`). O UEFI sai como `boot.img` v3 (kernel gzip no slot do kernel, como o
shim), então grava-se do mesmo jeito. Os fontes estão em `uefi-sea/`; o dispositivo no formato HakiOS está em `hakios/hakios-device-sea/`.

**Estado: compila (RELEASE e DEBUG); nenhuma parte foi testada no aparelho** (o shim equivalente foi). Nada aqui toca `userdata`.

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

## O que veio do shim (portado, ainda não testado no aparelho)

| No shim | No UEFI | Onde |
|---|---|---|
| `shim-sea/ufs.c`: clocks INFRA, takeover da lista de requests do UFSHCI, START STOP UNIT na W-LUN `0xD0`, acha a LUN com `EFI PART` | **`SeaUfsDxe`**: publica a LUN 2 como `EFI_BLOCK_IO_PROTOCOL` **somente leitura** (blocos de 4 KiB, bounce buffer abaixo de 4 GiB); o `PartitionDxe` lê a GPT e aparecem `boot_a`, `super`, `userdata`... Não existe SCSI WRITE no driver | `Drivers/SeaUfsDxe/` |
| `shim-sea/usb.c`: gadget fastboot no MUSB (PHY, VBUS forçado, EP0/EP1 por polling), `getvar`, `download`, `oem addr`, `oem go`, `reboot` | **`SeaLoaderDxe`**: o **mesmo `usb.c`** (o `build.sh` copia o do shim, sem mudanças) rodando no EndOfDxe, antes do menu. Sem host por 120 s, segue para o menu do UEFI. Desliga o watchdog de 5 min do UEFI (o stage de 3 GiB leva ~8 min) | `Drivers/SeaLoaderDxe/` |
| `shim-sea/loader.c` (`boot_from`): copia os segmentos SWEETXNU, ADT, `boot_args`, pula no XNU com MMU/cache desligados | `SeaBoot.c`: a mesma lógica (offsets de `boot_args` conferidos com o `loader.c`: Video 40, cmdline 108, 736 bytes), depois `ExitBootServices`, limpa os caches, desliga MMU e caches e salta com `x0 = boot_args` (só registradores nessa última parte) | `SeaBoot.c` |
| RAM `0xC0100000..0x20D808000` do stage | declarada **reservada** no `MemoryMapLib`: o UEFI nunca aloca ali, então o payload e o root de 3 GiB ficam intactos até o XNU | `MemoryMapLib.c` |

O fluxo no PC é o mesmo de antes: `tools/seastage.sh payload.bin root.img` (`fastboot oem addr`, `fastboot stage`, `fastboot oem go`).
Com a imagem DEBUG o log aparece na tela; na RELEASE uma **barra colorida** no topo mostra o estado: azul = esperando o host, ciano = comando,
amarelo = tamanho do download, verde = download completo, branco = saltando para o XNU, vermelho = o payload não iniciou.

## O que ainda NÃO tem (de propósito)

- **Clock, GPIO, PMIC e MSDC** do Mu-Silicium: são para os registradores do MT6768/MT6789; nada disso foi medido no MT6781. A LK deixa os clocks ligados.
- **Teclas**: `KeypadDeviceLib` é um stub (power/volume ficam no PMIC/GPIO).
- **ACPI** e **RamManager** (a RAM é estática no `MemoryMapLib`); **DMA do USB** (PIO, ~6 MB/s como no shim).
- Boot do XNU a partir de uma partição `darwin` do UFS (hoje só pelo USB).

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

O que esperar com DEBUG: o log do UEFI na tela (`FrameBufferSerialPortLib`), incluindo `SeaUfs: ... SSU rc=0` e `SeaUfs: LUN2 GPT`, depois a barra azul do loader USB esperando o host. Se reiniciar em ~1 min o watchdog não foi desligado (mande foto);
se a tela ficar preta, o framebuffer ou a pilha estão errados. Para voltar ao shim, grave `sea-usb-fb6.img` na mesma partição.
