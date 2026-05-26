/* Mac-native test driver. Compiles save.c + pokemon.c + legalizer.c outside
 * of libogc and dumps the contents of a Gen 3 .sav file so we can verify the
 * parser before deploying to GameCube hardware.
 *
 * Build:
 *   cc -I ../include -Wall -O2 \
 *      ../source/save.c ../source/pokemon.c ../source/legalizer.c \
 *      test_parse.c -o test_parse
 * Run:
 *   ./test_parse path/to/save.sav
 */
#include "save.h"
#include "pokemon.h"
#include "legalizer.h"
#include <stdio.h>
#include <string.h>

static void print_party_pkm(int idx, const uint8_t *raw) {
    pb_pkm_t p;
    pb_pkm_decode(&p, raw);
    if (p.is_empty) {
        printf("  %d. (empty slot)\n", idx + 1);
        return;
    }
    char nickname[16] = {0}, otname[12] = {0};
    pb_gen3_to_ascii(p.nickname, 10, nickname);
    pb_gen3_to_ascii(p.ot_name, 7, otname);
    uint8_t ivs[6];
    pb_pkm_ivs(&p, ivs);

    printf("  %d. \"%s\" -- %s (#%u)  OT:%s\n",
           idx + 1, nickname, pb_species_name(p.g.species), p.g.species, otname);
    printf("     PID=0x%08X  OT_ID=0x%08X  Cksum:%s\n",
           p.pid, p.ot_id, p.checksum_ok ? "OK" : "BAD");
    printf("     Moves: %u / %u / %u / %u\n",
           p.a.moves[0], p.a.moves[1], p.a.moves[2], p.a.moves[3]);
    printf("     IVs:   %u/%u/%u/%u/%u/%u  (HP/Atk/Def/Spe/SpA/SpD)\n",
           ivs[0], ivs[1], ivs[2], ivs[3], ivs[4], ivs[5]);
    printf("     EVs:   %u/%u/%u/%u/%u/%u\n",
           p.e.ev[0], p.e.ev[1], p.e.ev[2], p.e.ev[3], p.e.ev[4], p.e.ev[5]);
    printf("     Item:%u  Friend:%u  Exp:%u\n",
           p.g.held_item, p.g.friendship, p.g.experience);

    if (p.g.species > 386) {
        uint16_t remap = pb_species_remap(p.g.species);
        printf("     Hack-mon -> would legalize to #%u (%s)\n",
               remap, pb_species_name(remap));
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <savfile>\n", argv[0]);
        return 1;
    }

    pb_save_t s;
    if (!pb_save_load_file(&s, argv[1])) {
        fprintf(stderr, "FAILED to parse %s\n", argv[1]);
        return 1;
    }

    printf("=== %s ===\n\n", argv[1]);
    printf("Game        : %s\n", pb_game_name(s.game));
    printf("Trainer     : \"%s\"\n", s.trainer_name);
    printf("TID/SID     : %u / %u\n", s.trainer_id, s.secret_id);
    printf("Gender      : %s\n", s.gender ? "F" : "M");
    printf("Save index  : %u\n", s.save_index);
    printf("Security key: 0x%08X\n", s.security_key);

    uint8_t count = 0;
    const uint8_t *party = NULL;
    if (!pb_save_party(&s, &count, &party)) {
        printf("\nCouldn't locate party.\n");
        return 1;
    }
    printf("\nParty: %u/6\n", count);
    for (int i = 0; i < count; i++) {
        print_party_pkm(i, party + i * PB_PKM_PARTY_SIZE);
        printf("\n");
    }

    /* Peek at PC box 1, slot 1 too. */
    const uint8_t *box1 = pb_save_box(&s, 0);
    if (box1) {
        printf("PC Box 1 slot 1:\n");
        print_party_pkm(0, box1);
    }
    return 0;
}
