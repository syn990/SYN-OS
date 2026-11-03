/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* syn-mail: the command-line core (slice 1, G223). The window and the APK are built on these same modules. */
#include "box.h"
#include "config.h"
#include "imap.h"
#include "mime.h"
#include "msg.h"
#include "net.h"
#include "oauth.h"
#include "secret.h"
#include "smtp.h"
#include "ui.h"

#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static void usage(void) {
	fputs("usage: syn-mail [-v] COMMAND\n"
	      "  add NAME ADDRESS --preset google|ionos [--display NAME] [--title TITLE]   add an account\n"
	      "  title NAME [TITLE]            the title under the name in the account's signature (none: clear it)\n"
	      "  password NAME                 store the account's password in the keyring (read from stdin)\n"
	      "  login NAME [--no-browser]     Google: sign in once in the browser; the token goes to the keyring\n"
	      "  list                          the accounts\n"
	      "  check [NAME] [--last N]       sign in, count the inbox, list the last N (10)\n"
	      "  read NAME N                   one message as text (N: sequence number from check)\n"
	      "  send NAME TO SUBJECT [--attach FILE]... [--plain]   send the text on stdin from NAME to TO, each FILE\n"
	      "                                attached, as a signed company letter (text and HTML); --plain: the text alone\n"
	      "  folders NAME                  the account's folders\n"
	      "  headers NAME [FOLDER] [--skip N] [--last N]   a page of a folder by UID, newest first\n"
	      "  show NAME FOLDER UID          one message by UID as text\n"
	      "  window [--shot FILE.png] [--size WxH]   the SYN-Mail window (--shot: draw once offscreen and save)\n"
	      "  export NAME FILE              the account and its keyring secrets as one file (mode 600) for the phone app\n",
		stderr);
}

static int load(Config *c) {
	char err[256] = "";
	if (config_load(c, err, sizeof err) != 0) {
		fprintf(stderr, "syn-mail: %s\n", err);
		return -1;
	}
	return 0;
}

static Account *find(Config *c, const char *name) {
	Account *a = config_find(c, name);
	if (!a) {
		fprintf(stderr, "syn-mail: no account '%s' (see syn-mail list)\n", name);
	}
	return a;
}

static int cmd_add(int argc, char **argv) {
	if (argc < 3) {
		usage();
		return 2;
	}
	const char *preset = NULL, *display = "", *title = "";
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = argv[++i];
		else if (!strcmp(argv[i], "--display") && i + 1 < argc) display = argv[++i];
		else if (!strcmp(argv[i], "--title") && i + 1 < argc) title = argv[++i];
		else { usage(); return 2; }
	}
	if (!preset) {
		fputs("syn-mail: add needs --preset google or --preset ionos\n", stderr);
		return 2;
	}
	if (!msg_addr_ok(argv[2]) || strlen(argv[1]) >= 64 || !argv[1][0]) {
		fputs("syn-mail: a short name and a plain address are needed\n", stderr);
		return 2;
	}
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = config_find(&c, argv[1]);
	if (!a) {
		c.acc = realloc(c.acc, sizeof *c.acc * (size_t)(c.n + 1));
		a = &c.acc[c.n++];
	}
	*a = (Account){0};
	str_copy(a->name, sizeof a->name, argv[1]);
	str_copy(a->address, sizeof a->address, argv[2]);
	str_copy(a->display, sizeof a->display, display);
	str_copy(a->title, sizeof a->title, title);
	if (config_preset(a, preset) != 0) {
		fprintf(stderr, "syn-mail: no preset '%s'\n", preset);
		config_free(&c);
		return 2;
	}
	char err[256] = "";
	int rc = config_save(&c, err, sizeof err);
	if (rc != 0) fprintf(stderr, "syn-mail: %s\n", err);
	else printf("added %s (%s, %s)\n", a->name, preset, a->auth == AUTH_OAUTH ? "OAuth" : "password");
	config_free(&c);
	return rc ? 1 : 0;
}

static int cmd_password(int argc, char **argv) {
	if (argc != 2) { usage(); return 2; }
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = find(&c, argv[1]);
	if (!a) { config_free(&c); return 1; }
	struct termios old, raw;
	int tty = isatty(0) && tcgetattr(0, &old) == 0;
	if (tty) {
		fprintf(stderr, "Password for %s: ", a->address);
		raw = old;
		raw.c_lflag &= ~(tcflag_t)ECHO;
		tcsetattr(0, TCSAFLUSH, &raw);
	}
	char pw[512];
	int got = fgets(pw, sizeof pw, stdin) != NULL;
	if (tty) {
		tcsetattr(0, TCSAFLUSH, &old);
		fputc('\n', stderr);
	}
	int rc = 1;
	if (got) {
		pw[strcspn(pw, "\r\n")] = 0;
		char err[256] = "";
		if (!pw[0]) fputs("syn-mail: empty password, nothing stored\n", stderr);
		else if (secret_set(a->name, "password", pw, err, sizeof err) != 0) fprintf(stderr, "syn-mail: %s\n", err);
		else { printf("stored the password for %s in the keyring\n", a->name); rc = 0; }
	}
	explicit_bzero(pw, sizeof pw);
	config_free(&c);
	return rc;
}

static int cmd_login(int argc, char **argv) {
	if (argc < 2) { usage(); return 2; }
	int browser = !(argc > 2 && !strcmp(argv[2], "--no-browser"));
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = find(&c, argv[1]);
	if (!a) { config_free(&c); return 1; }
	int rc = 0;
	if (a->auth != AUTH_OAUTH) {
		fprintf(stderr, "syn-mail: %s signs in with a password: syn-mail password %s\n", a->name, a->name);
		rc = 2;
	} else {
		char err[256] = "";
		if (oauth_login(a, browser, 300, err, sizeof err) != 0) { fprintf(stderr, "syn-mail: %s\n", err); rc = 1; }
		else printf("%s is signed in; the token is in the keyring\n", a->name);
	}
	config_free(&c);
	return rc;
}

/* The account and its secrets as one JSON file for SYN-Mail on the phone (Import from PC). It is written mode 600 and
 * never over a link; carry it with `syn-link send FILE` and delete it: the phone app deletes its copy on import. */
static int cmd_export(int argc, char **argv) {
	if (argc != 3) { usage(); return 2; }
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = find(&c, argv[1]);
	if (!a) { config_free(&c); return 1; }
	static const char *pw_kinds[] = {"password", NULL};
	static const char *oa_kinds[] = {"refresh_token", "client_id", "client_secret", NULL};
	const char **kinds = a->auth == AUTH_OAUTH ? oa_kinds : pw_kinds;
	json_t *sec = json_object();
	char err[256] = "";
	int rc = 1;
	for (int i = 0; kinds[i]; i++) {
		char *v = secret_get(a->name, kinds[i], err, sizeof err);
		if (!v) {
			if (!*err) {
				snprintf(err, sizeof err, "no %s for %s in the keyring: %s", kinds[i], a->name,
					a->auth == AUTH_OAUTH ? "run syn-mail login first" : "run syn-mail password first");
			}
			fprintf(stderr, "syn-mail: %s, nothing written\n", err);
			goto out;
		}
		json_object_set_new(sec, kinds[i], json_string(v));
		secret_free(v);
	}
	json_t *root = json_pack("{s:i, s:s, s:s, s:s, s:s, s:i, s:s, s:i, s:s, s:O}", "syn_mail_account", 1,
		"name", a->name, "address", a->address, "display_name", a->display,
		"imap_host", a->imap_host, "imap_port", a->imap_port, "smtp_host", a->smtp_host, "smtp_port", a->smtp_port,
		"auth", a->auth == AUTH_OAUTH ? "oauth" : "password", "secrets", sec);
	char *text = json_dumps(root, JSON_INDENT(1) | JSON_PRESERVE_ORDER);
	json_decref(root);
	int fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0 || fchmod(fd, 0600) != 0) {
		fprintf(stderr, "syn-mail: %s: %s\n", argv[2], strerror(errno));
	} else {
		size_t n = strlen(text), off = 0;
		while (off < n) {
			ssize_t w = write(fd, text + off, n - off);
			if (w <= 0) break;
			off += (size_t)w;
		}
		if (off == n && fsync(fd) == 0) {
			printf("wrote %s (mode 600): send it with syn-link send, then delete it here\n", argv[2]);
			rc = 0;
		} else {
			fprintf(stderr, "syn-mail: %s: %s\n", argv[2], strerror(errno));
		}
	}
	if (fd >= 0) close(fd);
	if (rc != 0 && fd >= 0) unlink(argv[2]);
	explicit_bzero(text, strlen(text));
	free(text);
out:
	{
		/* wipe the secrets held in the JSON tree before it is freed */
		const char *k;
		json_t *v;
		json_object_foreach(sec, k, v) {
			explicit_bzero((char *)json_string_value(v), json_string_length(v));
		}
	}
	json_decref(sec);
	config_free(&c);
	return rc;
}

static int cmd_list(void) {
	Config c = {0};
	if (load(&c) != 0) return 1;
	if (!c.n) printf("no accounts yet: syn-mail add NAME ADDRESS --preset google|ionos\n");
	for (int i = 0; i < c.n; i++) {
		Account *a = &c.acc[i];
		printf("%-10s %-36s imap %s:%d  smtp %s:%d  %s\n", a->name, a->address, a->imap_host, a->imap_port, a->smtp_host,
			a->smtp_port, a->auth == AUTH_OAUTH ? "oauth" : "password");
	}
	config_free(&c);
	return 0;
}

static Imap *connect_in(const Account *a) {
	char err[512] = "";
	Imap *im = imap_open(a, err, sizeof err);
	if (!im) {
		fprintf(stderr, "syn-mail: %s: %s\n", a->name, err);
		return NULL;
	}
	if (imap_signin(im, a, err, sizeof err) != 0) {
		fprintf(stderr, "syn-mail: %s\n", err);
		imap_close(im);
		return NULL;
	}
	return im;
}

/* "* 12 EXISTS" in the last command's responses */
static long count_of(Imap *im, const char *what) {
	for (int i = 0; i < im->nresp; i++) {
		long v;
		char w[16];
		if (sscanf(im->resp[i].text, "* %ld %15s", &v, w) == 2 && !strcmp(w, what)) return v;
	}
	return -1;
}

static void one_line(Buf *b, size_t max) {
	for (size_t i = 0; i < b->n; i++) {
		if (b->p[i] == '\r' || b->p[i] == '\n' || b->p[i] == '\t') b->p[i] = ' ';
	}
	if (b->n > max) {
		/* cut on a UTF-8 boundary */
		size_t k = max;
		while (k > 0 && ((unsigned char)b->p[k] & 0xc0) == 0x80) k--;
		b->n = k;
		b->p[k] = 0;
	}
}

static int check_one(const Account *a, int last) {
	Imap *im = connect_in(a);
	if (!im) return 1;
	int rc = 1;
	if (imap_cmd(im, "EXAMINE INBOX") != 0) {
		fprintf(stderr, "syn-mail: %s: EXAMINE INBOX: %s\n", a->name, im->status);
		goto out;
	}
	long exists = count_of(im, "EXISTS");
	long unseen = -1;
	if (imap_cmd(im, "STATUS INBOX (UNSEEN)") == 0) {
		for (int i = 0; i < im->nresp; i++) {
			char *u = strstr(im->resp[i].text, "UNSEEN ");
			if (u) unseen = strtol(u + 7, NULL, 10);
		}
	}
	printf("%s <%s>: %ld in the inbox, %ld unread\n", a->name, a->address, exists, unseen);
	if (exists > 0 && last > 0) {
		long from = exists - last + 1 > 1 ? exists - last + 1 : 1;
		if (imap_cmd(im, "FETCH %ld:%ld (FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])", from, exists) != 0) {
			fprintf(stderr, "syn-mail: %s: FETCH: %s\n", a->name, im->status);
			goto out;
		}
		for (int i = im->nresp - 1; i >= 0; i--) {
			long seq;
			char kind[8];
			ImapResp *r = &im->resp[i];
			if (sscanf(r->text, "* %ld %7s", &seq, kind) != 2 || strcmp(kind, "FETCH") || !r->lit.n) continue;
			int seen = strstr(r->text, "\\Seen") != NULL;
			Buf f = {0}, s = {0}, d = {0};
			mime_header(r->lit.p, r->lit.n, "From", &f);
			mime_header(r->lit.p, r->lit.n, "Subject", &s);
			mime_header(r->lit.p, r->lit.n, "Date", &d);
			one_line(&f, 40);
			one_line(&s, 70);
			one_line(&d, 31);
			printf("%6ld %c %-31s  %-40s  %s\n", seq, seen ? ' ' : '*', d.p ? d.p : "", f.p ? f.p : "", s.p ? s.p : "");
			buf_free(&f);
			buf_free(&s);
			buf_free(&d);
		}
	}
	rc = 0;
out:
	imap_close(im);
	return rc;
}

static int cmd_check(int argc, char **argv) {
	const char *name = NULL;
	int last = 10;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--last") && i + 1 < argc) last = atoi(argv[++i]);
		else if (!name) name = argv[i];
		else { usage(); return 2; }
	}
	if (last < 0 || last > 500) last = 10;
	Config c = {0};
	if (load(&c) != 0) return 1;
	int rc = 0;
	if (name) {
		Account *a = find(&c, name);
		rc = a ? check_one(a, last) : 1;
	} else {
		if (!c.n) fputs("syn-mail: no accounts yet\n", stderr), rc = 1;
		for (int i = 0; i < c.n; i++) rc |= check_one(&c.acc[i], last);
	}
	config_free(&c);
	return rc;
}

static int cmd_read(int argc, char **argv) {
	if (argc != 3) { usage(); return 2; }
	long seq = strtol(argv[2], NULL, 10);
	if (seq < 1) { usage(); return 2; }
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = find(&c, argv[1]);
	Imap *im = a ? connect_in(a) : NULL;
	int rc = 1;
	if (im) {
		if (imap_cmd(im, "EXAMINE INBOX") != 0 || imap_cmd(im, "FETCH %ld BODY.PEEK[]", seq) != 0) {
			fprintf(stderr, "syn-mail: %s: %s\n", a->name, im->status);
		} else {
			for (int i = 0; i < im->nresp; i++) {
				Buf *m = &im->resp[i].lit;
				if (!m->n) continue;
				static const char *hs[] = {"From", "To", "Date", "Subject"};
				for (int h = 0; h < 4; h++) {
					Buf v = {0};
					if (mime_header(m->p, m->n, hs[h], &v) == 0 && v.p) printf("%s: %s\n", hs[h], v.p);
					buf_free(&v);
				}
				Buf t = {0};
				mime_text(m->p, m->n, &t);
				printf("\n%s\n", t.p ? t.p : "");
				buf_free(&t);
				rc = 0;
				break;
			}
			if (rc) fprintf(stderr, "syn-mail: %s: no message %ld\n", a->name, seq);
		}
		imap_close(im);
	}
	config_free(&c);
	return rc;
}

#define SEND_MAX_ATTACH 16
#define SEND_MAX_BYTES (18L << 20) /* raw; base64 makes it ~24 MiB, inside Gmail's 25 MB and IONOS's limits */

/* one file into memory for attaching: a regular file, read whole; its name is the last part of the path */
static int attach_load(const char *path, MsgAttach *at, long *total) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "syn-mail: cannot open %s: %s\n", path, strerror(errno));
		return -1;
	}
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
		fprintf(stderr, "syn-mail: not a plain file: %s\n", path);
		fclose(f);
		return -1;
	}
	if (st.st_size > SEND_MAX_BYTES - *total) {
		fprintf(stderr, "syn-mail: attachments over %ld MiB in all: %s\n", SEND_MAX_BYTES >> 20, path);
		fclose(f);
		return -1;
	}
	unsigned char *d = malloc(st.st_size ? (size_t)st.st_size : 1);
	size_t got = d ? fread(d, 1, (size_t)st.st_size, f) : 0;
	int bad = !d || got != (size_t)st.st_size || ferror(f);
	fclose(f);
	if (bad) {
		fprintf(stderr, "syn-mail: could not read %s\n", path);
		free(d);
		return -1;
	}
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	char *name = strndup(*base ? base : "attachment", 200);
	at->name = name;
	at->type = msg_type_for(name);
	at->data = d;
	at->n = got;
	*total += (long)got;
	return 0;
}

/* the account's signature title, set or cleared (G265) */
static int cmd_title(int argc, char **argv) {
	if (argc < 2 || argc > 3) { usage(); return 2; }
	const char *t = argc == 3 ? argv[2] : "";
	for (const char *p = t; *p; p++) {
		if ((unsigned char)*p < 0x20) {
			fputs("syn-mail: a title is one line\n", stderr);
			return 2;
		}
	}
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = find(&c, argv[1]);
	int rc = 1;
	if (a) {
		str_copy(a->title, sizeof a->title, t);
		char err[256] = "";
		rc = config_save(&c, err, sizeof err) != 0;
		if (rc) fprintf(stderr, "syn-mail: %s\n", err);
		else if (*a->title) printf("%s signs as %s, %s\n", a->name, *a->display ? a->display : a->address, a->title);
		else printf("%s signs with no title\n", a->name);
	}
	config_free(&c);
	return rc;
}

/* who signs the account's letters: its display name, else its address, with its title, for the company */
static MsgSign account_sign(const Account *a) {
	return (MsgSign){.name = *a->display ? a->display : a->address, .title = a->title, .company = LETTER_COMPANY,
		.address = a->address};
}

static int cmd_send(int argc, char **argv) {
	const char *pos[3];
	int npos = 0, natt = 0, rc = 2, plain = 0;
	MsgAttach att[SEND_MAX_ATTACH];
	long total = 0;
	Config c = {0};
	Buf body = {0}, m = {0};
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--attach")) {
			if (i + 1 >= argc) { usage(); goto done; }
			if (natt == SEND_MAX_ATTACH) {
				fprintf(stderr, "syn-mail: at most %d attachments\n", SEND_MAX_ATTACH);
				goto done;
			}
			if (attach_load(argv[++i], &att[natt], &total) != 0) goto done;
			natt++;
		} else if (!strcmp(argv[i], "--plain")) {
			plain = 1;
		} else if (npos < 3) {
			pos[npos++] = argv[i];
		} else {
			usage();
			goto done;
		}
	}
	if (npos != 3) { usage(); goto done; }
	rc = 1;
	if (load(&c) != 0) goto done;
	Account *a = find(&c, pos[0]);
	if (!a) goto done;
	if (!msg_addr_ok(pos[1])) {
		fprintf(stderr, "syn-mail: not a plain address: %s\n", pos[1]);
		rc = 2;
		goto done;
	}
	char chunk[4096];
	size_t r;
	while ((r = fread(chunk, 1, sizeof chunk, stdin)) > 0) buf_put(&body, chunk, r);
	char mid[160];
	msg_id(a->address, mid, sizeof mid);
	MsgSign sign = account_sign(a);
	if (msg_build_letter(a->display, a->address, pos[1], pos[2], body.p ? body.p : "", mid, NULL, time(NULL),
		att, natt, plain ? NULL : &sign, &m) != 0) {
		fputs("syn-mail: the message could not be built\n", stderr);
	} else {
		char err[512] = "";
		if (smtp_send(a, pos[1], m.p, err, sizeof err) != 0) fprintf(stderr, "syn-mail: %s\n", err);
		else {
			printf("sent from %s to %s, %s", a->name, pos[1], mid);
			if (natt) printf(", %d attachment%s", natt, natt == 1 ? "" : "s");
			printf("\n");
			rc = 0;
		}
	}
done:
	for (int i = 0; i < natt; i++) {
		free((void *)att[i].name);
		free((void *)att[i].data);
	}
	buf_free(&m);
	buf_free(&body);
	config_free(&c);
	return rc;
}

static Imap *box_in(Config *c, const char *name, Account **ap) {
	Account *a = find(c, name);
	if (!a) return NULL;
	char err[512];
	Imap *im = box_connect(a, err, sizeof err);
	if (!im) fprintf(stderr, "syn-mail: %s: %s\n", a->name, err);
	*ap = a;
	return im;
}

static int cmd_folders(int argc, char **argv) {
	if (argc != 2) { usage(); return 2; }
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = NULL;
	Imap *im = box_in(&c, argv[1], &a);
	int rc = 1;
	if (im) {
		Folder *f;
		int n;
		char err[512];
		if (box_folders(im, &f, &n, err, sizeof err) != 0) fprintf(stderr, "syn-mail: %s: %s\n", a->name, err);
		else {
			for (int i = 0; i < n; i++) printf("%s%s\t%s\n", f[i].name, f[i].noselect ? " (no messages)" : "", f[i].raw);
			free(f);
			rc = 0;
		}
		imap_close(im);
	}
	config_free(&c);
	return rc;
}

/* s cut or padded to w columns, counted in characters rather than bytes, so accented names keep the columns */
static void put_col(const char *s, int w) {
	size_t n = strlen(s), i = 0;
	int k = 0;
	while (i < n && k < w) {
		uint32_t cp;
		size_t step = utf8_next(s + i, n - i, &cp);
		fwrite(s + i, 1, step, stdout);
		i += step;
		k++;
	}
	for (; k < w; k++) putchar(' ');
}

static int cmd_headers(int argc, char **argv) {
	const char *name = NULL, *folder = "INBOX";
	long skip = 0;
	int last = 20, pos = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--skip") && i + 1 < argc) skip = atol(argv[++i]);
		else if (!strcmp(argv[i], "--last") && i + 1 < argc) last = atoi(argv[++i]);
		else if (pos == 0) name = argv[i], pos++;
		else if (pos == 1) folder = argv[i], pos++;
		else { usage(); return 2; }
	}
	if (!name || skip < 0 || last < 0 || last > 500) { usage(); return 2; }
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = NULL;
	Imap *im = box_in(&c, name, &a);
	int rc = 1;
	if (im) {
		Page p;
		char err[512];
		if (box_page(im, folder, skip, last, &p, err, sizeof err) != 0) fprintf(stderr, "syn-mail: %s: %s\n", a->name, err);
		else {
			printf("%s %s: %ld messages, %ld unread\n", a->name, folder, p.exists, p.unseen);
			for (int i = 0; i < p.n; i++) {
				Header *h = &p.h[i];
				printf("%8lu %c %-12s  ", h->uid, h->seen ? ' ' : '*', h->date);
				put_col(h->from, 30);
				printf("  %s\n", h->subject);
			}
			page_free(&p);
			rc = 0;
		}
		imap_close(im);
	}
	config_free(&c);
	return rc;
}

static int cmd_show(int argc, char **argv) {
	if (argc != 4) { usage(); return 2; }
	unsigned long uid = strtoul(argv[3], NULL, 10);
	if (!uid) { usage(); return 2; }
	Config c = {0};
	if (load(&c) != 0) return 1;
	Account *a = NULL;
	Imap *im = box_in(&c, argv[1], &a);
	int rc = 1;
	if (im) {
		Buf m = {0};
		char err[512];
		if (box_body(im, argv[2], uid, &m, err, sizeof err) != 0) fprintf(stderr, "syn-mail: %s: %s\n", a->name, err);
		else {
			static const char *hs[] = {"From", "To", "Date", "Subject"};
			for (int h = 0; h < 4; h++) {
				Buf v = {0};
				if (mime_header(m.p, m.n, hs[h], &v) == 0 && v.p) printf("%s: %s\n", hs[h], v.p);
				buf_free(&v);
			}
			Buf t = {0};
			mime_text(m.p, m.n, &t);
			printf("\n%s\n", t.p ? t.p : "");
			buf_free(&t);
			rc = 0;
		}
		buf_free(&m);
		imap_close(im);
	}
	config_free(&c);
	return rc;
}

int main(int argc, char **argv) {
	int i = 1;
	if (i < argc && !strcmp(argv[i], "-v")) {
		net_trace = 1;
		i++;
	}
	if (i >= argc) { usage(); return 2; }
	const char *cmd = argv[i];
	int n = argc - i;
	char **v = argv + i;
	if (!strcmp(cmd, "add")) return cmd_add(n, v);
	if (!strcmp(cmd, "password")) return cmd_password(n, v);
	if (!strcmp(cmd, "login")) return cmd_login(n, v);
	if (!strcmp(cmd, "list")) return cmd_list();
	if (!strcmp(cmd, "check")) return cmd_check(n, v);
	if (!strcmp(cmd, "read")) return cmd_read(n, v);
	if (!strcmp(cmd, "send")) return cmd_send(n, v);
	if (!strcmp(cmd, "title")) return cmd_title(n, v);
	if (!strcmp(cmd, "folders")) return cmd_folders(n, v);
	if (!strcmp(cmd, "headers")) return cmd_headers(n, v);
	if (!strcmp(cmd, "show")) return cmd_show(n, v);
	if (!strcmp(cmd, "window")) return ui_main(n, v);
	if (!strcmp(cmd, "export")) return cmd_export(n, v);
	usage();
	return 2;
}
