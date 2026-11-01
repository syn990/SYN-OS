/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "jwt.h"
#include "util.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *jwt_mint(const char *key_path, const char *issuer, int ttl_seconds, char *err, size_t errn) {
	FILE *f = fopen(key_path, "rb");
	if (!f) {
		snprintf(err, errn, "cannot open key %s", key_path);
		return NULL;
	}
	EVP_PKEY *key = PEM_read_PrivateKey(f, NULL, NULL, NULL);
	fclose(f);
	if (!key) {
		snprintf(err, errn, "%s is not a PEM private key", key_path);
		return NULL;
	}

	char head_json[] = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
	char body_json[512];
	snprintf(body_json, sizeof body_json, "{\"iss\":\"%s\",\"exp\":%lld}", issuer,
		(long long)time(NULL) + ttl_seconds);
	char *head = b64_encode((const uint8_t *)head_json, strlen(head_json), true);
	char *body = b64_encode((const uint8_t *)body_json, strlen(body_json), true);
	size_t signing_n = strlen(head) + 1 + strlen(body);
	char *signing = malloc(signing_n + 1);
	snprintf(signing, signing_n + 1, "%s.%s", head, body);

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	size_t sig_n = 0;
	unsigned char *sig = NULL;
	if (ctx && EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, key) == 1 &&
		EVP_DigestSign(ctx, NULL, &sig_n, (const unsigned char *)signing, signing_n) == 1) {
		sig = malloc(sig_n);
		if (EVP_DigestSign(ctx, sig, &sig_n, (const unsigned char *)signing, signing_n) != 1) {
			free(sig);
			sig = NULL;
		}
	}
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(key);

	char *token = NULL;
	if (sig) {
		char *s = b64_encode(sig, sig_n, true);
		size_t n = signing_n + strlen(s) + 2;
		token = malloc(n);
		snprintf(token, n, "%s.%s", signing, s);
		free(s);
	} else {
		snprintf(err, errn, "signing with %s failed", key_path);
	}
	free(sig);
	free(signing);
	free(head);
	free(body);
	return token;
}
