/* Verify SHA-1 against known test vectors. */
#include "sha1.h"
#include <stdio.h>
#include <string.h>

static void print_digest(const uint8_t d[20]) {
    for (int i = 0; i < 20; i++) printf("%02x", d[i]);
}

int main(void) {
    struct {
        const char *input;
        const char *expected;
    } vectors[] = {
        { "",     "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { "abc",  "a9993e364706816aba3e25717850c26c9cd0d89d" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
    };
    int fail = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t digest[20];
        pb_sha1(digest, (const uint8_t *)vectors[i].input, strlen(vectors[i].input));
        printf("[%d] input=%-20s\n  got  ", i, vectors[i].input[0] ? vectors[i].input : "(empty)");
        print_digest(digest);
        printf("\n  want %s\n", vectors[i].expected);
        char got[41];
        for (int j = 0; j < 20; j++) sprintf(got + j*2, "%02x", digest[j]);
        if (strcmp(got, vectors[i].expected) != 0) { printf("  FAIL\n"); fail++; }
        else printf("  ok\n");
    }
    return fail;
}
