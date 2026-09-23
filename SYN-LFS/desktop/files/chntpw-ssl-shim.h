/* ------------------------------------------------------------------------
 *   The DES and MD4 calls chntpw is written against, on libgcrypt.
 *
 *   chntpw (2014) uses OpenSSL 0.9's lowercase DES interface and MD4.
 *   OpenSSL dropped the first in 1.1 and the second into the legacy
 *   provider, and OpenSSL 4 has neither, so the recipe drops chntpw's
 *   openssl includes and forces this header in ahead of everything
 *   instead. Same key handling, same output; libgcrypt ignores DES parity
 *   bits exactly as OpenSSL did.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : CHNTPW (SYN-LFS)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CHNTPW_SSL_SHIM_H
#define SYN_CHNTPW_SSL_SHIM_H

#include <gcrypt.h>
#include <string.h>

/* --- DES, ECB, one block at a time --------------------------------------- */
typedef unsigned char des_cblock[8];
typedef struct {
	gcry_cipher_hd_t h;
} des_key_schedule[1];

#define DES_ENCRYPT 1
#define DES_DECRYPT 0

/* Sets the low bit of each byte so the byte has odd parity, which is what
 * the NT hash code expects to hand to des_set_key */
static inline void DES_set_odd_parity(des_cblock *key)
{
	unsigned char *k = (unsigned char *)key;
	int i;
	for (i = 0; i < 8; i++) {
		unsigned char p = k[i];
		p ^= p >> 4;
		p ^= p >> 2;
		p ^= p >> 1;
		if (!(p & 1)) {
			k[i] ^= 1;
		}
	}
}

static inline int des_set_key(des_cblock *key, des_key_schedule ks)
{
	if (gcry_cipher_open(&ks->h, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0)) {
		return -1;
	}
	return gcry_cipher_setkey(ks->h, key, 8) ? -1 : 0;
}

static inline void des_ecb_encrypt(des_cblock *in, des_cblock *out,
                                   des_key_schedule ks, int enc)
{
	if (enc == DES_ENCRYPT) {
		gcry_cipher_encrypt(ks->h, out, 8, in, 8);
	} else {
		gcry_cipher_decrypt(ks->h, out, 8, in, 8);
	}
}

/* --- MD4, for the NT half of the password hash --------------------------- */
typedef struct {
	gcry_md_hd_t h;
} MD4_CTX;

static inline int MD4_Init(MD4_CTX *c)
{
	return gcry_md_open(&c->h, GCRY_MD_MD4, 0) ? 0 : 1;
}

static inline int MD4_Update(MD4_CTX *c, const void *data, size_t len)
{
	gcry_md_write(c->h, data, len);
	return 1;
}

static inline int MD4_Final(unsigned char *md, MD4_CTX *c)
{
	memcpy(md, gcry_md_read(c->h, GCRY_MD_MD4), 16);
	gcry_md_close(c->h);
	return 1;
}

#endif
