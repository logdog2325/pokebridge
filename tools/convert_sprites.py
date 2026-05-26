#!/usr/bin/env python3
"""Convert pokeemerald-expansion anim_front.png sprites to compact 4bpp + palette.

Each sprite output:
  - 2048-byte 4bpp pixel block (64x64 / 2 = 2048 bytes, 2 pixels per byte)
  - 64-byte normal palette (16 RGBA8888 colors, index 0 alpha-keyed)
  - 64-byte shiny palette  (or NULL when no shiny.pal is present)

Total per species: ~2.1 KB. 1025 species ≈ 2.2 MB regardless of shiny variants.

Usage: python3 tools/convert_sprites.py [MAX_ID]   (default 386)
"""
from PIL import Image
import os
import re
import sys

REPO_ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXP_ROOT   = os.path.join(REPO_ROOT, "vendor/pokeemerald-expansion")
SPRITE_DIR = os.path.join(EXP_ROOT, "graphics/pokemon")
SPECIES_H  = os.path.join(EXP_ROOT, "include/constants/species.h")
OUT_HEADER = os.path.join(REPO_ROOT, "include/embedded_sprites.h")

MAX_ID = int(sys.argv[1]) if len(sys.argv) > 1 else 386


def build_id_to_folder():
    out = {}
    pattern = re.compile(r"^#define\s+SPECIES_([A-Z0-9_]+)\s+(\d+)\s*$")
    with open(SPECIES_H) as f:
        for line in f:
            m = pattern.match(line.strip())
            if not m:
                continue
            name, idstr = m.group(1), int(m.group(2))
            if idstr < 1 or idstr > MAX_ID:
                continue
            if idstr in out:
                continue
            out[idstr] = name.lower()
    return out


def parse_jasc_pal(path):
    try:
        with open(path) as f:
            lines = [l.strip() for l in f.readlines()]
    except (IOError, OSError):
        return None
    if not lines or lines[0] != "JASC-PAL":
        return None
    try:
        n = int(lines[2])
    except (IndexError, ValueError):
        return None
    out = []
    for i in range(n):
        if 3 + i >= len(lines):
            return None
        parts = lines[3 + i].split()
        if len(parts) < 3:
            return None
        out.append((int(parts[0]), int(parts[1]), int(parts[2])))
    return out


def palette_to_rgba(triplets):
    """16 (R,G,B) triplets -> 64 bytes of RGBA8888. Index 0 = transparent."""
    out = bytearray()
    for c in range(16):
        if c < len(triplets):
            r, g, b = triplets[c]
        else:
            r = g = b = 0
        a = 0 if c == 0 else 255
        out.extend([r, g, b, a])
    return bytes(out)


def find_sprite_dir(folder):
    """Return a folder name that has a usable front sprite.

    Tries folder as-is, then progressively strips trailing "_word" suffixes
    so SPECIES_DEOXYS_NORMAL -> deoxys, SPECIES_BURMY_PLANT -> burmy, etc.
    """
    candidates = [folder]
    parts = folder.split("_")
    for i in range(len(parts) - 1, 0, -1):
        candidates.append("_".join(parts[:i]))
    for c in candidates:
        d = os.path.join(SPRITE_DIR, c)
        if os.path.isdir(d):
            if (os.path.exists(os.path.join(d, "anim_front.png")) or
                os.path.exists(os.path.join(d, "front.png"))):
                return c
    return None

def convert(folder):
    """Returns (pixels_4bpp, pal_normal_rgba, pal_shiny_rgba_or_None) or None."""
    found = find_sprite_dir(folder)
    if not found:
        return None
    base = os.path.join(SPRITE_DIR, found)
    p = os.path.join(base, "anim_front.png")
    if not os.path.exists(p):
        p = os.path.join(base, "front.png")
    if not os.path.exists(p):
        return None
    img = Image.open(p)
    if img.mode != "P":
        # Sprite isn't indexed -- skip rather than guessing palette.
        return None
    img = img.crop((0, 0, 64, 64))
    pixels = list(img.getdata())  # 4096 indices 0..15
    if len(pixels) != 4096:
        return None
    # Pack 2 pixels per byte, high nibble = even (left) pixel.
    px = bytearray(2048)
    for i in range(2048):
        a = pixels[i * 2]     & 0x0F
        b = pixels[i * 2 + 1] & 0x0F
        px[i] = (a << 4) | b
    # Normal palette: from the PNG's own palette table. Some sprites have
    # fewer than 16 colors -- pad with black for the missing entries.
    pal_flat = img.getpalette() or []
    triplets = []
    for c in range(16):
        off = c * 3
        if off + 2 < len(pal_flat):
            triplets.append((pal_flat[off], pal_flat[off + 1], pal_flat[off + 2]))
        else:
            triplets.append((0, 0, 0))
    normal = palette_to_rgba(triplets)
    # Shiny palette: parse the JASC-PAL file if available.
    shiny_pal = parse_jasc_pal(os.path.join(base, "shiny.pal"))
    shiny = palette_to_rgba(shiny_pal) if shiny_pal else None
    return (bytes(px), normal, shiny)


def emit_byte_array(name, data, out_lines):
    out_lines.append(f"static const uint8_t {name}[{len(data)}] = {{")
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        out_lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    out_lines.append("};")


def main():
    id_to_folder = build_id_to_folder()
    print(f"Parsed {len(id_to_folder)} species IDs from species.h")

    out_lines = [
        "/* Auto-generated by tools/convert_sprites.py from pokeemerald-expansion. */",
        "/* 4bpp indexed sprites + 16-color RGBA palettes. ~2.1 KB per species. */",
        "#ifndef POKEBRIDGE_EMBEDDED_SPRITES_H",
        "#define POKEBRIDGE_EMBEDDED_SPRITES_H",
        "",
        "#include <stdint.h>",
        "",
        "#define PB_SPRITE_W 64",
        "#define PB_SPRITE_H 64",
        "#define PB_SPRITE_PIXEL_BYTES 2048  /* 64*64 / 2 */",
        "#define PB_SPRITE_PALETTE_BYTES 64  /* 16 RGBA8888 entries */",
        "",
    ]

    entries = {}
    skipped = []
    for species_id in range(1, MAX_ID + 1):
        folder = id_to_folder.get(species_id)
        if not folder:
            skipped.append((species_id, "(no enum)"))
            continue
        result = convert(folder)
        if result is None:
            skipped.append((species_id, folder))
            continue
        pixels, normal_pal, shiny_pal = result
        entries[species_id] = (folder, pixels, normal_pal, shiny_pal)
        sym_px = f"pb_px_{species_id:04d}"
        sym_pn = f"pb_pn_{species_id:04d}"
        out_lines.append(f"/* {folder} (#{species_id}) */")
        emit_byte_array(sym_px, pixels, out_lines)
        emit_byte_array(sym_pn, normal_pal, out_lines)
        if shiny_pal is not None:
            sym_ps = f"pb_ps_{species_id:04d}"
            emit_byte_array(sym_ps, shiny_pal, out_lines)
        out_lines.append("")

    out_lines.append("typedef struct {")
    out_lines.append("    uint16_t species;")
    out_lines.append("    const uint8_t *pixels;      /* 2048 bytes 4bpp */")
    out_lines.append("    const uint8_t *pal_normal;  /* 64 bytes RGBA */")
    out_lines.append("    const uint8_t *pal_shiny;   /* 64 bytes RGBA, or NULL */")
    out_lines.append("} pb_sprite_entry_t;")
    out_lines.append("")
    out_lines.append(f"static const pb_sprite_entry_t pb_sprite_table[{len(entries)}] = {{")
    for species_id in sorted(entries):
        sym_px = f"pb_px_{species_id:04d}"
        sym_pn = f"pb_pn_{species_id:04d}"
        sym_ps = f"pb_ps_{species_id:04d}" if entries[species_id][3] else "0"
        out_lines.append(f"    {{ {species_id}, {sym_px}, {sym_pn}, {sym_ps} }},")
    out_lines.append("};")
    out_lines.append(f"#define PB_SPRITE_TABLE_LEN {len(entries)}")
    out_lines.append("")
    out_lines.append("#endif")

    with open(OUT_HEADER, "w") as f:
        f.write("\n".join(out_lines))

    total = sum(len(px) + len(pn) + (len(ps) if ps else 0)
                for _, px, pn, ps in entries.values())
    print(f"Embedded {len(entries)} sprites, {total/1024/1024:.2f} MB total")
    if skipped:
        print(f"Skipped {len(skipped)}; first few: {skipped[:6]}")
    print(f"Wrote {OUT_HEADER}")


if __name__ == "__main__":
    main()
