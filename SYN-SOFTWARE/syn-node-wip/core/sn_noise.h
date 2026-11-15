/* ------------------------------------------------------------------------
 *   Noise_IK_25519_ChaChaPoly_BLAKE2b, on libsodium, exactly as the
 *   Noise spec (revision 34) defines it, nothing more: the one handshake
 *   SYN-NODE links use, then the two transport ciphers it splits into.
 *   Checked against the cacophony test vector in tests/test_noise.c.
 *
 *   IK: the initiator already knows the responder's static key (from
 *   the roster), so the first message is already encrypted and both
 *   sides are authenticated after one round trip.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_NOISE_H
#define SN_NOISE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SN_NOISE_KEYLEN 32
#define SN_NOISE_TAGLEN 16
#define SN_NOISE_MAXMSG 65535

typedef struct {
	uint8_t k[32];
	uint64_t n;
	bool has_key;
} sn_cipher;

typedef struct {
	bool initiator;
	uint8_t h[64];
	uint8_t ck[64];
	sn_cipher c;
	uint8_t s_priv[32], s_pub[32];   /* our static */
	uint8_t e_priv[32], e_pub[32];   /* our ephemeral */
	uint8_t rs[32], re[32];          /* their static, ephemeral */
	bool fixed_e;                    /* tests only: e_priv preset */
	int step;                        /* messages processed so far */
} sn_handshake;

/* s_priv/s_pub are X25519. rs is required for the initiator, ignored
 * for the responder (it learns rs from message 1). */
void sn_noise_init(sn_handshake *hs, bool initiator, const uint8_t *prologue, size_t prologue_len,
	const uint8_t s_priv[32], const uint8_t s_pub[32], const uint8_t rs[32]);
/* Tests only: use this ephemeral instead of a random one. */
void sn_noise_set_ephemeral(sn_handshake *hs, const uint8_t e_priv[32]);

/* Message 1 (initiator writes, responder reads) and message 2 (the
 * reverse). out must hold payload_len + 96 (msg 1) or + 48 (msg 2).
 * Return the message length, or -1. */
long sn_noise_write_msg1(sn_handshake *hs, const uint8_t *payload, size_t payload_len, uint8_t *out);
long sn_noise_read_msg1(sn_handshake *hs, const uint8_t *msg, size_t len, uint8_t *payload_out);
long sn_noise_write_msg2(sn_handshake *hs, const uint8_t *payload, size_t payload_len, uint8_t *out);
long sn_noise_read_msg2(sn_handshake *hs, const uint8_t *msg, size_t len, uint8_t *payload_out);

/* After message 2: the initiator's sending cipher and its receiving one
 * (swapped for the responder). The handshake hash is copied out for
 * tests. Wipes the handshake state. */
void sn_noise_split(sn_handshake *hs, sn_cipher *send, sn_cipher *recv, uint8_t hash_out[64]);

/* Transport. out holds len + 16 (encrypt) or len - 16 (decrypt).
 * Return the output length, or -1 (bad tag, nonce exhausted). */
long sn_cipher_encrypt(sn_cipher *c, const uint8_t *ad, size_t ad_len, const uint8_t *in, size_t len, uint8_t *out);
long sn_cipher_decrypt(sn_cipher *c, const uint8_t *ad, size_t ad_len, const uint8_t *in, size_t len, uint8_t *out);

/* HKDF over HMAC-BLAKE2b as Noise defines it, exposed for sn_pair. */
void sn_hkdf2(const uint8_t ck[64], const uint8_t *ikm, size_t ikm_len, uint8_t out1[64], uint8_t out2[64]);

#endif
