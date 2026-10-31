/* syn-speak-play: the other end of syn-speak's wire. Reads its framed answer on stdin (a 4-byte little-endian
 * length, that many bytes of 16 kHz mono s16le PCM, a zero length at the end) and writes the bare PCM to stdout,
 * for a player that takes raw audio (pw-play --raw, paplay --raw). No audio library: the player is the player.
 * Exit 0 when any sound passed, 3 when the line made none (an unknown or refused voice: syn-speak said why on its
 * stderr), 1 on a broken stream. Used by syn-bar-speak.zsh, the bar's read-aloud button. */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    unsigned char hdr[4], buf[65536];
    uint64_t total = 0;
    for (;;) {
        if (fread(hdr, 1, 4, stdin) != 4) return total ? 0 : 1;
        uint32_t n = hdr[0] | (uint32_t)hdr[1] << 8 | (uint32_t)hdr[2] << 16 | (uint32_t)hdr[3] << 24;
        if (n == 0) break;
        while (n) {
            size_t want = n < sizeof buf ? n : sizeof buf;
            size_t got = fread(buf, 1, want, stdin);
            if (!got) return 1;
            if (fwrite(buf, 1, got, stdout) != got) return 1;
            n -= got;
            total += got;
        }
        fflush(stdout);
    }
    return total ? 0 : 3;
}
