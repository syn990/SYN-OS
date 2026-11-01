/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* The keyring through libsecret. Tests set SYN_MAIL_SECRETS_FILE to a mode-600 JSON file instead, so they never touch
 * the real keyring; the file is refused if anyone but its owner can read it. */
#include "secret.h"
#include "util.h"

#include <jansson.h>
#include <libsecret/secret.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const SecretSchema schema = {
	"org.syn-os.syn-mail", SECRET_SCHEMA_DONT_MATCH_NAME,
	{{"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
	 {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
	 {"kind", SECRET_SCHEMA_ATTRIBUTE_STRING},
	 {NULL, 0}},
	0, 0, 0, 0, 0, 0, 0, 0,
};

static const char *test_file(void) {
	const char *f = getenv("SYN_MAIL_SECRETS_FILE");
	return f && *f ? f : NULL;
}

static json_t *test_load(const char *f, char *err, size_t errn) {
	struct stat st;
	if (stat(f, &st) == 0 && (st.st_mode & 077)) {
		snprintf(err, errn, "%s is readable by others; refusing it", f);
		return NULL;
	}
	json_t *o = json_load_file(f, 0, NULL);
	return o ? o : json_object();
}

char *secret_get(const char *account, const char *kind, char *err, size_t errn) {
	const char *f = test_file();
	if (f) {
		json_t *o = test_load(f, err, errn);
		if (!o) {
			return NULL;
		}
		char key[160];
		snprintf(key, sizeof key, "%s/%s", account, kind);
		const char *v = json_string_value(json_object_get(o, key));
		char *r = v ? strdup(v) : NULL;
		json_decref(o);
		return r;
	}
	GError *ge = NULL;
	gchar *pw = secret_password_lookup_sync(&schema, NULL, &ge, "service", "syn-mail", "account", account, "kind", kind, NULL);
	if (ge) {
		snprintf(err, errn, "keyring: %s", ge->message);
		g_error_free(ge);
		return NULL;
	}
	if (!pw) {
		return NULL;
	}
	char *r = strdup(pw);
	secret_password_free(pw);
	return r;
}

int secret_set(const char *account, const char *kind, const char *value, char *err, size_t errn) {
	const char *f = test_file();
	if (f) {
		json_t *o = test_load(f, err, errn);
		if (!o) {
			return -1;
		}
		char key[160];
		snprintf(key, sizeof key, "%s/%s", account, kind);
		json_object_set_new(o, key, json_string(value));
		mode_t old = umask(077);
		int rc = json_dump_file(o, f, 0);
		umask(old);
		json_decref(o);
		return rc;
	}
	char label[160];
	snprintf(label, sizeof label, "SYN-Mail %s %s", account, kind);
	GError *ge = NULL;
	gboolean ok = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, label, value, NULL, &ge,
		"service", "syn-mail", "account", account, "kind", kind, NULL);
	if (!ok) {
		snprintf(err, errn, "keyring: %s", ge ? ge->message : "store failed");
		if (ge) {
			g_error_free(ge);
		}
		return -1;
	}
	return 0;
}

int secret_clear(const char *account, const char *kind) {
	if (test_file()) {
		return 0;
	}
	return secret_password_clear_sync(&schema, NULL, NULL, "service", "syn-mail", "account", account, "kind", kind,
		NULL) ? 0 : -1;
}

void secret_free(char *s) {
	if (s) {
		OPENSSL_cleanse(s, strlen(s));
		free(s);
	}
}
