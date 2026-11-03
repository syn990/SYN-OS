/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "html.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <png.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define RENDER_TIMEOUT_MS 30000

/* no script ever; remote images, styles and fonts only when asked for; nothing may move the base or post a form */
static const char *CSP_BLOCKED = "default-src 'none'; img-src data: file: blob:; style-src 'unsafe-inline' file:; "
                                 "font-src data: file:; base-uri 'none'; form-action 'none'";
static const char *CSP_REMOTE = "default-src 'none'; img-src data: file: blob: http: https:; "
                                "style-src 'unsafe-inline' file: http: https:; font-src data: file: http: https:; "
                                "base-uri 'none'; form-action 'none'";

static const char *ext_for(const char *type) {
	static const struct { const char *t, *e; } m[] = {
		{"image/png", "png"}, {"image/jpeg", "jpg"}, {"image/jpg", "jpg"}, {"image/pjpeg", "jpg"}, {"image/gif", "gif"},
		{"image/webp", "webp"}, {"image/svg+xml", "svg"}, {"image/bmp", "bmp"}, {"image/x-icon", "ico"},
		{"image/avif", "avif"}, {"text/css", "css"},
	};
	for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) {
		if (!strcmp(type, m[i].t)) return m[i].e;
	}
	return "bin";
}

static int ci_at(const char *s, size_t n, size_t i, const char *w) {
	size_t l = strlen(w);
	return i + l <= n && !strncasecmp(s + i, w, l);
}

static int hexv(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	c = tolower(c);
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

/* does s[i..] (after spaces and quotes) start a remote address? */
static int remote_at(const char *s, size_t n, size_t i) {
	while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '"' || s[i] == '\'' || s[i] == '\n' || s[i] == '\r')) i++;
	return ci_at(s, n, i, "http:") || ci_at(s, n, i, "https:") || ci_at(s, n, i, "//");
}

void html_prepare(const MimeHtml *h, int remote, Buf *out, int *remote_refs) {
	const char *s = h->html.p ? h->html.p : "";
	size_t n = h->html.n, i = 0;
	int refs = 0;
	/* a byte order mark wins over any charset the page declares: the text is UTF-8 by now */
	buf_puts(out, "\xEF\xBB\xBF");
	if (n >= 3 && !memcmp(s, "\xEF\xBB\xBF", 3)) i = 3;
	size_t j = i;
	while (j < n && isspace((unsigned char)s[j])) j++;
	if (ci_at(s, n, j, "<!doctype")) {
		const char *gt = memchr(s + j, '>', n - j);
		if (gt) {
			buf_put(out, s + i, (size_t)(gt - s) + 1 - i);
			i = (size_t)(gt - s) + 1;
		}
	}
	buf_printf(out,
		"<meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"%s\">"
		"<meta http-equiv=\"x-dns-prefetch-control\" content=\"off\"><meta name=\"referrer\" content=\"no-referrer\">",
		remote ? CSP_REMOTE : CSP_BLOCKED);
	while (i < n) {
		if (ci_at(s, n, i, "cid:")) {
			/* cid:ID, percent-encoded (RFC 2392), ends at a quote, a bracket or a space */
			size_t e = i + 4;
			while (e < n && !strchr("\"'() \t\r\n<>", s[e])) e++;
			char id[256];
			size_t w = 0;
			for (size_t k = i + 4; k < e && w + 1 < sizeof id; k++) {
				if (s[k] == '%' && k + 2 < e && hexv(s[k + 1]) >= 0 && hexv(s[k + 2]) >= 0) {
					id[w++] = (char)(hexv(s[k + 1]) * 16 + hexv(s[k + 2]));
					k += 2;
				} else {
					id[w++] = s[k];
				}
			}
			id[w] = 0;
			int hit = -1;
			for (int p = 0; p < h->np && hit < 0; p++) {
				if (!strcmp(h->part[p].cid, id)) hit = p;
			}
			for (int p = 0; p < h->np && hit < 0; p++) {
				if (!strcasecmp(h->part[p].cid, id)) hit = p;
			}
			if (hit >= 0) {
				buf_printf(out, "cid-%d.%s", hit + 1, ext_for(h->part[hit].type));
				i = e;
				continue;
			}
		} else if (ci_at(s, n, i, "http-equiv")) {
			/* <meta http-equiv="refresh"> would move the page to an address of the sender's choosing */
			size_t e = i + 10;
			while (e < n && (s[e] == ' ' || s[e] == '=' || s[e] == '"' || s[e] == '\'')) e++;
			if (ci_at(s, n, e, "refresh")) {
				buf_put(out, s + i, e - i);
				buf_puts(out, "x-refresh-off");
				i = e + 7;
				continue;
			}
		} else if ((i == 0 || !isalnum((unsigned char)s[i - 1])) &&
			((ci_at(s, n, i, "src") && i + 3 < n && (s[i + 3] == '=' || isspace((unsigned char)s[i + 3])) &&
				 remote_at(s, n, i + 3 + strspn(s + i + 3, " \t=")) ) ||
				(ci_at(s, n, i, "background=") && remote_at(s, n, i + 11)) || (ci_at(s, n, i, "url(") && remote_at(s, n, i + 4)) ||
				(ci_at(s, n, i, "@import") && remote_at(s, n, i + 7 + strspn(s + i + 7, " \t(url")))))
		{
			refs++;
		}
		buf_put(out, s + i, 1);
		i++;
	}
	if (remote_refs) *remote_refs = refs;
}

static int base_dir(char *out, size_t n, char *err, size_t errn) {
	const char *rt = getenv("XDG_RUNTIME_DIR");
	const char *home = getenv("HOME");
	if (rt && *rt) snprintf(out, n, "%s/syn-mail", rt);
	else if (home && *home) snprintf(out, n, "%s/.cache/syn-mail", home);
	else {
		snprintf(err, errn, "no XDG_RUNTIME_DIR or HOME for the page");
		return -1;
	}
	if (mkdir(out, 0700) != 0 && errno != EEXIST) {
		snprintf(err, errn, "%s: %s", out, strerror(errno));
		return -1;
	}
	struct stat st;
	if (lstat(out, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
		snprintf(err, errn, "%s is not a folder of this user's", out);
		return -1;
	}
	if ((st.st_mode & 077) && chmod(out, 0700) != 0) {
		snprintf(err, errn, "%s: %s", out, strerror(errno));
		return -1;
	}
	return 0;
}

int html_dir(const char *kind, char *out, size_t n, char *err, size_t errn) {
	char base[3800];
	if (base_dir(base, sizeof base, err, errn) != 0) return -1;
	if ((size_t)snprintf(out, n, "%s/%s-XXXXXX", base, kind) >= n || !mkdtemp(out)) {
		snprintf(err, errn, "cannot make a folder in %s: %s", base, strerror(errno));
		return -1;
	}
	return 0;
}

static int write_file(const char *path, const void *p, size_t n, char *err, size_t errn) {
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) {
		snprintf(err, errn, "%s: %s", path, strerror(errno));
		return -1;
	}
	const char *c = p;
	while (n) {
		ssize_t w = write(fd, c, n);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) {
			snprintf(err, errn, "%s: %s", path, strerror(errno));
			close(fd);
			return -1;
		}
		c += w;
		n -= (size_t)w;
	}
	return close(fd);
}

int html_write(const MimeHtml *h, int remote, const char *dir, char *path, size_t pn, int *remote_refs, char *err,
	size_t errn) {
	char f[4200];
	for (int i = 0; i < h->np; i++) {
		snprintf(f, sizeof f, "%s/cid-%d.%s", dir, i + 1, ext_for(h->part[i].type));
		if (write_file(f, h->part[i].data.p ? h->part[i].data.p : "", h->part[i].data.n, err, errn) != 0) return -1;
	}
	Buf page = {0};
	html_prepare(h, remote, &page, remote_refs);
	snprintf(path, pn, "%s/message.html", dir);
	int rc = write_file(path, page.p, page.n, err, errn);
	buf_free(&page);
	return rc;
}

static int rm_one(const char *p, const struct stat *st, int flag, struct FTW *ftw) {
	(void)st; (void)flag; (void)ftw;
	remove(p);
	return 0;
}

void html_rmdir(const char *dir) {
	const char *b = strrchr(dir, '/');
	b = b ? b + 1 : dir;
	/* only a folder html_dir made */
	if (strncmp(b, "render-", 7) && strncmp(b, "open-", 5)) return;
	nftw(dir, rm_one, 16, FTW_DEPTH | FTW_PHYS);
}

void html_sweep(void) {
	char base[3800], err[200];
	if (base_dir(base, sizeof base, err, sizeof err) != 0) return;
	DIR *d = opendir(base);
	if (!d) return;
	time_t now = time(NULL);
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "open-", 5)) continue;
		char p[4200];
		struct stat st;
		snprintf(p, sizeof p, "%s/%s", base, e->d_name);
		if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode) && now - st.st_mtime > 3600) html_rmdir(p);
	}
	closedir(d);
}

/* a program by name on the PATH, or a path as given */
static const char *find_prog(const char *name, char *out, size_t n) {
	if (strchr(name, '/')) return access(name, X_OK) == 0 ? name : NULL;
	const char *path = getenv("PATH");
	if (!path) path = "/usr/local/bin:/usr/bin:/bin";
	while (*path) {
		size_t l = strcspn(path, ":");
		if (l && (size_t)snprintf(out, n, "%.*s/%s", (int)l, path, name) < n && access(out, X_OK) == 0) return out;
		path += l;
		if (*path == ':') path++;
	}
	return NULL;
}

static const char *pick(const char *env, const char *const *names, char *out, size_t n) {
	const char *e = getenv(env);
	if (e && *e) return strcmp(e, "none") ? find_prog(e, out, n) : NULL;
	for (int i = 0; names[i]; i++) {
		if (find_prog(names[i], out, n)) return out;
	}
	return NULL;
}

const char *html_renderer(void) {
	static char p[1024];
	static const char *const names[] = {"chromium", "google-chrome-stable", "google-chrome", NULL};
	return pick("SYN_MAIL_CHROME", names, p, sizeof p);
}

const char *html_browser(void) {
	static char p[1024];
	static const char *const names[] = {"google-chrome-stable", "google-chrome", "chromium", NULL};
	return pick("SYN_MAIL_BROWSER", names, p, sizeof p);
}

/* file:// with anything outside the plain path characters percent-encoded */
static void file_url(const char *path, char *out, size_t n) {
	size_t w = (size_t)snprintf(out, n, "file://");
	for (const unsigned char *c = (const unsigned char *)path; *c && w + 4 < n; c++) {
		if (isalnum(*c) || strchr("/-._~", *c)) out[w++] = (char)*c;
		else w += (size_t)snprintf(out + w, n - w, "%%%02X", *c);
	}
	out[w < n ? w : n - 1] = 0;
}

int html_render(const char *page, int width, int remote, const char *png, char *err, size_t errn) {
	const char *bin = html_renderer();
	if (!bin) {
		snprintf(err, errn, "no headless Chrome to draw the HTML (chromium or google-chrome)");
		return -1;
	}
	char dir[4096], prof[4200], shot[4200], size[64], scale[64], url[8192];
	str_copy(dir, sizeof dir, page);
	char *sl = strrchr(dir, '/');
	if (sl) *sl = 0;
	snprintf(prof, sizeof prof, "--user-data-dir=%s/profile", dir);
	snprintf(shot, sizeof shot, "--screenshot=%s", png);
	/* most mail is laid out 600 to 640 pixels wide: a narrower pane gets the page laid out at 640 and drawn smaller by
	 * Chrome itself (a device scale below 1), so nothing is cut off at the right and the text stays sharp */
	int css_w = width < HTML_MIN_CSS_W ? HTML_MIN_CSS_W : width;
	double dsf = (double)width / css_w;
	snprintf(size, sizeof size, "--window-size=%d,%d", css_w, (int)(HTML_RENDER_MAX_H / dsf));
	snprintf(scale, sizeof scale, "--force-device-scale-factor=%.6f", dsf);
	file_url(page, url, sizeof url);
	const char *argv[32];
	int a = 0;
	argv[a++] = bin;
	argv[a++] = "--headless";
	argv[a++] = "--disable-gpu";
	argv[a++] = "--hide-scrollbars";
	argv[a++] = scale;
	argv[a++] = "--no-first-run";
	argv[a++] = "--no-default-browser-check";
	argv[a++] = "--disable-extensions";
	argv[a++] = "--disable-sync";
	argv[a++] = "--disable-background-networking";
	argv[a++] = "--disable-component-update";
	argv[a++] = "--disable-crash-reporter";
	argv[a++] = "--mute-audio";
	argv[a++] = "--default-background-color=ffffffff";
	argv[a++] = prof;
	if (!remote) {
		/* the second lock: every request goes to a proxy that is not there, loopback included */
		argv[a++] = "--proxy-server=http://127.0.0.1:9";
		argv[a++] = "--proxy-bypass-list=<-loopback>";
	}
	argv[a++] = size;
	argv[a++] = shot;
	argv[a++] = url;
	argv[a] = NULL;

	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
	posix_spawnattr_init(&at);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);
	pid_t pid;
	int rc = posix_spawn(&pid, bin, &fa, &at, (char *const *)argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	if (rc != 0) {
		snprintf(err, errn, "%s: %s", bin, strerror(rc));
		return -1;
	}
	int st = 0, waited = 0;
	for (;;) {
		pid_t w = waitpid(pid, &st, WNOHANG);
		if (w == pid) break;
		if (w < 0 && errno != EINTR) break;
		if (waited >= RENDER_TIMEOUT_MS) {
			kill(-pid, SIGKILL);
			waitpid(pid, &st, 0);
			snprintf(err, errn, "the HTML took more than %d s to draw", RENDER_TIMEOUT_MS / 1000);
			return -1;
		}
		nanosleep(&(struct timespec){0, 50 * 1000000L}, NULL);
		waited += 50;
	}
	kill(-pid, SIGKILL); /* anything it left behind in its session */
	nftw(prof + strlen("--user-data-dir="), rm_one, 16, FTW_DEPTH | FTW_PHYS);
	struct stat ps;
	if (stat(png, &ps) != 0 || ps.st_size == 0) {
		snprintf(err, errn, "the headless Chrome drew nothing (exit %d)", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
		return -1;
	}
	return 0;
}

int html_open(const char *page, char *err, size_t errn) {
	const char *bin = html_browser();
	if (!bin) {
		snprintf(err, errn, "no Chrome to open the message in");
		return -1;
	}
	char url[8192];
	file_url(page, url, sizeof url);
	/* twice forked, so the browser is nobody's child and leaves no zombie */
	pid_t p = fork();
	if (p < 0) {
		snprintf(err, errn, "fork: %s", strerror(errno));
		return -1;
	}
	if (p == 0) {
		setsid();
		if (fork() != 0) _exit(0);
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) {
			dup2(nul, 0);
			dup2(nul, 1);
			dup2(nul, 2);
		}
		execl(bin, bin, url, (char *)NULL);
		_exit(127);
	}
	waitpid(p, NULL, 0);
	return 0;
}

uint32_t *html_png_load(const char *path, int *w, int *h, int *more) {
	png_image img = {0};
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&img, path)) return NULL;
	img.format = PNG_FORMAT_BGRA; /* in memory B G R A: 0xAARRGGBB on a little-endian machine */
	size_t W = img.width, H = img.height;
	if (!W || !H || W > 8000 || H > 20000) {
		png_image_free(&img);
		return NULL;
	}
	uint32_t *px = malloc(W * H * 4);
	if (!px || !png_image_finish_read(&img, NULL, px, (png_int_32)(W * 4), NULL)) {
		free(px);
		png_image_free(&img);
		return NULL;
	}
	/* the rows at the bottom the same colour as the bottom row's first pixel are the page's empty end */
	uint32_t bg = px[(H - 1) * W];
	size_t last = 0;
	int found = 0;
	for (size_t y = H; y-- > 0 && !found;) {
		for (size_t x = 0; x < W; x++) {
			if (px[y * W + x] != bg) {
				last = y;
				found = 1;
				break;
			}
		}
	}
	size_t keep = found ? last + 1 + 16 : 16;
	if (keep > H) keep = H;
	*more = found && last + 1 >= H;
	*w = (int)W;
	*h = (int)keep;
	return px;
}
