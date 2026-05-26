/* Mac-native test driver for the XD save parser.
 *
 * Build:
 *   cc -I ../include -Wall -O2 \
 *      ../source/pb_xd.c ../source/genius_crypto.c \
 *      test_xd.c -o test_xd
 * Run:
 *   ./test_xd path/to/xd_save.gci
 */
#include "pb_xd.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <xd.gci>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    pb_xd_save_t xs;
    if (!pb_xd_load(&xs, buf, sz)) {
        fprintf(stderr, "Failed to load XD save\n"); free(buf); return 1;
    }

    printf("Trainer    : \"%s\"\n", xs.trainer_name);
    printf("TID / SID  : %u / %u\n", xs.trainer_id, xs.secret_id);
    printf("Active slot: %d\n", xs.active_slot);
    printf("Save count : %u\n", (unsigned)xs.save_count);
    printf("Party      : %u/6\n\n", xs.party_count);

    printf("Trainer offset: 0x%X   Party offset: 0x%X\n\n",
           xs.trainer_offset, xs.party_offset);
    for (int i = 0; i < 6; i++) {
        pb_xd_pkm_t xp;
        pb_xd_pkm_decode(&xp, xs.slot + xs.party_offset + i * PB_XD_PKM_SIZE);
        if (xp.is_empty) { printf("  %d. (empty)\n", i + 1); continue; }
        printf("  %d. \"%s\" XD#%u L%u%s\n",
               i + 1, xp.nickname, xp.species_internal,
               xp.level, xp.is_shadow ? " [SHADOW]" : "");
        printf("     OT:\"%s\" TID:%u  PID:0x%08X  EXP:%u\n",
               xp.ot_name, xp.trainer_id, xp.pid, xp.exp);
        printf("     Moves: %u / %u / %u / %u\n",
               xp.moves[0], xp.moves[1], xp.moves[2], xp.moves[3]);
        printf("     IVs:   %u/%u/%u/%u/%u/%u\n",
               xp.iv[0], xp.iv[1], xp.iv[2], xp.iv[3], xp.iv[4], xp.iv[5]);
    }
    free(buf);
    return 0;
}
