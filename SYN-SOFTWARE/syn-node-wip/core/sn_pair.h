/* ------------------------------------------------------------------------
 *   Pairing by code: CPace on ristretto255 (libsodium), so an 8-digit
 *   code is enough. Each side derives a point from the code and a fresh
 *   session id, sends a random multiple of it, and both end with the same
 *   key only if they used the same code. Someone listening learns nothing
 *   they could test codes against offline; someone guessing gets one try
 *   per code, because a failed attempt burns it.
 *
 *   The two keys out of it seal one message each way (who I am / here is
 *   the mesh). Nothing about the network lives here; the daemon (member
 *   side) and the CLI (joining side) move the bytes.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_PAIR_H
#define SN_PAIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SN_PAIR_CODE_DIGITS 8
#define SN_PAIR_SID_LEN 16
#define SN_PAIR_SEAL_EXTRA 16
#define SN_PAIR_WINDOW_MS (5 * 60 * 1000)

typedef struct {
	bool initiator;           /* the joining side */
	uint8_t sid[SN_PAIR_SID_LEN];
	uint8_t y[32];            /* secret scalar */
	uint8_t Y[32];            /* y * g, sent to the other side */
} sn_cpace;

/* 8 random digits plus NUL. */
void sn_pair_new_code(char out[SN_PAIR_CODE_DIGITS + 1]);
/* Keeps the digits of what the user typed ("4821 9930", "4821-9930");
 * false unless exactly 8 remain. */
bool sn_pair_normalize(const char *in, char out[SN_PAIR_CODE_DIGITS + 1]);

bool sn_cpace_start(sn_cpace *cp, bool initiator, const char *code, const uint8_t sid[SN_PAIR_SID_LEN]);
/* Joiner-to-member and member-to-joiner keys. False if their point is bad. */
bool sn_cpace_finish(sn_cpace *cp, const uint8_t their_Y[32], uint8_t key_j2m[32], uint8_t key_m2j[32]);

/* One message per key: XChaCha20-Poly1305 with a zero nonce. out holds
 * len + 16 (seal) or len - 16 (open). */
void sn_pair_seal(const uint8_t key[32], const uint8_t *in, size_t len, uint8_t *out);
bool sn_pair_open(const uint8_t key[32], const uint8_t *in, size_t len, uint8_t *out);

#endif
