/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* The window. Layout, left to right: accounts and their folders; the folder's messages, newest first; the message
 * being read. A compose sheet covers the right two panes. Drawing follows SYN-CAST's ui.c (wl_shm buffers, pixman,
 * fcft, syn_theme). All mail traffic runs on the workers (worker.c); this file only draws and asks. */
#include "ui.h"
#include "box.h"
#include "config.h"
#include "html.h"
#include "mime.h"
#include "msg.h"
#include "syn_theme.h"
#include "worker.h"
#include "xdg-decoration-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pixman.h>
#include <png.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#define PAGE 50            /* headers asked for at a time */
#define REFRESH_MS 120000  /* the open folder is looked at again this often */
#define FONT "Terminus:size=14"

typedef struct {
	struct wl_buffer *wl;
	pixman_image_t *pix;
	uint32_t *data;
	size_t size;
	int w, h;
	bool busy;
} buffer;

typedef struct {
	int x, y, w, h;
} rect;

typedef struct {
	Folder *f;
	int nf;
	bool loaded, failed;
	long inbox_unseen;
} AccView;

/* a row of the sidebar: an account (folder -1) or one of its folders */
typedef struct {
	int acc, folder;
} SideRow;

enum { PANE_SIDE, PANE_LIST, PANE_READ };
enum { F_FROM, F_TO, F_SUBJECT, F_BODY, F_COUNT };

typedef struct {
	char *p;
	size_t n, cap;
} Text;

static struct {
	/* model */
	Config cfg;
	AccView *acc;
	SideRow side[512];
	int nside, side_sel;          /* the highlighted sidebar row */
	int open_acc;                 /* the folder on show */
	char open_raw[256], open_name[256];
	Page page;
	bool page_loading, page_failed;
	long gen_page, gen_body;
	int sel, list_top;            /* selected message, first row shown */
	Buf raw;                      /* the message being read */
	Text body;                    /* its readable text */
	char rd_from[300], rd_to[300], rd_date[100], rd_subject[300];
	unsigned long rd_uid;
	bool body_loading;
	int read_top;                 /* first wrapped line shown */

	/* the HTML view (G265): the page drawn by a headless Chrome off this thread, the text view beside it */
	MimeHtml mh;
	bool has_html, view_text, allow_remote, html_busy, html_pending, html_more;
	bool remote_always;           /* --images: every message read as if i had been pressed */
	int remote_refs, html_w, html_h, html_drawn_w;
	long html_gen;
	uint32_t *html_px;
	pixman_image_t *html_img;
	char html_err[300];
	int html_fd[2];
	int pane;
	char status[512];
	bool status_bad;
	int64_t last_refresh;

	/* compose */
	bool composing, sending;
	int c_from, c_field;
	Text c[F_COUNT];
	char c_reply_id[256];

	/* shot mode */
	const char *shot;
	int open_n;                   /* --open N: the message read in a shot */

	/* wayland */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wl_data_device_manager *ddm;
	struct zxdg_decoration_manager_v1 *deco_mgr;
	struct zxdg_toplevel_decoration_v1 *deco;
	struct wl_data_device *ddev;
	struct wl_data_offer *offer;
	int clip_fd;
	char *clip_buf;
	size_t clip_len;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	buffer buf[2];
	int w, h, want_w, want_h;
	bool configured, running, frame_pending, dirty;
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	int repeat_fd, repeat_rate, repeat_delay;
	uint32_t repeat_key;
	double px, py;
	int64_t last_click;
	int last_click_row;

	/* drawing */
	struct fcft_font *font, *font_big;
	int lh, cw;
	syn_palette pal;
	rect r_head, r_side, r_list, r_read, r_foot;
	FILE *drawlog;
} ui = {.clip_fd = -1, .repeat_fd = -1, .open_acc = -1, .last_click_row = -1, .html_fd = {-1, -1}};

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------- text fields ---------- */

static void text_set(Text *t, const char *s) {
	t->n = 0;
	size_t n = strlen(s);
	if (n + 1 > t->cap) {
		t->cap = n + 64;
		t->p = realloc(t->p, t->cap);
	}
	memcpy(t->p, s, n + 1);
	t->n = n;
}

static void text_add(Text *t, const char *s, size_t n) {
	if (t->n + n + 1 > t->cap) {
		t->cap = (t->n + n + 1) * 2;
		t->p = realloc(t->p, t->cap);
	}
	memcpy(t->p + t->n, s, n);
	t->n += n;
	t->p[t->n] = 0;
}

static void text_backspace(Text *t) {
	if (!t->n) return;
	size_t k = t->n - 1;
	while (k > 0 && ((unsigned char)t->p[k] & 0xc0) == 0x80) k--;
	t->n = k;
	t->p[k] = 0;
}

static const char *text_str(const Text *t) {
	return t->p ? t->p : "";
}

static void set_status(bool bad, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_status(bool bad, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(ui.status, sizeof ui.status, fmt, ap);
	va_end(ap);
	ui.status_bad = bad;
	ui.dirty = true;
}

/* ---------- drawing (after SYN-CAST's ui.c) ---------- */

static pixman_image_t *dst;
static void layout(void);
static void start_render(void);

static pixman_color_t col(syn_rgb c) {
	return (pixman_color_t){(uint16_t)(c.r * 257), (uint16_t)(c.g * 257), (uint16_t)(c.b * 257), 0xFFFF};
}

static syn_rgb mix(syn_rgb a, syn_rgb b, int pct_b) {
	return (syn_rgb){(short)((a.r * (100 - pct_b) + b.r * pct_b) / 100),
		(short)((a.g * (100 - pct_b) + b.g * pct_b) / 100), (short)((a.b * (100 - pct_b) + b.b * pct_b) / 100)};
}

static int luma(syn_rgb c) {
	return (c.r * 299 + c.g * 587 + c.b * 114) / 1000;
}

static syn_rgb ink_on(syn_rgb c) {
	int l = luma(c);
	int dt = abs(luma(ui.pal.text) - l), db = abs(luma(ui.pal.bg) - l);
	return dt >= db ? ui.pal.text : ui.pal.bg;
}

static void fill(int x, int y, int w, int h, syn_rgb c) {
	if (w <= 0 || h <= 0) return;
	pixman_color_t pc = col(c);
	pixman_box32_t box = {x, y, x + w, y + h};
	pixman_image_fill_boxes(PIXMAN_OP_SRC, dst, &pc, 1, &box);
}

static const struct fcft_glyph *glyph(struct fcft_font *f, uint32_t cp) {
	if (cp == '\t' || cp == '\n' || cp == '\r') cp = ' ';
	return fcft_rasterize_char_utf32(f, cp, FCFT_SUBPIXEL_NONE);
}

static int text_width_n(struct fcft_font *f, const char *s, size_t n) {
	int w = 0;
	for (size_t i = 0; i < n;) {
		uint32_t cp;
		i += utf8_next(s + i, n - i, &cp);
		const struct fcft_glyph *g = glyph(f, cp);
		if (g) w += g->advance.x;
	}
	return w;
}

static int text_width(struct fcft_font *f, const char *s) {
	return text_width_n(f, s, strlen(s));
}

static void logged(const char *s, size_t n) {
	if (ui.drawlog && n) {
		fwrite(s, 1, n, ui.drawlog);
		fputc('\n', ui.drawlog);
	}
}

static int draw_text_n(struct fcft_font *f, int x, int y, const char *s, size_t n, syn_rgb c, int max_w) {
	pixman_color_t pc = col(c);
	pixman_image_t *solid = pixman_image_create_solid_fill(&pc);
	int pen = x;
	size_t i = 0;
	while (i < n) {
		uint32_t cp;
		size_t k = utf8_next(s + i, n - i, &cp);
		const struct fcft_glyph *g = glyph(f, cp);
		if (g) {
			if (max_w > 0 && pen + g->advance.x - x > max_w) break;
			int gx = pen + g->x, gy = y + f->ascent - g->y;
			if (g->is_color_glyph) {
				pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, dst, 0, 0, 0, 0, gx, gy, g->width, g->height);
			} else {
				pixman_image_composite32(PIXMAN_OP_OVER, solid, g->pix, dst, 0, 0, 0, 0, gx, gy, g->width, g->height);
			}
			pen += g->advance.x;
		}
		i += k;
	}
	logged(s, i);
	pixman_image_unref(solid);
	return pen - x;
}

static int draw_text(struct fcft_font *f, int x, int y, const char *s, syn_rgb c) {
	return draw_text_n(f, x, y, s, strlen(s), c, 0);
}

/* fits s into max_w, ending in an ellipsis when cut */
static int draw_text_fit(struct fcft_font *f, int x, int y, const char *s, syn_rgb c, int max_w) {
	if (max_w <= 0) return 0;
	size_t n = strlen(s);
	if (text_width_n(f, s, n) <= max_w) return draw_text_n(f, x, y, s, n, c, 0);
	int ell = text_width(f, "…");
	int w = 0;
	size_t i = 0;
	while (i < n) {
		uint32_t cp;
		size_t k = utf8_next(s + i, n - i, &cp);
		const struct fcft_glyph *g = glyph(f, cp);
		int a = g ? g->advance.x : 0;
		if (w + a + ell > max_w) break;
		w += a;
		i += k;
	}
	draw_text_n(f, x, y, s, i, c, 0);
	pixman_color_t pc = col(c);
	(void)pc;
	draw_text_n(f, x + w, y, "…", strlen("…"), c, 0);
	return w + ell;
}

typedef struct {
	const char *p;
	size_t n;
} line;

/* word-wraps s into lines no wider than width; returns the count (out may be NULL to count only) */
static int wrap(struct fcft_font *f, const char *s, int width, line *out, int max) {
	int count = 0;
	size_t len = strlen(s), start = 0;
	while (start < len && count < max) {
		int w = 0;
		size_t i = start, brk = 0;
		bool hard = false;
		while (i < len) {
			uint32_t cp;
			size_t k = utf8_next(s + i, len - i, &cp);
			if (cp == '\n') {
				hard = true;
				break;
			}
			const struct fcft_glyph *g = glyph(f, cp);
			int a = g ? g->advance.x : 0;
			if (w + a > width && i > start) break;
			if (cp == ' ') brk = i;
			w += a;
			i += k;
		}
		size_t end = i;
		if (!hard && i < len && brk > start) end = brk;
		if (out) {
			out[count].p = s + start;
			out[count].n = end - start;
		}
		count++;
		start = end;
		if (start < len && (s[start] == ' ' || s[start] == '\n')) start++;
	}
	return count;
}

/* ---------- the model ---------- */

static void rebuild_side(void) {
	ui.nside = 0;
	for (int a = 0; a < ui.cfg.n && ui.nside < (int)(sizeof ui.side / sizeof ui.side[0]); a++) {
		ui.side[ui.nside++] = (SideRow){a, -1};
		for (int f = 0; f < ui.acc[a].nf && ui.nside < (int)(sizeof ui.side / sizeof ui.side[0]); f++) {
			ui.side[ui.nside++] = (SideRow){a, f};
		}
	}
	if (ui.side_sel >= ui.nside) ui.side_sel = ui.nside - 1;
	if (ui.side_sel < 0) ui.side_sel = 0;
}

static void ask_page(long skip) {
	Job j = {.kind = JOB_PAGE, .acc = ui.open_acc, .gen = skip ? ui.gen_page : ++ui.gen_page, .skip = skip, .count = PAGE};
	str_copy(j.folder, sizeof j.folder, ui.open_raw);
	workers_push(&j);
	ui.page_loading = true;
	ui.dirty = true;
}

static void html_drop_image(void) {
	if (ui.html_img) pixman_image_unref(ui.html_img);
	free(ui.html_px);
	ui.html_img = NULL;
	ui.html_px = NULL;
	ui.html_w = ui.html_h = ui.html_drawn_w = 0;
	ui.html_more = false;
}

static void clear_reader(void) {
	html_drop_image();
	mime_html_free(&ui.mh);
	ui.has_html = ui.view_text = ui.allow_remote = ui.html_pending = false;
	ui.remote_refs = 0;
	ui.html_err[0] = 0;
	ui.html_gen++; /* a drawing still under way is for a message no longer read */
	buf_free(&ui.raw);
	text_set(&ui.body, "");
	ui.rd_from[0] = ui.rd_to[0] = ui.rd_date[0] = ui.rd_subject[0] = 0;
	ui.rd_uid = 0;
	ui.read_top = 0;
	ui.body_loading = false;
}

static void open_folder(int acc, const char *raw, const char *name) {
	ui.open_acc = acc;
	str_copy(ui.open_raw, sizeof ui.open_raw, raw);
	str_copy(ui.open_name, sizeof ui.open_name, name);
	page_free(&ui.page);
	ui.page.exists = ui.page.unseen = -1;
	ui.sel = -1;
	ui.list_top = 0;
	ui.page_failed = false;
	clear_reader();
	ask_page(0);
	ui.last_refresh = now_ms();
	set_status(false, "%s: opening %s…", ui.cfg.acc[acc].name, name);
}

static void ask_body(void) {
	if (ui.sel < 0 || ui.sel >= ui.page.n) return;
	Header *h = &ui.page.h[ui.sel];
	if (h->uid == ui.rd_uid && (ui.raw.n || ui.body_loading)) return;
	clear_reader();
	ui.rd_uid = h->uid;
	str_copy(ui.rd_subject, sizeof ui.rd_subject, h->subject);
	snprintf(ui.rd_from, sizeof ui.rd_from, "%s <%s>", h->from, h->from_addr);
	Job j = {.kind = JOB_BODY, .acc = ui.open_acc, .gen = ++ui.gen_body, .uid = h->uid};
	str_copy(j.folder, sizeof j.folder, ui.open_raw);
	workers_push(&j);
	ui.body_loading = true;
	ui.dirty = true;
}

static void take_body(Result *r) {
	buf_free(&ui.raw);
	ui.raw = r->body;
	memset(&r->body, 0, sizeof r->body);
	Buf v = {0};
	static const struct {
		const char *h;
		char *out;
		size_t n;
	} hs[] = {{"From", ui.rd_from, sizeof ui.rd_from}, {"To", ui.rd_to, sizeof ui.rd_to},
		{"Date", ui.rd_date, sizeof ui.rd_date}, {"Subject", ui.rd_subject, sizeof ui.rd_subject}};
	for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) {
		v.n = 0;
		if (mime_header(ui.raw.p, ui.raw.n, hs[i].h, &v) == 0 && v.p) {
			box_one_line(&v, hs[i].n - 1);
			str_copy(hs[i].out, hs[i].n, v.p);
		}
		buf_free(&v);
	}
	Buf t = {0};
	mime_text(ui.raw.p, ui.raw.n, &t);
	/* tabs to spaces, CRs out: the wrap and the glyphs want plain lines */
	Text o = {0};
	for (size_t i = 0; t.p && i < t.n; i++) {
		if (t.p[i] == '\r') continue;
		if (t.p[i] == '\t') text_add(&o, "    ", 4);
		else text_add(&o, t.p + i, 1);
	}
	buf_free(&t);
	free(ui.body.p);
	ui.body = o;
	if (!ui.body.p) text_set(&ui.body, "");
	ui.body_loading = false;
	ui.read_top = 0;
	ui.has_html = mime_html(ui.raw.p, ui.raw.n, &ui.mh) == 0;
	ui.allow_remote = ui.remote_always;
	if (ui.has_html) {
		Buf probe = {0};
		html_prepare(&ui.mh, 0, &probe, &ui.remote_refs);
		buf_free(&probe);
		start_render();
	} else {
		mime_html_free(&ui.mh);
	}
}


/* ---------- the HTML view (G265) ---------- */

typedef struct {
	long gen;
	int width, remote;
	char dir[4096], page[4200];
} RenderJob;

typedef struct {
	long gen;
	int width, w, h, more;
	uint32_t *px;
	char err[300];
} RenderOut;

static pthread_mutex_t render_mu = PTHREAD_MUTEX_INITIALIZER;
static RenderOut *render_done;

/* the render thread: Chrome draws the page, the PNG is read, the folder goes; the window is woken over the pipe */
static void *render_main(void *arg) {
	RenderJob *j = arg;
	RenderOut *o = calloc(1, sizeof *o);
	o->gen = j->gen;
	o->width = j->width;
	char png[4300];
	snprintf(png, sizeof png, "%s/render.png", j->dir);
	if (html_render(j->page, j->width, j->remote, png, o->err, sizeof o->err) == 0) {
		o->px = html_png_load(png, &o->w, &o->h, &o->more);
		if (!o->px) snprintf(o->err, sizeof o->err, "the drawing of the HTML could not be read");
	}
	html_rmdir(j->dir);
	free(j);
	pthread_mutex_lock(&render_mu);
	if (render_done) {
		free(render_done->px);
		free(render_done);
	}
	render_done = o;
	pthread_mutex_unlock(&render_mu);
	if (write(ui.html_fd[1], "r", 1) < 0) {
		/* the window still sees it on its next look */
	}
	return NULL;
}

static int html_width(void) {
	int w = ui.r_read.w - 16;
	return w < 200 ? 200 : w;
}

static void start_render(void) {
	if (!ui.has_html || ui.view_text) return;
	if (!html_renderer()) {
		snprintf(ui.html_err, sizeof ui.html_err, "no headless Chrome here to draw the HTML");
		return;
	}
	if (ui.html_busy) {
		ui.html_pending = true;
		return;
	}
	if (ui.html_fd[0] < 0 && pipe2(ui.html_fd, O_CLOEXEC | O_NONBLOCK) != 0) {
		snprintf(ui.html_err, sizeof ui.html_err, "pipe: %s", strerror(errno));
		return;
	}
	layout();
	RenderJob *j = calloc(1, sizeof *j);
	j->gen = ui.html_gen;
	j->width = html_width();
	j->remote = ui.allow_remote;
	char err[300];
	ui.html_drawn_w = j->width; /* a failure here is not tried again on every frame */
	if (html_dir("render", j->dir, sizeof j->dir, err, sizeof err) != 0 ||
		html_write(&ui.mh, j->remote, j->dir, j->page, sizeof j->page, &ui.remote_refs, err, sizeof err) != 0) {
		if (j->dir[0]) html_rmdir(j->dir);
		snprintf(ui.html_err, sizeof ui.html_err, "%s", err);
		free(j);
		return;
	}
	pthread_t th;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th, &at, render_main, j) != 0) {
		html_rmdir(j->dir);
		free(j);
		snprintf(ui.html_err, sizeof ui.html_err, "could not start the drawing");
	} else {
		ui.html_busy = true;
		ui.html_err[0] = 0;
	}
	pthread_attr_destroy(&at);
}

/* a drawing has finished: keep it if it is for the message on show, then start any drawing that waited */
static void on_render(void) {
	char b[64];
	while (ui.html_fd[0] >= 0 && read(ui.html_fd[0], b, sizeof b) > 0) {
	}
	pthread_mutex_lock(&render_mu);
	RenderOut *o = render_done;
	render_done = NULL;
	pthread_mutex_unlock(&render_mu);
	if (!o) return;
	ui.html_busy = false;
	if (o->gen == ui.html_gen) {
		if (o->px) {
			html_drop_image();
			ui.html_px = o->px;
			ui.html_w = o->w;
			ui.html_h = o->h;
			ui.html_more = o->more;
			ui.html_drawn_w = o->width;
			ui.html_img = pixman_image_create_bits(PIXMAN_x8r8g8b8, o->w, o->h, o->px, o->w * 4);
			o->px = NULL;
			ui.html_err[0] = 0;
		} else {
			snprintf(ui.html_err, sizeof ui.html_err, "%s", o->err);
			ui.html_drawn_w = o->width; /* tried at this width: no retry on every frame */
		}
	}
	free(o->px);
	free(o);
	if (ui.html_pending) {
		ui.html_pending = false;
		start_render();
	}
	ui.dirty = true;
}

/* o: the message in a new Chrome tab, its inline images with it (remote ones as the reader has them) */
static void open_in_chrome(void) {
	if (!ui.rd_uid || ui.body_loading) return;
	MimeHtml plain = {0};
	const MimeHtml *h = &ui.mh;
	if (!ui.has_html) {
		/* a text message goes as its text */
		buf_puts(&plain.html, "<!doctype html><html><body><pre style=\"white-space:pre-wrap;font:15px monospace\">");
		for (const char *c = text_str(&ui.body); *c; c++) {
			if (*c == '<') buf_puts(&plain.html, "&lt;");
			else if (*c == '>') buf_puts(&plain.html, "&gt;");
			else if (*c == '&') buf_puts(&plain.html, "&amp;");
			else buf_put(&plain.html, c, 1);
		}
		buf_puts(&plain.html, "</pre></body></html>");
		h = &plain;
	}
	char dir[4096], page[4200], err[300];
	html_sweep();
	if (html_dir("open", dir, sizeof dir, err, sizeof err) != 0 ||
		html_write(h, ui.allow_remote, dir, page, sizeof page, NULL, err, sizeof err) != 0 || html_open(page, err, sizeof err) != 0)
		set_status(true, "Not opened: %s", err);
	else
		set_status(false, "Opened in Chrome%s", ui.has_html && ui.remote_refs && !ui.allow_remote ? ", remote images held back (i loads them)" : "");
	buf_free(&plain.html);
}

static void handle(Result *r) {
	AccView *av = &ui.acc[r->acc];
	const char *an = ui.cfg.acc[r->acc].name;
	switch (r->kind) {
	case JOB_FOLDERS:
		if (r->ok) {
			free(av->f);
			av->f = r->f;
			av->nf = r->nf;
			r->f = NULL;
			av->loaded = true;
			av->failed = false;
			rebuild_side();
		} else {
			av->failed = true;
			set_status(true, "%s: %s", an, r->err);
		}
		break;
	case JOB_PAGE:
		if (r->ok && !strcasecmp(r->folder, "INBOX")) av->inbox_unseen = r->page.unseen;
		if (r->gen < 0) break; /* a count for the header only */
		if (r->acc != ui.open_acc || strcmp(r->folder, ui.open_raw) != 0 || r->gen != ui.gen_page) break;
		ui.page_loading = false;
		if (!r->ok) {
			ui.page_failed = true;
			set_status(true, "%s: %s", an, r->err);
			break;
		}
		if (r->skip == 0) {
			/* a fresh first page: keep the selection on the same message if it is still there */
			unsigned long keep = ui.sel >= 0 && ui.sel < ui.page.n ? ui.page.h[ui.sel].uid : 0;
			page_free(&ui.page);
			ui.page = r->page;
			memset(&r->page, 0, sizeof r->page);
			ui.sel = -1;
			for (int i = 0; keep && i < ui.page.n; i++) {
				if (ui.page.h[i].uid == keep) ui.sel = i;
			}
			if (ui.sel < 0 && ui.page.n) {
				ui.sel = ui.open_n > 0 && ui.open_n <= ui.page.n ? ui.open_n - 1 : 0;
				ui.list_top = 0;
				ask_body();
			}
		} else if (r->skip == ui.page.n) {
			ui.page.h = realloc(ui.page.h, sizeof *ui.page.h * (size_t)(ui.page.n + r->page.n));
			memcpy(ui.page.h + ui.page.n, r->page.h, sizeof *ui.page.h * (size_t)r->page.n);
			ui.page.n += r->page.n;
			ui.page.exists = r->page.exists;
			ui.page.unseen = r->page.unseen;
		}
		set_status(false, "%s · %s: %ld messages, %ld unread", an, ui.open_name, ui.page.exists, ui.page.unseen);
		break;
	case JOB_BODY:
		if (r->gen != ui.gen_body) break;
		if (!r->ok) {
			ui.body_loading = false;
			set_status(true, "%s: %s", an, r->err);
			break;
		}
		take_body(r);
		break;
	case JOB_SEND:
		ui.sending = false;
		if (r->ok) {
			ui.composing = false;
			set_status(false, "Sent from %s to %s", an, r->to);
		} else {
			set_status(true, "Not sent: %s", r->err);
		}
		break;
	}
	ui.dirty = true;
}

static void drain(void) {
	Result *r = workers_take();
	for (Result *k = r; k; k = k->next) handle(k);
	result_free(r);
}

/* every other account's inbox is counted (no headers) so the header bar shows its unread */
static void count_inboxes(void) {
	for (int a = 0; a < ui.cfg.n; a++) {
		if (a == ui.open_acc && !strcasecmp(ui.open_raw, "INBOX")) continue;
		Job j = {.kind = JOB_PAGE, .acc = a, .gen = -1, .skip = -1, .count = 0};
		str_copy(j.folder, sizeof j.folder, "INBOX");
		workers_push(&j);
	}
}

static void refresh(void) {
	for (int a = 0; a < ui.cfg.n; a++) {
		workers_push(&(Job){.kind = JOB_FOLDERS, .acc = a});
	}
	if (ui.open_acc >= 0) ask_page(0);
	count_inboxes();
	ui.last_refresh = now_ms();
}

/* ---------- compose ---------- */

static void compose(bool reply) {
	for (int i = 0; i < F_COUNT; i++) text_set(&ui.c[i], "");
	ui.c_reply_id[0] = 0;
	ui.c_from = ui.open_acc >= 0 ? ui.open_acc : 0;
	ui.c_field = F_TO;
	if (reply && ui.sel >= 0 && ui.sel < ui.page.n) {
		Header *h = &ui.page.h[ui.sel];
		text_set(&ui.c[F_TO], h->from_addr);
		Text s = {0};
		if (strncasecmp(h->subject, "Re:", 3) != 0) text_add(&s, "Re: ", 4);
		text_add(&s, h->subject, strlen(h->subject));
		text_set(&ui.c[F_SUBJECT], text_str(&s));
		free(s.p);
		str_copy(ui.c_reply_id, sizeof ui.c_reply_id, h->msgid);
		/* the message quoted under a blank line to write in */
		Text b = {0};
		char head[600];
		snprintf(head, sizeof head, "\n\nOn %s, %s wrote:\n", ui.rd_date[0] ? ui.rd_date : h->date, h->from);
		text_add(&b, head, strlen(head));
		const char *p = text_str(&ui.body);
		for (int ln = 0; *p && ln < 400; ln++) {
			const char *e = strchr(p, '\n');
			size_t n = e ? (size_t)(e - p) : strlen(p);
			text_add(&b, "> ", 2);
			text_add(&b, p, n);
			text_add(&b, "\n", 1);
			p += n + (e ? 1 : 0);
		}
		text_set(&ui.c[F_BODY], text_str(&b));
		free(b.p);
		ui.c_field = F_BODY;
	}
	ui.composing = true;
	ui.sending = false;
	set_status(false, reply ? "Reply: Ctrl+Enter sends, Esc closes" : "New message: Ctrl+Enter sends, Esc closes");
}

static void send_now(void) {
	if (ui.sending) return;
	const char *to = text_str(&ui.c[F_TO]);
	while (*to == ' ') to++;
	char addr[256];
	str_copy(addr, sizeof addr, to);
	for (size_t n = strlen(addr); n && addr[n - 1] == ' '; n--) addr[n - 1] = 0;
	if (!msg_addr_ok(addr)) {
		set_status(true, "Not a plain address: %s", addr[0] ? addr : "(empty)");
		ui.c_field = F_TO;
		return;
	}
	Job j = {.kind = JOB_SEND, .acc = ui.c_from};
	j.to = strdup(addr);
	j.subject = strdup(text_str(&ui.c[F_SUBJECT]));
	j.body = strdup(text_str(&ui.c[F_BODY]));
	j.reply_to = ui.c_reply_id[0] ? strdup(ui.c_reply_id) : NULL;
	workers_push(&j);
	ui.sending = true;
	set_status(false, "Sending from %s to %s…", ui.cfg.acc[ui.c_from].name, addr);
}

/* ---------- layout and drawing ---------- */

static void layout(void) {
	int head = ui.lh * 2 + 4, foot = ui.lh + 10;
	ui.r_head = (rect){0, 0, ui.w, head};
	ui.r_foot = (rect){0, ui.h - foot, ui.w, foot};
	int body_h = ui.h - head - foot;
	int side_w = ui.cw * 26;
	if (side_w > ui.w / 4) side_w = ui.w / 4;
	int rest = ui.w - side_w;
	int list_w = rest * 45 / 100;
	ui.r_side = (rect){0, head, side_w, body_h};
	ui.r_list = (rect){side_w, head, list_w, body_h};
	ui.r_read = (rect){side_w + list_w, head, ui.w - side_w - list_w, body_h};
}

static int list_rows(void) {
	int rh = ui.lh * 2 + 6;
	int n = (ui.r_list.h - ui.lh - 8) / rh;
	return n < 1 ? 1 : n;
}

static void draw_header(void) {
	rect r = ui.r_head;
	fill(r.x, r.y, r.w, r.h, ui.pal.panel);
	fill(r.x, r.y + r.h - 2, r.w, 2, ui.pal.accent);
	int y = r.y + (r.h - 2 - ui.font_big->height) / 2;
	int x = 14 + draw_text(ui.font_big, 14, y, "SYN-MAIL", ui.pal.accent);
	x += ui.cw * 2;
	/* every account's unread count */
	for (int a = 0; a < ui.cfg.n; a++) {
		char s[200];
		long u = ui.acc[a].inbox_unseen;
		if (u >= 0) snprintf(s, sizeof s, "%s %ld unread", ui.cfg.acc[a].name, u);
		else snprintf(s, sizeof s, "%s", ui.cfg.acc[a].name);
		x += draw_text(ui.font, x, r.y + (r.h - 2 - ui.lh) / 2, s, ui.acc[a].failed ? ui.pal.urgent : ui.pal.text);
		x += ui.cw * 3;
	}
	char clock[32];
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(clock, sizeof clock, "%a %d %b %H:%M", &tm);
	int cwid = text_width(ui.font, clock);
	draw_text(ui.font, r.w - cwid - 14, r.y + (r.h - 2 - ui.lh) / 2, clock, mix(ui.pal.text, ui.pal.panel, 35));
}

static void pane_title(rect r, const char *title, bool focus) {
	fill(r.x, r.y, r.w, ui.lh + 8, focus ? ui.pal.accent_dim : mix(ui.pal.panel, ui.pal.bg, 40));
	draw_text_fit(ui.font, r.x + 10, r.y + 4, title, focus ? ink_on(ui.pal.accent_dim) : ui.pal.text, r.w - 20);
}

static void draw_side(void) {
	rect r = ui.r_side;
	fill(r.x, r.y, r.w, r.h, ui.pal.panel);
	fill(r.x + r.w - 1, r.y, 1, r.h, ui.pal.border);
	pane_title(r, "Mailboxes", ui.pane == PANE_SIDE && !ui.composing);
	int y = r.y + ui.lh + 14, rh = ui.lh + 6;
	int first = 0, fits = (r.h - ui.lh - 14) / rh;
	if (ui.side_sel >= fits) first = ui.side_sel - fits + 1;
	for (int i = first; i < ui.nside && y + rh <= r.y + r.h; i++, y += rh) {
		SideRow *s = &ui.side[i];
		bool open = s->folder >= 0 && s->acc == ui.open_acc && !strcmp(ui.acc[s->acc].f[s->folder].raw, ui.open_raw);
		if (i == ui.side_sel) fill(r.x, y, r.w - 1, rh, ui.pane == PANE_SIDE ? ui.pal.panel_hover : mix(ui.pal.panel, ui.pal.panel_hover, 50));
		if (open) fill(r.x, y, 4, rh, ui.pal.accent);
		if (s->folder < 0) {
			const Account *a = &ui.cfg.acc[s->acc];
			char t[300];
			snprintf(t, sizeof t, "%s  %s", a->name, a->address);
			draw_text_fit(ui.font, r.x + 10, y + 3, t, ui.pal.accent, r.w - 20);
			if (!ui.acc[s->acc].loaded) {
				y += rh;
				draw_text_fit(ui.font, r.x + 24, y + 3, ui.acc[s->acc].failed ? "cannot connect" : "connecting…",
					ui.acc[s->acc].failed ? ui.pal.urgent : mix(ui.pal.text, ui.pal.panel, 45), r.w - 34);
			}
		} else {
			Folder *f = &ui.acc[s->acc].f[s->folder];
			syn_rgb c = f->noselect ? mix(ui.pal.text, ui.pal.panel, 50) : ui.pal.text;
			draw_text_fit(ui.font, r.x + 24, y + 3, f->name, c, r.w - 34);
		}
	}
}

static void draw_list(void) {
	rect r = ui.r_list;
	fill(r.x, r.y, r.w, r.h, ui.pal.bg);
	fill(r.x + r.w - 1, r.y, 1, r.h, ui.pal.border);
	char title[400];
	if (ui.open_acc < 0) snprintf(title, sizeof title, "Messages");
	else if (ui.page.exists >= 0) snprintf(title, sizeof title, "%s · %s · %ld", ui.cfg.acc[ui.open_acc].name, ui.open_name, ui.page.exists);
	else snprintf(title, sizeof title, "%s · %s", ui.cfg.acc[ui.open_acc].name, ui.open_name);
	pane_title(r, title, ui.pane == PANE_LIST && !ui.composing);
	int rh = ui.lh * 2 + 6, rows = list_rows();
	if (ui.sel >= 0) {
		if (ui.sel < ui.list_top) ui.list_top = ui.sel;
		if (ui.sel >= ui.list_top + rows) ui.list_top = ui.sel - rows + 1;
	}
	int y = r.y + ui.lh + 8;
	syn_rgb dim = mix(ui.pal.text, ui.pal.bg, 40);
	for (int i = ui.list_top; i < ui.page.n && i < ui.list_top + rows; i++, y += rh) {
		Header *h = &ui.page.h[i];
		bool sel = i == ui.sel;
		syn_rgb bg = sel ? (ui.pane == PANE_LIST ? ui.pal.panel_hover : mix(ui.pal.bg, ui.pal.panel_hover, 60)) : ui.pal.bg;
		fill(r.x, y, r.w - 1, rh, bg);
		fill(r.x + 8, y + rh - 1, r.w - 16, 1, mix(ui.pal.border, ui.pal.bg, 50));
		if (!h->seen) fill(r.x + 4, y + 4, 4, rh - 8, ui.pal.accent);
		int dw = text_width(ui.font, h->date);
		draw_text(ui.font, r.x + r.w - dw - 12, y + 3, h->date, dim);
		draw_text_fit(ui.font, r.x + 16, y + 3, h->from[0] ? h->from : h->from_addr, h->seen ? ui.pal.text : ui.pal.accent,
			r.w - dw - 40);
		draw_text_fit(ui.font, r.x + 16, y + 3 + ui.lh, h->subject[0] ? h->subject : "(no subject)", sel ? ink_on(bg) : dim,
			r.w - 30);
	}
	const char *note = NULL;
	if (ui.page_loading) note = "loading…";
	else if (ui.page_failed) note = "could not load this folder";
	else if (ui.open_acc >= 0 && ui.page.exists == 0) note = "nothing here";
	else if (ui.page.n && ui.page.n < ui.page.exists && ui.list_top + rows >= ui.page.n) note = "End: load older";
	if (note) draw_text(ui.font, r.x + 16, y + 6, note, ui.page_failed ? ui.pal.urgent : dim);
}

static void draw_reader(void) {
	rect r = ui.r_read;
	fill(r.x, r.y, r.w, r.h, ui.pal.bg);
	pane_title(r, "Reading", ui.pane == PANE_READ && !ui.composing);
	if (!ui.rd_uid) {
		draw_text(ui.font, r.x + 16, r.y + ui.lh * 2, ui.open_acc < 0 ? "Pick a folder on the left." : "", ui.pal.text);
		return;
	}
	int x = r.x + 16, w = r.w - 32, y = r.y + ui.lh + 16;
	line sl[4];
	int ns = wrap(ui.font_big, ui.rd_subject[0] ? ui.rd_subject : "(no subject)", w, sl, 4);
	for (int i = 0; i < ns; i++, y += ui.font_big->height) draw_text_n(ui.font_big, x, y, sl[i].p, sl[i].n, ui.pal.accent, w);
	y += 4;
	syn_rgb dim = mix(ui.pal.text, ui.pal.bg, 35);
	const char *lab[] = {"From", "To", "Date"};
	const char *val[] = {ui.rd_from, ui.rd_to, ui.rd_date};
	int lw = text_width(ui.font, "From  ");
	for (int i = 0; i < 3; i++) {
		if (!val[i][0]) continue;
		draw_text(ui.font, x, y, lab[i], dim);
		draw_text_fit(ui.font, x + lw, y, val[i], ui.pal.text, w - lw);
		y += ui.lh;
	}
	y += 6;
	fill(x, y, w, 1, ui.pal.border);
	y += 8;
	if (ui.body_loading) {
		draw_text(ui.font, x, y, "loading…", dim);
		return;
	}
	if (ui.has_html) {
		/* one line on what is shown and the keys that change it */
		char bar[400];
		bool pic = ui.html_img && !ui.view_text;
		if (ui.view_text) snprintf(bar, sizeof bar, "Text view   t shows the HTML   o opens it in Chrome");
		else if (ui.html_err[0] && !pic) snprintf(bar, sizeof bar, "HTML not drawn: %s   o opens it in Chrome", ui.html_err);
		else if (!pic) snprintf(bar, sizeof bar, "Drawing the HTML…   t text   o opens it in Chrome");
		else if (ui.remote_refs && !ui.allow_remote)
			snprintf(bar, sizeof bar, "HTML   %d remote image%s held back: i loads them   o Chrome   t text%s", ui.remote_refs,
				ui.remote_refs == 1 ? "" : "s", ui.html_more ? "   longer than shown: o for the rest" : "");
		else snprintf(bar, sizeof bar, "HTML%s   o opens it in Chrome   t text%s", ui.allow_remote ? ", remote images loaded" : "",
			ui.html_more ? "   longer than shown: o for the rest" : "");
		fill(r.x, y - 4, r.w, ui.lh + 6, ui.pal.panel);
		draw_text_fit(ui.font, x, y - 1, bar, ui.html_err[0] && !pic ? ui.pal.urgent : dim, w);
		y += ui.lh + 8;
		if (pic) {
			/* the page as Chrome drew it, scrolled a line height at a time */
			if (!ui.html_busy && abs(ui.html_drawn_w - html_width()) > 8) {
				ui.html_gen++;
				start_render(); /* the pane changed width */
			}
			int px_x = r.x + 8, vw = r.w - 16, vh = r.y + r.h - y;
			if (vh < 1) return;
			int max_off = ui.html_h > vh ? ui.html_h - vh : 0;
			int off = ui.read_top * ui.lh;
			if (off > max_off) {
				off = max_off;
				ui.read_top = (max_off + ui.lh - 1) / ui.lh;
			}
			if (off < 0) off = ui.read_top = 0;
			int cw = ui.html_w < vw ? ui.html_w : vw, chh = ui.html_h - off < vh ? ui.html_h - off : vh;
			fill(px_x, y, vw, vh, (syn_rgb){255, 255, 255});
			pixman_image_composite32(PIXMAN_OP_SRC, ui.html_img, NULL, dst, 0, off, 0, 0, px_x, y, cw, chh);
			char note[64];
			int nl = snprintf(note, sizeof note, "[html %dx%d]", ui.html_w, ui.html_h);
			logged(note, (size_t)nl);
			if (ui.html_h > vh) {
				int bh = vh, th = bh * vh / ui.html_h, ty = y + bh * off / ui.html_h;
				fill(r.x + r.w - 5, ty, 3, th < 12 ? 12 : th, ui.pal.accent_dim);
			}
			return;
		}
	}
	int total = wrap(ui.font, text_str(&ui.body), w, NULL, 100000);
	int fits = (r.y + r.h - y - 4) / ui.lh;
	if (fits < 1) fits = 1;
	if (ui.read_top > total - fits) ui.read_top = total - fits;
	if (ui.read_top < 0) ui.read_top = 0;
	int want = ui.read_top + fits;
	line *ls = malloc(sizeof *ls * (size_t)(want > 0 ? want : 1));
	int got = wrap(ui.font, text_str(&ui.body), w, ls, want);
	for (int i = ui.read_top; i < got; i++, y += ui.lh) {
		bool quote = ls[i].n && ls[i].p[0] == '>';
		draw_text_n(ui.font, x, y, ls[i].p, ls[i].n, quote ? dim : ui.pal.text, w);
	}
	free(ls);
	if (total > fits) {
		/* a scroll bar */
		int bh = r.h - ui.lh - 8, by = r.y + ui.lh + 8;
		int th = bh * fits / total, ty = by + bh * ui.read_top / total;
		fill(r.x + r.w - 5, ty, 3, th < 12 ? 12 : th, ui.pal.accent_dim);
	}
}

static void draw_compose(void) {
	rect r = {ui.r_list.x, ui.r_list.y, ui.r_list.w + ui.r_read.w, ui.r_list.h};
	fill(r.x, r.y, r.w, r.h, ui.pal.bg);
	pane_title(r, ui.c_reply_id[0] ? "Reply" : "New message", true);
	int x = r.x + 16, w = r.w - 32, y = r.y + ui.lh + 18;
	int lw = text_width(ui.font, "Subject  ");
	syn_rgb dim = mix(ui.pal.text, ui.pal.bg, 35);
	const char *lab[] = {"From", "To", "Subject"};
	for (int f = 0; f < 3; f++) {
		bool on = ui.c_field == f;
		fill(x - 6, y - 3, w + 12, ui.lh + 6, on ? ui.pal.panel_hover : ui.pal.panel);
		draw_text(ui.font, x, y, lab[f], on ? ui.pal.accent : dim);
		char v[600];
		if (f == F_FROM) {
			const Account *a = &ui.cfg.acc[ui.c_from];
			snprintf(v, sizeof v, "%s <%s>%s", a->display[0] ? a->display : a->name, a->address, ui.cfg.n > 1 ? "   (←/→ to change)" : "");
		} else {
			snprintf(v, sizeof v, "%s%s", text_str(&ui.c[f]), on ? "▏" : "");
		}
		draw_text_fit(ui.font, x + lw, y, v, ui.pal.text, w - lw);
		y += ui.lh + 10;
	}
	bool on = ui.c_field == F_BODY;
	rect b = {x - 6, y, w + 12, r.y + r.h - y - 10};
	fill(b.x, b.y, b.w, b.h, on ? mix(ui.pal.bg, ui.pal.panel_hover, 40) : ui.pal.panel);
	Text t = {0};
	text_set(&t, text_str(&ui.c[F_BODY]));
	if (on) text_add(&t, "▏", strlen("▏"));
	int fits = (b.h - 12) / ui.lh;
	if (fits < 1) fits = 1;
	int total = wrap(ui.font, t.p, w - 12, NULL, 100000);
	int top = total > fits ? total - fits : 0; /* keep the end, where the writing is, in view */
	line *ls = malloc(sizeof *ls * (size_t)(total ? total : 1));
	int got = wrap(ui.font, t.p, w - 12, ls, total ? total : 1);
	int ly = b.y + 6;
	for (int i = top; i < got; i++, ly += ui.lh) {
		bool quote = ls[i].n && ls[i].p[0] == '>';
		draw_text_n(ui.font, x, ly, ls[i].p, ls[i].n, quote ? dim : ui.pal.text, w - 12);
	}
	free(ls);
	free(t.p);
}

static void draw_footer(void) {
	rect r = ui.r_foot;
	fill(r.x, r.y, r.w, r.h, ui.pal.panel);
	fill(r.x, r.y, r.w, 1, ui.pal.border);
	const char *keys = ui.composing ? "Tab next field   Ctrl+Enter send   Ctrl+V paste   Esc close"
	                 : ui.has_html ? "↑↓ move   ←→ pane   Enter open   c new   r reply   o Chrome   i images   t text   F5   q quit"
	                               : "↑↓ move   ←→ pane   Enter open   c new   r reply   o Chrome   F5 refresh   q quit";
	int kw = text_width(ui.font, keys);
	draw_text(ui.font, r.x + r.w - kw - 14, r.y + 5, keys, mix(ui.pal.text, ui.pal.panel, 40));
	draw_text_fit(ui.font, r.x + 14, r.y + 5, ui.status, ui.status_bad ? ui.pal.urgent : ui.pal.text, r.w - kw - 50);
}

static void draw_all(void) {
	layout();
	fill(0, 0, ui.w, ui.h, ui.pal.bg);
	draw_header();
	draw_side();
	if (ui.composing) {
		draw_compose();
	} else {
		draw_list();
		draw_reader();
	}
	draw_footer();
	if (ui.drawlog) fflush(ui.drawlog);
}

/* ---------- actions ---------- */

static void open_side_row(void) {
	if (ui.side_sel < 0 || ui.side_sel >= ui.nside) return;
	SideRow *s = &ui.side[ui.side_sel];
	if (s->folder < 0) {
		/* an account row opens its inbox */
		for (int f = 0; f < ui.acc[s->acc].nf; f++) {
			if (!strcasecmp(ui.acc[s->acc].f[f].raw, "INBOX")) {
				open_folder(s->acc, "INBOX", ui.acc[s->acc].f[f].name);
				return;
			}
		}
		open_folder(s->acc, "INBOX", "Inbox");
		return;
	}
	Folder *f = &ui.acc[s->acc].f[s->folder];
	if (f->noselect) {
		set_status(false, "%s holds folders, not messages", f->name);
		return;
	}
	open_folder(s->acc, f->raw, f->name);
	ui.pane = PANE_LIST;
}

static void move_list(int d) {
	if (!ui.page.n) return;
	int s = ui.sel < 0 ? 0 : ui.sel + d;
	if (s < 0) s = 0;
	if (s >= ui.page.n) {
		s = ui.page.n - 1;
		if (ui.page.n < ui.page.exists && !ui.page_loading) ask_page(ui.page.n);
	}
	if (s != ui.sel) {
		ui.sel = s;
		ask_body();
	}
	ui.dirty = true;
}

static bool ctrl_down(void) {
	return ui.xkb_state && xkb_state_mod_name_is_active(ui.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
}

static void paste(void);

/* returns true if the key may repeat */
static bool compose_key(xkb_keysym_t sym, uint32_t key) {
	Text *t = &ui.c[ui.c_field];
	if (sym == XKB_KEY_Escape) {
		ui.composing = false;
		set_status(false, "Closed without sending");
		return false;
	}
	if ((sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) && ctrl_down()) {
		send_now();
		return false;
	}
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
		int d = sym == XKB_KEY_ISO_Left_Tab ? F_COUNT - 1 : 1;
		ui.c_field = (ui.c_field + d) % F_COUNT;
		return false;
	}
	if (ctrl_down() && (sym == XKB_KEY_v || sym == XKB_KEY_V)) {
		paste();
		return false;
	}
	if (ui.c_field == F_FROM) {
		if (sym == XKB_KEY_Left || sym == XKB_KEY_Up) ui.c_from = (ui.c_from + ui.cfg.n - 1) % ui.cfg.n;
		else if (sym == XKB_KEY_Right || sym == XKB_KEY_Down) ui.c_from = (ui.c_from + 1) % ui.cfg.n;
		else if (sym == XKB_KEY_Return) ui.c_field = F_TO;
		return false;
	}
	if (sym == XKB_KEY_BackSpace) {
		text_backspace(t);
		return true;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (ui.c_field == F_BODY) text_add(t, "\n", 1);
		else ui.c_field++;
		return true;
	}
	if (ctrl_down()) return false;
	char s[16];
	int n = xkb_state_key_get_utf8(ui.xkb_state, key + 8, s, sizeof s);
	if (n > 0 && (unsigned char)s[0] >= 0x20 && s[0] != 0x7f) {
		text_add(t, s, (size_t)n);
		return true;
	}
	return false;
}

static bool key_action(uint32_t key, bool repeat) {
	(void)repeat;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(ui.xkb_state, key + 8);
	ui.dirty = true;
	if (ctrl_down() && (sym == XKB_KEY_q || sym == XKB_KEY_Q)) {
		ui.running = false;
		return false;
	}
	if (ui.composing) return compose_key(sym, key);
	switch (sym) {
	case XKB_KEY_q:
		ui.running = false;
		return false;
	case XKB_KEY_F5:
		refresh();
		set_status(false, "Refreshing…");
		return false;
	case XKB_KEY_c:
		compose(false);
		return false;
	case XKB_KEY_r:
		if (ui.rd_uid) compose(true);
		return false;
	case XKB_KEY_o:
		open_in_chrome();
		return false;
	case XKB_KEY_i:
		if (ui.has_html && !ui.allow_remote) {
			ui.allow_remote = true;
			ui.view_text = false;
			ui.html_gen++;
			start_render();
			set_status(false, "Loading the remote images for this message only");
		}
		return false;
	case XKB_KEY_t:
		if (ui.has_html) {
			ui.view_text = !ui.view_text;
			ui.read_top = 0;
			if (!ui.view_text && !ui.html_img) start_render();
		}
		return false;
	case XKB_KEY_Tab:
	case XKB_KEY_Right:
	case XKB_KEY_l:
		if (ui.pane < PANE_READ) ui.pane++;
		return false;
	case XKB_KEY_ISO_Left_Tab:
	case XKB_KEY_Left:
	case XKB_KEY_h:
		if (ui.pane > PANE_SIDE) ui.pane--;
		return false;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (ui.pane == PANE_SIDE) open_side_row();
		else if (ui.pane == PANE_LIST) ui.pane = PANE_READ;
		return false;
	case XKB_KEY_Up:
	case XKB_KEY_k:
	case XKB_KEY_Down:
	case XKB_KEY_j: {
		int d = sym == XKB_KEY_Up || sym == XKB_KEY_k ? -1 : 1;
		if (ui.pane == PANE_SIDE) {
			ui.side_sel += d;
			if (ui.side_sel < 0) ui.side_sel = 0;
			if (ui.side_sel >= ui.nside) ui.side_sel = ui.nside - 1;
		} else if (ui.pane == PANE_LIST) {
			move_list(d);
		} else {
			ui.read_top += d * 3;
		}
		return true;
	}
	case XKB_KEY_Page_Up:
	case XKB_KEY_Page_Down: {
		int d = sym == XKB_KEY_Page_Up ? -1 : 1;
		if (ui.pane == PANE_LIST) move_list(d * list_rows());
		else ui.read_top += d * (ui.r_read.h / ui.lh - 8);
		return true;
	}
	case XKB_KEY_space:
		ui.read_top += ui.r_read.h / ui.lh - 8;
		return true;
	case XKB_KEY_Home:
		if (ui.pane == PANE_READ) ui.read_top = 0;
		else move_list(-ui.sel);
		return false;
	case XKB_KEY_End:
		if (ui.pane == PANE_READ) ui.read_top = 1 << 28;
		else move_list(ui.page.n);
		return false;
	}
	return false;
}

/* ---------- buffers and frames ---------- */

static void buffer_release(void *data, struct wl_buffer *wl) {
	(void)wl;
	((buffer *)data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {.release = buffer_release};

static void buffer_destroy(buffer *b) {
	if (b->pix) pixman_image_unref(b->pix);
	if (b->wl) wl_buffer_destroy(b->wl);
	if (b->data) munmap(b->data, b->size);
	memset(b, 0, sizeof *b);
}

static int buffer_create(buffer *b, int w, int h) {
	size_t stride = (size_t)w * 4, size = stride * (size_t)h;
	int fd = memfd_create("syn-mail", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
		if (fd >= 0) close(fd);
		return -1;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return -1;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(ui.shm, fd, (int32_t)size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, (int32_t)stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	b->data = data;
	b->size = size;
	b->w = w;
	b->h = h;
	b->pix = pixman_image_create_bits(PIXMAN_x8r8g8b8, w, h, data, (int)stride);
	return 0;
}

static buffer *next_buffer(void) {
	for (int i = 0; i < 2; i++) {
		buffer *b = &ui.buf[i];
		if (b->busy) continue;
		if (b->w != ui.w || b->h != ui.h) {
			buffer_destroy(b);
			if (buffer_create(b, ui.w, ui.h) != 0) return NULL;
		}
		return b;
	}
	return NULL;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t t) {
	(void)data;
	(void)t;
	wl_callback_destroy(cb);
	ui.frame_pending = false;
}

static const struct wl_callback_listener frame_listener = {.done = frame_done};

static void frame(void) {
	if (!ui.configured || ui.frame_pending || !ui.dirty) return;
	buffer *b = next_buffer();
	if (!b) return;
	dst = b->pix;
	draw_all();
	dst = NULL;
	wl_surface_attach(ui.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(ui.surface, 0, 0, ui.w, ui.h);
	struct wl_callback *cb = wl_surface_frame(ui.surface);
	wl_callback_add_listener(cb, &frame_listener, NULL);
	wl_surface_commit(ui.surface);
	b->busy = true;
	ui.frame_pending = true;
	ui.dirty = false;
}

/* ---------- clipboard paste (into compose) ---------- */

static void offer_offer(void *d, struct wl_data_offer *o, const char *mime) {
	(void)d;
	if (!strcmp(mime, "text/plain;charset=utf-8") || !strcmp(mime, "text/plain") || !strcmp(mime, "UTF8_STRING")) {
		wl_data_offer_set_user_data(o, (void *)1);
	}
}

static void offer_actions(void *d, struct wl_data_offer *o, uint32_t a) {
	(void)d; (void)o; (void)a;
}

static const struct wl_data_offer_listener offer_listener = {.offer = offer_offer, .source_actions = offer_actions, .action = offer_actions};

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *o) {
	(void)d; (void)dd;
	wl_data_offer_add_listener(o, &offer_listener, NULL);
}

static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y,
	struct wl_data_offer *o) {
	(void)d; (void)dd; (void)serial; (void)s; (void)x; (void)y;
	if (o) wl_data_offer_destroy(o);
}

static void dd_leave(void *d, struct wl_data_device *dd) {
	(void)d; (void)dd;
}

static void dd_motion(void *d, struct wl_data_device *dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
	(void)d; (void)dd; (void)t; (void)x; (void)y;
}

static void dd_drop(void *d, struct wl_data_device *dd) {
	(void)d; (void)dd;
}

static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *o) {
	(void)d; (void)dd;
	if (ui.offer && ui.offer != o) wl_data_offer_destroy(ui.offer);
	ui.offer = o;
}

static const struct wl_data_device_listener dd_listener = {
	.data_offer = dd_data_offer, .enter = dd_enter, .leave = dd_leave, .motion = dd_motion, .drop = dd_drop, .selection = dd_selection};

static void paste(void) {
	if (!ui.offer || !wl_data_offer_get_user_data(ui.offer) || ui.clip_fd >= 0) return;
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0) return;
	wl_data_offer_receive(ui.offer, "text/plain;charset=utf-8", fds[1]);
	close(fds[1]);
	wl_display_flush(ui.display);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	ui.clip_fd = fds[0];
	ui.clip_len = 0;
}

static void on_clip(void) {
	char b[4096];
	for (;;) {
		ssize_t r = read(ui.clip_fd, b, sizeof b);
		if (r > 0) {
			ui.clip_buf = realloc(ui.clip_buf, ui.clip_len + (size_t)r + 1);
			memcpy(ui.clip_buf + ui.clip_len, b, (size_t)r);
			ui.clip_len += (size_t)r;
			continue;
		}
		if (r < 0 && errno == EAGAIN) return;
		break;
	}
	close(ui.clip_fd);
	ui.clip_fd = -1;
	if (ui.composing && ui.clip_len) {
		/* one-line fields take one line */
		if (ui.c_field != F_BODY) {
			for (size_t i = 0; i < ui.clip_len; i++) {
				if (ui.clip_buf[i] == '\n' || ui.clip_buf[i] == '\r' || ui.clip_buf[i] == '\t') ui.clip_buf[i] = ' ';
			}
		}
		if (ui.c_field != F_FROM) text_add(&ui.c[ui.c_field], ui.clip_buf, ui.clip_len);
	}
	free(ui.clip_buf);
	ui.clip_buf = NULL;
	ui.clip_len = 0;
	ui.dirty = true;
}

/* ---------- keyboard ---------- */

static void arm_repeat(uint32_t key) {
	if (ui.repeat_fd < 0 || ui.repeat_rate <= 0) return;
	ui.repeat_key = key;
	struct itimerspec ts = {
		.it_value = {ui.repeat_delay / 1000, (long)(ui.repeat_delay % 1000) * 1000000L},
		.it_interval = {0, 1000000000L / ui.repeat_rate},
	};
	timerfd_settime(ui.repeat_fd, 0, &ts, NULL);
}

static void disarm_repeat(void) {
	struct itimerspec ts = {0};
	ui.repeat_key = 0;
	if (ui.repeat_fd >= 0) timerfd_settime(ui.repeat_fd, 0, &ts, NULL);
}

static void on_repeat(void) {
	uint64_t n;
	if (read(ui.repeat_fd, &n, sizeof n) != sizeof n || !ui.repeat_key) return;
	key_action(ui.repeat_key, true);
}

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size) {
	(void)d; (void)k;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return;
	struct xkb_keymap *km = xkb_keymap_new_from_string(ui.xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (!km) return;
	xkb_state_unref(ui.xkb_state);
	xkb_keymap_unref(ui.keymap);
	ui.keymap = km;
	ui.xkb_state = xkb_state_new(km);
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys) {
	(void)d; (void)k; (void)serial; (void)s; (void)keys;
	syn_theme_load(&ui.pal); /* a theme switched while away */
	ui.dirty = true;
}

static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s) {
	(void)d; (void)k; (void)serial; (void)s;
	disarm_repeat();
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state) {
	(void)d; (void)k; (void)serial; (void)t;
	if (!ui.xkb_state) return;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		bool rep = key_action(key, false);
		if (rep && xkb_keymap_key_repeats(ui.keymap, key + 8)) arm_repeat(key);
		else disarm_repeat();
	} else if (key == ui.repeat_key) {
		disarm_repeat();
	}
}

static void kb_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
	(void)d; (void)k; (void)serial;
	if (ui.xkb_state) xkb_state_update_mask(ui.xkb_state, dep, lat, lock, 0, 0, group);
}

static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {
	(void)d; (void)k;
	ui.repeat_rate = rate;
	ui.repeat_delay = delay;
}

static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave, .key = kb_key, .modifiers = kb_mods, .repeat_info = kb_repeat};

/* ---------- pointer ---------- */

static bool in_rect(rect r, double x, double y) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void set_cursor(uint32_t serial) {
	if (!ui.cursor_theme) return;
	struct wl_cursor *c = wl_cursor_theme_get_cursor(ui.cursor_theme, "default");
	if (!c) c = wl_cursor_theme_get_cursor(ui.cursor_theme, "left_ptr");
	if (!c || c->image_count == 0) return;
	struct wl_cursor_image *img = c->images[0];
	wl_pointer_set_cursor(ui.pointer, serial, ui.cursor_surface, (int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
	wl_surface_attach(ui.cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
	wl_surface_damage_buffer(ui.cursor_surface, 0, 0, (int32_t)img->width, (int32_t)img->height);
	wl_surface_commit(ui.cursor_surface);
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
	(void)d; (void)p; (void)s;
	ui.px = wl_fixed_to_double(x);
	ui.py = wl_fixed_to_double(y);
	set_cursor(serial);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
	(void)d; (void)p; (void)serial; (void)s;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
	(void)d; (void)p; (void)t;
	ui.px = wl_fixed_to_double(x);
	ui.py = wl_fixed_to_double(y);
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state) {
	(void)d; (void)p; (void)serial; (void)t;
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED || ui.composing) return;
	ui.dirty = true;
	if (in_rect(ui.r_side, ui.px, ui.py)) {
		ui.pane = PANE_SIDE;
		int rh = ui.lh + 6, fits = (ui.r_side.h - ui.lh - 14) / rh;
		int first = ui.side_sel >= fits ? ui.side_sel - fits + 1 : 0;
		int row = (int)(ui.py - (ui.r_side.y + ui.lh + 14)) / rh;
		if (ui.py < ui.r_side.y + ui.lh + 14 || row < 0) return;
		/* an account still connecting takes two rows */
		int i = first, y = 0;
		for (; i < ui.nside; i++) {
			int h = ui.side[i].folder < 0 && !ui.acc[ui.side[i].acc].loaded ? 2 : 1;
			if (row < y + h) break;
			y += h;
		}
		if (i < ui.nside) {
			ui.side_sel = i;
			open_side_row();
		}
	} else if (in_rect(ui.r_list, ui.px, ui.py)) {
		ui.pane = PANE_LIST;
		int top = ui.r_list.y + ui.lh + 8;
		if (ui.py < top) return;
		int i = ui.list_top + (int)(ui.py - top) / (ui.lh * 2 + 6);
		if (i >= ui.page.n) {
			if (ui.page.n < ui.page.exists && !ui.page_loading) ask_page(ui.page.n);
			return;
		}
		int64_t now = now_ms();
		bool dbl = i == ui.last_click_row && now - ui.last_click < 400;
		ui.last_click = now;
		ui.last_click_row = i;
		if (i != ui.sel) {
			ui.sel = i;
			ask_body();
		}
		if (dbl) ui.pane = PANE_READ;
	} else if (in_rect(ui.r_read, ui.px, ui.py)) {
		ui.pane = PANE_READ;
	}
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v) {
	(void)d; (void)p; (void)t;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || ui.composing) return;
	int steps = wl_fixed_to_double(v) > 0 ? 1 : -1;
	if (in_rect(ui.r_read, ui.px, ui.py)) {
		ui.read_top += steps * 3;
	} else if (in_rect(ui.r_list, ui.px, ui.py)) {
		ui.list_top += steps;
		int rows = list_rows();
		if (ui.list_top > ui.page.n - rows) {
			ui.list_top = ui.page.n - rows;
			if (ui.page.n < ui.page.exists && !ui.page_loading) ask_page(ui.page.n);
		}
		if (ui.list_top < 0) ui.list_top = 0;
		/* keep the selection on screen, the way the draw expects */
		if (ui.sel >= 0 && (ui.sel < ui.list_top || ui.sel >= ui.list_top + rows)) {
			ui.sel = ui.sel < ui.list_top ? ui.list_top : ui.list_top + rows - 1;
			if (ui.sel >= ui.page.n) ui.sel = ui.page.n - 1;
			ask_body();
		}
	}
	ui.dirty = true;
}

static void ptr_frame(void *d, struct wl_pointer *p) {
	(void)d; (void)p;
}

static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) {
	(void)d; (void)p; (void)s;
}

static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {
	(void)d; (void)p; (void)t; (void)a;
}

static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {
	(void)d; (void)p; (void)a; (void)v;
}

static const struct wl_pointer_listener ptr_listener = {
	.enter = ptr_enter, .leave = ptr_leave, .motion = ptr_motion, .button = ptr_button, .axis = ptr_axis,
	.frame = ptr_frame, .axis_source = ptr_axis_source, .axis_stop = ptr_axis_stop, .axis_discrete = ptr_axis_discrete};

/* ---------- seat, shell, registry ---------- */

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
	(void)d;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ui.keyboard) {
		ui.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(ui.keyboard, &kb_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ui.pointer) {
		ui.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(ui.pointer, &ptr_listener, NULL);
	}
}

static void seat_name(void *d, struct wl_seat *s, const char *name) {
	(void)d; (void)s; (void)name;
}

static const struct wl_seat_listener seat_listener = {.capabilities = seat_caps, .name = seat_name};

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial) {
	(void)d;
	xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = {.ping = wm_ping};

static void xs_configure(void *d, struct xdg_surface *s, uint32_t serial) {
	(void)d;
	xdg_surface_ack_configure(s, serial);
	ui.w = ui.want_w > 0 ? ui.want_w : 1280;
	ui.h = ui.want_h > 0 ? ui.want_h : 800;
	ui.configured = true;
	ui.dirty = true;
}

static const struct xdg_surface_listener xs_listener = {.configure = xs_configure};

static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states) {
	(void)d; (void)t; (void)states;
	ui.want_w = w;
	ui.want_h = h;
}

static void tl_close(void *d, struct xdg_toplevel *t) {
	(void)d; (void)t;
	ui.running = false;
}

static void tl_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) {
	(void)d; (void)t; (void)w; (void)h;
}

static void tl_caps(void *d, struct xdg_toplevel *t, struct wl_array *caps) {
	(void)d; (void)t; (void)caps;
}

static const struct xdg_toplevel_listener tl_listener = {
	.configure = tl_configure, .close = tl_close, .configure_bounds = tl_bounds, .wm_capabilities = tl_caps};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name)) {
		ui.compositor = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		ui.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		ui.wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(ui.wm_base, &wm_listener, NULL);
	} else if (!strcmp(iface, wl_seat_interface.name) && !ui.seat) {
		ui.seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
		wl_seat_add_listener(ui.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
		ui.deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
	} else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
		ui.ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, ver < 3 ? ver : 3);
	}
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {
	(void)d; (void)r; (void)name;
}

static const struct wl_registry_listener reg_listener = {.global = reg_global, .global_remove = reg_remove};

/* ---------- start, shot, loop ---------- */

static struct fcft_font *load_font(const char *spec, int bump) {
	char name[300];
	str_copy(name, sizeof name, spec);
	char *sz = strstr(name, "size=");
	if (bump && sz) snprintf(sz, sizeof name - (size_t)(sz - name), "size=%d", atoi(sz + 5) + bump);
	char fallback[64];
	sz = strstr(name, "size=");
	snprintf(fallback, sizeof fallback, "monospace:%s", sz ? sz : "size=11");
	const char *names[] = {name, fallback};
	return fcft_from_name(2, names, NULL);
}

static int fonts(void) {
	fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	const char *spec = getenv("SYN_MAIL_FONT") && *getenv("SYN_MAIL_FONT") ? getenv("SYN_MAIL_FONT") : FONT;
	ui.font = load_font(spec, 0);
	ui.font_big = load_font(spec, 4);
	if (!ui.font || !ui.font_big) {
		fprintf(stderr, "syn-mail: no usable font for \"%s\"\n", spec);
		return -1;
	}
	ui.lh = ui.font->height;
	ui.cw = ui.font->max_advance.x > 0 ? ui.font->max_advance.x : ui.lh / 2;
	syn_theme_load(&ui.pal);
	return 0;
}

static int wayland_init(void) {
	ui.display = wl_display_connect(NULL);
	if (!ui.display) {
		fputs("syn-mail: no Wayland display (WAYLAND_DISPLAY): the window needs the desktop; --shot draws without it\n", stderr);
		return -1;
	}
	ui.registry = wl_display_get_registry(ui.display);
	wl_registry_add_listener(ui.registry, &reg_listener, NULL);
	wl_display_roundtrip(ui.display);
	if (!ui.compositor || !ui.shm || !ui.wm_base) {
		fputs("syn-mail: the compositor lacks wl_compositor, wl_shm or xdg_wm_base\n", stderr);
		return -1;
	}
	ui.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	ui.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (ui.ddm && ui.seat) {
		ui.ddev = wl_data_device_manager_get_data_device(ui.ddm, ui.seat);
		wl_data_device_add_listener(ui.ddev, &dd_listener, NULL);
	}
	ui.cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), getenv("XCURSOR_SIZE") ? atoi(getenv("XCURSOR_SIZE")) : 24, ui.shm);
	ui.cursor_surface = wl_compositor_create_surface(ui.compositor);
	ui.surface = wl_compositor_create_surface(ui.compositor);
	ui.xdg_surface = xdg_wm_base_get_xdg_surface(ui.wm_base, ui.surface);
	xdg_surface_add_listener(ui.xdg_surface, &xs_listener, NULL);
	ui.toplevel = xdg_surface_get_toplevel(ui.xdg_surface);
	xdg_toplevel_add_listener(ui.toplevel, &tl_listener, NULL);
	xdg_toplevel_set_title(ui.toplevel, "SYN-MAIL");
	xdg_toplevel_set_app_id(ui.toplevel, "syn-mail");
	xdg_toplevel_set_min_size(ui.toplevel, 900, 500);
	if (ui.deco_mgr) {
		ui.deco = zxdg_decoration_manager_v1_get_toplevel_decoration(ui.deco_mgr, ui.toplevel);
		zxdg_toplevel_decoration_v1_set_mode(ui.deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	wl_surface_commit(ui.surface);
	wl_display_roundtrip(ui.display);
	ui.running = true;
	return 0;
}

static void wayland_close(void) {
	if (!ui.display) return;
	for (int i = 0; i < 2; i++) buffer_destroy(&ui.buf[i]);
	if (ui.deco) zxdg_toplevel_decoration_v1_destroy(ui.deco);
	if (ui.toplevel) xdg_toplevel_destroy(ui.toplevel);
	if (ui.xdg_surface) xdg_surface_destroy(ui.xdg_surface);
	if (ui.surface) wl_surface_destroy(ui.surface);
	if (ui.repeat_fd >= 0) close(ui.repeat_fd);
	if (ui.clip_fd >= 0) close(ui.clip_fd);
	wl_display_flush(ui.display);
	wl_display_disconnect(ui.display);
	ui.display = NULL;
}

static int write_png(const char *path, const uint32_t *px, int w, int h) {
	png_image img = {0};
	img.version = PNG_IMAGE_VERSION;
	img.width = (png_uint_32)w;
	img.height = (png_uint_32)h;
	img.format = PNG_FORMAT_RGB;
	unsigned char *rgb = malloc((size_t)w * (size_t)h * 3);
	for (int i = 0; i < w * h; i++) {
		rgb[i * 3] = (unsigned char)(px[i] >> 16);
		rgb[i * 3 + 1] = (unsigned char)(px[i] >> 8);
		rgb[i * 3 + 2] = (unsigned char)px[i];
	}
	int ok = png_image_write_to_file(&img, path, 0, rgb, w * 3, NULL);
	free(rgb);
	if (!ok) fprintf(stderr, "syn-mail: %s: %s\n", path, img.message);
	return ok ? 0 : -1;
}

/* waits for the folders, the first page, its first message and that message's HTML drawing (or 45 s), draws once and
 * saves the PNG */
static int shot_run(void) {
	int64_t until = now_ms() + 45000;
	for (;;) {
		bool folders = true;
		for (int a = 0; a < ui.cfg.n; a++) {
			folders = folders && (ui.acc[a].loaded || ui.acc[a].failed) && (ui.acc[a].inbox_unseen >= 0 || ui.acc[a].failed);
		}
		bool page = !ui.page_loading;
		bool body = !ui.body_loading && !ui.html_busy;
		if ((folders && page && body) || now_ms() >= until) break;
		struct pollfd p[2] = {{workers_fd(), POLLIN, 0}, {ui.html_fd[0], POLLIN, 0}};
		poll(p, 2, 250);
		drain();
		if (ui.html_busy) on_render();
	}
	uint32_t *px = calloc((size_t)ui.w * (size_t)ui.h, 4);
	dst = pixman_image_create_bits(PIXMAN_x8r8g8b8, ui.w, ui.h, px, ui.w * 4);
	draw_all();
	pixman_image_unref(dst);
	dst = NULL;
	int rc = write_png(ui.shot, px, ui.w, ui.h);
	free(px);
	if (rc == 0) {
		bool loaded = ui.page.n > 0 || ui.page.exists == 0;
		printf("drew %dx%d to %s: %d folders, %d messages on the page%s\n", ui.w, ui.h, ui.shot, ui.nside, ui.page.n,
			loaded ? "" : " (the folder did not load)");
		if (!loaded) rc = 1;
	}
	return rc;
}

static int window_loop(void) {
	if (wayland_init() != 0) return 1;
	while (ui.running) {
		frame();
		struct pollfd p[5] = {
			{wl_display_get_fd(ui.display), POLLIN, 0},
			{workers_fd(), POLLIN, 0},
			{ui.repeat_fd, POLLIN, 0},
			{ui.html_fd[0], POLLIN, 0},
			{ui.clip_fd, POLLIN, 0},
		};
		while (wl_display_prepare_read(ui.display) != 0) wl_display_dispatch_pending(ui.display);
		wl_display_flush(ui.display);
		/* the clock in the header wants a frame each minute; the open folder a look every two */
		int64_t now = now_ms();
		int timeout = (int)(60000 - (time(NULL) % 60) * 1000);
		int64_t next_refresh = ui.last_refresh + REFRESH_MS - now;
		if (next_refresh < timeout) timeout = next_refresh < 0 ? 0 : (int)next_refresh;
		int n = poll(p, ui.clip_fd >= 0 ? 5 : 4, timeout);
		if (n > 0 && (p[0].revents & POLLIN)) {
			if (wl_display_read_events(ui.display) != 0) break;
		} else {
			wl_display_cancel_read(ui.display);
		}
		if (wl_display_dispatch_pending(ui.display) < 0) break;
		if (p[0].revents & (POLLERR | POLLHUP)) break;
		if (n > 0 && (p[1].revents & POLLIN)) drain();
		if (n > 0 && (p[2].revents & POLLIN)) on_repeat();
		if (n > 0 && (p[3].revents & POLLIN)) on_render();
		if (n > 0 && ui.clip_fd >= 0 && (p[4].revents & (POLLIN | POLLHUP))) on_clip();
		if (n == 0) ui.dirty = true;
		if (now_ms() - ui.last_refresh >= REFRESH_MS) refresh();
	}
	wayland_close();
	return 0;
}

int ui_main(int argc, char **argv) {
	int w = 1280, h = 800;
	const char *acc_name = NULL, *folder = "INBOX";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--shot") && i + 1 < argc) ui.shot = argv[++i];
		else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
			if (sscanf(argv[++i], "%dx%d", &w, &h) != 2 || w < 400 || h < 300 || w > 8000 || h > 8000) {
				fputs("syn-mail: --size WxH, at least 400x300\n", stderr);
				return 2;
			}
		} else if (!strcmp(argv[i], "--open") && i + 1 < argc) ui.open_n = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--account") && i + 1 < argc) acc_name = argv[++i];
		else if (!strcmp(argv[i], "--folder") && i + 1 < argc) folder = argv[++i];
		else if (!strcmp(argv[i], "--images")) ui.remote_always = true;
		else {
			fputs("usage: syn-mail window [--shot FILE.png] [--size WxH] [--open N] [--account NAME] [--folder FOLDER] [--images]\n", stderr);
			return 2;
		}
	}
	char err[256] = "";
	if (config_load(&ui.cfg, err, sizeof err) != 0) {
		fprintf(stderr, "syn-mail: %s\n", err);
		return 1;
	}
	if (!ui.cfg.n) {
		fputs("syn-mail: no accounts yet: syn-mail add NAME ADDRESS --preset google|ionos\n", stderr);
		config_free(&ui.cfg);
		return 1;
	}
	const char *dl = getenv("SYN_MAIL_DRAWLOG");
	if (dl && *dl) ui.drawlog = fopen(dl, "a");
	if (fonts() != 0) return 1;
	ui.w = w;
	ui.h = h;
	ui.acc = calloc((size_t)ui.cfg.n, sizeof *ui.acc);
	for (int a = 0; a < ui.cfg.n; a++) ui.acc[a].inbox_unseen = -1;
	ui.page.exists = ui.page.unseen = -1;
	ui.sel = -1;
	rebuild_side();
	if (workers_start(&ui.cfg) != 0) {
		fputs("syn-mail: could not start the mail workers\n", stderr);
		return 1;
	}
	for (int a = 0; a < ui.cfg.n; a++) workers_push(&(Job){.kind = JOB_FOLDERS, .acc = a});
	int first = 0;
	for (int a = 0; acc_name && a < ui.cfg.n; a++) {
		if (!strcmp(ui.cfg.acc[a].name, acc_name)) first = a;
	}
	if (acc_name && strcmp(ui.cfg.acc[first].name, acc_name)) {
		fprintf(stderr, "syn-mail: no account '%s' (see syn-mail list)\n", acc_name);
		return 1;
	}
	if (strpbrk(folder, "\r\n")) {
		fputs("syn-mail: a folder name cannot hold a line break\n", stderr);
		return 2;
	}
	open_folder(first, folder, strcasecmp(folder, "INBOX") ? folder : "Inbox");
	count_inboxes();
	ui.pane = PANE_LIST;
	int rc = ui.shot ? shot_run() : window_loop();
	/* a drawing under way is let finish (10 s at most), so its folder does not outlive the window */
	for (int64_t until = now_ms() + 10000; ui.html_busy && now_ms() < until;) {
		struct pollfd p = {ui.html_fd[0], POLLIN, 0};
		if (poll(&p, 1, 250) > 0) on_render();
	}
	workers_stop();
	if (ui.drawlog) fclose(ui.drawlog);
	fcft_destroy(ui.font);
	fcft_destroy(ui.font_big);
	fcft_fini();
	return rc;
}
