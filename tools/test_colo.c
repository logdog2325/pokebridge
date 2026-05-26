/* Mac-native test for Colosseum parser.
 *
 * Build:  cc -I ../include -Wall -O2 ../source/pb_colo.c ../source/sha1.c test_colo.c -o test_colo
 * Run:    ./test_colo ../vendor/sample_colosseum.gcs
 */
#include "pb_colo.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <colosseum.gci>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    pb_colo_save_t cs;
    if (!pb_colo_load(&cs, buf, (size_t)sz)) {
        fprintf(stderr, "Failed to load Colosseum save\n");
        free(buf); return 1;
    }
    printf("Trainer    : \"%s\"\n", cs.trainer_name);
    printf("TID / SID  : %u / %u\n", cs.trainer_id, cs.secret_id);
    printf("Active slot: %d\n", cs.active_slot);
    printf("Save count : %u\n", (unsigned)cs.save_count);
    printf("Party      : %u/6\n\n", cs.party_count);

    for (int i = 0; i < 6; i++) {
        pb_colo_pkm_t p;
        pb_colo_pkm_decode(&p, cs.slot + PB_COLO_PARTY_OFF + i * PB_COLO_PKM_SIZE);
        if (p.is_empty) { printf("  %d. (empty)\n", i + 1); continue; }
        printf("  %d. \"%s\" CXD#%u L%u%s\n",
               i + 1, p.nickname, p.species_internal,
               p.level, p.is_shadow ? " [SHADOW]" : "");
        printf("     OT:\"%s\" TID:%u  PID:0x%08X  EXP:%u\n",
               p.ot_name, p.trainer_id, p.pid, p.exp);
        printf("     Moves: %u / %u / %u / %u\n",
               p.moves[0], p.moves[1], p.moves[2], p.moves[3]);
        printf("     IVs:   %u/%u/%u/%u/%u/%u\n",
               p.iv[0], p.iv[1], p.iv[2], p.iv[3], p.iv[4], p.iv[5]);
    }
    free(buf);
    return 0;
}
