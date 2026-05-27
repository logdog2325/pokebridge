#!/usr/bin/env bash
# Set up build assets after a fresh clone of pokebridge.
#
# This script:
#   1. Verifies devkitPPC + devkitARM + Python+Pillow are present
#   2. Sparse-clones pokeemerald-expansion for sprite/species data
#   3. Clones FIX94/gba-link-cable-dumper and builds the GBA-side ROM
#   4. Generates include/embedded_sprites.h via tools/convert_sprites.py
#   5. Generates include/embedded_gba_mb.h from the built ROM
#   6. Creates empty stub embedded_*save.h files
#
# Run from the repo root: ./scripts/setup_assets.sh

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "==> PokéBridge asset setup"
echo "    Repo root: $REPO_ROOT"

# 1. Check toolchain
: "${DEVKITPRO:?DEVKITPRO not set. Install devkitPro and export DEVKITPRO=/opt/devkitpro}"
: "${DEVKITPPC:?DEVKITPPC not set. Export DEVKITPPC=\$DEVKITPRO/devkitPPC}"
: "${DEVKITARM:?DEVKITARM not set. Export DEVKITARM=\$DEVKITPRO/devkitARM}"
if [[ ! -x "$DEVKITPPC/bin/powerpc-eabi-gcc" ]]; then
    echo "ERROR: devkitPPC GCC not found at $DEVKITPPC/bin/powerpc-eabi-gcc"
    exit 1
fi
if [[ ! -x "$DEVKITARM/bin/arm-none-eabi-gcc" ]]; then
    echo "ERROR: devkitARM GCC not found at $DEVKITARM/bin/arm-none-eabi-gcc"
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found"
    exit 1
fi
if ! python3 -c "from PIL import Image" 2>/dev/null; then
    echo "ERROR: Pillow not installed. Run: pip3 install pillow"
    exit 1
fi

mkdir -p vendor data include

# 2. Sparse-clone pokeemerald-expansion (sprites + species enum only)
if [[ ! -d vendor/pokeemerald-expansion/.git ]]; then
    echo "==> Cloning pokeemerald-expansion (sparse: graphics/pokemon + include/constants)..."
    git clone --depth=1 --filter=blob:none --sparse \
        https://github.com/rh-hideout/pokeemerald-expansion.git \
        vendor/pokeemerald-expansion
    (cd vendor/pokeemerald-expansion && \
     git sparse-checkout set graphics/pokemon include/constants)
else
    echo "==> pokeemerald-expansion already present"
fi

# 3. Clone FIX94/gba-link-cable-dumper
if [[ ! -d vendor/gba-link-cable-dumper/.git ]]; then
    echo "==> Cloning FIX94/gba-link-cable-dumper..."
    git clone --depth=1 https://github.com/FIX94/gba-link-cable-dumper.git \
        vendor/gba-link-cable-dumper
else
    echo "==> gba-link-cable-dumper already present"
fi

# 4. Build the GBA-side dumper ROM
echo "==> Building GBA-side dumper ROM..."
(cd vendor/gba-link-cable-dumper/gba && make)
cp vendor/gba-link-cable-dumper/gba/gba_mb.gba data/gba_mb.gba

# 5. Generate embedded_gba_mb.h
echo "==> Generating include/embedded_gba_mb.h..."
xxd -i -n pb_embedded_gba_mb data/gba_mb.gba > include/embedded_gba_mb.h

# 6. Generate embedded_sprites.h
echo "==> Generating include/embedded_sprites.h (all 1025 species + shiny)..."
python3 tools/convert_sprites.py 1025

# 7. Create stub embedded save files if they don't exist
for kind in save emerald_save xd_save colo_save; do
    sym="pb_embedded_${kind%_save}_sav"
    [[ "$kind" == "save" ]] && sym="pb_embedded_firered_sav"
    h="include/embedded_${kind}.h"
    if [[ ! -f "$h" ]]; then
        echo "==> Creating empty stub $h"
        {
            echo "/* Placeholder for a personal save file (gitignored). */"
            echo "/* See README.md > 'Optional: embed your own demo saves'. */"
            echo "#ifndef POKEBRIDGE_$(echo $kind | tr a-z A-Z)_H"
            echo "#define POKEBRIDGE_$(echo $kind | tr a-z A-Z)_H"
            echo "#include <stdint.h>"
            echo "static const uint8_t ${sym}[1] = {0};"
            echo "static const unsigned int ${sym}_len = 0;"
            echo "#endif"
        } > "$h"
    fi
done

# 8. Stubs for the embedded background music playlist. Use
# embed_audio.py to replace any of these with a real song.
for atrack in title match colosseum; do
    h="include/embedded_${atrack}_audio.h"
    if [[ ! -f "$h" ]]; then
        sym="pb_embedded_${atrack}_audio"
        GUARD="POKEBRIDGE_EMBEDDED_$(echo "$atrack" | tr a-z A-Z)_AUDIO_H"
        echo "==> Creating empty stub $h"
        {
            echo "/* Placeholder for a personal background-music WAV (gitignored). */"
            echo "/* See README.md > 'Optional: embed background music'. */"
            echo "#ifndef $GUARD"
            echo "#define $GUARD"
            echo "#include <stdint.h>"
            echo "#define ${sym}_sample_rate 22050"
            echo "#define ${sym}_channels    1"
            echo "#define ${sym}_bits        16"
            echo "#define ${sym}_len         0"
            echo "static const uint8_t ${sym}[1] = {0};"
            echo "#endif"
        } > "$h"
    fi
done

echo ""
echo "==> Setup complete. You can now build with:"
echo "    make"
