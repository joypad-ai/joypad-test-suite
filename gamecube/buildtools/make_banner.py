#!/usr/bin/env python3
"""Generate opening.bnr (BNR1 format) for Swiss-GC display.

Produces a 96x32 RGB5A3 banner image plus a single English description block.
"""

import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

W, H = 96, 32

# If branding/banner.png exists in this gamecube/ dir, it's used verbatim
# (must be 96x32 RGB/RGBA). Otherwise we render one procedurally from the
# logo + a "Joypad" title.
BRANDING_DIR = Path(__file__).parent.parent / 'branding'
BANNER_PNG = BRANDING_DIR / 'banner.png'
LOGO_PATH = BRANDING_DIR / 'logo.png'


def rgb5a3(r: int, g: int, b: int, a: int = 255) -> int:
    if a == 255:
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def tile_image(img: Image.Image) -> bytes:
    out = bytearray()
    for ty in range(0, H, 4):
        for tx in range(0, W, 4):
            for py in range(4):
                for px in range(4):
                    pix = img.getpixel((tx + px, ty + py))
                    r, g, b = pix[:3]
                    a = pix[3] if len(pix) == 4 else 255
                    out.extend(struct.pack('>H', rgb5a3(r, g, b, a)))
    return bytes(out)


def find_font():
    candidates = [
        '/System/Library/Fonts/Helvetica.ttc',
        '/System/Library/Fonts/Supplemental/Arial Bold.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    ]
    for path in candidates:
        if Path(path).exists():
            return path
    return None


def render_banner() -> Image.Image:
    img = Image.new('RGB', (W, H), (0, 0, 0))
    draw = ImageDraw.Draw(img)

    font_path = find_font()
    title_font = ImageFont.truetype(font_path, 18) if font_path else ImageFont.load_default()

    title = 'Joypad'
    bbox = draw.textbbox((0, 0), title, font=title_font)
    title_w = bbox[2] - bbox[0]
    title_h = bbox[3] - bbox[1]

    logo_size = 28
    gap = 3
    total_w = logo_size + gap + title_w
    start_x = (W - total_w) // 2

    # Logo: white silhouette of the source icon on the black background.
    if LOGO_PATH.exists():
        logo = Image.open(LOGO_PATH).convert('L')
        mask = ImageOps.invert(logo).resize((logo_size, logo_size), Image.LANCZOS)
        white = Image.new('RGB', (logo_size, logo_size), (255, 255, 255))
        img.paste(white, (start_x, (H - logo_size) // 2), mask)

    text_x = start_x + logo_size + gap
    text_y = (H - title_h) // 2 - bbox[1]
    draw.text((text_x, text_y), title, font=title_font, fill=(255, 255, 255))
    return img


def main():
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('opening.bnr')

    if BANNER_PNG.exists():
        img = Image.open(BANNER_PNG).convert('RGB')
        if img.size != (W, H):
            sys.exit(f'{BANNER_PNG} must be {W}x{H}, got {img.size}')
        print(f'using hand-edited banner: {BANNER_PNG}')
    else:
        img = render_banner()
        print('using procedural banner')

    pixels = tile_image(img)
    assert len(pixels) == 6144

    header = b'BNR1' + b'\x00' * 28

    def pad(s: bytes, length: int) -> bytes:
        return s[:length].ljust(length, b'\x00')

    desc = (
        pad(b'Joypad Test Suite', 0x20) +
        pad(b'corenting + N64 fork', 0x20) +
        pad(b'Joypad Test Suite', 0x40) +
        pad(b'corenting + N64 fork', 0x40) +
        pad(b'Test GameCube and N64 controllers via passive adapter.', 0x80)
    )
    assert len(desc) == 0x140

    out_path.write_bytes(header + pixels + desc)
    print(f'wrote {out_path} ({out_path.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
