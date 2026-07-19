/* ------------------------------------------------------------------------
 *   REDSHIRT — an Uplink-style joke "encryption": flips the top bit of
 *   every byte (equivalent to XOR 0x80). Not real security: there is no
 *   key, every file transforms against the same fixed constant.
 *
 *   Three on-disk variants, selectable at encrypt time (decrypt sniffs
 *   the marker/magic and picks automatically):
 *
 *   - REDSHIRT (v1): the original Uplink game format. 9-byte marker
 *     "REDSHIRT\0", no integrity check — matches
 *     lib/redshirt/redshirt.cpp's writeRsEncryptedHeader/marker
 *     (vb6mmorpg/uplink-source-code).
 *
 *   - REDSHRT2 (v2): the game's newer format. 9-byte marker
 *     "REDSHRT2\0" + a 20-byte SHA-1 over the ciphertext — matches
 *     writeRsEncryptedCheckSum's hash-after-the-header order.
 *
 *   - SYNX (v3): this tool's own variant. 4-byte magic + 1-byte version
 *     + a 32-byte SHA-256 of the plaintext, computed before the
 *     transform (opposite order from REDSHRT2; either order detects
 *     corruption equally well).
 *
 *   Every encrypt_* path refuses to run if the input already looks like
 *   a recognized syn-crypter/Redshirt format (see syn_crypt_looks_encrypted)
 *   rather than double-wrapping it.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_crypt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>

#define SYNX_HASH_LEN 32     /* SHA-256 */
#define REDSHRT2_HASH_LEN 20 /* SHA-1 */
#define XOR_CONST 0x80

static void xor_transform(unsigned char *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		buf[i] ^= XOR_CONST;
	}
}

/* hash.cpp writes its SHA-1 result as 5 raw uint32 words via memcpy —
 * on x86/x86_64 that's 5 little-endian words, not the big-endian
 * byte-stream a normal SHA-1 API (OpenSSL's EVP_sha1(), used here)
 * produces. Without this swap, REDSHRT2 files from the real game/tool
 * fail to verify here. */
static void swap_sha1_word_endianness(unsigned char *hash) {
	for (int word = 0; word < 5; word++) {
		unsigned char *b = hash + word * 4;
		unsigned char tmp;
		tmp = b[0]; b[0] = b[3]; b[3] = tmp;
		tmp = b[1]; b[1] = b[2]; b[2] = tmp;
	}
}

static int digest(const EVP_MD *md, const unsigned char *data, size_t len, unsigned char *out, unsigned int expect_len) {
	unsigned int out_len = 0;
	return EVP_Digest(data, len, out, &out_len, md, NULL) == 1 && out_len == expect_len ? 0 : -1;
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

int syn_crypt_redshirt_encrypt(const char *in_path, const char *out_path, syn_redshirt_variant variant) {
	if (syn_crypt_looks_encrypted(in_path)) {
		return SYN_CRYPT_ERR_ALREADY_ENCRYPTED;
	}

	unsigned char *plaintext = NULL;
	size_t plaintext_len = 0;
	if (read_all(in_path, &plaintext, &plaintext_len) != 0) {
		return SYN_CRYPT_ERR_IO;
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		free(plaintext);
		return SYN_CRYPT_ERR_IO;
	}

	int wrote_ok;
	if (variant == SYN_REDSHIRT_V1_REDSHIRT) {
		xor_transform(plaintext, plaintext_len);
		wrote_ok =
			fwrite(SYN_CRYPT_MARKER_REDSHIRT, 1, SYN_CRYPT_MARKER_LEN, out) == SYN_CRYPT_MARKER_LEN &&
			(plaintext_len == 0 || fwrite(plaintext, 1, plaintext_len, out) == plaintext_len);

	} else if (variant == SYN_REDSHIRT_V2_REDSHRT2) {
		/* Hash the ciphertext, not the plaintext. */
		xor_transform(plaintext, plaintext_len);
		unsigned char hash[REDSHRT2_HASH_LEN];
		if (digest(EVP_sha1(), plaintext, plaintext_len, hash, REDSHRT2_HASH_LEN) != 0) {
			fclose(out);
			free(plaintext);
			remove(out_path);
			return SYN_CRYPT_ERR_CRYPTO;
		}
		swap_sha1_word_endianness(hash);
		wrote_ok =
			fwrite(SYN_CRYPT_MARKER_REDSHRT2, 1, SYN_CRYPT_MARKER_LEN, out) == SYN_CRYPT_MARKER_LEN &&
			fwrite(hash, 1, REDSHRT2_HASH_LEN, out) == REDSHRT2_HASH_LEN &&
			(plaintext_len == 0 || fwrite(plaintext, 1, plaintext_len, out) == plaintext_len);

	} else {
		/* SYNX: hash the plaintext before transforming (one buffer pass
		 * instead of two; either order is an equally valid integrity check). */
		unsigned char hash[SYNX_HASH_LEN];
		if (digest(EVP_sha256(), plaintext, plaintext_len, hash, SYNX_HASH_LEN) != 0) {
			fclose(out);
			free(plaintext);
			remove(out_path);
			return SYN_CRYPT_ERR_CRYPTO;
		}
		xor_transform(plaintext, plaintext_len);
		wrote_ok =
			fwrite(SYN_CRYPT_MAGIC_REDSHIRT_SYNX, 1, SYN_CRYPT_MAGIC_LEN, out) == SYN_CRYPT_MAGIC_LEN &&
			fputc(SYN_CRYPT_VERSION, out) != EOF &&
			fwrite(hash, 1, SYNX_HASH_LEN, out) == SYNX_HASH_LEN &&
			(plaintext_len == 0 || fwrite(plaintext, 1, plaintext_len, out) == plaintext_len);
	}

	fclose(out);
	free(plaintext);
	if (!wrote_ok) {
		remove(out_path);
	}
	return wrote_ok ? SYN_CRYPT_OK : SYN_CRYPT_ERR_IO;
}

int syn_crypt_redshirt_decrypt(const char *in_path, const char *out_path) {
	unsigned char *file_buf = NULL;
	size_t file_len = 0;
	if (read_all(in_path, &file_buf, &file_len) != 0) {
		return SYN_CRYPT_ERR_IO;
	}

	int is_redshrt2 = file_len >= SYN_CRYPT_MARKER_LEN &&
		memcmp(file_buf, SYN_CRYPT_MARKER_REDSHRT2, SYN_CRYPT_MARKER_LEN) == 0;
	int is_redshirt_v1 = !is_redshrt2 && file_len >= SYN_CRYPT_MARKER_LEN &&
		memcmp(file_buf, SYN_CRYPT_MARKER_REDSHIRT, SYN_CRYPT_MARKER_LEN) == 0;
	int is_synx = !is_redshrt2 && !is_redshirt_v1 && file_len >= SYN_CRYPT_MAGIC_LEN &&
		memcmp(file_buf, SYN_CRYPT_MAGIC_REDSHIRT_SYNX, SYN_CRYPT_MAGIC_LEN) == 0;

	if (!is_redshrt2 && !is_redshirt_v1 && !is_synx) {
		free(file_buf);
		return SYN_CRYPT_ERR_NOT_ENCRYPTED;
	}

	unsigned char *data;
	size_t data_len;

	if (is_redshirt_v1) {
		data = file_buf + SYN_CRYPT_MARKER_LEN;
		data_len = file_len - SYN_CRYPT_MARKER_LEN;
		xor_transform(data, data_len);

	} else if (is_redshrt2) {
		size_t header_len = SYN_CRYPT_MARKER_LEN + REDSHRT2_HASH_LEN;
		if (file_len < header_len) {
			free(file_buf);
			return SYN_CRYPT_ERR_CORRUPT;
		}
		const unsigned char *stored_hash = file_buf + SYN_CRYPT_MARKER_LEN;
		data = file_buf + header_len;
		data_len = file_len - header_len;

		/* Verify before transforming — the hash is over the ciphertext. */
		unsigned char computed_hash[REDSHRT2_HASH_LEN];
		if (digest(EVP_sha1(), data, data_len, computed_hash, REDSHRT2_HASH_LEN) != 0) {
			free(file_buf);
			return SYN_CRYPT_ERR_CRYPTO;
		}
		swap_sha1_word_endianness(computed_hash);
		if (memcmp(stored_hash, computed_hash, REDSHRT2_HASH_LEN) != 0) {
			free(file_buf);
			return SYN_CRYPT_ERR_CORRUPT;
		}
		xor_transform(data, data_len);

	} else {
		size_t header_len = SYN_CRYPT_MAGIC_LEN + 1 + SYNX_HASH_LEN;
		if (file_len < header_len) {
			free(file_buf);
			return SYN_CRYPT_ERR_CORRUPT;
		}
		const unsigned char *stored_hash = file_buf + SYN_CRYPT_MAGIC_LEN + 1;
		data = file_buf + header_len;
		data_len = file_len - header_len;

		xor_transform(data, data_len);
		unsigned char computed_hash[SYNX_HASH_LEN];
		if (digest(EVP_sha256(), data, data_len, computed_hash, SYNX_HASH_LEN) != 0) {
			free(file_buf);
			return SYN_CRYPT_ERR_CRYPTO;
		}
		if (memcmp(stored_hash, computed_hash, SYNX_HASH_LEN) != 0) {
			free(file_buf);
			return SYN_CRYPT_ERR_CORRUPT;
		}
	}

	FILE *out = fopen(out_path, "wb");
	if (!out) {
		free(file_buf);
		return SYN_CRYPT_ERR_IO;
	}
	int wrote_ok = data_len == 0 || fwrite(data, 1, data_len, out) == data_len;
	fclose(out);
	free(file_buf);
	return wrote_ok ? SYN_CRYPT_OK : SYN_CRYPT_ERR_IO;
}
