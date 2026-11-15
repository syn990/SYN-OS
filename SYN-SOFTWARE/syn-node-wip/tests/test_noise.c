/* ------------------------------------------------------------------------
 *   Noise_IK_25519_ChaChaPoly_BLAKE2b against the cacophony test vector
 *   (cacophony.txt, the Noise project's shared vector set): fixed keys
 *   and ephemerals in, the exact bytes of every message and the final
 *   handshake hash out. Then a random handshake and a tampered message.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (tests)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "sn_noise.h"
#include "sn_util.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static size_t unhex_any(const char *hex, uint8_t *out) {
	size_t n = strlen(hex) / 2;
	sn_unhex(hex, out, n);
	return n;
}

static void vector_test(void) {
	uint8_t prologue[64], init_s[32], init_e[32], resp_s[32], resp_e[32], hash[64];
	size_t pl = unhex_any("4a6f686e2047616c74", prologue);
	unhex_any("e61ef9919cde45dd5f82166404bd08e38bceb5dfdfded0a34c8df7ed542214d1", init_s);
	unhex_any("893e28b9dc6ca8d611ab664754b8ceb7bac5117349a4439a6b0569da977c464a", init_e);
	unhex_any("4a3acbfdb163dec651dfa3194dece676d437029c62a408b4c5ea9114246e4893", resp_s);
	unhex_any("bbdb4cdbd309f1a1f2e1456967fe288cadd6f712d65dc7b7793d5e63da6b375b", resp_e);
	unhex_any("1c8fa891cb414fedba6daa7c6f4ae0a6d98e5f9768cc9cecd27e805614943ee9"
	          "c8a1b27fbfb76dc197255c8aa69f6b4285c423840b8bedf45e652ca64f797d81", hash);

	static const char *payloads[] = {
		"4c756477696720766f6e204d69736573", "4d757272617920526f746862617264",
		"462e20412e20486179656b", "4361726c204d656e676572",
	};
	static const char *ciphertexts[] = {
		"ca35def5ae56cec33dc2036731ab14896bc4c75dbb07a61f879f8e3afa4c7944ba83a447b38c83e327ad936929812f62"
		"4884847b7831e95e197b2f797088efdd2f88f1db7e1fb0e99c64419097af91cee64e470f4b6fcd9298ce0b56fe20f86e"
		"13bf70439c538e3602a7127af71a29cc",
		"95ebc60d2b1fa672c1f46a8aa265ef51bfe38e7ccb39ec5be34069f1448088439f069b267a06b3de3ecb1043bcb098e9"
		"af91d9c64748d998c7b47890871571",
		"cd54383060e7a28434cca27fb1cc524cfbabeb18181589df219d07",
		"a856d3bf0246bfc476c655009cd1ed677b8dcc5b349ae8ef2a05f2",
	};

	uint8_t init_s_pub[32], resp_s_pub[32];
	crypto_scalarmult_base(init_s_pub, init_s);
	crypto_scalarmult_base(resp_s_pub, resp_s);

	sn_handshake ini, res;
	sn_noise_init(&ini, true, prologue, pl, init_s, init_s_pub, resp_s_pub);
	sn_noise_init(&res, false, prologue, pl, resp_s, resp_s_pub, NULL);
	sn_noise_set_ephemeral(&ini, init_e);
	sn_noise_set_ephemeral(&res, resp_e);

	uint8_t p[256], want[256], msg[512], got[256];
	size_t plen = unhex_any(payloads[0], p), wlen = unhex_any(ciphertexts[0], want);
	long n = sn_noise_write_msg1(&ini, p, plen, msg);
	CHECK(n == (long)wlen && memcmp(msg, want, wlen) == 0, "vector message 1 bytes");
	CHECK(sn_noise_read_msg1(&res, msg, (size_t)n, got) == (long)plen && memcmp(got, p, plen) == 0, "vector message 1 read");
	CHECK(memcmp(res.rs, init_s_pub, 32) == 0, "responder learned initiator static");

	plen = unhex_any(payloads[1], p);
	wlen = unhex_any(ciphertexts[1], want);
	n = sn_noise_write_msg2(&res, p, plen, msg);
	CHECK(n == (long)wlen && memcmp(msg, want, wlen) == 0, "vector message 2 bytes");
	CHECK(sn_noise_read_msg2(&ini, msg, (size_t)n, got) == (long)plen && memcmp(got, p, plen) == 0, "vector message 2 read");

	sn_cipher is, ir, rsend, rrecv;
	uint8_t hi[64], hr[64];
	sn_noise_split(&ini, &is, &ir, hi);
	sn_noise_split(&res, &rsend, &rrecv, hr);
	CHECK(memcmp(hi, hash, 64) == 0 && memcmp(hr, hash, 64) == 0, "vector handshake hash");

	/* transport: initiator, responder, alternating */
	for (int i = 2; i < 4; i++) {
		plen = unhex_any(payloads[i], p);
		wlen = unhex_any(ciphertexts[i], want);
		sn_cipher *tx = (i % 2 == 0) ? &is : &rsend;
		sn_cipher *rx = (i % 2 == 0) ? &rrecv : &ir;
		n = sn_cipher_encrypt(tx, NULL, 0, p, plen, msg);
		CHECK(n == (long)wlen && memcmp(msg, want, wlen) == 0, "vector transport bytes");
		CHECK(sn_cipher_decrypt(rx, NULL, 0, msg, (size_t)n, got) == (long)plen, "vector transport read");
	}
}

static void random_and_tamper_test(void) {
	uint8_t a_priv[32], a_pub[32], b_priv[32], b_pub[32], c_priv[32], c_pub[32];
	randombytes_buf(a_priv, 32); crypto_scalarmult_base(a_pub, a_priv);
	randombytes_buf(b_priv, 32); crypto_scalarmult_base(b_pub, b_priv);
	randombytes_buf(c_priv, 32); crypto_scalarmult_base(c_pub, c_priv);
	const uint8_t pro[] = "syn-node test";
	uint8_t msg[512], got[256];

	/* tampered message 1 is refused */
	sn_handshake ini, res;
	sn_noise_init(&ini, true, pro, sizeof(pro), a_priv, a_pub, b_pub);
	sn_noise_init(&res, false, pro, sizeof(pro), b_priv, b_pub, NULL);
	long n = sn_noise_write_msg1(&ini, (const uint8_t *)"hi", 2, msg);
	msg[40] ^= 1;
	CHECK(sn_noise_read_msg1(&res, msg, (size_t)n, got) < 0, "tampered message 1 refused");

	/* initiator aiming at the wrong responder key: responder can't read it */
	sn_noise_init(&ini, true, pro, sizeof(pro), a_priv, a_pub, c_pub);
	sn_noise_init(&res, false, pro, sizeof(pro), b_priv, b_pub, NULL);
	n = sn_noise_write_msg1(&ini, (const uint8_t *)"hi", 2, msg);
	CHECK(sn_noise_read_msg1(&res, msg, (size_t)n, got) < 0, "wrong responder key refused");

	/* different prologue (different mesh): refused */
	const uint8_t pro2[] = "syn-node other";
	sn_noise_init(&ini, true, pro, sizeof(pro), a_priv, a_pub, b_pub);
	sn_noise_init(&res, false, pro2, sizeof(pro2), b_priv, b_pub, NULL);
	n = sn_noise_write_msg1(&ini, (const uint8_t *)"hi", 2, msg);
	CHECK(sn_noise_read_msg1(&res, msg, (size_t)n, got) < 0, "different prologue refused");
}

int main(void) {
	if (sodium_init() < 0) return 1;
	vector_test();
	random_and_tamper_test();
	if (failures) {
		fprintf(stderr, "test_noise: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_noise: ok\n");
	return 0;
}
