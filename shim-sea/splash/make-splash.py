#!/usr/bin/env python3
"""Gera o splash.bin do loader a partir de um PNG (versão ios-sea do make-splash.py).
Uso: python3 make-splash.py patrick.png splash.bin
Formato do .bin: u32 largura, u32 altura (little-endian), depois pixels
BGRA (ordem que o framebuffer espera), de cima para baixo, sem compressão.
O loader só copia esses pixels no centro de uma tela preta.
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

SRC, DST = sys.argv[1], sys.argv[2]
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

CARD_W, PATRICK_W = 1080, 760
src = Image.open(SRC).convert("RGBA")
PATRICK_H = round(PATRICK_W * src.height / src.width)   # mantém a proporção
patrick = src.resize((PATRICK_W, PATRICK_H), Image.LANCZOS)

lines = [("XNU loader OK", 84), ("(ios-sea / Redmi Note 12S)", 60),
         ("Jumping into XNU now", 56)]
tmp = ImageDraw.Draw(Image.new("RGB", (10, 10)))
fonts = []
for text, start in lines:
    size = start
    while size > 20:
        f = ImageFont.truetype(FONT, size)
        w = tmp.textbbox((0, 0), text, font=f)[2] - tmp.textbbox((0, 0), text, font=f)[0]
        if w <= CARD_W - 80:
            break
        size -= 2
    fonts.append((text, f))
heights = [tmp.textbbox((0, 0), t, font=f)[3] - tmp.textbbox((0, 0), t, font=f)[1]
           for t, f in fonts]

card_h = 60 + PATRICK_H + 70 + heights[0] + 28 + heights[1] + 60 + heights[2] + 60
card = Image.new("RGBA", (CARD_W, card_h), (0, 0, 0, 255))
card.paste(patrick, ((CARD_W - PATRICK_W) // 2, 60), patrick)
d = ImageDraw.Draw(card)
y = 60 + PATRICK_H + 70
for i, (text, f) in enumerate(fonts):
    bbox = d.textbbox((0, 0), text, font=f)
    d.text(((CARD_W - (bbox[2] - bbox[0])) // 2 - bbox[0], y), text,
           font=f, fill=(255, 255, 255, 255))
    y += heights[i] + (28 if i == 0 else 60 if i == 1 else 0)

raw = bytearray(card.convert("RGBA").tobytes())  # RGBA -> BGRA: troca R e B
for i in range(0, len(raw), 4):
    raw[i], raw[i + 2] = raw[i + 2], raw[i]
with open(DST, "wb") as f:
    f.write(struct.pack("<II", CARD_W, card_h))
    f.write(raw)
print(f"{DST}: {CARD_W}x{card_h}, {8 + len(raw)} bytes")
