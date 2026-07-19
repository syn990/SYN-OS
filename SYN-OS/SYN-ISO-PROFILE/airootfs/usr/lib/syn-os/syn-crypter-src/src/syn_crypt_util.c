/* ------------------------------------------------------------------------
 *   Small helpers shared by every syn_crypt_* algorithm module: stdin
 *   secret reading, secure buffer wipe, and the "does this already look
 *   encrypted" magic/marker sniff.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_crypt.h"

#include <stdio.h>
#include <string.h>
#include <openssl/crypto.h>

int syn_crypt_read_secret(char *buf, size_t buflen) {
	if (!fgets(buf, (int)buflen, stdin)) {
		return -1;
	}
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
	}
	return 0;
}

void syn_crypt_wipe(void *buf, size_t len) {
	OPENSSL_cleanse(buf, len);
}

int syn_crypt_looks_encrypted(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return 0;
	}
	/* Longest marker to check is 9 bytes (REDSHIRT/REDSHRT2); the 4-byte
	 * magics are checked against just the first 4 of the same buffer. */
	unsigned char head[SYN_CRYPT_MARKER_LEN];
	size_t got = fread(head, 1, sizeof(head), f);
	fclose(f);

	if (got >= SYN_CRYPT_MAGIC_LEN &&
			(memcmp(head, SYN_CRYPT_MAGIC_AES, SYN_CRYPT_MAGIC_LEN) == 0 ||
			 memcmp(head, SYN_CRYPT_MAGIC_BLOWFISH, SYN_CRYPT_MAGIC_LEN) == 0 ||
			 memcmp(head, SYN_CRYPT_MAGIC_RSA, SYN_CRYPT_MAGIC_LEN) == 0 ||
			 memcmp(head, SYN_CRYPT_MAGIC_REDSHIRT_SYNX, SYN_CRYPT_MAGIC_LEN) == 0)) {
		return 1;
	}
	if (got >= SYN_CRYPT_MARKER_LEN &&
			(memcmp(head, SYN_CRYPT_MARKER_REDSHIRT, SYN_CRYPT_MARKER_LEN) == 0 ||
			 memcmp(head, SYN_CRYPT_MARKER_REDSHRT2, SYN_CRYPT_MARKER_LEN) == 0)) {
		return 1;
	}
	return 0;
}
