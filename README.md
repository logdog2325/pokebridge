# PokéBridge

A **GameCube homebrew app** for reading, editing, and exporting Pokémon Gen 3 saves — covering Ruby, Sapphire, Emerald, FireRed, LeafGreen, **all pokeemerald-expansion ROM hacks** (Cassora, Seaglass, Lazarus, etc.), and the two Gen 3 GameCube titles **Pokémon XD: Gale of Darkness** and **Pokémon Colosseum**.

The interface is fully graphical with a Pokémon Box: Ruby & Sapphire aesthetic — pastel-blue gradient backgrounds, rounded translucent panels, real Gen 3-style sprites for **all 1025 species + shiny variants**, and box-art previews for every mainstream Gen 3-era game.

## What it does

- **Read** Gen 3 GBA `.sav` files (128 KB) directly from SD card via Swiss
- **Read** Pokémon XD `.gci` files (encrypted with GeniusCrypto)
- **Read** Pokémon Colosseum `.gci` files (encrypted with SHA-1 chain XOR)
- **Edit** any Pokémon: IVs, EVs, moves, nature, shiny toggle, friendship, held item
  - Shiny toggle preserves nature via PID re-rolling; nature edit preserves shiny status
  - Sprites swap to the shiny variant live in the preview
- **Write** edited saves back to SD with correct checksums + encryption for the source format
- **Legalize** ROM-hack Pokémon into Gen 3-compatible `.pk3` files for the Pokémon HOME transfer chain (Cassora-aware species/move remap)
- **Read GBA cart saves over the link cable** by multiboot-loading FIX94's dumper onto a connected GBA (untested — needs hardware verification)

## Screenshots

### Main menu — game-themed box art previews

![Main menu with FireRed box art preview](docs/screenshots/main_menu.png)

### XD party browser — MARK's iconic team, with detail panel

![Pokemon XD party screen — MARK's team](docs/screenshots/xd_party.png)

### Editor — live shiny sprite swap

![Editor: Charizard with shiny toggled — sprite swaps to the black/silver variant in real time](docs/screenshots/edit_shiny.png)

### Move picker — full 354 Gen 3 moves, paged

![Move picker, paged through all 354 Gen 3 moves](docs/screenshots/moves_picker.png)

### Game Art Gallery — every mainstream Gen 3-era title

![Game Art Gallery with Pokemon Emerald / Rayquaza featured](docs/screenshots/game_art_gallery.png)

## Quick start

### Build prerequisites

- **devkitPPC** with libogc, libfat-ogc, SDL2, SDL2_test
- **devkitARM** (to build the GBA-side dumper ROM)
- **Python 3** with Pillow (`pip install pillow`) for sprite conversion
- macOS, Linux, or WSL

Install via `dkp-pacman`:

```bash
sudo dkp-pacman -S --needed gamecube-dev gamecube-sdl2 gba-dev
```

### Clone + set up assets

```bash
git clone https://github.com/<you>/pokebridge.git
cd pokebridge
./scripts/setup_assets.sh
```

The setup script:
1. Sparse-clones `pokeemerald-expansion` (for the 1025-species sprite set)
2. Clones `FIX94/gba-link-cable-dumper`, builds the GBA-side multiboot ROM
3. Generates `include/embedded_sprites.h` (~2 MB, 4bpp + palette) covering all 1025 species + shiny
4. Generates `include/embedded_gba_mb.h` (60 KB) from the dumper ROM
5. Creates stub `embedded_*save.h` files (empty placeholders for the optional demo saves)

### Build

```bash
make
```

Produces `pokebridge.dol` (~3 MB without demo saves, larger if you embed your own).

### Install on GameCube

Copy to your Swiss-formatted SD card:

```
sd:/apps/pokebridge/boot.dol     ← rename pokebridge.dol to boot.dol
sd:/pokebridge/saves/            ← drop your .sav / .gci / .gcs files here
sd:/pokebridge/export/           ← legalized .pk3 files land here
```

Boot via Swiss → `sd:/apps/pokebridge/boot.dol`. The graphics menu loads directly.

## Optional: embed your own demo saves

If you want save files baked into the `.dol` (so they work without an SD card — handy for Dolphin testing), drop them into the `data/` folder and run:

```bash
python3 tools/embed_save.py firered  data/my_firered.sav
python3 tools/embed_save.py emerald  data/my_emerald.sav
python3 tools/embed_save.py xd       data/my_pokemon_xd.gci
python3 tools/embed_save.py colo     data/my_pokemon_colosseum.gcs
make
```

Each command writes `include/embedded_<kind>_save.h` which the build picks up automatically.

**Note:** the `embedded_*save.h` files are gitignored — don't commit them. They contain user save data which is technically copyrighted.

## How it works

PokéBridge is several modular pieces:

### Save parsers

- **`source/save.c`** — Gen 3 GBA save: 14 sections of 4096 bytes per slot, latest-slot detection by save_index, full Trainer Info + Team/Items + PC Storage layout. Little-endian throughout.
- **`source/pokemon.c`** — Gen 3 Pokémon: 80-byte boxed format with PID/OT-XOR encryption and 24-permutation substructure shuffle. Verified byte-for-byte against PKHeX's `BlockPosition` table.
- **`source/pb_xd.c`** + **`source/genius_crypto.c`** — Pokémon XD: `SAV3XD` slot layout + GeniusCrypto stream cipher (4 BE u16 keys, additive). Dynamic sub-offset resolution. CK3 / XK3 (196-byte XD Pokémon).
- **`source/pb_colo.c`** + **`source/sha1.c`** — Pokémon Colosseum: 3-slot save container, SHA-1 chain XOR encryption (digest = inverted at-rest hash, then XOR + re-hash per 20-byte block). CK3 (312-byte Colosseum Pokémon).

All four formats expose a common edit surface via `pb_pkm_t`. Edits write back through format-specific apply functions (`pb_pkm_encode`, `pb_xk3_apply_pkm_edits`, `pb_ck3_apply_pkm_edits`) with checksum recomputation + re-encryption as appropriate.

### Legalizer

ROM-hack Pokémon (species ID > 386) are mapped to nearest Gen 3 analogues via a hand-curated table (`include/cassora_species_map.h`) — pseudos map to Bagon line, regional starters to Gen 3 starters of matching type, etc. Output is a PKHeX-compatible 80-byte `.pk3` file that rides the legitimate Gen 3 → Pal Park → Gen 4 → Bank → HOME chain (see [docs/HOME_TRANSFER.md](docs/HOME_TRANSFER.md)).

### Graphics

- **SDL2** on devkitPPC's GameCube target. 640×480 framebuffer.
- **Sprite cache** is 2-bank (normal + shiny) keyed by species ID. Sprites are stored 4bpp indexed (16-color palette) and decoded to RGBA on first use, then cached as `SDL_Texture`.
- **Box art** is procedural — gradient panel + cover-species sprite + game name. Eight games: Ruby/Sapphire/Emerald/FireRed/LeafGreen + Colosseum/XD/Pokémon Box: R&S.
- **Editor** is graphics-mode throughout with sub-screens for each field. Shiny re-rolls PID under a nature-preserving constraint; nature edit preserves shiny status.

### GBA link cable (untested)

Mirror of FIX94's `gba-link-cable-dumper`. Detects a GBA on a controller port, runs the Nintendo multiboot handshake to upload a 60 KB dumper ROM, then the dumper streams the cart's SRAM back. Untested without a physical cable but the protocol port is faithful.

## Status

| Format | Read | Edit | Write |
|--------|------|------|-------|
| Gen 3 GBA (Ruby/Sapphire/Emerald/FireRed/LeafGreen) | ✅ | ✅ | ✅ |
| pokeemerald-expansion ROM hacks (Cassora, Seaglass, Lazarus...) | ✅ | ✅ | ✅ |
| Pokémon XD: Gale of Darkness | ✅ | ✅ | ✅ |
| Pokémon Colosseum | ✅ | ✅ | ✅ |
| GBA cart over link cable | 🟡 ported, untested | n/a | n/a |
| Pokémon Box: Ruby & Sapphire | ❌ planned | ❌ | ❌ |

## Credits

See [CREDITS.md](CREDITS.md). Standing on the shoulders of:
- **PKHeX** by kwsch — save format documentation and algorithm references
- **FIX94's gba-link-cable-dumper** — GBA multiboot + save dump protocol
- **pokeemerald-expansion** by rh-hideout — Gen 3-style sprites for 1025 species
- **devkitPro** — toolchain + libogc + SDL2 port

## License

GPL-3.0. See [LICENSE](LICENSE).

The "Pokémon" name and all species names are trademarks of Nintendo / Game Freak / The Pokémon Company. Nominative use only.
