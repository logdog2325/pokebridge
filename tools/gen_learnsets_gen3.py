#!/usr/bin/env python3
"""Generate include/learnsets_gen3.h from vanilla pokeemerald data.

Sources (fetched by hand into tools/learnset_gen/):
  - level_up_learnsets.h    -- LEVEL_UP_MOVE(lvl, MOVE_X) per species
  - tmhm_learnsets.h        -- struct TMHMLearnset { .MOVE = TRUE, ... }

Constants (from vendor/pokeemerald-expansion clone):
  - include/constants/moves.h   -- MOVE_X = N enum
  - include/constants/species.h -- SPECIES_X = N enum

Output: include/learnsets_gen3.h containing per-species sorted uint16
lists of legally-learnable Gen 3 move IDs, plus a species index table.
Only Gen 3 species (natdex 1..386) and Gen 3 moves (id 1..354) are
included; expansion adds newer ones we don't care about.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(ROOT, "tools", "learnset_gen")
CONSTS = os.path.join(ROOT, "vendor", "pokeemerald-expansion", "include", "constants")
OUT = os.path.join(ROOT, "include", "learnsets_gen3.h")

GEN3_MAX_SPECIES = 386
GEN3_MAX_MOVE = 354


def parse_enum(path, prefix):
    """Parse a C enum where each line has `PREFIX_NAME = N,` and return {name: n}."""
    name_to_id = {}
    with open(path) as f:
        text = f.read()
    for m in re.finditer(rf"^\s*({re.escape(prefix)}[A-Z0-9_]+)\s*=\s*(\d+|0x[0-9a-fA-F]+)", text, re.MULTILINE):
        name = m.group(1)
        val = int(m.group(2), 0)
        # Skip aliases where the RHS is another identifier (e.g. DOUBLESLAP = DOUBLE_SLAP).
        name_to_id.setdefault(name, val)
    return name_to_id


def parse_enum_sequential(path, prefix):
    """Some pokeemerald constants are defined implicitly by enum order
    without explicit = N. Fall back to walking the enum block in order."""
    name_to_id = {}
    with open(path) as f:
        text = f.read()
    # Find the enum block
    m = re.search(rf"enum[^{{]*{{(.+?)}};", text, re.DOTALL)
    if not m:
        return name_to_id
    body = m.group(1)
    counter = 0
    for line in body.splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        # Match "NAME" or "NAME = VALUE" or "NAME = OTHER_NAME"
        em = re.match(rf"^({re.escape(prefix)}[A-Z0-9_]+)\s*(?:=\s*(\S+))?$", line)
        if not em:
            continue
        name = em.group(1)
        val = em.group(2)
        if val is None:
            name_to_id[name] = counter
            counter += 1
        else:
            if val.isdigit() or val.startswith("0x"):
                counter = int(val, 0)
                name_to_id[name] = counter
                counter += 1
            elif val in name_to_id:
                name_to_id[name] = name_to_id[val]
            # Ignore complex aliases we can't resolve
    return name_to_id


def parse_level_up(path, move_id, species_id):
    """Parse `static const u16 sBulbasaurLevelUpLearnset[] = { LEVEL_UP_MOVE(N, MOVE_X), ..., LEVEL_UP_END };`
    into {species_id: set(move_ids)}."""
    result = {}
    with open(path) as f:
        text = f.read()
    for m in re.finditer(
            r"static const u16 s([A-Za-z0-9_]+)LevelUpLearnset\[\]\s*=\s*\{([^}]*)\}",
            text, re.DOTALL):
        species_key = m.group(1).upper()
        body = m.group(2)
        # Map friendly key back to SPECIES_ constant
        sp_name = "SPECIES_" + species_key
        # Handle nidoran gender-specific etc. that use _M/_F variants
        if sp_name not in species_id:
            # Try inserting underscores between lowercase-uppercase transitions
            # (e.g. "MrMime" -> "MR_MIME"). Simple heuristic.
            variants = [sp_name, "SPECIES_" + species_key.replace("MRMIME", "MR_MIME")]
            for v in variants:
                if v in species_id:
                    sp_name = v
                    break
        sp_id = species_id.get(sp_name)
        if sp_id is None or sp_id < 1 or sp_id > GEN3_MAX_SPECIES:
            continue
        moves_seen = set()
        for mm in re.finditer(r"LEVEL_UP_MOVE\(\s*\d+\s*,\s*(MOVE_[A-Z0-9_]+)\s*\)", body):
            mv_id = move_id.get(mm.group(1))
            if mv_id and 1 <= mv_id <= GEN3_MAX_MOVE:
                moves_seen.add(mv_id)
        result.setdefault(sp_id, set()).update(moves_seen)
    return result


def parse_tmhm(path, move_id, species_id):
    """Parse `[SPECIES_X] = { .learnset = { .MOVE_X = TRUE, ... } }` blocks
    into {species_id: set(move_ids)}."""
    result = {}
    with open(path) as f:
        text = f.read()
    # Find entries of the form:
    #   [SPECIES_BULBASAUR] = { .learnset = { .TOXIC = TRUE, ..., } },
    # or without the .learnset wrapper.
    pat = re.compile(
        r"\[(SPECIES_[A-Z0-9_]+)\]\s*=\s*\{\s*(?:\.learnset\s*=\s*)?\{([^}]*)\}",
        re.DOTALL)
    for m in pat.finditer(text):
        sp_name = m.group(1)
        sp_id = species_id.get(sp_name)
        if sp_id is None or sp_id < 1 or sp_id > GEN3_MAX_SPECIES:
            continue
        moves = set()
        # Field names correspond to MOVE_X (without the MOVE_ prefix)
        for field in re.finditer(r"\.([A-Z][A-Z0-9_]*)\s*=\s*TRUE", m.group(2)):
            fname = field.group(1)
            candidates = [f"MOVE_{fname}"]
            for cand in candidates:
                mv_id = move_id.get(cand)
                if mv_id and 1 <= mv_id <= GEN3_MAX_MOVE:
                    moves.add(mv_id)
                    break
        result.setdefault(sp_id, set()).update(moves)
    return result


def parse_defines(path, prefix):
    """Parse `#define PREFIX_NAME N` lines and return {name: n}."""
    name_to_id = {}
    with open(path) as f:
        text = f.read()
    for m in re.finditer(
            rf"^\s*#define\s+({re.escape(prefix)}[A-Z0-9_]+)\s+(\d+|0x[0-9a-fA-F]+)",
            text, re.MULTILINE):
        name_to_id.setdefault(m.group(1), int(m.group(2), 0))
    return name_to_id


def main():
    move_id = parse_enum_sequential(os.path.join(CONSTS, "moves.h"), "MOVE_")
    species_id = parse_defines(os.path.join(CONSTS, "species.h"), "SPECIES_")
    print(f"Parsed {len(move_id)} moves, {len(species_id)} species from constants")

    lvl = parse_level_up(
        os.path.join(DATA_DIR, "level_up_learnsets.h"), move_id, species_id)
    print(f"Level-up learnsets: {len(lvl)} species with data")

    tm = parse_tmhm(os.path.join(DATA_DIR, "tmhm_learnsets.h"), move_id, species_id)
    print(f"TM/HM learnsets: {len(tm)} species with data")

    all_moves = {}
    for sp in range(1, GEN3_MAX_SPECIES + 1):
        s = set()
        s.update(lvl.get(sp, set()))
        s.update(tm.get(sp, set()))
        if s:
            all_moves[sp] = sorted(s)

    print(f"Total species with learnable moves: {len(all_moves)}")

    # Emit as a flat bitmap. One bit per (species, move) pair. 386
    # species × 354 moves, packed 8 bits per byte, indexed as
    #   bit = species * BYTES_PER_SPECIES * 8 + move_id
    # ->  byte = bit / 8, mask = 1 << (bit % 8).
    # This produces ONE static symbol (17 KB) instead of 384 separate
    # arrays + a pointer table (which broke the .dol boot in the
    # earlier per-species-array layout).
    BYTES_PER_SPECIES = (GEN3_MAX_MOVE + 8) // 8  # 45 bytes covers 1..354 with room
    TABLE_SIZE = (GEN3_MAX_SPECIES + 1) * BYTES_PER_SPECIES

    bitmap = bytearray(TABLE_SIZE)
    for sp, moves in all_moves.items():
        base = sp * BYTES_PER_SPECIES
        for mv in moves:
            if 1 <= mv <= GEN3_MAX_MOVE:
                bitmap[base + (mv >> 3)] |= 1 << (mv & 7)

    with open(OUT, "w") as f:
        f.write("/* Auto-generated by tools/gen_learnsets_gen3.py. */\n")
        f.write("/* Source: pret/pokeemerald master + pokeemerald-expansion constants. */\n")
        f.write("/* Layout: flat bitmap, one bit per (species, move_id) pair. */\n")
        f.write("#ifndef POKEBRIDGE_LEARNSETS_GEN3_H\n")
        f.write("#define POKEBRIDGE_LEARNSETS_GEN3_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define PB_LEARNSET_BYTES_PER_SPECIES {BYTES_PER_SPECIES}\n\n")
        f.write(f"static const uint8_t pb_learnset_gen3_bits[{TABLE_SIZE}] = {{\n")
        for i in range(0, TABLE_SIZE, 16):
            row = bitmap[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02X}" for b in row) + ",\n")
        f.write("};\n\n")
        f.write("#endif\n")

    print(f"Wrote {OUT} ({TABLE_SIZE} bytes = {TABLE_SIZE/1024:.1f} KB bitmap)")


if __name__ == "__main__":
    main()
