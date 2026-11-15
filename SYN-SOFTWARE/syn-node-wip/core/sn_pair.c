/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "sn_pair.h"

#include <sodium.h>
#include <string.h>

static const char CPACE_DSI[] = "syn-node cpace 1";
static const char ISK_DSI[] = "syn-node isk 1";

void sn_pair_new_code(char out[SN_PAIR_CODE_DIGITS + 1]) {
	for (int i = 0; i < SN_PAIR_CODE_DIGITS; i++) {
		out[i] = (char)('0' + randombytes_uniform(10));
	}
	out[SN_PAIR_CODE_DIGITS] = '\0';
}

bool sn_pair_normalize(const char *in, char out[SN_PAIR_CODE_DIGITS + 1]) {
	int n = 0;
	for (const char *p = in; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			if (n == SN_PAIR_CODE_DIGITS) return false;
			out[n++] = *p;
		} else if (*p != ' ' && *p != '-') {
			return false;
		}
	}
	out[n] = '\0';
	return n == SN_PAIR_CODE_DIGITS;
}

/* g = ristretto255_from_hash(SHA-512(DSI || len(code) || code || sid)) */
static void generator(const char *code, const uint8_t sid[SN_PAIR_SID_LEN], uint8_t g[32]) {
	uint8_t h[64];
	uint8_t len = (uint8_t)strlen(code);
	crypto_hash_sha512_state st;
	crypto_hash_sha512_init(&st);
	crypto_hash_sha512_update(&st, (const uint8_t *)CPACE_DSI, sizeof(CPACE_DSI) - 1);
	crypto_hash_sha512_update(&st, &len, 1);
	crypto_hash_sha512_update(&st, (const uint8_t *)code, len);
	crypto_hash_sha512_update(&st, sid, SN_PAIR_SID_LEN);
	crypto_hash_sha512_final(&st, h);
	crypto_core_ristretto255_from_hash(g, h);
	sodium_memzero(h, sizeof(h));
}

bool sn_cpace_start(sn_cpace *cp, bool initiator, const char *code, const uint8_t sid[SN_PAIR_SID_LEN]) {
	memset(cp, 0, sizeof(*cp));
	cp->initiator = initiator;
	memcpy(cp->sid, sid, SN_PAIR_SID_LEN);
	uint8_t g[32];
	generator(code, sid, g);
	crypto_core_ristretto255_scalar_random(cp->y);
	bool ok = crypto_scalarmult_ristretto255(cp->Y, cp->y, g) == 0;
	sodium_memzero(g, sizeof(g));
	return ok;
}

bool sn_cpace_finish(sn_cpace *cp, const uint8_t their_Y[32], uint8_t key_j2m[32], uint8_t key_m2j[32]) {
	if (!crypto_core_ristretto255_is_valid_point(their_Y)) {
		return false;
	}
	uint8_t K[32];
	if (crypto_scalarmult_ristretto255(K, cp->y, their_Y) != 0) {
		return false; /* identity: refuse */
	}
	const uint8_t *Yj = cp->initiator ? cp->Y : their_Y;
	const uint8_t *Ym = cp->initiator ? their_Y : cp->Y;

	uint8_t isk[64];
	crypto_generichash_blake2b_state st;
	crypto_generichash_blake2b_init(&st, NULL, 0, 64);
	crypto_generichash_blake2b_update(&st, (const uint8_t *)ISK_DSI, sizeof(ISK_DSI) - 1);
	crypto_generichash_blake2b_update(&st, cp->sid, SN_PAIR_SID_LEN);
	crypto_generichash_blake2b_update(&st, K, 32);
	crypto_generichash_blake2b_update(&st, Yj, 32);
	crypto_generichash_blake2b_update(&st, Ym, 32);
	crypto_generichash_blake2b_final(&st, isk, 64);

	crypto_generichash_blake2b(key_j2m, 32, (const uint8_t *)"joiner to member", 16, isk, 64);
	crypto_generichash_blake2b(key_m2j, 32, (const uint8_t *)"member to joiner", 16, isk, 64);
	sodium_memzero(K, sizeof(K));
	sodium_memzero(isk, sizeof(isk));
	sodium_memzero(cp->y, sizeof(cp->y));
	return true;
}

void sn_pair_seal(const uint8_t key[32], const uint8_t *in, size_t len, uint8_t *out) {
	uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {0};
	crypto_aead_xchacha20poly1305_ietf_encrypt(out, NULL, in, len, NULL, 0, NULL, nonce, key);
}

bool sn_pair_open(const uint8_t key[32], const uint8_t *in, size_t len, uint8_t *out) {
	uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {0};
	if (len < SN_PAIR_SEAL_EXTRA) return false;
	return crypto_aead_xchacha20poly1305_ietf_decrypt(out, NULL, NULL, in, len, NULL, 0, nonce, key) == 0;
}
