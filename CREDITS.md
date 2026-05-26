# Credits

PokéBridge stands on the shoulders of several existing projects. Every algorithm port below preserves the original author's copyright and license; the resulting C ports are released under GPL-3.0 to comply with PKHeX's terms.

## Major upstream projects

### PKHeX (Save format reference)
- **Author:** kwsch and contributors
- **Repository:** https://github.com/kwsch/PKHeX
- **License:** GPL-3.0
- **What we use:**
  - Gen 3 Pokémon substructure layout (`PK3.cs`)
  - 24-permutation `BlockPosition` table for PID-based substructure shuffling
  - XOR encryption with `PID ^ OID` seed
  - XD save format (`SAV3XD.cs`, `XK3.cs`)
  - Colosseum save format (`SAV3Colosseum.cs`, `CK3.cs`)
  - GeniusCrypto (XD encryption, `ColoCrypto.cs`)
  - ColoCrypto (Colosseum SHA-1 chain encryption)
  - English species + move name tables

PKHeX is THE reference for Gen 3-9 save manipulation. PokéBridge couldn't exist without it.

### FIX94's gba-link-cable-dumper
- **Author:** FIX94
- **Repository:** https://github.com/FIX94/gba-link-cable-dumper
- **License:** MIT
- **What we use:**
  - GameCube ↔ GBA joybus multiboot protocol (key derivation, session-key handshake, encrypted ROM upload, CRC echo)
  - GBA-side save dumper ROM (we build this from FIX94's source via devkitARM and embed the resulting `gba_mb.gba`)

### pokeemerald-expansion
- **Maintainer:** rh-hideout
- **Repository:** https://github.com/rh-hideout/pokeemerald-expansion
- **License:** AGPL-3.0 / GPL-3.0
- **What we use:**
  - `anim_front.png` front sprites for all 1025 species
  - `shiny.pal` JASC-PAL palettes for shiny variants
  - `include/constants/species.h` for the species name → natdex ID map

The full sprite set is converted at setup time via `tools/convert_sprites.py` to a compact 4bpp + 16-color-palette format (~2 MB total, vs. 32 MB raw RGBA).

### devkitPro
- **Maintainers:** WinterMute, fincs, et al.
- **Site:** https://devkitpro.org/
- **License:** various (BSD-style for libogc; per-package for portlibs)
- **What we use:** devkitPPC compiler, libogc (GameCube SDK), libfat-ogc, gamecube-sdl2 + SDL2_test (built-in 8×8 font)

## Other references

- **gc-forever forum threads** on the GameCube–GBA joybus protocol — confirmed the SI command sequence used by Pokémon Box and Pokémon Channel
- **Bulbapedia** + **PokéCommunity** for save format documentation
- **TuxSH/PkmGCTools** (LGPL-3.0) — referenced for CXD format details

## Original work in this repository

- All `source/*.c` and `include/*.h` files written for PokéBridge (UI, save dispatch, graphics module, editor, legalizer)
- `source/sha1.c` — FIPS-180 SHA-1 implementation written from spec (verified against the three canonical test vectors)
- `tools/convert_sprites.py` — sprite-conversion pipeline (Python)
- `tools/embed_save.py` — user-save embedding helper (Python)
- `scripts/setup_assets.sh` — one-shot dependency setup

## Acknowledgments

- The user, for the cool homebrew idea and patient real-hardware testing.
