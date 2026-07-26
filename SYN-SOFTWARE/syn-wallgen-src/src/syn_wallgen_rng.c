/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_wallgen_rng.h"

#include <stdint.h>
#include <stdio.h>

static unsigned long long rng_s[2];

void syn_wg_rng_seed(void) {
	FILE *f = fopen("/dev/urandom", "rb");
	if (f) {
		size_t got = fread(rng_s, sizeof(rng_s), 1, f);
		fclose(f);
		if (got == 1) {
			return;
		}
	}
	rng_s[0] = 0x9E3779B97F4A7C15ULL;
	rng_s[1] = (unsigned long long)(uintptr_t)&rng_s;
}

static unsigned long long rng_next(void) {
	unsigned long long x = rng_s[0], y = rng_s[1];
	rng_s[0] = y;
	x ^= x << 23;
	x ^= x >> 17;
	x ^= y ^ (y >> 26);
	rng_s[1] = x;
	return x + y;
}

double syn_wg_rng_uniform(double lo, double hi) {
	double f = (double)(rng_next() >> 11) / (double)(1ULL << 53); /* [0,1) */
	return lo + f * (hi - lo);
}

int syn_wg_rng_randint(int lo, int hi) {
	return lo + (int)(rng_next() % (unsigned long long)(hi - lo + 1));
}
