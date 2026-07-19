/* ------------------------------------------------------------------------
 *   Blowfish-CBC via EVP — a legacy option, AES-256-GCM is the
 *   recommended algorithm. Blowfish has no AEAD mode in OpenSSL, so this
 *   file format adds an HMAC-SHA256 (a second, independently-derived
 *   key) over the ciphertext for the same tamper-detection AES-GCM gets
 *   natively. Layout:
 *
 *     "SYNB" | version(1) | salt(16) | iv(8) | hmac(32) | ciphertext...
 *
 *   OpenSSL 3.x moved Blowfish into the "legacy" provider, so it has to
 *   be loaded explicitly or EVP_bf_cbc() resolves to a cipher with no
 *   working implementation.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_crypt.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/provider.h>
#include <openssl/crypto.h>

#define SALT_LEN 16
#define IV_LEN 8
#define HMAC_LEN 32
#define KEY_LEN 16      /* Blowfish's key schedule accepts up to 56 bytes; 16 (128-bit) is plenty */
#define HMAC_KEY_LEN 32
#define PBKDF2_ITERATIONS 200000

static OSSL_PROVIDER *legacy_provider = NULL;
static OSSL_PROVIDER *default_provider = NULL;

/* OpenSSL 3.x auto-loads the "default" provider (SHA-256, PBKDF2, AES,
 * ...) only until the first explicit OSSL_PROVIDER_load call, at which
 * point auto-loading stops and *only* the explicitly-loaded provider(s)
 * are active. Loading "legacy" alone therefore silently breaks every
 * non-legacy algorithm (PBKDF2/SHA-256 included, needed for this file's
 * own key derivation) — "default" has to be loaded explicitly too. */
static void ensure_legacy_provider(void) {
	if (!default_provider) {
		default_provider = OSSL_PROVIDER_load(NULL, "default");
	}
	if (!legacy_provider) {
		legacy_provider = OSSL_PROVIDER_load(NULL, "legacy");
	}
}

/* One PBKDF2 call, sliced into a cipher key and a MAC key. */
static int derive_keys(const char *password, const unsigned char *salt,
		unsigned char *cipher_key, unsigned char *hmac_key) {
	unsigned char combined[KEY_LEN + HMAC_KEY_LEN];
	if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT_LEN,
			PBKDF2_ITERATIONS, EVP_sha256(), sizeof(combined), combined) != 1) {
		return -1;
	}
	memcpy(cipher_key, combined, KEY_LEN);
	memcpy(hmac_key, combined + KEY_LEN, HMAC_KEY_LEN);
	syn_crypt_wipe(combined, sizeof(combined));
	return 0;
}

static int read_all(const char *path, unsigned char **out, size_t *out_len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return -1;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return -1;
	}
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = malloc((size_t)sz > 0 ? (size_t)sz : 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		fclose(f);
		free(buf);
		return -1;
	}
	fclose(f);
	*out = buf;
	*out_len = (size_t)sz;
	return 0;
}

int syn_crypt_blowfish_encrypt(const char *in_path, const char *out_path, const char *password) {
	if (syn_crypt_looks_encrypted(in_path)) {
		return SYN_CRYPT_ERR_ALREADY_ENCRYPTED;
	}
	ensure_legacy_provider();

	unsigned char *plaintext = NULL;
	size_t plaintext_len = 0;
	if (read_all(in_path, &plaintext, &plaintext_len) != 0) {
		return SYN_CRYPT_ERR_IO;
	}

	unsigned char salt[SALT_LEN], iv[IV_LEN], cipher_key[KEY_LEN], hmac_key[HMAC_KEY_LEN];
	if (RAND_bytes(salt, SALT_LEN) != 1 || RAND_bytes(iv, IV_LEN) != 1) {
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}
	if (derive_keys(password, salt, cipher_key, hmac_key) != 0) {
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	/* CBC needs room for up to one extra block of PKCS#7 padding. */
	unsigned char *ciphertext = malloc(plaintext_len + IV_LEN);
	if (!ciphertext) {
		syn_crypt_wipe(cipher_key, KEY_LEN);
		syn_crypt_wipe(hmac_key, HMAC_KEY_LEN);
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	int ok = ctx != NULL;
	int len = 0, ciphertext_len = 0;

	ok = ok && EVP_EncryptInit_ex(ctx, EVP_bf_cbc(), NULL, cipher_key, iv) == 1;
	ok = ok && EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, (int)plaintext_len) == 1;
	ciphertext_len = len;
	ok = ok && EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &len) == 1;
	ciphertext_len += len;

	if (ctx) {
		EVP_CIPHER_CTX_free(ctx);
	}
	if (!ok) {
		syn_crypt_wipe(cipher_key, KEY_LEN);
		syn_crypt_wipe(hmac_key, HMAC_KEY_LEN);
		free(ciphertext);
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	unsigned char mac[HMAC_LEN];
	unsigned int mac_len = 0;
	int mac_ok = HMAC(EVP_sha256(), hmac_key, HMAC_KEY_LEN, ciphertext, (size_t)ciphertext_len,
		mac, &mac_len) != NULL && mac_len == HMAC_LEN;

	syn_crypt_wipe(cipher_key, KEY_LEN);
	syn_crypt_wipe(hmac_key, HMAC_KEY_LEN);
	free(plaintext);

	if (!mac_ok) {
		free(ciphertext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		free(ciphertext);
		return SYN_CRYPT_ERR_IO;
	}
	int wrote_ok =
		fwrite(SYN_CRYPT_MAGIC_BLOWFISH, 1, SYN_CRYPT_MAGIC_LEN, out) == SYN_CRYPT_MAGIC_LEN &&
		fputc(SYN_CRYPT_VERSION, out) != EOF &&
		fwrite(salt, 1, SALT_LEN, out) == SALT_LEN &&
		fwrite(iv, 1, IV_LEN, out) == IV_LEN &&
		fwrite(mac, 1, HMAC_LEN, out) == HMAC_LEN &&
		((size_t)ciphertext_len == 0 || fwrite(ciphertext, 1, (size_t)ciphertext_len, out) == (size_t)ciphertext_len);
	fclose(out);
	free(ciphertext);
	return wrote_ok ? SYN_CRYPT_OK : SYN_CRYPT_ERR_IO;
}

int syn_crypt_blowfish_decrypt(const char *in_path, const char *out_path, const char *password) {
	ensure_legacy_provider();

	unsigned char *file_buf = NULL;
	size_t file_len = 0;
	if (read_all(in_path, &file_buf, &file_len) != 0) {
		return SYN_CRYPT_ERR_IO;
	}

	size_t header_len = SYN_CRYPT_MAGIC_LEN + 1 + SALT_LEN + IV_LEN + HMAC_LEN;
	if (file_len < header_len || memcmp(file_buf, SYN_CRYPT_MAGIC_BLOWFISH, SYN_CRYPT_MAGIC_LEN) != 0) {
		free(file_buf);
		return SYN_CRYPT_ERR_FORMAT;
	}

	const unsigned char *p = file_buf + SYN_CRYPT_MAGIC_LEN;
	p += 1; /* version */
	const unsigned char *salt = p; p += SALT_LEN;
	const unsigned char *iv = p; p += IV_LEN;
	const unsigned char *stored_mac = p; p += HMAC_LEN;
	const unsigned char *ciphertext = p;
	size_t ciphertext_len = file_len - header_len;

	unsigned char cipher_key[KEY_LEN], hmac_key[HMAC_KEY_LEN];
	if (derive_keys(password, salt, cipher_key, hmac_key) != 0) {
		free(file_buf);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	unsigned char computed_mac[HMAC_LEN];
	unsigned int computed_mac_len = 0;
	int mac_ok = HMAC(EVP_sha256(), hmac_key, HMAC_KEY_LEN, ciphertext, ciphertext_len,
		computed_mac, &computed_mac_len) != NULL && computed_mac_len == HMAC_LEN;

	/* Constant-time compare — this is a MAC check, not a plain string
	 * compare, so a timing side-channel here would leak forgeries. */
	int mismatch = 0;
	if (mac_ok) {
		mismatch = CRYPTO_memcmp(stored_mac, computed_mac, HMAC_LEN) != 0;
	}
	syn_crypt_wipe(hmac_key, HMAC_KEY_LEN);

	if (!mac_ok || mismatch) {
		syn_crypt_wipe(cipher_key, KEY_LEN);
		free(file_buf);
		return SYN_CRYPT_ERR_BAD_PASSWORD;
	}

	unsigned char *plaintext = malloc(ciphertext_len > 0 ? ciphertext_len : 1);
	if (!plaintext) {
		syn_crypt_wipe(cipher_key, KEY_LEN);
		free(file_buf);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	int ok = ctx != NULL;
	int len = 0, plaintext_len = 0;

	ok = ok && EVP_DecryptInit_ex(ctx, EVP_bf_cbc(), NULL, cipher_key, iv) == 1;
	ok = ok && EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ciphertext_len) == 1;
	plaintext_len = len;
	ok = ok && EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &len) == 1;
	if (ok) {
		plaintext_len += len;
	}

	syn_crypt_wipe(cipher_key, KEY_LEN);
	if (ctx) {
		EVP_CIPHER_CTX_free(ctx);
	}
	free(file_buf);

	if (!ok) {
		syn_crypt_wipe(plaintext, ciphertext_len);
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		syn_crypt_wipe(plaintext, (size_t)plaintext_len);
		free(plaintext);
		return SYN_CRYPT_ERR_IO;
	}
	int wrote_ok = (size_t)plaintext_len == 0 ||
		fwrite(plaintext, 1, (size_t)plaintext_len, out) == (size_t)plaintext_len;
	fclose(out);

	syn_crypt_wipe(plaintext, (size_t)plaintext_len);
	free(plaintext);
	return wrote_ok ? SYN_CRYPT_OK : SYN_CRYPT_ERR_IO;
}
