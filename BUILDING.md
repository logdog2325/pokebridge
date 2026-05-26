# Building PokéBridge

## Prerequisites

You need **devkitPro** (toolchain + libraries) and **Python 3** with Pillow.

### macOS

```bash
brew install --cask devkitpro-pacman
sudo dkp-pacman -S --needed gamecube-dev gamecube-sdl2 gba-dev
pip3 install pillow
```

### Linux

Follow https://devkitpro.org/wiki/devkitPro_pacman to install `dkp-pacman`, then:

```bash
sudo dkp-pacman -S --needed gamecube-dev gamecube-sdl2 gba-dev
pip3 install pillow
```

### Windows

Use WSL2 (Ubuntu) and follow the Linux instructions. Native Windows builds are not tested.

## Environment

Set these in your shell rc (`.zshrc` / `.bashrc`):

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export DEVKITARM=$DEVKITPRO/devkitARM
```

## First-time setup

```bash
git clone https://github.com/<you>/pokebridge.git
cd pokebridge
./scripts/setup_assets.sh
```

This will:
1. Sparse-clone `pokeemerald-expansion` into `vendor/` (just the species table + sprites)
2. Clone FIX94's `gba-link-cable-dumper` into `vendor/`
3. Build the GBA-side dumper ROM using devkitARM
4. Generate `include/embedded_sprites.h` (all 1025 species, ~2 MB)
5. Generate `include/embedded_gba_mb.h` (60 KB dumper ROM)
6. Create empty `embedded_*save.h` stub files so the build doesn't error

The first sprite generation takes a few minutes (Python processes ~1000 PNGs).

## Build

```bash
make
```

Produces `pokebridge.dol`.

## Run

### Dolphin (no SD emulation; use embedded demo saves)

```bash
make run
# or
open -a "Dolphin 2" pokebridge.dol
```

Without embedded saves, the demo menu items will show "no save embedded — see README". Use the SD picker option to load from Dolphin's SD card emulation if you have it set up.

### Real GameCube via Swiss

1. Format SD card FAT32
2. Copy `pokebridge.dol` to `sd:/apps/pokebridge/boot.dol`
3. Create directory `sd:/pokebridge/saves/` and drop your `.sav` / `.gci` / `.gcs` files there
4. Boot the GameCube — Swiss should load
5. Navigate to `sd:/apps/pokebridge/boot.dol` and press A

## Embedding your own demo saves (optional)

For Dolphin development convenience, you can embed your own save files into the .dol:

```bash
python3 tools/embed_save.py firered  data/my_firered.sav
python3 tools/embed_save.py emerald  data/my_emerald.sav
python3 tools/embed_save.py xd       data/my_xd.gci
python3 tools/embed_save.py colo     data/my_colo.gcs
make
```

This regenerates the relevant `include/embedded_*save.h` files and the build picks them up.

**Do not commit the generated `embedded_*save.h` files.** They're in `.gitignore` for a reason — they contain copyrighted Pokémon save data.

## Native tests (for development)

The save parsers can be built natively on Mac/Linux for fast iteration:

```bash
cd tools
cc -I ../include -Wall -O2 ../source/save.c ../source/pokemon.c ../source/legalizer.c test_parse.c -o test_parse
./test_parse path/to/save.sav

cc -I ../include -Wall -O2 ../source/pb_xd.c ../source/genius_crypto.c test_xd.c -o test_xd
./test_xd path/to/xd.gci

cc -I ../include -Wall -O2 ../source/pb_colo.c ../source/sha1.c test_colo.c -o test_colo
./test_colo path/to/colosseum.gci
```

## Troubleshooting

### `make` fails with `embedded_sprites.h: No such file`
Run `./scripts/setup_assets.sh` first.

### Build size very large (>5 MB)
You probably embedded large save files. Empty out the `include/embedded_*save.h` files to remove them from the build.

### Sprites look color-swapped on real hardware
The 4bpp decoder uses `SDL_PIXELFORMAT_RGBA32` which is endianness-aware. If you see swapped colors, double-check that `SDL_CreateRGBSurfaceWithFormatFrom` is using `_WithFormat` (not the mask-based form). The mask-based form fails on big-endian PPC.

### Multiboot doesn't detect the GBA
- Verify the GBA is connected to a controller port (typically Port 2)
- Power on the GBA with no cart inserted; reach the multiboot screen (blue / "CRC ERROR")
- Check the link cable — Nintendo's GameCube–GBA cable (DOL-011) is required, not a generic GBA link cable

## Project layout

```
pokebridge/
├── source/         C source files
├── include/        Headers (incl. generated assets)
├── data/           Binary blobs to embed (sprites, dumper ROM)
├── docs/           Documentation
├── tools/          Native test drivers + Python helpers
├── scripts/        setup_assets.sh + other build helpers
├── vendor/         Cloned reference repos (gitignored)
├── Makefile
├── README.md
├── LICENSE
└── CREDITS.md
```
