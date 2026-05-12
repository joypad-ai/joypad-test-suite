#!/usr/bin/env python3
"""Generate src/gen_logo.h: the joypad logo as 4x 32x32 PCE sprite quadrants.

Reads the same source PNG as gcn/buildtools/make_logo.py
(../gcn/assets/logo.png) so the bouncing logo looks identical across
consoles. Each quadrant is encoded in PCE 4bpp planar format using a
1-bit alpha mask (foreground = palette index 1, background = 0 /
transparent). The PCE side cycles palette color 1 on each wall bounce
to match the GC / GBA screensavers.
"""

import io
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

LOGO_W = 64
LOGO_H = 64

SCRIPT = Path(__file__).resolve()
PCE_ROOT = SCRIPT.parent.parent           # pce/
REPO_ROOT = PCE_ROOT.parent               # joypad-tester/
JOYPAD_BRANDING = REPO_ROOT.parent / "assets"   # joypad/assets/

# Search paths (highest priority first). A hand-edited 64x64 PNG in
# pce/assets/ wins outright -- treat it as the ground-truth pixel
# art and skip the scale-from-vector pipeline. SVG/large PNG sources
# only kick in when there's no hand-edit yet.
LOGO_PATHS = [
    PCE_ROOT / "assets" / "logo_64.png",
    JOYPAD_BRANDING / "logo_solid.svg",
    JOYPAD_BRANDING / "logo_solid.png",
    PCE_ROOT / "assets" / "logo.png",
    REPO_ROOT / "gcn" / "assets" / "logo.png",
]


def find_logo():
    for p in LOGO_PATHS:
        if p.exists():
            return p
    raise FileNotFoundError(
        "No logo source found. Searched:\n  " +
        "\n  ".join(str(p) for p in LOGO_PATHS)
    )


def rasterize_svg(svg_path, w, h):
    """Render an SVG to a PIL Image at exactly (w, h) using rsvg-convert."""
    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        raise RuntimeError(
            "rsvg-convert not on PATH (install librsvg). "
            "Falling back requires moving the .svg out of the way."
        )
    png_bytes = subprocess.check_output([
        rsvg, "-w", str(w), "-h", str(h), str(svg_path),
    ])
    return Image.open(io.BytesIO(png_bytes))


def load_logo(path):
    if path.suffix.lower() == ".svg":
        return rasterize_svg(path, LOGO_W, LOGO_H)
    return Image.open(path)


def silhouette_image(img):
    """Reduce `img` to an L-mode (grayscale) image where 0 = foreground
    (logo silhouette) and 255 = background. Works for either:
      - RGBA with alpha-based silhouette (e.g. rasterized SVG): alpha
        becomes the silhouette (high alpha = foreground = 0).
      - RGB/L with dark silhouette on light background (e.g. logo.png):
        darkness becomes the silhouette directly.
    """
    if img.mode == "RGBA":
        alpha = img.split()[-1]
        # Invert so foreground (high alpha) reads as 0 (= "dark"), to
        # share the downstream code path with the dark-on-light case.
        return Image.eval(alpha, lambda v: 255 - v)
    return img.convert("L")


def to_1bit_mask(img):
    """Produce a LOGO_W x LOGO_H 1-bit mask (1 = silhouette).

    Hand-edited sources that are already exactly LOGO_W x LOGO_H pass
    through pixel-for-pixel (no crop, no resize, no anti-alias) so the
    on-screen sprite matches the source PNG exactly. Larger sources get
    cropped to their silhouette bbox and resized to fit.
    """
    flat = silhouette_image(img)

    if flat.size == (LOGO_W, LOGO_H):
        canvas = flat
    else:
        # Crop to silhouette bbox, fit-resize, center on white canvas.
        bw = flat.point(lambda v: 255 if v < 128 else 0, 'L')
        bbox = bw.getbbox()
        if bbox:
            flat = flat.crop(bbox)
        sw, sh = flat.size
        scale = min(LOGO_W / sw, LOGO_H / sh)
        new_w = max(1, int(sw * scale))
        new_h = max(1, int(sh * scale))
        scaled = flat.resize((new_w, new_h), Image.LANCZOS)
        canvas = Image.new("L", (LOGO_W, LOGO_H), 255)
        off_x = (LOGO_W - new_w) // 2
        off_y = (LOGO_H - new_h) // 2
        canvas.paste(scaled, (off_x, off_y))

    px = canvas.load()
    mask = [[0] * LOGO_W for _ in range(LOGO_H)]
    for y in range(LOGO_H):
        for x in range(LOGO_W):
            if px[x, y] < 128:
                mask[y][x] = 1
    return mask


def encode_16x16_cell(mask, x0, y0):
    """Encode one 16x16 PCE sprite cell.

    PCE 4bpp planar: 128 bytes per cell, organised as 4 bit-planes of
    32 bytes each (16 rows x 2 bytes). We only need plane 0 because
    the source is 1-bit; planes 1-3 stay zero.
    """
    out = bytearray(128)
    for row in range(16):
        byte0 = 0
        byte1 = 0
        for col in range(16):
            pixel = mask[y0 + row][x0 + col]
            if col < 8:
                byte0 |= pixel << (7 - col)
            else:
                byte1 |= pixel << (15 - col)
        # PCE VDC reads sprite rows as 16-bit words where bit 15 = pixel 0
        # (leftmost). VRAM words are little-endian-stored in our byte
        # array, so the byte holding pixels 0-7 lands at offset+1 (high
        # byte of the word) and the byte for pixels 8-15 at offset+0
        # (low byte). Got this wrong on the first pass; the symptom was
        # the logo splitting into two 8-column slices in swapped order.
        out[row * 2 + 0] = byte1
        out[row * 2 + 1] = byte0
    return bytes(out)


def encode_32x32_quadrant(mask, qx, qy):
    """Encode a 32x32 quadrant. Returns 512 bytes (4 cells: TL, TR, BL, BR)."""
    out = bytearray()
    out += encode_16x16_cell(mask, qx,      qy)        # TL
    out += encode_16x16_cell(mask, qx + 16, qy)        # TR
    out += encode_16x16_cell(mask, qx,      qy + 16)   # BL
    out += encode_16x16_cell(mask, qx + 16, qy + 16)   # BR
    return bytes(out)


def emit_c_array(name, data, per_line=16):
    lines = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    # Trim trailing comma on the last value to stay friendly with old C
    # toolchains (HuC's parser dislikes trailing commas in initialisers).
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    src = find_logo()
    print(f"[pce/make_logo.py] Source: {src}")
    img = load_logo(src)
    mask = to_1bit_mask(img)

    q0 = encode_32x32_quadrant(mask, 0,  0)    # top-left
    q1 = encode_32x32_quadrant(mask, 32, 0)    # top-right
    q2 = encode_32x32_quadrant(mask, 0,  32)   # bottom-left
    q3 = encode_32x32_quadrant(mask, 32, 32)   # bottom-right

    out_path = PCE_ROOT / "src" / "gen_logo.h"
    try:
        rel_src = src.relative_to(REPO_ROOT)
    except ValueError:
        rel_src = src

    with open(out_path, "w") as f:
        f.write("/*\n")
        f.write(" * Auto-generated by buildtools/make_logo.py - DO NOT EDIT.\n")
        f.write(f" * Source: {rel_src}\n")
        f.write(" *\n")
        f.write(" * 4x 32x32 PCE sprite quadrants of the joypad logo, in PCE 4bpp\n")
        f.write(" * planar format. Foreground pixels use palette colour index 1;\n")
        f.write(" * background = 0 (transparent on sprite layer).\n")
        f.write(" */\n\n")
        f.write("#ifndef GEN_LOGO_H\n")
        f.write("#define GEN_LOGO_H\n\n")
        f.write(f"#define LOGO_W {LOGO_W}\n")
        f.write(f"#define LOGO_H {LOGO_H}\n\n")
        f.write("/* Each quadrant is 512 bytes (one 32x32 PCE sprite). */\n")
        f.write(emit_c_array("logo_q0", q0))
        f.write("\n")
        f.write(emit_c_array("logo_q1", q1))
        f.write("\n")
        f.write(emit_c_array("logo_q2", q2))
        f.write("\n")
        f.write(emit_c_array("logo_q3", q3))
        f.write("\n#endif /* GEN_LOGO_H */\n")

    print(f"[pce/make_logo.py] Wrote {out_path} ({4 * 512} bytes of sprite data)")


if __name__ == "__main__":
    main()
