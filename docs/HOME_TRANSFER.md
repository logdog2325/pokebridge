# How a ROM-hack Pokémon gets to Pokémon HOME

**Short answer:** PokéBridge can produce HOME-eligible Gen 3 `.pk3` files, but
the GameCube cannot complete the chain on its own. Bank/HOME require Nintendo's
online services and current-gen hardware. PokéBridge gets you to the doorstep.

## The legitimate chain

Pokémon HOME accepts data from a small set of upstream sources. The relevant
chain for a Gen 3 mon is:

```
Gen 3 cart (.sav)               ← this is what PokéBridge reads
   │
   │  PokéBridge → produces a legal .pk3 (80 bytes) that a real Gen 3 game
   │  would accept. The mon's species, moves, item, and ability are remapped
   │  to legal Gen 3 values; PID/IVs/OT/checksum are recomputed.
   ▼
Gen 3 cart (legit Ruby/Sapphire/FR/LG/Emerald)
   │
   │  Migration: in-game NPC ("Pal Park" on Gen 4) accepts six Gen 3 mons
   │  at a time. One-way, one-time per game.
   ▼
Gen 4 DS cart (Diamond/Pearl/Platinum/HeartGold/SoulSilver)
   │
   │  Pokémon Bank — Nintendo's paid Gen 7 transfer service. Bank moved Gen
   │  4–7 mons up; Poké Transporter handles the Gen 4/5 → Bank step.
   ▼
Pokémon Bank (3DS)
   │
   │  Bank → HOME bridge (Nintendo enabled this; one-way).
   ▼
Pokémon HOME
```

## What PokéBridge does

1. **Reads** a Gen 3 save (from SD, or later from a GBA cart via link cable).
2. **Decodes** every party + boxed pokemon — including the PID-scrambled
   substructure data, IVs, EVs, moves, OT, friendship, ribbons.
3. **Remaps hack data** to legal Gen 3 equivalents:
   - Species ID > 386 → mapped (e.g. a hack-only Deino → some Gen 3 dragon).
   - Move ID > 354 → mapped or dropped.
   - Held item > 376 → cleared.
4. **Re-encrypts** the record and writes an 80-byte `.pk3` to
   `sd:/pokebridge/export/`.

The user can then drop those `.pk3` files into [PKHeX](https://github.com/kwsch/PKHeX)
and inject them into a legitimate Gen 3 save, or directly into a higher-gen
save (skipping Pal Park / Bank, which PKHeX supports).

## What it doesn't do

- **Bank/HOME communication.** Those are HTTPS APIs into Nintendo's services.
  Running them from a GameCube is technically possible (BBA + TLS stack) but
  would violate ToS and risk account bans. Out of scope.
- **Legality enforcement.** A remap can produce a syntactically valid Gen 3
  mon that's still flagged as illegal by HOME's server-side check (e.g.
  impossible move combination, wrong met location). PKHeX's auto-legalize
  plugin handles the final polish step.

## Why "PKHeX-compatible" matters

PKHeX is the canonical save editor; its `.pk3` parser is the de-facto reference.
By emitting `.pk3` (raw 80-byte boxed-pokemon format with re-encrypted
substructures and correct checksum), PokéBridge interoperates with everything
PKHeX touches:

- Drag-drop the `.pk3` into PKHeX
- Edit further if needed
- Inject into any Gen 3+ save
- Run the legality analyzer


## ROM-hack species mapping

ROM hacks based on `pokeemerald-expansion` typically use Gen 1–9 species IDs
beyond Gen 3's 1..386 range. `include/rom_hack_species_map.h` provides a
curated 387..1024 → 1..386 lookup. Strategy per entry:

- **Pseudo-legendaries** map to Gen 3 pseudos (Bagon → Salamence line)
- **Starters** map to Gen 3 starters of matching type
- **Cross-gen evolutions** map back to their Gen 3 prevolution
- **Regional form variants** map to their base species
- Anything not in the curated set falls back to Unown (#201) as a clear
  "needs manual review" marker

Same approach for moves — pokeemerald-expansion's Gen 4+ moves with known
Gen 3 analogues (Bullet Punch ↔ ThunderPunch, Aqua Jet ↔ Quick Attack, etc.)
get mapped; the rest are dropped to move 0 with PP cleared.
