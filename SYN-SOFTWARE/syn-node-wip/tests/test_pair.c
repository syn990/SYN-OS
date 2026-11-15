/* ------------------------------------------------------------------------
 *   Pairing crypto: same code gives the same keys both ways, a wrong
 *   code gives keys that open nothing, a bad point is refused, and code
 *   input is normalised.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (tests)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "sn_pair.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static bool run(const char *code_j, const char *code_m) {
	uint8_t sid[SN_PAIR_SID_LEN];
	randombytes_buf(sid, sizeof(sid));
	sn_cpace j, m;
	sn_cpace_start(&j, true, code_j, sid);
	sn_cpace_start(&m, false, code_m, sid);
	uint8_t jj2m[32], jm2j[32], mj2m[32], mm2j[32];
	uint8_t Yj[32], Ym[32];
	memcpy(Yj, j.Y, 32);
	memcpy(Ym, m.Y, 32);
	if (!sn_cpace_finish(&j, Ym, jj2m, jm2j) || !sn_cpace_finish(&m, Yj, mj2m, mm2j)) return false;
	const char msg[] = "hello member";
	uint8_t sealed[sizeof(msg) + SN_PAIR_SEAL_EXTRA], opened[sizeof(msg)];
	sn_pair_seal(jj2m, (const uint8_t *)msg, sizeof(msg), sealed);
	return sn_pair_open(mj2m, sealed, sizeof(sealed), opened) && memcmp(opened, msg, sizeof(msg)) == 0 &&
		memcmp(jm2j, mm2j, 32) == 0 && memcmp(jj2m, jm2j, 32) != 0;
}

int main(void) {
	if (sodium_init() < 0) return 1;
	CHECK(run("48219930", "48219930"), "same code pairs");
	CHECK(!run("48219930", "48219931"), "wrong code fails");

	uint8_t sid[SN_PAIR_SID_LEN] = {0}, k1[32], k2[32], zero[32] = {0};
	sn_cpace j;
	sn_cpace_start(&j, true, "12345678", sid);
	CHECK(!sn_cpace_finish(&j, zero, k1, k2), "identity point refused");

	char out[SN_PAIR_CODE_DIGITS + 1];
	CHECK(sn_pair_normalize("4821 9930", out) && strcmp(out, "48219930") == 0, "spaces");
	CHECK(sn_pair_normalize("4821-9930", out), "dash");
	CHECK(!sn_pair_normalize("4821993", out), "too short");
	CHECK(!sn_pair_normalize("482199301", out), "too long");
	CHECK(!sn_pair_normalize("4821x930", out), "letters");
	char code[SN_PAIR_CODE_DIGITS + 1];
	sn_pair_new_code(code);
	CHECK(strlen(code) == 8 && sn_pair_normalize(code, out), "new code valid");

	if (failures) {
		fprintf(stderr, "test_pair: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_pair: ok\n");
	return 0;
}
