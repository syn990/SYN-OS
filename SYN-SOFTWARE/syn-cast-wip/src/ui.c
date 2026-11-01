/* ------------------------------------------------------------------------
 *   Layout, top to bottom:
 *
 *   SYN-CAST | user @ zello.io                 AVAILABLE  VOX  1x
 *   CHANNELS        | # channel              3 ONLINE  TEXT IMG LOC
 *   > channel    3  |  20:01  m990          VOICE 0:07
 *     other     *2  |  20:02  you           hello there
 *                   |  20:03  syn-zello     IMAGE 1280x720  [thumb]
 *                   |  ON AIR  m990  |||||||....  0:03     QUEUE 1
 *                   |  > type a message_
 *                   |  [        HOLD SPACE TO TALK  -> channel       ]
 *   hints or the last notice
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "ui.h"
#include "app.h"
#include "image.h"
#include "syn_theme.h"
#include "util.h"
#include "xdg-decoration-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pixman.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#define PAD 8
#define THUMB_W 220
#define MAX_LINES 64
#define DOUBLE_CLICK_MS 400

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
	uint64_t id;
	pixman_image_t *img;
	uint32_t *px;
	bool missing;
} thumb;

static struct {
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
	bool offer_text;
	struct wl_data_source *source;
	char *copy_text;
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
	uint32_t kb_serial, ptr_serial;
	double px, py;
	bool ptr_in;
	bool ptt_key, ptt_mouse;

	struct fcft_font *font, *font_big;
	int lh, cw;
	syn_palette pal;

	char input[4096];
	size_t input_len, caret;
	int sel;                 /* selected history index, -1 = none */
	int scroll;              /* newest messages scrolled out below */
	int64_t last_click;
	int last_click_idx;

	rect r_side, r_hist, r_input, r_ptt;
	struct {
		int y0, y1, idx;
	} hits[256];
	int nhits;
	struct {
		int y0, y1, ch;
	} chits[CFG_MAX_CHANNELS];
	int nchits;
	int64_t last_draw;
	thumb thumbs[48];
	int thumb_next;
} ui = {.clip_fd = -1, .repeat_fd = -1, .sel = -1};

/* ---------- drawing ---------- */

static pixman_image_t *dst;

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

/* The palette's text or background colour, whichever reads better on c. */
static syn_rgb ink_on(syn_rgb c) {
	int l = luma(c);
	int dt = abs(luma(ui.pal.text) - l), db = abs(luma(ui.pal.bg) - l);
	return dt >= db ? ui.pal.text : ui.pal.bg;
}

static void fill(int x, int y, int w, int h, syn_rgb c) {
	if (w <= 0 || h <= 0) {
		return;
	}
	pixman_color_t pc = col(c);
	pixman_box32_t box = {x, y, x + w, y + h};
	pixman_image_fill_boxes(PIXMAN_OP_SRC, dst, &pc, 1, &box);
}

static void frame_rect(int x, int y, int w, int h, int t, syn_rgb c) {
	fill(x, y, w, t, c);
	fill(x, y + h - t, w, t, c);
	fill(x, y, t, h, c);
	fill(x + w - t, y, t, h, c);
}

static const struct fcft_glyph *glyph(struct fcft_font *f, uint32_t cp) {
	if (cp == '\t' || cp == '\n' || cp == '\r') {
		cp = ' ';
	}
	return fcft_rasterize_char_utf32(f, cp, FCFT_SUBPIXEL_NONE);
}

static int text_width_n(struct fcft_font *f, const char *s, size_t n) {
	int w = 0;
	for (size_t i = 0; i < n;) {
		uint32_t cp;
		i += utf8_next(s + i, n - i, &cp);
		const struct fcft_glyph *g = glyph(f, cp);
		if (g) {
			w += g->advance.x;
		}
	}
	return w;
}

static int text_width(struct fcft_font *f, const char *s) {
	return text_width_n(f, s, strlen(s));
}

/* Draws n bytes of s with the top of the line at y; stops at max_w (0 =
 * no limit). Returns the width drawn. */
static int draw_text_n(struct fcft_font *f, int x, int y, const char *s, size_t n, syn_rgb c, int max_w) {
	pixman_color_t pc = col(c);
	pixman_image_t *solid = pixman_image_create_solid_fill(&pc);
	int pen = x;
	for (size_t i = 0; i < n;) {
		uint32_t cp;
		i += utf8_next(s + i, n - i, &cp);
		const struct fcft_glyph *g = glyph(f, cp);
		if (!g) {
			continue;
		}
		if (max_w > 0 && pen + g->advance.x - x > max_w) {
			break;
		}
		int gx = pen + g->x, gy = y + f->ascent - g->y;
		if (g->is_color_glyph) {
			pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, dst, 0, 0, 0, 0, gx, gy, g->width, g->height);
		} else {
			pixman_image_composite32(PIXMAN_OP_OVER, solid, g->pix, dst, 0, 0, 0, 0, gx, gy, g->width, g->height);
		}
		pen += g->advance.x;
	}
	pixman_image_unref(solid);
	return pen - x;
}

static int draw_text(struct fcft_font *f, int x, int y, const char *s, syn_rgb c) {
	return draw_text_n(f, x, y, s, strlen(s), c, 0);
}

/* Fits s into max_w, ending in an ellipsis when cut. */
static int draw_text_fit(struct fcft_font *f, int x, int y, const char *s, syn_rgb c, int max_w) {
	if (max_w <= 0) {
		return 0;
	}
	size_t n = strlen(s);
	if (text_width_n(f, s, n) <= max_w) {
		return draw_text_n(f, x, y, s, n, c, 0);
	}
	int ell = text_width(f, "…");
	int w = 0;
	size_t i = 0;
	while (i < n) {
		uint32_t cp;
		size_t k = utf8_next(s + i, n - i, &cp);
		const struct fcft_glyph *g = glyph(f, cp);
		int a = g ? g->advance.x : 0;
		if (w + a + ell > max_w) {
			break;
		}
		w += a;
		i += k;
	}
	draw_text_n(f, x, y, s, i, c, 0);
	draw_text(f, x + w, y, "…", c);
	return w + ell;
}

typedef struct {
	const char *p;
	size_t n;
} line;

/* Word-wraps s into lines no wider than width. */
static int wrap(struct fcft_font *f, const char *s, int width, line *out, int max) {
	int count = 0;
	size_t len = strlen(s);
	size_t start = 0;
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
			if (w + a > width && i > start) {
				break;
			}
			if (cp == ' ') {
				brk = i;
			}
			w += a;
			i += k;
		}
		size_t end = i;
		if (!hard && i < len && brk > start) {
			end = brk;
		}
		out[count].p = s + start;
		out[count].n = end - start;
		count++;
		start = end;
		if (start < len && (s[start] == ' ' || s[start] == '\n')) {
			start++;
		}
	}
	if (count == 0 && max > 0) {
		out[0].p = s;
		out[0].n = 0;
		count = 1;
	}
	return count;
}

/* ---------- thumbnails ---------- */

static thumb *get_thumb(int ch, const hmsg *m) {
	for (size_t i = 0; i < ARRAY_LEN(ui.thumbs); i++) {
		if (ui.thumbs[i].id == m->id) {
			return ui.thumbs[i].missing && m->partial ? NULL : &ui.thumbs[i];
		}
	}
	char path[1200];
	history_image_path(&app.chan[ch].hist, m->id, true, path, sizeof path);
	int w, h;
	uint32_t *px = image_load_argb(path, THUMB_W, ui.lh * 5, &w, &h);
	if (!px && m->partial) {
		return NULL; /* not here yet: try again next frame */
	}
	thumb *t = &ui.thumbs[ui.thumb_next];
	ui.thumb_next = (ui.thumb_next + 1) % (int)ARRAY_LEN(ui.thumbs);
	if (t->img) {
		pixman_image_unref(t->img);
	}
	free(t->px);
	memset(t, 0, sizeof *t);
	t->id = m->id;
	if (px) {
		t->px = px;
		t->img = pixman_image_create_bits(PIXMAN_x8r8g8b8, w, h, px, w * 4);
	} else {
		t->missing = true;
	}
	return t;
}

/* ---------- the parts ---------- */

/* "4.2s" under a minute (a quick key tap is a tenth of a second, not
 * "0:00"), "1:05" after */
static void fmt_dur(int ms, char *out, size_t cap) {
	if (ms < 60000) {
		snprintf(out, cap, "%d.%ds", ms / 1000, (ms % 1000) / 100);
	} else {
		snprintf(out, cap, "%d:%02d", ms / 60000, (ms / 1000) % 60);
	}
}

static void draw_header(void) {
	syn_palette *p = &ui.pal;
	int h = ui.lh + PAD;
	fill(0, 0, ui.w, h, p->panel);
	fill(0, h - 1, ui.w, 1, p->border);
	int x = 0;
	int tw = text_width(ui.font, "SYN-CAST") + 2 * PAD;
	fill(0, 0, tw, h - 1, p->accent);
	draw_text(ui.font, PAD, PAD / 2, "SYN-CAST", ink_on(p->accent));
	x = tw + PAD;
	char who[300];
	const char *host = strstr(app.cfg.server, "://");
	host = host ? host + 3 : app.cfg.server;
	snprintf(who, sizeof who, "%s @ %.*s", app.cfg.username[0] ? app.cfg.username : "anonymous",
		(int)strcspn(host, "/"), host);
	x += draw_text(ui.font, x, PAD / 2, who, p->text);

	/* badges, right-aligned */
	char speed[16];
	snprintf(speed, sizeof speed, "%.4gx", app.speed);
	const char *badges[4] = {app.offline ? "OFFLINE" : "ONLINE", app.busy ? "BUSY" : "AVAILABLE",
		capture_vox() ? "VOX" : NULL, speed};
	int rx = ui.w - PAD;
	for (int i = 3; i >= 0; i--) {
		if (!badges[i]) continue;
		int bw = text_width(ui.font, badges[i]) + PAD;
		rx -= bw;
		bool hot = (i == 0 && app.offline) || (i == 1 && app.busy) || i == 2;
		if (hot) {
			fill(rx, 2, bw, h - 5, i <= 1 ? p->urgent : p->accent_dim);
		}
		frame_rect(rx, 2, bw, h - 5, 1, hot ? p->accent : p->border);
		draw_text(ui.font, rx + PAD / 2, PAD / 2, badges[i], hot ? p->text : mix(p->text, p->bg, 30));
		rx -= PAD / 2;
	}
	(void)x;
}

static void draw_sidebar(void) {
	syn_palette *p = &ui.pal;
	rect r = ui.r_side;
	fill(r.x, r.y, r.w, r.h, p->bg);
	fill(r.x + r.w - 1, r.y, 1, r.h, p->border);
	draw_text(ui.font, r.x + PAD, r.y + PAD / 2, "CHANNELS", p->accent);
	int y = r.y + ui.lh + PAD;
	ui.nchits = 0;
	if (app.n_chan == 0) {
		draw_text_fit(ui.font, r.x + PAD, y, "none configured", mix(p->text, p->bg, 50), r.w - 2 * PAD);
		return;
	}
	for (int k = 0; k < app.n_chan; k++) {
		channel *c = &app.chan[k];
		int rh = ui.lh + 6;
		bool act = k == app.active;
		if (act) {
			fill(r.x, y, r.w - 1, rh, p->accent_dim);
			fill(r.x, y, 3, rh, p->accent);
		}
		char right[32];
		if (c->online) {
			snprintf(right, sizeof right, "%d", c->users_online);
		} else {
			const char *l = app_session_label(k);
			snprintf(right, sizeof right, "%s", strcmp(l, "NOT JOINED") == 0 ? "--" : strncmp(l, "RETRY", 5) == 0 ? "RETRY" :
				strcmp(l, "ONLINE") == 0 ? "-" : strcmp(l, "SIGNING IN") == 0 ? "..." : strcmp(l, "CONNECTING") == 0 ? ".." : "OFF");
		}
		if (c->unread) {
			char u[16];
			snprintf(u, sizeof u, "*%d ", c->unread);
			size_t ul = strlen(u);
			memmove(right + ul, right, strlen(right) + 1);
			memcpy(right, u, ul);
		}
		int rw = text_width(ui.font, right);
		syn_rgb dot = c->online ? p->accent : p->border;
		fill(r.x + PAD, y + rh / 2 - 3, 6, 6, dot);
		int nx = r.x + PAD + 12;
		draw_text_fit(ui.font, nx, y + 3, c->name, act ? p->text : mix(p->text, p->bg, 20),
			r.w - (nx - r.x) - rw - 2 * PAD - (c->muted ? ui.cw * 2 : 0));
		if (c->muted) {
			draw_text(ui.font, r.x + r.w - rw - 2 * PAD - ui.cw * 2, y + 3, "M", p->urgent);
		}
		draw_text(ui.font, r.x + r.w - rw - PAD, y + 3, right, c->unread ? p->urgent : mix(p->text, p->bg, 30));
		if (ui.nchits < CFG_MAX_CHANNELS) {
			ui.chits[ui.nchits].y0 = y;
			ui.chits[ui.nchits].y1 = y + rh;
			ui.chits[ui.nchits].ch = k;
			ui.nchits++;
		}
		y += rh;
	}
}

/* Draws (or with draw=false only measures) one history row. */
static int draw_msg(int ch, const hmsg *m, int x, int y, int w, bool draw, bool selected) {
	syn_palette *p = &ui.pal;
	syn_rgb dim = mix(p->text, p->bg, 45);
	int col_from = x + PAD + ui.cw * 6;
	int col_body = x + PAD + ui.cw * 22;
	int body_w = x + w - PAD - col_body;
	if (body_w < ui.cw * 10) {
		body_w = ui.cw * 10;
	}
	bool on_air = app.playing && app.queue[0].channel == ch && app.queue[0].msg_id == m->id;

	char summary[400] = "";
	const char *body = NULL;
	char tag[96] = "";
	if (m->forwho[0]) {
		snprintf(tag, sizeof tag, m->out ? "[to %s] " : "[private] ", m->forwho);
	}
	char dur[16];
	switch (m->kind) {
	case HM_VOICE:
		fmt_dur(m->duration_ms, dur, sizeof dur);
		if (m->partial) {
			snprintf(summary, sizeof summary, "%sVOICE  %s", tag, m->out ? "SENDING" : "LIVE");
		} else if (on_air) {
			char at[16];
			int el = (int)((double)(now_ms() - app.queue[0].began) *
				(app.queue[0].kind == PI_REPLAY ? app.speed : 1.0));
			fmt_dur(el < m->duration_ms ? el : m->duration_ms, at, sizeof at);
			snprintf(summary, sizeof summary, "%sVOICE  %s / %s  PLAYING", tag, at, dur);
		} else {
			snprintf(summary, sizeof summary, "%sVOICE  %s%s", tag, dur, !m->played && !m->out ? "  NEW" : "");
		}
		body = m->text; /* transcription */
		break;
	case HM_TEXT:
		snprintf(summary, sizeof summary, "%s", tag);
		body = m->text;
		break;
	case HM_IMAGE:
		snprintf(summary, sizeof summary, "%sIMAGE %dx%d%s", tag, m->w, m->h, m->partial ? "  LOADING" : "");
		break;
	case HM_LOCATION:
		snprintf(summary, sizeof summary, "%sLOCATION %.5f, %.5f", tag, m->lat, m->lon);
		body = m->text && *m->text ? m->text : NULL;
		break;
	case HM_EVENT:
		break;
	}

	line lines[MAX_LINES];
	int nl = 0;
	int first_line_x = col_body;
	if (m->kind == HM_TEXT && body) {
		/* the text starts on the first line, after any [private] tag */
		int tagw = text_width(ui.font, summary);
		nl = wrap(ui.font, body, body_w - tagw > ui.cw * 10 ? body_w - tagw : body_w, lines, MAX_LINES);
		first_line_x = col_body + tagw;
	} else if (body) {
		nl = wrap(ui.font, body, body_w, lines, MAX_LINES);
	}
	int rows = 1 + (m->kind == HM_TEXT ? (nl > 1 ? nl - 1 : 0) : nl);
	int h = rows * ui.lh + 4;
	thumb *t = NULL;
	if (m->kind == HM_IMAGE) {
		t = get_thumb(ch, m);
		h += (t && t->img ? pixman_image_get_height(t->img) : ui.lh) + 4;
	}
	if (!draw) {
		return h;
	}

	if (on_air) {
		fill(x, y, w, h, p->accent_dim);
		fill(x, y, 3, h, p->accent);
	} else if (selected) {
		fill(x, y, w, h, p->panel_hover);
	}
	char when[8];
	time_t ts = (time_t)(m->ts / 1000);
	struct tm tm;
	localtime_r(&ts, &tm);
	strftime(when, sizeof when, "%H:%M", &tm);
	int ty = y + 2;
	draw_text(ui.font, x + PAD, ty, when, dim);
	if (m->kind == HM_EVENT) {
		draw_text_fit(ui.font, col_from, ty, m->text ? m->text : "", dim, x + w - PAD - col_from);
		return h;
	}
	draw_text_fit(ui.font, col_from, ty, m->from, m->out ? p->accent : p->text, col_body - col_from - ui.cw);
	syn_rgb sc = m->kind == HM_VOICE && !m->played && !m->out ? p->urgent : m->forwho[0] ? p->urgent : p->text;
	if (m->kind == HM_TEXT) {
		draw_text(ui.font, col_body, ty, summary, p->urgent);
		syn_rgb tc = m->partial ? dim : p->text;
		for (int i = 0; i < nl; i++) {
			draw_text_n(ui.font, i == 0 ? first_line_x : col_body, ty + i * ui.lh, lines[i].p, lines[i].n, tc, 0);
		}
	} else {
		draw_text_fit(ui.font, col_body, ty, summary, sc, body_w);
		for (int i = 0; i < nl; i++) {
			draw_text_n(ui.font, col_body, ty + (i + 1) * ui.lh, lines[i].p, lines[i].n, dim, 0);
		}
	}
	if (m->kind == HM_IMAGE) {
		int iy = ty + rows * ui.lh + 2;
		if (t && t->img) {
			int iw = pixman_image_get_width(t->img), ih = pixman_image_get_height(t->img);
			pixman_image_composite32(PIXMAN_OP_SRC, t->img, NULL, dst, 0, 0, 0, 0, col_body, iy, iw, ih);
			frame_rect(col_body - 1, iy - 1, iw + 2, ih + 2, 1, p->border);
		} else {
			draw_text(ui.font, col_body, iy, m->partial ? "loading…" : "(no thumbnail)", dim);
		}
	}
	return h;
}

static void draw_history(void) {
	syn_palette *p = &ui.pal;
	rect r = ui.r_hist;
	fill(r.x, r.y, r.w, r.h, p->bg);
	ui.nhits = 0;
	if (app.n_chan == 0) {
		char path[1024];
		const char *lines[] = {
			"Not configured yet.",
			"",
			"Run  syn-cast setup  in a terminal, or write",
			config_default_path(path, sizeof path),
			"with server, username, password, issuer, key and channels.",
		};
		for (size_t i = 0; i < ARRAY_LEN(lines); i++) {
			draw_text_fit(ui.font, r.x + 2 * PAD, r.y + 2 * PAD + (int)i * ui.lh, lines[i],
				i == 0 ? p->accent : p->text, r.w - 4 * PAD);
		}
		return;
	}
	channel *c = &app.chan[app.active];

	/* channel title bar */
	int th = ui.font_big->height + PAD;
	fill(r.x, r.y, r.w, th, p->bg);
	fill(r.x, r.y + th - 1, r.w, 1, p->border);
	int tx = r.x + PAD;
	tx += draw_text(ui.font_big, tx, r.y + PAD / 2, "# ", p->accent);
	char info[300];
	if (c->online) {
		snprintf(info, sizeof info, "%d ONLINE%s%s%s", c->users_online, c->texting_ok ? "  TEXT" : "",
			c->images_ok ? "  IMG" : "", c->locations_ok ? "  LOC" : "");
	} else {
		const char *err = app.sess[c->session].error;
		snprintf(info, sizeof info, "%s%s%s", app_session_label(app.active), err[0] ? ": " : "", err);
	}
	int iw = text_width(ui.font, info);
	int maxw = r.w - (tx - r.x) - iw - 3 * PAD;
	draw_text_fit(ui.font_big, tx, r.y + PAD / 2, c->name, p->text, maxw > ui.cw * 6 ? maxw : ui.cw * 6);
	draw_text_fit(ui.font, r.x + r.w - iw - PAD, r.y + (th - ui.lh) / 2, info,
		c->online ? mix(p->text, p->bg, 25) : p->urgent, r.w / 2);

	history *h = &c->hist;
	int top = r.y + th + 2;
	int bottom = r.y + r.h;
	if (h->n == 0) {
		draw_text(ui.font, r.x + 2 * PAD, top + PAD, "No messages yet. Hold space and talk.", mix(p->text, p->bg, 45));
		return;
	}
	if (ui.scroll > (int)h->n - 1) {
		ui.scroll = (int)h->n - 1;
	}
	if (ui.scroll < 0) {
		ui.scroll = 0;
	}
	int y = bottom;
	for (int i = (int)h->n - 1 - ui.scroll; i >= 0 && y > top; i--) {
		const hmsg *m = &h->v[i];
		int mh = draw_msg(app.active, m, r.x, 0, r.w, false, false);
		y -= mh;
		/* clip to the pane: draw into a sub-region by skipping rows above */
		pixman_region32_t clip;
		pixman_region32_init_rect(&clip, r.x, top, (unsigned)r.w, (unsigned)(bottom - top));
		pixman_image_set_clip_region32(dst, &clip);
		draw_msg(app.active, m, r.x, y, r.w, true, i == ui.sel);
		pixman_image_set_clip_region32(dst, NULL);
		pixman_region32_fini(&clip);
		if (ui.nhits < (int)ARRAY_LEN(ui.hits)) {
			ui.hits[ui.nhits].y0 = y < top ? top : y;
			ui.hits[ui.nhits].y1 = y + mh;
			ui.hits[ui.nhits].idx = i;
			ui.nhits++;
		}
	}
	if (ui.scroll > 0) {
		char more[64];
		snprintf(more, sizeof more, " %d NEWER BELOW (END) ", ui.scroll);
		int mw = text_width(ui.font, more);
		fill(r.x + r.w - mw - PAD, bottom - ui.lh - 2, mw, ui.lh + 2, p->accent);
		draw_text(ui.font, r.x + r.w - mw - PAD, bottom - ui.lh - 1, more, ink_on(p->accent));
	}
}

static void meter(int x, int y, int w, int h, int level, syn_rgb on, syn_rgb off) {
	int bars = w / 6;
	int lit = bars * level / 100;
	for (int b = 0; b < bars; b++) {
		fill(x + b * 6, y, 4, h, b < lit ? on : off);
	}
}

static void draw_onair(rect r) {
	syn_palette *p = &ui.pal;
	fill(r.x, r.y, r.w, r.h, p->panel);
	fill(r.x, r.y, r.w, 1, p->border);
	int y = r.y + (r.h - ui.lh) / 2;
	int x = r.x + PAD;
	char t[200];
	if (app.tx.phase != TX_IDLE) {
		x += draw_text(ui.font, x, y, app.tx.phase == TX_SENDING ? "ON AIR  " : "OPENING ", p->accent);
		snprintf(t, sizeof t, "%s -> %s", app.tx.say_pcm ? "file" : app.tx.vox ? "vox" : "mic",
			app.chan[app.tx.channel].name);
		x += draw_text_fit(ui.font, x, y, t, p->text, r.w / 3) + PAD;
		meter(x, y + 2, 120, ui.lh - 4, app.tx.say_pcm ? app.tx.level : capture_level(), p->accent, p->accent_dim);
		x += 130;
		char d[16];
		fmt_dur((int)(now_ms() - app.tx.started), d, sizeof d);
		draw_text(ui.font, x, y, d, p->text);
	} else if (app.playing) {
		int level, el;
		const char *who = app_rx_name(&level, &el);
		play_item *it = &app.queue[0];
		x += draw_text(ui.font, x, y, it->kind == PI_REPLAY ? "REPLAY  " : "RX  ", p->accent);
		snprintf(t, sizeof t, "%s%s%s", who, it->channel != app.active ? " in " : "",
			it->channel != app.active ? app.chan[it->channel].name : "");
		x += draw_text_fit(ui.font, x, y, t, p->text, r.w / 3) + PAD;
		meter(x, y + 2, 120, ui.lh - 4, level, p->accent, p->accent_dim);
		x += 130;
		char d[16];
		fmt_dur(el, d, sizeof d);
		draw_text(ui.font, x, y, d, p->text);
	} else {
		draw_text(ui.font, x, y, "STANDBY", mix(p->text, p->bg, 45));
	}
	if (app.qn > (app.playing ? 1 : 0)) {
		snprintf(t, sizeof t, "QUEUE %d", app.qn - (app.playing ? 1 : 0));
		int w = text_width(ui.font, t);
		draw_text(ui.font, r.x + r.w - w - PAD, y, t, p->urgent);
	}
}

static void draw_input(void) {
	syn_palette *p = &ui.pal;
	rect r = ui.r_input;
	fill(r.x, r.y, r.w, r.h, p->bg);
	frame_rect(r.x + PAD / 2, r.y + 2, r.w - PAD, r.h - 4, 1, ui.input_len ? p->accent : p->border);
	int x = r.x + PAD + 2;
	int y = r.y + (r.h - ui.lh) / 2;
	x += draw_text(ui.font, x, y, "> ", p->accent);
	int avail = r.x + r.w - PAD - 2 - x;
	if (ui.input_len == 0) {
		draw_text_fit(ui.font, x, y, "type a message, or /help", mix(p->text, p->bg, 55), avail);
		fill(x, y, 2, ui.lh, p->accent);
		return;
	}
	/* keep the caret visible: drop characters from the left */
	size_t start = 0;
	while (start < ui.caret && text_width_n(ui.font, ui.input + start, ui.caret - start) > avail - ui.cw) {
		uint32_t cp;
		start += utf8_next(ui.input + start, ui.input_len - start, &cp);
	}
	draw_text_n(ui.font, x, y, ui.input + start, ui.input_len - start, p->text, avail);
	int cx = x + text_width_n(ui.font, ui.input + start, ui.caret - start);
	fill(cx, y, 2, ui.lh, p->accent);
}

static bool in_rect(rect r, double x, double y) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void draw_ptt(void) {
	syn_palette *p = &ui.pal;
	rect r = ui.r_ptt;
	fill(r.x, r.y, r.w, r.h, p->bg);
	rect b = {r.x + PAD / 2, r.y + 2, r.w - PAD, r.h - PAD};
	talk_state st = app_talk_state();
	bool hover = ui.ptr_in && in_rect(b, ui.px, ui.py);
	syn_rgb bg = p->panel, fg = p->text, edge = p->accent;
	char label[300], sub[300] = "";
	const char *dest = app.n_chan ? app.chan[app.active].name : "";
	switch (st) {
	case TB_DISABLED:
		bg = p->bg;
		fg = mix(p->text, p->bg, 50);
		edge = p->border;
		snprintf(label, sizeof label, "OFFLINE");
		if (app.offline) {
			snprintf(sub, sizeof sub, "F6 goes online");
		} else if (app.n_chan) {
			snprintf(sub, sizeof sub, "%s", app_session_label(app.active));
		}
		break;
	case TB_IDLE:
		bg = hover ? p->panel_hover : p->panel;
		snprintf(label, sizeof label, "%s SPACE TO TALK", app.cfg.ptt_toggle ? "PRESS" : "HOLD");
		snprintf(sub, sizeof sub, "-> %s", dest);
		break;
	case TB_CONNECTING:
		bg = p->accent_dim;
		snprintf(label, sizeof label, "CONNECTING…");
		snprintf(sub, sizeof sub, "wait for the beep");
		break;
	case TB_SENDING:
		bg = p->accent;
		fg = ink_on(p->accent);
		edge = p->text;
		snprintf(label, sizeof label, "TALKING");
		snprintf(sub, sizeof sub, "-> %s", app.chan[app.tx.channel].name);
		break;
	case TB_RECEIVING: {
		int lv, el;
		bg = p->panel_hover;
		snprintf(label, sizeof label, "RECEIVING  %s", app_rx_name(&lv, &el));
		snprintf(sub, sizeof sub, "ESC stops playback");
		break;
	}
	}
	fill(b.x, b.y, b.w, b.h, bg);
	frame_rect(b.x, b.y, b.w, b.h, 2, edge);
	int lw = text_width(ui.font_big, label);
	int ly = b.y + (b.h - ui.font_big->height - ui.lh) / 2;
	draw_text_fit(ui.font_big, b.x + (b.w - lw) / 2 > b.x + PAD ? b.x + (b.w - lw) / 2 : b.x + PAD, ly, label, fg, b.w - 2 * PAD);
	int sw = text_width(ui.font, sub);
	draw_text_fit(ui.font, b.x + (b.w - sw) / 2 > b.x + PAD ? b.x + (b.w - sw) / 2 : b.x + PAD,
		ly + ui.font_big->height, sub, mix(fg, bg, 25), b.w - 2 * PAD);
	if (st == TB_SENDING) {
		meter(b.x + PAD, b.y + b.h - 8, b.w - 2 * PAD, 4, app.tx.say_pcm ? app.tx.level : capture_level(),
			fg, mix(p->accent, p->bg, 40));
	}
}

static void draw_footer(rect r) {
	syn_palette *p = &ui.pal;
	fill(r.x, r.y, r.w, r.h, p->panel);
	fill(r.x, r.y, r.w, 1, p->border);
	int y = r.y + (r.h - ui.lh) / 2;
	if (app.toast[0]) {
		draw_text_fit(ui.font, r.x + PAD, y, app.toast, p->text, r.w - 2 * PAD);
		return;
	}
	draw_text_fit(ui.font, r.x + PAD, y,
		"SPACE talk  ENTER send/replay  ^R last  ESC stop  TAB channel  "
		"F2 speed  F3 busy  F4 mute  F5 vox  F6 offline  ^Q quit",
		mix(p->text, p->bg, 40), r.w - 2 * PAD);
}

static void layout(void) {
	int head = ui.lh + PAD;
	int foot = ui.lh + PAD;
	int side_w = ui.cw * 26;
	if (side_w > ui.w / 3) side_w = ui.w / 3;
	int ptt_h = ui.font_big->height + ui.lh + 3 * PAD;
	int input_h = ui.lh + 2 * PAD;
	int onair_h = ui.lh + PAD;
	int main_x = side_w, main_w = ui.w - side_w;
	ui.r_side = (rect){0, head, side_w, ui.h - head - foot};
	int y = ui.h - foot - ptt_h;
	ui.r_ptt = (rect){main_x, y, main_w, ptt_h};
	y -= input_h;
	ui.r_input = (rect){main_x, y, main_w, input_h};
	y -= onair_h;
	ui.r_hist = (rect){main_x, head, main_w, y - head};
}

static void draw_all(void) {
	layout();
	fill(0, 0, ui.w, ui.h, ui.pal.bg);
	draw_header();
	draw_sidebar();
	draw_history();
	draw_onair((rect){ui.r_hist.x, ui.r_hist.y + ui.r_hist.h, ui.r_hist.w, ui.r_input.y - ui.r_hist.y - ui.r_hist.h});
	draw_input();
	draw_ptt();
	draw_footer((rect){0, ui.h - ui.lh - PAD, ui.w, ui.lh + PAD});
}

/* ---------- buffers ---------- */

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
	size_t stride = (size_t)w * 4;
	size_t size = stride * (size_t)h;
	int fd = memfd_create("syn-cast", MFD_CLOEXEC);
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
	b->busy = false;
	return 0;
}

static buffer *next_buffer(void) {
	for (int i = 0; i < 2; i++) {
		buffer *b = &ui.buf[i];
		if (b->busy) {
			continue;
		}
		if (b->w != ui.w || b->h != ui.h) {
			buffer_destroy(b);
			if (buffer_create(b, ui.w, ui.h) != 0) {
				return NULL;
			}
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

static bool animating(void) {
	return app.tx.phase != TX_IDLE || app.playing;
}

void ui_frame(void) {
	if (!ui.configured || ui.frame_pending) {
		return;
	}
	int64_t now = now_ms();
	bool tick = now - ui.last_draw >= 1000;  /* clocks, retry countdowns */
	if (!ui.dirty && !app.dirty && !(animating() && now - ui.last_draw >= 40) && !tick) {
		return;
	}
	buffer *b = next_buffer();
	if (!b) {
		return;
	}
	dst = b->pix;
	draw_all();
	dst = NULL;
	if (app.snapshot[0]) {
		FILE *f = fopen(app.snapshot, "wb");
		if (f) {
			fprintf(f, "P6 %d %d 255\n", ui.w, ui.h);
			for (int k = 0; k < ui.w * ui.h; k++) {
				uint32_t px = b->data[k];
				uint8_t rgb[3] = {(uint8_t)(px >> 16), (uint8_t)(px >> 8), (uint8_t)px};
				fwrite(rgb, 1, 3, f);
			}
			fclose(f);
		}
		app.snapshot[0] = 0;
	}
	wl_surface_attach(ui.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(ui.surface, 0, 0, ui.w, ui.h);
	struct wl_callback *cb = wl_surface_frame(ui.surface);
	wl_callback_add_listener(cb, &frame_listener, NULL);
	wl_surface_commit(ui.surface);
	b->busy = true;
	ui.frame_pending = true;
	ui.dirty = false;
	app.dirty = false;
	ui.last_draw = now;
}

int ui_timeout_ms(void) {
	if (animating()) {
		return 40;
	}
	int64_t left = 1000 - (now_ms() - ui.last_draw);
	return left < 0 ? 0 : (int)left;
}

/* ---------- clipboard ---------- */

static void source_target(void *d, struct wl_data_source *s, const char *m) {
	(void)d; (void)s; (void)m;
}

static void source_send(void *d, struct wl_data_source *s, const char *mime, int32_t fd) {
	(void)d; (void)s; (void)mime;
	if (ui.copy_text) {
		size_t n = strlen(ui.copy_text);
		const char *p = ui.copy_text;
		while (n) {
			ssize_t w = write(fd, p, n);
			if (w <= 0) break;
			p += w;
			n -= (size_t)w;
		}
	}
	close(fd);
}

static void source_cancelled(void *d, struct wl_data_source *s) {
	(void)d;
	wl_data_source_destroy(s);
	if (ui.source == s) {
		ui.source = NULL;
	}
}

static void source_noop(void *d, struct wl_data_source *s) {
	(void)d; (void)s;
}

static void source_action(void *d, struct wl_data_source *s, uint32_t a) {
	(void)d; (void)s; (void)a;
}

static const struct wl_data_source_listener source_listener = {
	.target = source_target,
	.send = source_send,
	.cancelled = source_cancelled,
	.dnd_drop_performed = source_noop,
	.dnd_finished = source_noop,
	.action = source_action,
};

static void copy_text(const char *text) {
	if (!ui.ddm || !ui.ddev) {
		app_toast("no clipboard on this compositor");
		return;
	}
	free(ui.copy_text);
	ui.copy_text = strdup(text);
	ui.source = wl_data_device_manager_create_data_source(ui.ddm);
	wl_data_source_add_listener(ui.source, &source_listener, NULL);
	wl_data_source_offer(ui.source, "text/plain;charset=utf-8");
	wl_data_source_offer(ui.source, "text/plain");
	wl_data_source_offer(ui.source, "UTF8_STRING");
	wl_data_device_set_selection(ui.ddev, ui.source, ui.kb_serial);
	app_toast("copied");
}

static void offer_offer(void *d, struct wl_data_offer *o, const char *mime) {
	(void)d;
	/* user data marks an offer that has text */
	if (strcmp(mime, "text/plain;charset=utf-8") == 0 || strcmp(mime, "text/plain") == 0 ||
		strcmp(mime, "UTF8_STRING") == 0) {
		wl_data_offer_set_user_data(o, (void *)1);
	}
}

static void offer_actions(void *d, struct wl_data_offer *o, uint32_t a) {
	(void)d; (void)o; (void)a;
}

static const struct wl_data_offer_listener offer_listener = {
	.offer = offer_offer,
	.source_actions = offer_actions,
	.action = offer_actions,
};

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *o) {
	(void)d; (void)dd;
	wl_data_offer_add_listener(o, &offer_listener, NULL);
}

static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *s,
	wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o) {
	(void)d; (void)dd; (void)serial; (void)s; (void)x; (void)y;
	if (o) {
		wl_data_offer_destroy(o); /* no drag and drop */
	}
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
	if (ui.offer && ui.offer != o) {
		wl_data_offer_destroy(ui.offer);
	}
	ui.offer = o;
}

static const struct wl_data_device_listener dd_listener = {
	.data_offer = dd_data_offer,
	.enter = dd_enter,
	.leave = dd_leave,
	.motion = dd_motion,
	.drop = dd_drop,
	.selection = dd_selection,
};

static void paste(void) {
	if (!ui.offer || !wl_data_offer_get_user_data(ui.offer) || ui.clip_fd >= 0) {
		return;
	}
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0) {
		return;
	}
	wl_data_offer_receive(ui.offer, "text/plain;charset=utf-8", fds[1]);
	close(fds[1]);
	wl_display_flush(ui.display);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	ui.clip_fd = fds[0];
	ui.clip_len = 0;
}

static void insert_text(const char *s, size_t n);

void ui_on_clip(void) {
	char buf[4096];
	for (;;) {
		ssize_t r = read(ui.clip_fd, buf, sizeof buf);
		if (r > 0) {
			ui.clip_buf = realloc(ui.clip_buf, ui.clip_len + (size_t)r + 1);
			memcpy(ui.clip_buf + ui.clip_len, buf, (size_t)r);
			ui.clip_len += (size_t)r;
			continue;
		}
		if (r < 0 && errno == EAGAIN) {
			return;
		}
		break;
	}
	close(ui.clip_fd);
	ui.clip_fd = -1;
	for (size_t i = 0; i < ui.clip_len; i++) {
		if (ui.clip_buf[i] == '\n' || ui.clip_buf[i] == '\r' || ui.clip_buf[i] == '\t') {
			ui.clip_buf[i] = ' ';
		}
	}
	if (ui.clip_len) {
		insert_text(ui.clip_buf, ui.clip_len);
	}
	free(ui.clip_buf);
	ui.clip_buf = NULL;
	ui.clip_len = 0;
}

int ui_clip_fd(void) {
	return ui.clip_fd;
}

/* ---------- actions ---------- */

static void insert_text(const char *s, size_t n) {
	if (ui.input_len + n >= sizeof ui.input) {
		n = sizeof ui.input - 1 - ui.input_len;
	}
	memmove(ui.input + ui.caret + n, ui.input + ui.caret, ui.input_len - ui.caret);
	memcpy(ui.input + ui.caret, s, n);
	ui.input_len += n;
	ui.caret += n;
	ui.input[ui.input_len] = 0;
	ui.dirty = true;
}

static void delete_before(void) {
	size_t k = utf8_prev_len(ui.input, ui.caret);
	if (!k) return;
	memmove(ui.input + ui.caret - k, ui.input + ui.caret, ui.input_len - ui.caret);
	ui.caret -= k;
	ui.input_len -= k;
	ui.input[ui.input_len] = 0;
}

static void delete_after(void) {
	if (ui.caret >= ui.input_len) return;
	uint32_t cp;
	size_t k = utf8_next(ui.input + ui.caret, ui.input_len - ui.caret, &cp);
	memmove(ui.input + ui.caret, ui.input + ui.caret + k, ui.input_len - ui.caret - k);
	ui.input_len -= k;
	ui.input[ui.input_len] = 0;
}

static hmsg *selected_msg(void) {
	if (app.n_chan == 0 || ui.sel < 0 || ui.sel >= (int)app.chan[app.active].hist.n) {
		return NULL;
	}
	return &app.chan[app.active].hist.v[ui.sel];
}

static void open_url(const char *url) {
	char *argv[] = {"xdg-open", (char *)url, NULL};
	spawn_detached(argv);
}

static void activate(void) {
	hmsg *m = selected_msg();
	if (!m) {
		app_replay_last();
		return;
	}
	char path[1200];
	switch (m->kind) {
	case HM_VOICE:
		app_replay(app.active, m->id);
		break;
	case HM_IMAGE:
		history_image_path(&app.chan[app.active].hist, m->id, false, path, sizeof path);
		if (access(path, R_OK) == 0) {
			open_url(path);
		}
		break;
	case HM_LOCATION:
		snprintf(path, sizeof path, "https://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f#map=16/%.6f/%.6f",
			m->lat, m->lon, m->lat, m->lon);
		open_url(path);
		break;
	case HM_TEXT:
		if (m->text) copy_text(m->text);
		break;
	default:
		break;
	}
}

static void move_sel(int d) {
	if (app.n_chan == 0) return;
	int n = (int)app.chan[app.active].hist.n;
	if (n == 0) return;
	if (ui.sel < 0) {
		ui.sel = d < 0 ? n - 1 : n - 1;
	} else {
		ui.sel += d;
	}
	if (ui.sel < 0) ui.sel = 0;
	if (ui.sel >= n) ui.sel = n - 1;
	/* keep it in view: scroll so the selection is at or above the bottom */
	if (n - 1 - ui.sel < ui.scroll) {
		ui.scroll = n - 1 - ui.sel;
	}
	bool visible = false;
	for (int i = 0; i < ui.nhits; i++) {
		if (ui.hits[i].idx == ui.sel && ui.hits[i].y0 >= ui.r_hist.y) visible = true;
	}
	if (!visible && d < 0) {
		ui.scroll = n - 1 - ui.sel - 2 > 0 ? n - 1 - ui.sel - 2 : 0;
	}
	ui.dirty = true;
}

static void switch_channel(int k) {
	if (k < 0 || k >= app.n_chan) return;
	app_select_channel(k);
	ui.sel = -1;
	ui.scroll = 0;
	ui.dirty = true;
}

static void ptt_press(void) {
	if (app.cfg.ptt_toggle) app_ptt_toggle();
	else app_ptt_down();
}

static void ptt_release(void) {
	if (!app.cfg.ptt_toggle) app_ptt_up();
}

static bool ctrl(void) {
	return ui.xkb_state && xkb_state_mod_name_is_active(ui.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
}

static bool shift(void) {
	return ui.xkb_state && xkb_state_mod_name_is_active(ui.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
}

/* Returns true if the key may repeat. */
static bool key_action(uint32_t key, bool repeat) {
	xkb_keysym_t sym = xkb_state_key_get_one_sym(ui.xkb_state, key + 8);
	bool c = ctrl();
	ui.dirty = true;
	if (sym == XKB_KEY_space && (c || ui.input_len == 0)) {
		if (!repeat && !ui.ptt_key) {
			ui.ptt_key = true;
			ptt_press();
		}
		return false;
	}
	if (c) {
		if (sym == XKB_KEY_q || sym == XKB_KEY_Q) app.quit = true;
		else if (sym == XKB_KEY_r || sym == XKB_KEY_R) app_replay_last();
		else if (sym == XKB_KEY_v || sym == XKB_KEY_V) paste();
		else if (sym == XKB_KEY_c || sym == XKB_KEY_C) {
			hmsg *m = selected_msg();
			if (ui.input_len) copy_text(ui.input);
			else if (m && m->text) copy_text(m->text);
		} else if (sym == XKB_KEY_u || sym == XKB_KEY_U) {
			ui.input_len = ui.caret = 0;
			ui.input[0] = 0;
		} else if (sym == XKB_KEY_BackSpace) {
			while (ui.caret > 0 && ui.input[ui.caret - 1] == ' ') delete_before();
			while (ui.caret > 0 && ui.input[ui.caret - 1] != ' ') delete_before();
			return true;
		} else if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
			switch_channel((int)(sym - XKB_KEY_1));
		}
		return false;
	}
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (ui.input_len) {
			char line[sizeof ui.input];
			memcpy(line, ui.input, ui.input_len + 1);
			ui.input_len = ui.caret = 0;
			ui.input[0] = 0;
			app_input(line);
			ui.sel = -1;
			ui.scroll = 0;
		} else {
			activate();
		}
		return false;
	case XKB_KEY_BackSpace:
		delete_before();
		return true;
	case XKB_KEY_Delete:
		delete_after();
		return true;
	case XKB_KEY_Left:
		ui.caret -= utf8_prev_len(ui.input, ui.caret);
		return true;
	case XKB_KEY_Right:
		if (ui.caret < ui.input_len) {
			uint32_t cp;
			ui.caret += utf8_next(ui.input + ui.caret, ui.input_len - ui.caret, &cp);
		}
		return true;
	case XKB_KEY_Home:
		if (ui.input_len) ui.caret = 0;
		else if (app.n_chan) {
			ui.sel = 0;
			ui.scroll = (int)app.chan[app.active].hist.n - 1;
		}
		return false;
	case XKB_KEY_End:
		if (ui.input_len) ui.caret = ui.input_len;
		else {
			ui.sel = -1;
			ui.scroll = 0;
		}
		return false;
	case XKB_KEY_Up:
		move_sel(-1);
		return true;
	case XKB_KEY_Down:
		move_sel(1);
		return true;
	case XKB_KEY_Prior:
		ui.scroll += 5;
		return true;
	case XKB_KEY_Next:
		ui.scroll = ui.scroll > 5 ? ui.scroll - 5 : 0;
		return true;
	case XKB_KEY_Escape:
		if (ui.input_len) {
			ui.input_len = ui.caret = 0;
			ui.input[0] = 0;
		} else if (app.playing) {
			app_stop_playback();
		} else if (ui.sel >= 0) {
			ui.sel = -1;
		}
		return false;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		if (app.n_chan > 1) {
			int d = (sym == XKB_KEY_ISO_Left_Tab || shift()) ? app.n_chan - 1 : 1;
			switch_channel((app.active + d) % app.n_chan);
		}
		return false;
	case XKB_KEY_F2:
		app_cycle_speed();
		return false;
	case XKB_KEY_F3:
		app_toggle_busy();
		return false;
	case XKB_KEY_F4:
		app_toggle_mute();
		return false;
	case XKB_KEY_F5:
		app_toggle_vox();
		return false;
	case XKB_KEY_F6:
		app_set_offline(!app.offline);
		return false;
	default:
		break;
	}
	char utf8[16];
	int n = xkb_state_key_get_utf8(ui.xkb_state, key + 8, utf8, sizeof utf8);
	if (n > 0 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7F) {
		insert_text(utf8, (size_t)n);
		return true;
	}
	return false;
}

static void key_release(uint32_t key) {
	xkb_keysym_t sym = xkb_state_key_get_one_sym(ui.xkb_state, key + 8);
	if (sym == XKB_KEY_space && ui.ptt_key) {
		ui.ptt_key = false;
		ptt_release();
	}
}

static void arm_repeat(uint32_t key) {
	if (ui.repeat_fd < 0 || ui.repeat_rate <= 0) {
		return;
	}
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
	if (ui.repeat_fd >= 0) {
		timerfd_settime(ui.repeat_fd, 0, &ts, NULL);
	}
}

void ui_on_repeat(void) {
	uint64_t n;
	if (read(ui.repeat_fd, &n, sizeof n) != sizeof n || !ui.repeat_key) {
		return;
	}
	key_action(ui.repeat_key, true);
}

int ui_repeat_fd(void) {
	return ui.repeat_fd;
}

/* ---------- keyboard ---------- */

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size) {
	(void)d; (void)k;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		return;
	}
	struct xkb_keymap *km = xkb_keymap_new_from_string(ui.xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (!km) {
		return;
	}
	xkb_state_unref(ui.xkb_state);
	xkb_keymap_unref(ui.keymap);
	ui.keymap = km;
	ui.xkb_state = xkb_state_new(km);
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys) {
	(void)d; (void)k; (void)s; (void)keys;
	ui.kb_serial = serial;
	app.focused = true;
	syn_theme_load(&ui.pal); /* pick up a theme switched while away */
	ui.dirty = true;
}

static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s) {
	(void)d; (void)k; (void)serial; (void)s;
	app.focused = false;
	disarm_repeat();
	if (ui.ptt_key) {
		/* the release would go to another window: never leave the mic open */
		ui.ptt_key = false;
		ptt_release();
	}
	ui.dirty = true;
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state) {
	(void)d; (void)k; (void)t;
	ui.kb_serial = serial;
	if (!ui.xkb_state) {
		return;
	}
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		bool rep = key_action(key, false);
		if (rep && xkb_keymap_key_repeats(ui.keymap, key + 8)) {
			arm_repeat(key);
		} else {
			disarm_repeat();
		}
	} else {
		if (key == ui.repeat_key) {
			disarm_repeat();
		}
		key_release(key);
	}
}

static void kb_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
	(void)d; (void)k; (void)serial;
	if (ui.xkb_state) {
		xkb_state_update_mask(ui.xkb_state, dep, lat, lock, 0, 0, group);
	}
}

static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {
	(void)d; (void)k;
	ui.repeat_rate = rate;
	ui.repeat_delay = delay;
}

static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_mods,
	.repeat_info = kb_repeat,
};

/* ---------- pointer ---------- */

static void set_cursor(uint32_t serial) {
	if (!ui.cursor_theme) {
		return;
	}
	struct wl_cursor *c = wl_cursor_theme_get_cursor(ui.cursor_theme, "default");
	if (!c) c = wl_cursor_theme_get_cursor(ui.cursor_theme, "left_ptr");
	if (!c || c->image_count == 0) {
		return;
	}
	struct wl_cursor_image *img = c->images[0];
	wl_pointer_set_cursor(ui.pointer, serial, ui.cursor_surface, (int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
	wl_surface_attach(ui.cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
	wl_surface_damage_buffer(ui.cursor_surface, 0, 0, (int32_t)img->width, (int32_t)img->height);
	wl_surface_commit(ui.cursor_surface);
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
	(void)d; (void)p; (void)s;
	ui.ptr_in = true;
	ui.px = wl_fixed_to_double(x);
	ui.py = wl_fixed_to_double(y);
	set_cursor(serial);
	ui.dirty = true;
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
	(void)d; (void)p; (void)serial; (void)s;
	ui.ptr_in = false;
	ui.dirty = true;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
	(void)d; (void)p; (void)t;
	bool was = in_rect(ui.r_ptt, ui.px, ui.py);
	ui.px = wl_fixed_to_double(x);
	ui.py = wl_fixed_to_double(y);
	if (was != in_rect(ui.r_ptt, ui.px, ui.py)) {
		ui.dirty = true;
	}
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state) {
	(void)d; (void)p; (void)t;
	ui.ptr_serial = serial;
	bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
	if (button != BTN_LEFT) {
		if (button == BTN_RIGHT && down && in_rect(ui.r_hist, ui.px, ui.py)) {
			/* right click: replay or open, like Enter */
			for (int i = 0; i < ui.nhits; i++) {
				if (ui.py >= ui.hits[i].y0 && ui.py < ui.hits[i].y1) {
					ui.sel = ui.hits[i].idx;
					activate();
				}
			}
		}
		return;
	}
	if (!down) {
		if (ui.ptt_mouse) {
			ui.ptt_mouse = false;
			ptt_release();
		}
		return;
	}
	ui.dirty = true;
	if (in_rect(ui.r_ptt, ui.px, ui.py)) {
		ui.ptt_mouse = true;
		ptt_press();
		return;
	}
	if (in_rect(ui.r_side, ui.px, ui.py)) {
		for (int i = 0; i < ui.nchits; i++) {
			if (ui.py >= ui.chits[i].y0 && ui.py < ui.chits[i].y1) {
				switch_channel(ui.chits[i].ch);
			}
		}
		return;
	}
	if (in_rect(ui.r_hist, ui.px, ui.py)) {
		for (int i = 0; i < ui.nhits; i++) {
			if (ui.py >= ui.hits[i].y0 && ui.py < ui.hits[i].y1) {
				int idx = ui.hits[i].idx;
				int64_t now = now_ms();
				if (idx == ui.last_click_idx && now - ui.last_click < DOUBLE_CLICK_MS) {
					ui.sel = idx;
					activate();
					ui.last_click = 0;
				} else {
					ui.sel = idx;
					ui.last_click = now;
					ui.last_click_idx = idx;
				}
			}
		}
	}
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v) {
	(void)d; (void)p; (void)t;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !in_rect(ui.r_hist, ui.px, ui.py)) {
		return;
	}
	double dv = wl_fixed_to_double(v);
	int steps = dv > 0 ? -1 : 1; /* wheel up = older */
	ui.scroll += steps;
	if (ui.scroll < 0) ui.scroll = 0;
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
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
	.frame = ptr_frame,
	.axis_source = ptr_axis_source,
	.axis_stop = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
};

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
	int w = ui.want_w > 0 ? ui.want_w : app.cfg.width;
	int h = ui.want_h > 0 ? ui.want_h : app.cfg.height;
	if (w != ui.w || h != ui.h) {
		ui.w = w;
		ui.h = h;
	}
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
	.configure = tl_configure,
	.close = tl_close,
	.configure_bounds = tl_bounds,
	.wm_capabilities = tl_caps,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
	(void)d;
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		ui.compositor = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		ui.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		ui.wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(ui.wm_base, &wm_listener, NULL);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !ui.seat) {
		ui.seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
		wl_seat_add_listener(ui.seat, &seat_listener, NULL);
	} else if (strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
		ui.deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
	} else if (strcmp(iface, wl_data_device_manager_interface.name) == 0) {
		ui.ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, ver < 3 ? ver : 3);
	}
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {
	(void)d; (void)r; (void)name;
}

static const struct wl_registry_listener reg_listener = {.global = reg_global, .global_remove = reg_remove};

static struct fcft_font *load_font(const char *spec, int bump) {
	char name[300];
	str_copy(name, sizeof name, spec);
	if (bump) {
		/* "Family:size=N" -> N + bump */
		char *sz = strstr(name, "size=");
		if (sz) {
			int n = atoi(sz + 5);
			snprintf(sz, sizeof name - (size_t)(sz - name), "size=%d", n + bump);
		}
	}
	char fallback[64];
	char *sz = strstr(name, "size=");
	snprintf(fallback, sizeof fallback, "monospace:%s", sz ? sz : "size=11");
	const char *names[] = {name, fallback};
	return fcft_from_name(2, names, NULL);
}

int ui_init(void) {
	ui.display = wl_display_connect(NULL);
	if (!ui.display) {
		return -1;
	}
	ui.running = true;
	ui.registry = wl_display_get_registry(ui.display);
	wl_registry_add_listener(ui.registry, &reg_listener, NULL);
	wl_display_roundtrip(ui.display);
	if (!ui.compositor || !ui.shm || !ui.wm_base) {
		LOG("the compositor lacks wl_compositor, wl_shm or xdg_wm_base");
		return -1;
	}
	ui.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	ui.font = load_font(app.cfg.font, 0);
	ui.font_big = load_font(app.cfg.font, 6);
	if (!ui.font || !ui.font_big) {
		LOG("no usable font for \"%s\"", app.cfg.font);
		return -1;
	}
	ui.lh = ui.font->height;
	ui.cw = ui.font->max_advance.x > 0 ? ui.font->max_advance.x : ui.lh / 2;
	syn_theme_load(&ui.pal);
	ui.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (ui.ddm && ui.seat) {
		ui.ddev = wl_data_device_manager_get_data_device(ui.ddm, ui.seat);
		wl_data_device_add_listener(ui.ddev, &dd_listener, NULL);
	}
	ui.cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"),
		getenv("XCURSOR_SIZE") ? atoi(getenv("XCURSOR_SIZE")) : 24, ui.shm);
	ui.cursor_surface = wl_compositor_create_surface(ui.compositor);

	ui.surface = wl_compositor_create_surface(ui.compositor);
	ui.xdg_surface = xdg_wm_base_get_xdg_surface(ui.wm_base, ui.surface);
	xdg_surface_add_listener(ui.xdg_surface, &xs_listener, NULL);
	ui.toplevel = xdg_surface_get_toplevel(ui.xdg_surface);
	xdg_toplevel_add_listener(ui.toplevel, &tl_listener, NULL);
	xdg_toplevel_set_title(ui.toplevel, "SYN-CAST");
	xdg_toplevel_set_app_id(ui.toplevel, "syn-cast");
	xdg_toplevel_set_min_size(ui.toplevel, 640, 420);
	if (ui.deco_mgr) {
		/* the compositor's frame; with no decoration manager there is none,
		 * as before */
		ui.deco = zxdg_decoration_manager_v1_get_toplevel_decoration(ui.deco_mgr, ui.toplevel);
		zxdg_toplevel_decoration_v1_set_mode(ui.deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	wl_surface_commit(ui.surface);
	wl_display_roundtrip(ui.display);
	return 0;
}

void ui_shutdown(void) {
	if (!ui.display) {
		return;
	}
	for (int i = 0; i < 2; i++) buffer_destroy(&ui.buf[i]);
	for (size_t i = 0; i < ARRAY_LEN(ui.thumbs); i++) {
		if (ui.thumbs[i].img) pixman_image_unref(ui.thumbs[i].img);
		free(ui.thumbs[i].px);
	}
	if (ui.deco) zxdg_toplevel_decoration_v1_destroy(ui.deco);
	if (ui.toplevel) xdg_toplevel_destroy(ui.toplevel);
	if (ui.xdg_surface) xdg_surface_destroy(ui.xdg_surface);
	if (ui.surface) wl_surface_destroy(ui.surface);
	if (ui.font) fcft_destroy(ui.font);
	if (ui.font_big) fcft_destroy(ui.font_big);
	fcft_fini();
	if (ui.repeat_fd >= 0) close(ui.repeat_fd);
	if (ui.clip_fd >= 0) close(ui.clip_fd);
	free(ui.copy_text);
	wl_display_flush(ui.display);
	wl_display_disconnect(ui.display);
	ui.display = NULL;
}

bool ui_running(void) {
	return ui.running;
}

int ui_display_fd(void) {
	return wl_display_get_fd(ui.display);
}

static bool reading;

void ui_before_poll(void) {
	while (wl_display_prepare_read(ui.display) != 0) {
		wl_display_dispatch_pending(ui.display);
	}
	reading = true;
	wl_display_flush(ui.display);
}

void ui_after_poll(short revents) {
	if (!reading) {
		return;
	}
	reading = false;
	if (revents & POLLIN) {
		if (wl_display_read_events(ui.display) != 0) {
			ui.running = false;
			return;
		}
	} else {
		wl_display_cancel_read(ui.display);
	}
	if (wl_display_dispatch_pending(ui.display) < 0) {
		ui.running = false;
	}
	if (revents & (POLLERR | POLLHUP)) {
		ui.running = false;
	}
}
