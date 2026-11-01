/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "oauth.h"
#include "secret.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <jansson.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define AUTH_URL "https://accounts.google.com/o/oauth2/v2/auth"
#define TOKEN_URL "https://oauth2.googleapis.com/token"
#define SCOPE "https://mail.google.com/"

static const char *token_url(void) {
	const char *e = getenv("SYN_MAIL_TOKEN_URL");
	return e && *e ? e : TOKEN_URL;
}

static void b64url(const unsigned char *in, size_t n, Buf *out) {
	Buf t = {0};
	b64_encode(in, n, &t);
	for (size_t i = 0; i < t.n && t.p[i] != '='; i++) {
		char c = t.p[i] == '+' ? '-' : t.p[i] == '/' ? '_' : t.p[i];
		buf_put(out, &c, 1);
	}
	buf_free(&t);
}

static void urlenc(Buf *out, const char *s) {
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("-._~", c)) {
			buf_put(out, s, 1);
		} else {
			buf_printf(out, "%%%02X", c);
		}
	}
}

static void form(Buf *b, const char *k, const char *v) {
	if (b->n) {
		buf_puts(b, "&");
	}
	buf_puts(b, k);
	buf_puts(b, "=");
	urlenc(b, v);
}

static size_t got(char *p, size_t sz, size_t n, void *ud) {
	buf_put(ud, p, sz * n);
	return sz * n;
}

/* POST a form to the token endpoint; the JSON reply in *out */
static int token_post(const char *body, json_t **out, char *err, size_t errn) {
	CURL *h = curl_easy_init();
	if (!h) {
		snprintf(err, errn, "curl failed to start");
		return -1;
	}
	Buf r = {0};
	curl_easy_setopt(h, CURLOPT_URL, token_url());
	curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, got);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &r);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
	const char *ca = getenv("SYN_MAIL_CAFILE");
	if (ca && *ca) {
		curl_easy_setopt(h, CURLOPT_CAINFO, ca);
	}
	CURLcode cc = curl_easy_perform(h);
	long code = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(h);
	if (cc != CURLE_OK) {
		snprintf(err, errn, "token endpoint: %s", curl_easy_strerror(cc));
		buf_wipe(&r);
		return -1;
	}
	json_t *j = json_loads(r.p ? r.p : "", 0, NULL);
	buf_wipe(&r);
	if (!j || code != 200) {
		const char *e = j ? json_string_value(json_object_get(j, "error")) : NULL;
		const char *d = j ? json_string_value(json_object_get(j, "error_description")) : NULL;
		snprintf(err, errn, "token endpoint answered %ld: %s%s%s", code, e ? e : "no JSON", d ? ": " : "", d ? d : "");
		json_decref(j);
		return -1;
	}
	*out = j;
	return 0;
}

static int client_keys(const Account *a, char **id, char **sec, char *err, size_t errn) {
	*id = secret_get(a->name, "client_id", err, errn);
	*sec = secret_get(a->name, "client_secret", err, errn);
	if (!*id || !*sec) {
		if (!*err) {
			snprintf(err, errn, "no OAuth client in the keyring for %s (kinds client_id and client_secret)", a->name);
		}
		secret_free(*id);
		secret_free(*sec);
		return -1;
	}
	return 0;
}

int oauth_access(const Account *a, Buf *tok, char *err, size_t errn) {
	char *id, *sec;
	err[0] = 0;
	if (client_keys(a, &id, &sec, err, errn) != 0) {
		return -1;
	}
	char *rt = secret_get(a->name, "refresh_token", err, errn);
	if (!rt) {
		if (!*err) {
			snprintf(err, errn, "%s is not signed in yet: run syn-mail login %s", a->name, a->name);
		}
		secret_free(id);
		secret_free(sec);
		return -1;
	}
	Buf body = {0};
	form(&body, "client_id", id);
	form(&body, "client_secret", sec);
	form(&body, "refresh_token", rt);
	form(&body, "grant_type", "refresh_token");
	secret_free(id);
	secret_free(sec);
	secret_free(rt);
	json_t *j = NULL;
	int rc = token_post(body.p, &j, err, errn);
	buf_wipe(&body);
	if (rc != 0) {
		return -1;
	}
	const char *at = json_string_value(json_object_get(j, "access_token"));
	if (!at) {
		snprintf(err, errn, "token endpoint gave no access token");
		json_decref(j);
		return -1;
	}
	buf_puts(tok, at);
	json_decref(j);
	return 0;
}

void oauth_xoauth2(const char *user, const char *token, Buf *out) {
	Buf raw = {0};
	buf_printf(&raw, "user=%s\x01" "auth=Bearer %s\x01\x01", user, token);
	b64_encode(raw.p, raw.n, out);
	buf_wipe(&raw);
}

/* the value of k in a query string, decoded, into out */
static int query_get(const char *q, const char *k, char *out, size_t n) {
	size_t kl = strlen(k);
	for (const char *p = q; p && *p;) {
		if (strncmp(p, k, kl) == 0 && p[kl] == '=') {
			p += kl + 1;
			size_t w = 0;
			while (*p && *p != '&' && *p != ' ' && w + 1 < n) {
				if (*p == '%' && p[1] && p[2]) {
					char hx[3] = {p[1], p[2], 0};
					out[w++] = (char)strtol(hx, NULL, 16);
					p += 3;
				} else {
					out[w++] = *p == '+' ? ' ' : *p;
					p++;
				}
			}
			out[w] = 0;
			return 0;
		}
		p = strchr(p, '&');
		if (p) {
			p++;
		}
	}
	return -1;
}

static void open_url(const char *url) {
	pid_t pid = fork();
	if (pid == 0) {
		int nul = open("/dev/null", 1);
		if (nul >= 0) {
			dup2(nul, 1);
			dup2(nul, 2);
		}
		execlp("xdg-open", "xdg-open", url, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		waitpid(pid, NULL, 0);
	}
}

int oauth_login(const Account *a, int open_browser, int wait_s, char *err, size_t errn) {
	char *id, *sec;
	err[0] = 0;
	if (client_keys(a, &id, &sec, err, errn) != 0) {
		return -1;
	}
	int ls = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_in sa = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
	socklen_t sl = sizeof sa;
	if (ls < 0 || bind(ls, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(ls, 4) != 0 ||
		getsockname(ls, (struct sockaddr *)&sa, &sl) != 0) {
		snprintf(err, errn, "cannot listen on 127.0.0.1");
		secret_free(id);
		secret_free(sec);
		return -1;
	}
	char redir[64];
	snprintf(redir, sizeof redir, "http://127.0.0.1:%d", ntohs(sa.sin_port));

	unsigned char rnd[48], dig[SHA256_DIGEST_LENGTH], st[16];
	RAND_bytes(rnd, sizeof rnd);
	RAND_bytes(st, sizeof st);
	Buf ver = {0}, chal = {0}, state = {0}, url = {0};
	b64url(rnd, sizeof rnd, &ver);
	SHA256((unsigned char *)ver.p, ver.n, dig);
	b64url(dig, sizeof dig, &chal);
	b64url(st, sizeof st, &state);
	Buf q = {0};
	form(&q, "client_id", id);
	form(&q, "redirect_uri", redir);
	form(&q, "response_type", "code");
	form(&q, "scope", SCOPE);
	form(&q, "code_challenge", chal.p);
	form(&q, "code_challenge_method", "S256");
	form(&q, "state", state.p);
	form(&q, "access_type", "offline");
	form(&q, "prompt", "consent");
	form(&q, "login_hint", a->address);
	const char *au = getenv("SYN_MAIL_AUTH_URL");
	buf_printf(&url, "%s?%s", au && *au ? au : AUTH_URL, q.p);
	buf_free(&q);
	fprintf(stderr, "syn-mail: sign in to %s at:\n%s\n", a->address, url.p);
	if (open_browser) {
		open_url(url.p);
	}

	char code[1024] = "", gerr[128] = "";
	time_t end = time(NULL) + wait_s;
	while (!code[0] && !gerr[0] && time(NULL) < end) {
		struct pollfd pf = {.fd = ls, .events = POLLIN};
		if (poll(&pf, 1, 1000) != 1) {
			continue;
		}
		int cs = accept4(ls, NULL, NULL, SOCK_CLOEXEC);
		if (cs < 0) {
			continue;
		}
		char req[4096];
		ssize_t r = recv(cs, req, sizeof req - 1, 0);
		req[r > 0 ? r : 0] = 0;
		char *qs = strchr(req, '?'), sv[64] = "";
		int ours = qs && strncmp(req, "GET /", 5) == 0 && query_get(qs + 1, "state", sv, sizeof sv) == 0 &&
			strcmp(sv, state.p) == 0;
		if (ours) {
			if (query_get(qs + 1, "code", code, sizeof code) != 0) {
				query_get(qs + 1, "error", gerr, sizeof gerr);
				if (!gerr[0]) {
					str_copy(gerr, sizeof gerr, "no code");
				}
			}
		}
		const char *page = ours && code[0]
			? "SYN-Mail is signed in. You may close this tab."
			: ours ? "SYN-Mail was not allowed." : "Not a SYN-Mail sign-in.";
		char resp[512];
		int m = snprintf(resp, sizeof resp,
			"HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
			"<html><body style=\"background:#111;color:#ddd;font-family:monospace\"><h2>%s</h2></body></html>",
			ours ? "200 OK" : "400 Bad Request", page);
		send(cs, resp, (size_t)m, MSG_NOSIGNAL);
		close(cs);
	}
	close(ls);
	buf_free(&url);
	buf_free(&chal);
	buf_free(&state);
	int rc = -1;
	if (!code[0]) {
		snprintf(err, errn, "sign-in %s", gerr[0] ? gerr : "timed out");
	} else {
		Buf body = {0};
		form(&body, "code", code);
		form(&body, "client_id", id);
		form(&body, "client_secret", sec);
		form(&body, "redirect_uri", redir);
		form(&body, "grant_type", "authorization_code");
		form(&body, "code_verifier", ver.p);
		json_t *j = NULL;
		if (token_post(body.p, &j, err, errn) == 0) {
			const char *rt = json_string_value(json_object_get(j, "refresh_token"));
			if (!rt) {
				snprintf(err, errn, "Google gave no refresh token");
			} else if (secret_set(a->name, "refresh_token", rt, err, errn) == 0) {
				rc = 0;
			}
			json_decref(j);
		}
		buf_wipe(&body);
	}
	buf_wipe(&ver);
	secret_free(id);
	secret_free(sec);
	return rc;
}
