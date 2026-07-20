/* ------------------------------------------------------------------------
 *   RSA-OAEP, hybrid-encrypted: RSA can only encrypt a message smaller
 *   than key_size - padding_overhead (roughly 446 bytes for a 4096-bit
 *   key with OAEP-SHA256), nowhere near enough for a real file. This
 *   generates a random AES-256-GCM key, encrypts the file with that,
 *   then wraps just the 32-byte AES key with RSA-OAEP — same shape
 *   PGP/age use. Layout:
 *
 *     "SYNR" | version(1) | wrapped_key_len(4,BE) | wrapped_key
 *           | iv(12) | tag(16) | ciphertext...
 *
 *   wrapped_key_len varies with RSA key size, hence the length prefix
 *   (every other format here has fixed-size fields throughout).
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
#include <openssl/pem.h>
#include <openssl/rsa.h>

#define AES_KEY_LEN 32
#define IV_LEN 12
#define TAG_LEN 16

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

int syn_crypt_rsa_encrypt(const char *in_path, const char *out_path, const char *key_path) {
	if (syn_crypt_looks_encrypted(in_path)) {
		return SYN_CRYPT_ERR_ALREADY_ENCRYPTED;
	}

	FILE *keyf = fopen(key_path, "r");
	if (!keyf) {
		return SYN_CRYPT_ERR_IO;
	}
	EVP_PKEY *pkey = PEM_read_PUBKEY(keyf, NULL, NULL, NULL);
	fclose(keyf);
	if (!pkey) {
		return SYN_CRYPT_ERR_FORMAT;
	}

	unsigned char *plaintext = NULL;
	size_t plaintext_len = 0;
	if (read_all(in_path, &plaintext, &plaintext_len) != 0) {
		EVP_PKEY_free(pkey);
		return SYN_CRYPT_ERR_IO;
	}

	unsigned char aes_key[AES_KEY_LEN], iv[IV_LEN], tag[TAG_LEN];
	if (RAND_bytes(aes_key, AES_KEY_LEN) != 1 || RAND_bytes(iv, IV_LEN) != 1) {
		free(plaintext);
		EVP_PKEY_free(pkey);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	unsigned char *ciphertext = malloc(plaintext_len > 0 ? plaintext_len : 1);
	EVP_CIPHER_CTX *ctx = ciphertext ? EVP_CIPHER_CTX_new() : NULL;
	int ok = ctx != NULL;
	int len = 0, ciphertext_len = 0;

	ok = ok && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) == 1;
	ok = ok && EVP_EncryptInit_ex(ctx, NULL, NULL, aes_key, iv) == 1;
	ok = ok && EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, (int)plaintext_len) == 1;
	ciphertext_len = len;
	ok = ok && EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &len) == 1;
	ciphertext_len += len;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag) == 1;

	if (ctx) {
		EVP_CIPHER_CTX_free(ctx);
	}
	free(plaintext);

	if (!ok) {
		syn_crypt_wipe(aes_key, AES_KEY_LEN);
		free(ciphertext);
		EVP_PKEY_free(pkey);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	/* RSA-OAEP-wrap the AES key. */
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new(pkey, NULL);
	unsigned char wrapped_key[1024]; /* generous upper bound for key sizes up to 8192-bit RSA */
	size_t wrapped_len = sizeof(wrapped_key);
	int wrap_ok = pctx != NULL &&
		EVP_PKEY_encrypt_init(pctx) == 1 &&
		EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_OAEP_PADDING) == 1 &&
		EVP_PKEY_CTX_set_rsa_oaep_md(pctx, EVP_sha256()) == 1 &&
		EVP_PKEY_encrypt(pctx, wrapped_key, &wrapped_len, aes_key, AES_KEY_LEN) == 1;

	syn_crypt_wipe(aes_key, AES_KEY_LEN);
	if (pctx) {
		EVP_PKEY_CTX_free(pctx);
	}
	EVP_PKEY_free(pkey);

	if (!wrap_ok) {
		free(ciphertext);
		return SYN_CRYPT_ERR_CRYPTO;
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		free(ciphertext);
		return SYN_CRYPT_ERR_IO;
	}
	uint32_t wrapped_len_be = htonl((uint32_t)wrapped_len);
	int wrote_ok =
		fwrite(SYN_CRYPT_MAGIC_RSA, 1, SYN_CRYPT_MAGIC_LEN, out) == SYN_CRYPT_MAGIC_LEN &&
		fputc(SYN_CRYPT_VERSION, out) != EOF &&
		fwrite(&wrapped_len_be, 1, 4, out) == 4 &&
		fwrite(wrapped_key, 1, wrapped_len, out) == wrapped_len &&
		fwrite(iv, 1, IV_LEN, out) == IV_LEN &&
		fwrite(tag, 1, TAG_LEN, out) == TAG_LEN &&
		((size_t)ciphertext_len == 0 || fwrite(ciphertext, 1, (size_t)ciphertext_len, out) == (size_t)ciphertext_len);
	fclose(out);
	free(ciphertext);
	return wrote_ok ? SYN_CRYPT_OK : SYN_CRYPT_ERR_IO;
}

int syn_crypt_rsa_decrypt(const char *in_path, const char *out_path, const char *key_path) {
	FILE *keyf = fopen(key_path, "r");
	if (!keyf) {
		return SYN_CRYPT_ERR_IO;
	}
	EVP_PKEY *pkey = PEM_read_PrivateKey(keyf, NULL, NULL, NULL);
	fclose(keyf);
	if (!pkey) {
		return SYN_CRYPT_ERR_FORMAT;
	}

	unsigned char *file_buf = NULL;
	size_t file_len = 0;
	if (read_all(in_path, &file_buf, &file_len) != 0) {
		EVP_PKEY_free(pkey);
		return SYN_CRYPT_ERR_IO;
	}

	size_t min_header = SYN_CRYPT_MAGIC_LEN + 1 + 4;
	if (file_len < min_header || memcmp(file_buf, SYN_CRYPT_MAGIC_RSA, SYN_CRYPT_MAGIC_LEN) != 0) {
		free(file_buf);
		EVP_PKEY_free(pkey);
		return SYN_CRYPT_ERR_FORMAT;
	}

	const unsigned char *p = file_buf + SYN_CRYPT_MAGIC_LEN;
	p += 1; /* version */
	uint32_t wrapped_len_be;
	memcpy(&wrapped_len_be, p, 4); p += 4;
	uint32_t wrapped_len = ntohl(wrapped_len_be);

	size_t full_header = min_header + wrapped_len + IV_LEN + TAG_LEN;
	if (wrapped_len > file_len || full_header < min_header || file_len < full_header) {
		free(file_buf);
		EVP_PKEY_free(pkey);
		return SYN_CRYPT_ERR_CORRUPT;
	}

	const unsigned char *wrapped_key = p; p += wrapped_len;
	const unsigned char *iv = p; p += IV_LEN;
	const unsigned char *tag = p; p += TAG_LEN;
	const unsigned char *ciphertext = p;
	size_t ciphertext_len = file_len - full_header;

	/* EVP_PKEY_decrypt uses the two-call convention: the first call with
	 * out=NULL reports the buffer size it needs (the RSA modulus size,
	 * always >= the 32-byte AES key actually recovered) before it will
	 * do the real decrypt — passing a tightly-sized buffer on the first
	 * and only call fails with "bad length" regardless of key size. */
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new(pkey, NULL);
	unsigned char aes_key[AES_KEY_LEN];
	size_t aes_key_len = AES_KEY_LEN;
	int unwrap_ok = pctx != NULL &&
		EVP_PKEY_decrypt_init(pctx) == 1 &&
		EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_OAEP_PADDING) == 1 &&
		EVP_PKEY_CTX_set_rsa_oaep_md(pctx, EVP_sha256()) == 1;

	if (unwrap_ok) {
		size_t needed_len = 0;
		unsigned char scratch[1024]; /* generous upper bound for key sizes up to 8192-bit RSA */
		unwrap_ok = EVP_PKEY_decrypt(pctx, NULL, &needed_len, wrapped_key, wrapped_len) == 1 &&
			needed_len <= sizeof(scratch) &&
			EVP_PKEY_decrypt(pctx, scratch, &needed_len, wrapped_key, wrapped_len) == 1 &&
			needed_len == AES_KEY_LEN;
		if (unwrap_ok) {
			memcpy(aes_key, scratch, AES_KEY_LEN);
		}
		syn_crypt_wipe(scratch, sizeof(scratch));
	}

	if (pctx) {
		EVP_PKEY_CTX_free(pctx);
	}
	EVP_PKEY_free(pkey);

	if (!unwrap_ok) {
		free(file_buf);
		return SYN_CRYPT_ERR_BAD_PASSWORD; /* wrong key, or a key that doesn't match the file */
	}

	unsigned char *plaintext = malloc(ciphertext_len > 0 ? ciphertext_len : 1);
	EVP_CIPHER_CTX *ctx = plaintext ? EVP_CIPHER_CTX_new() : NULL;
	int ok = ctx != NULL;
	int len = 0, plaintext_len = 0;

	ok = ok && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) == 1;
	ok = ok && EVP_DecryptInit_ex(ctx, NULL, NULL, aes_key, iv) == 1;
	ok = ok && EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ciphertext_len) == 1;
	plaintext_len = len;
	ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, (void *)tag) == 1;
	int final_ok = ok && EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &len) == 1;
	if (final_ok) {
		plaintext_len += len;
	}

	syn_crypt_wipe(aes_key, AES_KEY_LEN);
	if (ctx) {
		EVP_CIPHER_CTX_free(ctx);
	}
	free(file_buf);

	if (!ok || !final_ok) {
		if (plaintext) {
			syn_crypt_wipe(plaintext, ciphertext_len);
			free(plaintext);
		}
		return SYN_CRYPT_ERR_CORRUPT;
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
