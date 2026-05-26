# How a Cassora Pokémon gets to Pokémon HOME

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
   - Species ID > 386 → mapped (e.g. Cassora's Deino → some Gen 3 dragon).
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

## Cassora-specific mapping (TODO)

Cassora's `POKEDEX.md` (currently 229 species, IDs 1..229 reused + new IDs
in the 387..N range) needs an explicit `custom_id → official_id` table. The
plan is to generate `include/cassora_species_map.h` from POKEDEX.md so that:

- Custom regional variants → their base species (e.g. Cassora-Cedaroar → Houndoom)
- Cassora-exclusive starters → flavor-matching Gen 3 starters
- Cassora's pseudo-legendaries → Gen 3 pseudo-legendaries by type match

Same approach for moves — pokeemerald-expansion's move IDs above 354 map to
Gen 4+ moves with known Gen 3 analogues (Bullet Punch ↔ ThunderPunch,
Aqua Jet ↔ Quick Attack, etc.) or get dropped.
