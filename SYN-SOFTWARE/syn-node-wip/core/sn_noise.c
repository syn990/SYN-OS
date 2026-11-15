/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "sn_noise.h"

#include <sodium.h>
#include <string.h>

static const char PROTOCOL_NAME[] = "Noise_IK_25519_ChaChaPoly_BLAKE2b";

/* ---- hash, HMAC, HKDF (BLAKE2b, HASHLEN 64, BLOCKLEN 128) ---- */

static void hash2(uint8_t out[64], const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
	crypto_generichash_blake2b_state st;
	crypto_generichash_blake2b_init(&st, NULL, 0, 64);
	crypto_generichash_blake2b_update(&st, a, alen);
	if (blen) {
		crypto_generichash_blake2b_update(&st, b, blen);
	}
	crypto_generichash_blake2b_final(&st, out, 64);
}

static void hmac(const uint8_t key[64], const uint8_t *data, size_t len, uint8_t out[64]) {
	uint8_t pad[128], inner[64];
	memset(pad, 0, sizeof(pad));
	memcpy(pad, key, 64);
	for (int i = 0; i < 128; i++) pad[i] ^= 0x36;
	hash2(inner, pad, 128, data, len);
	for (int i = 0; i < 128; i++) pad[i] ^= 0x36 ^ 0x5c;
	hash2(out, pad, 128, inner, 64);
	sodium_memzero(pad, sizeof(pad));
	sodium_memzero(inner, sizeof(inner));
}

void sn_hkdf2(const uint8_t ck[64], const uint8_t *ikm, size_t ikm_len, uint8_t out1[64], uint8_t out2[64]) {
	uint8_t temp[64], in[65];
	hmac(ck, ikm, ikm_len, temp);
	in[0] = 1;
	hmac(temp, in, 1, out1);
	memcpy(in, out1, 64);
	in[64] = 2;
	hmac(temp, in, 65, out2);
	sodium_memzero(temp, sizeof(temp));
	sodium_memzero(in, sizeof(in));
}

/* ---- CipherState ---- */

static void nonce12(uint64_t n, uint8_t out[12]) {
	memset(out, 0, 4);
	for (int i = 0; i < 8; i++) out[4 + i] = (uint8_t)(n >> (8 * i));
}

long sn_cipher_encrypt(sn_cipher *c, const uint8_t *ad, size_t ad_len, const uint8_t *in, size_t len, uint8_t *out) {
	if (!c->has_key) {
		memmove(out, in, len);
		return (long)len;
	}
	if (c->n == UINT64_MAX) {
		return -1;
	}
	uint8_t nonce[12];
	nonce12(c->n, nonce);
	unsigned long long clen = 0;
	crypto_aead_chacha20poly1305_ietf_encrypt(out, &clen, in, len, ad, ad_len, NULL, nonce, c->k);
	c->n++;
	return (long)clen;
}

long sn_cipher_decrypt(sn_cipher *c, const uint8_t *ad, size_t ad_len, const uint8_t *in, size_t len, uint8_t *out) {
	if (!c->has_key) {
		memmove(out, in, len);
		return (long)len;
	}
	if (c->n == UINT64_MAX || len < SN_NOISE_TAGLEN) {
		return -1;
	}
	uint8_t nonce[12];
	nonce12(c->n, nonce);
	unsigned long long plen = 0;
	if (crypto_aead_chacha20poly1305_ietf_decrypt(out, &plen, NULL, in, len, ad, ad_len, nonce, c->k) != 0) {
		return -1;
	}
	c->n++;
	return (long)plen;
}

/* ---- SymmetricState ---- */

static void mix_hash(sn_handshake *hs, const uint8_t *data, size_t len) {
	hash2(hs->h, hs->h, 64, data, len);
}

static void mix_key(sn_handshake *hs, const uint8_t *ikm, size_t len) {
	uint8_t ck[64], k[64];
	sn_hkdf2(hs->ck, ikm, len, ck, k);
	memcpy(hs->ck, ck, 64);
	memcpy(hs->c.k, k, 32);
	hs->c.n = 0;
	hs->c.has_key = true;
	sodium_memzero(ck, sizeof(ck));
	sodium_memzero(k, sizeof(k));
}

static long encrypt_and_hash(sn_handshake *hs, const uint8_t *in, size_t len, uint8_t *out) {
	long n = sn_cipher_encrypt(&hs->c, hs->h, 64, in, len, out);
	if (n >= 0) {
		mix_hash(hs, out, (size_t)n);
	}
	return n;
}

static long decrypt_and_hash(sn_handshake *hs, const uint8_t *in, size_t len, uint8_t *out) {
	uint8_t h_before[64];
	memcpy(h_before, hs->h, 64);
	long n = sn_cipher_decrypt(&hs->c, h_before, 64, in, len, out);
	if (n >= 0) {
		mix_hash(hs, in, len);
	}
	return n;
}

static bool dh(const uint8_t priv[32], const uint8_t pub[32], uint8_t out[32]) {
	return crypto_scalarmult(out, priv, pub) == 0; /* rejects an all-zero result */
}

/* ---- HandshakeState, IK only ---- */

void sn_noise_init(sn_handshake *hs, bool initiator, const uint8_t *prologue, size_t prologue_len,
		const uint8_t s_priv[32], const uint8_t s_pub[32], const uint8_t rs[32]) {
	memset(hs, 0, sizeof(*hs));
	hs->initiator = initiator;
	size_t name_len = sizeof(PROTOCOL_NAME) - 1; /* 33 <= 64: padded, not hashed */
	memcpy(hs->h, PROTOCOL_NAME, name_len);
	memcpy(hs->ck, hs->h, 64);
	mix_hash(hs, prologue, prologue_len);
	memcpy(hs->s_priv, s_priv, 32);
	memcpy(hs->s_pub, s_pub, 32);
	/* pre-message: <- s */
	if (initiator) {
		memcpy(hs->rs, rs, 32);
		mix_hash(hs, hs->rs, 32);
	} else {
		mix_hash(hs, hs->s_pub, 32);
	}
}

void sn_noise_set_ephemeral(sn_handshake *hs, const uint8_t e_priv[32]) {
	memcpy(hs->e_priv, e_priv, 32);
	hs->fixed_e = true;
}

static void make_ephemeral(sn_handshake *hs) {
	if (!hs->fixed_e) {
		randombytes_buf(hs->e_priv, 32);
	}
	crypto_scalarmult_base(hs->e_pub, hs->e_priv);
}

/* -> e, es, s, ss */
long sn_noise_write_msg1(sn_handshake *hs, const uint8_t *payload, size_t payload_len, uint8_t *out) {
	if (!hs->initiator || hs->step != 0) return -1;
	uint8_t shared[32];
	make_ephemeral(hs);
	memcpy(out, hs->e_pub, 32);
	mix_hash(hs, hs->e_pub, 32);
	if (!dh(hs->e_priv, hs->rs, shared)) return -1;
	mix_key(hs, shared, 32);
	long n = encrypt_and_hash(hs, hs->s_pub, 32, out + 32);
	if (n != 48) return -1;
	if (!dh(hs->s_priv, hs->rs, shared)) return -1;
	mix_key(hs, shared, 32);
	sodium_memzero(shared, sizeof(shared));
	long p = encrypt_and_hash(hs, payload, payload_len, out + 80);
	if (p < 0) return -1;
	hs->step = 1;
	return 80 + p;
}

long sn_noise_read_msg1(sn_handshake *hs, const uint8_t *msg, size_t len, uint8_t *payload_out) {
	if (hs->initiator || hs->step != 0 || len < 96) return -1;
	uint8_t shared[32];
	memcpy(hs->re, msg, 32);
	mix_hash(hs, hs->re, 32);
	if (!dh(hs->s_priv, hs->re, shared)) return -1;
	mix_key(hs, shared, 32);
	if (decrypt_and_hash(hs, msg + 32, 48, hs->rs) != 32) return -1;
	if (!dh(hs->s_priv, hs->rs, shared)) return -1;
	mix_key(hs, shared, 32);
	sodium_memzero(shared, sizeof(shared));
	long p = decrypt_and_hash(hs, msg + 80, len - 80, payload_out);
	if (p < 0) return -1;
	hs->step = 1;
	return p;
}

/* <- e, ee, se */
long sn_noise_write_msg2(sn_handshake *hs, const uint8_t *payload, size_t payload_len, uint8_t *out) {
	if (hs->initiator || hs->step != 1) return -1;
	uint8_t shared[32];
	make_ephemeral(hs);
	memcpy(out, hs->e_pub, 32);
	mix_hash(hs, hs->e_pub, 32);
	if (!dh(hs->e_priv, hs->re, shared)) return -1; /* ee */
	mix_key(hs, shared, 32);
	if (!dh(hs->e_priv, hs->rs, shared)) return -1; /* se: their static, our ephemeral */
	mix_key(hs, shared, 32);
	sodium_memzero(shared, sizeof(shared));
	long p = encrypt_and_hash(hs, payload, payload_len, out + 32);
	if (p < 0) return -1;
	hs->step = 2;
	return 32 + p;
}

long sn_noise_read_msg2(sn_handshake *hs, const uint8_t *msg, size_t len, uint8_t *payload_out) {
	if (!hs->initiator || hs->step != 1 || len < 48) return -1;
	uint8_t shared[32];
	memcpy(hs->re, msg, 32);
	mix_hash(hs, hs->re, 32);
	if (!dh(hs->e_priv, hs->re, shared)) return -1; /* ee */
	mix_key(hs, shared, 32);
	if (!dh(hs->s_priv, hs->re, shared)) return -1; /* se: our static, their ephemeral */
	mix_key(hs, shared, 32);
	sodium_memzero(shared, sizeof(shared));
	long p = decrypt_and_hash(hs, msg + 32, len - 32, payload_out);
	if (p < 0) return -1;
	hs->step = 2;
	return p;
}

void sn_noise_split(sn_handshake *hs, sn_cipher *send, sn_cipher *recv, uint8_t hash_out[64]) {
	uint8_t k1[64], k2[64];
	sn_hkdf2(hs->ck, NULL, 0, k1, k2);
	sn_cipher c1 = {.n = 0, .has_key = true}, c2 = {.n = 0, .has_key = true};
	memcpy(c1.k, k1, 32);
	memcpy(c2.k, k2, 32);
	if (hs->initiator) {
		*send = c1;
		*recv = c2;
	} else {
		*send = c2;
		*recv = c1;
	}
	if (hash_out) {
		memcpy(hash_out, hs->h, 64);
	}
	sodium_memzero(k1, sizeof(k1));
	sodium_memzero(k2, sizeof(k2));
	sodium_memzero(&c1, sizeof(c1));
	sodium_memzero(&c2, sizeof(c2));
	bool initiator = hs->initiator;
	uint8_t rs[32];
	memcpy(rs, hs->rs, 32);
	sodium_memzero(hs, sizeof(*hs));
	hs->initiator = initiator;
	memcpy(hs->rs, rs, 32); /* the caller still needs to know who this is */
	hs->step = 3;
}
