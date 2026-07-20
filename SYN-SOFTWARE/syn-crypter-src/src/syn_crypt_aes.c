/* ------------------------------------------------------------------------
 *   AES-256-GCM via EVP. File layout (all fixed-size, no length prefixes
 *   needed since every field has a known size):
 *
 *     "SYNA" | version(1) | salt(16) | iv(12) | iterations(4, BE) | tag(16) | ciphertext...
 *
 *   PBKDF2-HMAC-SHA256 derives the 256-bit key from the password + salt.
 *   GCM's tag authenticates the ciphertext and is checked before any
 *   plaintext is written out, so a wrong password or corrupted file
 *   fails loudly rather than decrypting into garbage.
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
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define SALT_LEN 16
#define IV_LEN 12
#define TAG_LEN 16
#define KEY_LEN 32
#define PBKDF2_ITERATIONS 200000

static int derive_key(const char *password, const unsigned char *salt, unsigned char *key_out) {
	return PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT_LEN,
		PBKDF2_ITERATIONS, EVP_sha256(), KEY_LEN, key_out) == 1 ? 0 : -1;
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

int syn_crypt_aes_encrypt(const char *in_path, const char *out_path, const char *password) {
	if (syn_crypt_looks_encrypted(in_path)) {
		return SYN_CRYPT_ERR_ALREADY_ENCRYPTED;
	}

	unsigned char *plaintext = NULL;
	size_t plaintext_len = 0;
	if (read_all(in_path, &plaintext, &plaintext_len) != 0) {
		return SYN_CRYPT_ERR_IO;
	}

	unsigned char salt[SALT_LEN], iv[IV_LEN], key[KEY_LEN], tag[TAG_LEN];
	if (RAND_bytes(salt, SALT_LEN) != 1 || RAND_bytes(iv, IV_LEN) != 1) {
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}
	if (derive_key(password, salt, key) != 0) {
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	unsigned char *ciphertext = malloc(plaintext_len > 0 ? plaintext_len : 1);
	if (!ciphertext) {
		syn_crypt_wipe(key, KEY_LEN);
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	int ok = ctx != NULL;
	int len = 0, ciphertext_len = 0;

	ok = ok && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) == 1;
	ok = ok && EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1;
	ok = ok && EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, (int)plaintext_len) == 1;
	ciphertext_len = len;
	ok = ok && EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &len) == 1;
	ciphertext_len += len;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag) == 1;

	syn_crypt_wipe(key, KEY_LEN);
	if (ctx) {
		EVP_CIPHER_CTX_free(ctx);
	}
	if (!ok) {
		free(ciphertext);
		free(plaintext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		free(ciphertext);
		free(plaintext);
		return SYN_CRYPT_ERR_IO;
	}
	uint32_t iterations_be = htonl(PBKDF2_ITERATIONS);
	int wrote_ok =
		fwrite(SYN_CRYPT_MAGIC_AES, 1, SYN_CRYPT_MAGIC_LEN, out) == SYN_CRYPT_MAGIC_LEN &&
		fputc(SYN_CRYPT_VERSION, out) != EOF &&
		fwrite(salt, 1, SALT_LEN, out) == SALT_LEN &&
		fwrite(iv, 1, IV_LEN, out) == IV_LEN &&
		fwrite(&iterations_be, 1, 4, out) == 4 &&
		fwrite(tag, 1, TAG_LEN, out) == TAG_LEN &&
		((size_t)ciphertext_len == 0 || fwrite(ciphertext, 1, (size_t)ciphertext_len, out) == (size_t)ciphertext_len);
	fclose(out);

	free(ciphertext);
	free(plaintext);
	return wrote_ok ? SYN_CRYPT_OK : SYN_CRYPT_ERR_IO;
}

int syn_crypt_aes_decrypt(const char *in_path, const char *out_path, const char *password) {
	unsigned char *file_buf = NULL;
	size_t file_len = 0;
	if (read_all(in_path, &file_buf, &file_len) != 0) {
		return SYN_CRYPT_ERR_IO;
	}

	size_t header_len = SYN_CRYPT_MAGIC_LEN + 1 + SALT_LEN + IV_LEN + 4 + TAG_LEN;
	if (file_len < header_len || memcmp(file_buf, SYN_CRYPT_MAGIC_AES, SYN_CRYPT_MAGIC_LEN) != 0) {
		free(file_buf);
		return SYN_CRYPT_ERR_FORMAT;
	}

	const unsigned char *p = file_buf + SYN_CRYPT_MAGIC_LEN;
	p += 1; /* version, unused for now */
	const unsigned char *salt = p; p += SALT_LEN;
	const unsigned char *iv = p; p += IV_LEN;
	uint32_t iterations_be;
	memcpy(&iterations_be, p, 4); p += 4;
	const unsigned char *tag = p; p += TAG_LEN;
	const unsigned char *ciphertext = p;
	size_t ciphertext_len = file_len - header_len;
	(void)iterations_be; /* PBKDF2_ITERATIONS is fixed for now; field reserved for future tuning */

	unsigned char key[KEY_LEN];
	if (derive_key(password, salt, key) != 0) {
		free(file_buf);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	unsigned char *plaintext = malloc(ciphertext_len > 0 ? ciphertext_len : 1);
	if (!plaintext) {
		syn_crypt_wipe(key, KEY_LEN);
		free(file_buf);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	int ok = ctx != NULL;
	int len = 0, plaintext_len = 0;

	ok = ok && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) == 1;
	ok = ok && EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) == 1;
	ok = ok && EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ciphertext_len) == 1;
	plaintext_len = len;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, (void *)tag) == 1;
	int final_ok = ok && EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &len) == 1;
	if (final_ok) {
		plaintext_len += len;
	}

	syn_crypt_wipe(key, KEY_LEN);
	if (ctx) {
		EVP_CIPHER_CTX_free(ctx);
	}
	free(file_buf);

	if (!ok || !final_ok) {
		syn_crypt_wipe(plaintext, ciphertext_len);
		free(plaintext);
		/* Tag mismatch and wrong password are indistinguishable here. */
		return SYN_CRYPT_ERR_BAD_PASSWORD;
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
