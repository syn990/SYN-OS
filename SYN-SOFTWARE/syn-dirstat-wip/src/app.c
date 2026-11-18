/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "disks.h"
#include "draw.h"
#include "gl.h"
#include "scan.h"
#include "scene.h"
#include "theme.h"
#include "vmath.h"

#define FOV 0.8f /* the solid view's angle, top to bottom, in radians */

typedef enum { SCREEN_DISKS, SCREEN_SCANNING, SCREEN_TREE } Screen;
typedef enum { VIEW_BLOCKS, VIEW_GRAPH } View;
typedef enum { SORT_NAME, SORT_SIZE, SORT_FILES, SORT_CHANGED } SortKey;

typedef struct {
	float x, y, w, h;
} Rect;

typedef struct {
	Node *node;
	int depth;
} Row;

enum { B_DISKS, B_UP, B_RESCAN, B_PATH, B_BLOCKS, B_GRAPH, B_FLAT, B_SOLID, B_THEME, B_COUNT };

typedef struct {
	SDL_Window *win;
	int w, h;     /* the window in pixels */
	float dpi;    /* pixels per mouse unit */
	int text_h, char_w;
	const AppOptions *opt;
	bool quit;

	Theme *themes;
	int nthemes, theme;

	Screen screen;
	Disk disks[32];
	int ndisks;
	char message[1024]; /* why the last scan failed */

	/* the scan in its thread */
	SDL_Thread *thread;
	ScanProgress progress;
	SDL_atomic_t scan_done;
	char scan_path[4096], scan_err[1024];
	Node *scan_result;

	Node *root, *focus, *sel, *hover;
	View view;
	bool solid;
	SortKey sort;
	bool sort_desc;
	Row *rows;
	int nrows, rows_cap, scroll;

	Scene scene;
	float scene_w, scene_h; /* the view size the scene was built for; 0 when stale */
	float yaw, pitch, dist;

	bool typing; /* a path is being typed into the bar */
	char entry[4096];

	float mx, my;
	bool pressed, dragged; /* the left button is down in the view; it has moved since */
	float press_x, press_y;
	int settled; /* frames drawn since the last scan ended, for --shot */
} App;

static const Theme *theme(const App *a) { return &a->themes[a->theme]; }

static bool in_rect(Rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static SDL_Color lighter(SDL_Color c, float t) { return mix(c, (SDL_Color){255, 255, 255, 255}, t); }
static SDL_Color darker(SDL_Color c, float t) { return mix(c, (SDL_Color){0, 0, 0, 255}, t); }

/* the accent as text: on a dark theme the accent itself is often too dark to read */
static SDL_Color accent_text(const Theme *t) { return t->light ? rgb(t->accent) : lighter(rgb(t->accent), 0.35f); }

/* black or white, whichever reads on the colour */
static SDL_Color ink(SDL_Color on) {
	bool bright = on.r * 3 + on.g * 6 + on.b > 1400;
	return bright ? (SDL_Color){0, 0, 0, 255} : (SDL_Color){255, 255, 255, 255};
}

static void format_date(int64_t t, char *buf, int len) {
	time_t tt = (time_t)t;
	struct tm *tm = localtime(&tt);
	if (!tm || !strftime(buf, (size_t)len, "%Y-%m-%d", tm)) {
		snprintf(buf, (size_t)len, "-");
	}
}

/* ======================================================================
 *   Where things are
 * ====================================================================== */

static Rect bar_rect(const App *a) { return (Rect){0, 0, (float)a->w, (float)a->text_h + 12}; }
static Rect status_rect(const App *a) {
	float h = (float)a->text_h * 2 + 14; /* what the mouse is on, and under it what the colours mean */
	return (Rect){0, (float)a->h - h, (float)a->w, h};
}
static Rect list_rect(const App *a) {
	Rect bar = bar_rect(a), st = status_rect(a);
	float w = floorf((float)a->w * 0.42f);
	return (Rect){0, bar.h, w, st.y - bar.h};
}
static Rect view_rect(const App *a) {
	Rect l = list_rect(a);
	return (Rect){l.w + 1, l.y, (float)a->w - l.w - 1, l.h};
}
static float row_h(const App *a) { return (float)a->text_h + 4; }
static int rows_fit(const App *a) { return (int)(list_rect(a).h / row_h(a)) - 1; }

static const char *button_label(const App *a, int b, char *buf, int len) {
	static const char *PLAIN[B_COUNT] = {"DISKS", "UP", "RESCAN", "", "BLOCKS", "GRAPH", "2D", "3D", ""};
	if (b == B_THEME) {
		const char *name = theme(a)->name;
		snprintf(buf, (size_t)len, "%s", strncmp(name, "SYN-OS-", 7) ? name : name + 7);
		return buf;
	}
	return PLAIN[b];
}

/* The bar's buttons: three at the left, five at the right, the path between. */
static void bar_layout(const App *a, Rect out[B_COUNT]) {
	Rect bar = bar_rect(a);
	float pad = (float)a->char_w, x = 4, right = bar.w - 4;
	char buf[64];
	for (int b = B_DISKS; b <= B_RESCAN; b++) {
		float w = (float)d2_text_width(button_label(a, b, buf, sizeof buf)) + 2 * pad;
		out[b] = (Rect){x, 3, w, bar.h - 6};
		x += w + 4;
	}
	for (int b = B_THEME; b >= B_BLOCKS; b--) {
		float w = (float)d2_text_width(button_label(a, b, buf, sizeof buf)) + 2 * pad;
		right -= w;
		out[b] = (Rect){right, 3, w, bar.h - 6};
		right -= b == B_FLAT || b == B_THEME ? 12 : 4;
	}
	out[B_PATH] = (Rect){x + 4, 3, right - x - 8, bar.h - 6};
}

/* ======================================================================
 *   The tree, the list's rows and what is in focus
 * ====================================================================== */

static SortKey g_sort;
static bool g_desc;

static int row_order(const void *pa, const void *pb) {
	const Node *x = *(Node *const *)pa, *y = *(Node *const *)pb;
	int d = 0;
	if (g_sort == SORT_SIZE) {
		d = x->size < y->size ? -1 : x->size > y->size;
	} else if (g_sort == SORT_FILES) {
		d = x->files < y->files ? -1 : x->files > y->files;
	} else if (g_sort == SORT_CHANGED) {
		d = x->mtime < y->mtime ? -1 : x->mtime > y->mtime;
	}
	if (!d) {
		d = SDL_strcasecmp(x->name, y->name);
	}
	return g_desc ? -d : d;
}

static void rows_add(App *a, Node *n, int depth) {
	if (a->nrows == a->rows_cap) {
		a->rows_cap = a->rows_cap ? a->rows_cap * 2 : 256;
		a->rows = SDL_realloc(a->rows, (size_t)a->rows_cap * sizeof *a->rows);
	}
	a->rows[a->nrows++] = (Row){n, depth};
	if (!n->open || !n->nkids) {
		return;
	}
	Node **order = SDL_malloc((size_t)n->nkids * sizeof *order);
	memcpy(order, n->kids, (size_t)n->nkids * sizeof *order);
	qsort(order, (size_t)n->nkids, sizeof *order, row_order);
	for (int i = 0; i < n->nkids; i++) {
		rows_add(a, order[i], depth + 1);
	}
	SDL_free(order);
}

static void rows_build(App *a) {
	g_sort = a->sort;
	g_desc = a->sort_desc;
	a->nrows = 0;
	rows_add(a, a->root, 0);
	int most = a->nrows - rows_fit(a);
	a->scroll = a->scroll > most ? most : a->scroll;
	a->scroll = a->scroll < 0 ? 0 : a->scroll;
}

static int row_of(const App *a, const Node *n) {
	for (int i = 0; i < a->nrows; i++) {
		if (a->rows[i].node == n) {
			return i;
		}
	}
	return -1;
}

static bool is_under(const Node *n, const Node *dir) {
	for (; n; n = n->parent) {
		if (n == dir) {
			return true;
		}
	}
	return false;
}

static void set_focus(App *a, Node *dir) {
	a->focus = dir;
	a->hover = NULL;
	a->scene_w = 0;
}

/* Selects the node: the list opens down to it and scrolls to show it, and
 * the view moves to its folder if it was showing somewhere else. */
static void select_node(App *a, Node *n) {
	a->sel = n;
	for (Node *p = n->parent; p; p = p->parent) {
		p->open = true;
	}
	rows_build(a);
	int row = row_of(a, n), fit = rows_fit(a);
	if (row < a->scroll) {
		a->scroll = row;
	} else if (row >= a->scroll + fit) {
		a->scroll = row - fit + 1;
	}
	if (!is_under(n, a->focus)) {
		set_focus(a, n->is_dir ? n : n->parent);
	}
}

static void go_up(App *a) {
	if (a->focus->parent) {
		set_focus(a, a->focus->parent);
	}
}

/* Goes into what was double-clicked: in the blocks one level toward it,
 * in the graph straight to it. */
static void go_into(App *a, Node *n) {
	if (!n) {
		return;
	}
	if (n == a->focus) {
		go_up(a);
		return;
	}
	while (a->view == VIEW_BLOCKS && n->parent != a->focus) {
		n = n->parent;
	}
	if (n->is_dir && n->nkids) {
		set_focus(a, n);
	}
}

static void camera_reset(App *a) {
	a->yaw = 0.55f;
	a->pitch = a->view == VIEW_BLOCKS ? 0.72f : 0.95f;
	a->dist = a->view == VIEW_BLOCKS ? 1.9f : 1.75f;
}

static void set_view(App *a, View v, bool solid) {
	a->view = v;
	a->solid = solid;
	a->scene_w = 0;
	a->hover = NULL;
	camera_reset(a);
}

/* ======================================================================
 *   Scanning
 * ====================================================================== */

static int scan_thread(void *arg) {
	App *a = arg;
	a->scan_result = scan_tree(a->scan_path, &a->progress, a->scan_err, sizeof a->scan_err);
	SDL_AtomicSet(&a->scan_done, 1);
	SDL_Event wake = {.type = SDL_USEREVENT};
	SDL_PushEvent(&wake);
	return 0;
}

static void scan_start(App *a, const char *path) {
	if (a->thread || !path[0]) {
		return;
	}
	if (a->root) {
		scan_free(a->root);
		a->root = a->focus = a->sel = a->hover = NULL;
	}
	snprintf(a->scan_path, sizeof a->scan_path, "%s", path);
	a->progress = (ScanProgress){0};
	a->message[0] = '\0';
	SDL_AtomicSet(&a->scan_done, 0);
	a->screen = SCREEN_SCANNING;
	a->thread = SDL_CreateThread(scan_thread, "scan", a);
}

static void scan_finish(App *a) {
	SDL_WaitThread(a->thread, NULL);
	a->thread = NULL;
	if (!a->scan_result) {
		snprintf(a->message, sizeof a->message, "%s", a->scan_err);
		a->ndisks = disks_list(a->disks, 32);
		a->screen = SCREEN_DISKS;
		return;
	}
	a->root = a->scan_result;
	a->root->open = true;
	a->sel = a->root;
	a->scroll = 0;
	set_focus(a, a->root);
	rows_build(a);
	a->screen = SCREEN_TREE;
}

static void show_disks(App *a) {
	a->ndisks = disks_list(a->disks, 32);
	a->screen = SCREEN_DISKS;
}

/* ======================================================================
 *   The solid view's camera and what the mouse is on
 * ====================================================================== */

static Vec3 camera_target(const App *a) { return a->view == VIEW_BLOCKS ? v3(0, 0.06f, 0) : v3(0, -0.2f, 0); }

static Vec3 camera_eye(const App *a) {
	Vec3 back = v3(cosf(a->pitch) * sinf(a->yaw), sinf(a->pitch), cosf(a->pitch) * cosf(a->yaw));
	return v3_add(camera_target(a), v3_scale(back, a->dist));
}

static void camera_matrix(const App *a, Rect view, float mvp[16]) {
	float proj[16], look[16];
	m4_perspective(proj, FOV, view.w / view.h, 0.02f, 20);
	m4_look_at(look, camera_eye(a), camera_target(a), v3(0, 1, 0));
	m4_mul(mvp, proj, look);
}

/* how far along the ray it enters the item, or -1 */
static float ray_hits(Vec3 from, Vec3 dir, const Item *it) {
	Vec3 c = v3(it->pos[0], it->pos[1], it->pos[2]);
	if (it->ball) {
		Vec3 to = v3_sub(c, from);
		float along = v3_dot(to, dir), r = it->size3[0] / 2;
		float off2 = v3_dot(to, to) - along * along;
		return off2 > r * r || along < 0 ? -1 : along - sqrtf(r * r - off2);
	}
	float o[3] = {from.x, from.y, from.z}, d[3] = {dir.x, dir.y, dir.z};
	float enter = 0, leave = 1e9f;
	for (int k = 0; k < 3; k++) {
		float lo = it->pos[k] - it->size3[k] / 2, hi = it->pos[k] + it->size3[k] / 2;
		float inv = 1.0f / (fabsf(d[k]) < 1e-9f ? 1e-9f : d[k]);
		float t0 = (lo - o[k]) * inv, t1 = (hi - o[k]) * inv;
		if (t0 > t1) {
			float s = t0;
			t0 = t1;
			t1 = s;
		}
		enter = t0 > enter ? t0 : enter;
		leave = t1 < leave ? t1 : leave;
	}
	return enter <= leave ? enter : -1;
}

static const Item *pick(const App *a, Rect view, float mx, float my) {
	const Scene *s = &a->scene;
	const Item *found = NULL;
	if (!a->solid) {
		/* a folder's items come after it, so the last one under the mouse is the deepest */
		float x = mx - view.x, y = my - view.y;
		for (int i = 0; i < s->n; i++) {
			const Item *it = &s->v[i];
			bool on = it->ball ? (x - it->x) * (x - it->x) + (y - it->y) * (y - it->y) <= it->w * it->w
			                   : x >= it->x && y >= it->y && x < it->x + it->w && y < it->y + it->h;
			found = on ? it : found;
		}
		return found;
	}
	Vec3 eye = camera_eye(a), f = v3_norm(v3_sub(camera_target(a), eye));
	Vec3 side = v3_norm(v3_cross(f, v3(0, 1, 0))), up = v3_cross(side, f);
	float nx = (mx - view.x) / view.w * 2 - 1, ny = 1 - (my - view.y) / view.h * 2, th = tanf(FOV / 2);
	Vec3 dir = v3_norm(v3_add(f, v3_add(v3_scale(side, nx * th * view.w / view.h), v3_scale(up, ny * th))));
	float nearest = 1e9f;
	for (int i = 0; i < s->n; i++) {
		float t = ray_hits(eye, dir, &s->v[i]);
		/* <= so that of two things starting at one place the deeper wins */
		if (t >= 0 && t <= nearest) {
			nearest = t;
			found = &s->v[i];
		}
	}
	return found;
}

/* ======================================================================
 *   Drawing
 * ====================================================================== */

static void button(const App *a, Rect r, const char *label, bool active) {
	const Theme *t = theme(a);
	SDL_Color fill = active ? rgb(t->accent) : in_rect(r, a->mx, a->my) ? rgb(t->panel_hover) : rgb(t->panel);
	d2_rect(r.x, r.y, r.w, r.h, fill);
	d2_frame(r.x, r.y, r.w, r.h, 1, rgb(t->border));
	d2_text(r.x + (r.w - (float)d2_text_width(label)) / 2, r.y + (r.h - (float)a->text_h) / 2, label,
	        active ? ink(fill) : rgb(t->text));
}

static void draw_bar(const App *a) {
	const Theme *t = theme(a);
	Rect bar = bar_rect(a), b[B_COUNT];
	bar_layout(a, b);
	d2_rect(bar.x, bar.y, bar.w, bar.h, rgb(t->bg_alt));
	d2_rect(bar.x, bar.h - 1, bar.w, 1, rgb(t->border));
	char buf[64];
	bool tree = a->screen == SCREEN_TREE;
	for (int i = 0; i < B_COUNT; i++) {
		if (i == B_PATH) {
			continue;
		}
		bool active = (i == B_DISKS && a->screen == SCREEN_DISKS) ||
		              (tree && ((i == B_BLOCKS && a->view == VIEW_BLOCKS) || (i == B_GRAPH && a->view == VIEW_GRAPH) ||
		                        (i == B_FLAT && !a->solid) || (i == B_SOLID && a->solid)));
		button(a, b[i], button_label(a, i, buf, sizeof buf), active);
	}
	Rect p = b[B_PATH];
	d2_rect(p.x, p.y, p.w, p.h, rgb(t->bg));
	d2_frame(p.x, p.y, p.w, p.h, 1, a->typing ? rgb(t->accent) : rgb(t->border));
	char path[4200];
	if (a->typing) {
		snprintf(path, sizeof path, "%s_", a->entry);
	} else if (a->screen == SCREEN_TREE) {
		node_path(a->focus, path, sizeof path);
	} else if (a->screen == SCREEN_SCANNING) {
		snprintf(path, sizeof path, "%s", a->scan_path);
	} else {
		snprintf(path, sizeof path, "Type a path here (Ctrl+L), or drop a folder on the window");
	}
	/* a path too long for the field shows its end, which is where one types */
	float room = p.w - 2 * (float)a->char_w;
	const char *from = path;
	while (*from && (float)d2_text_width(from) > room) {
		from++;
		while ((*from & 0xC0) == 0x80) {
			from++;
		}
	}
	bool hint = !a->typing && a->screen == SCREEN_DISKS;
	d2_text(p.x + (float)a->char_w, p.y + (p.h - (float)a->text_h) / 2, from,
	        hint ? mix(rgb(t->text), rgb(t->bg), 0.5f) : rgb(t->text));
}

static void draw_disks(const App *a) {
	const Theme *t = theme(a);
	float w = (float)a->w - 80 > 900 ? 900 : (float)a->w - 80, x = ((float)a->w - w) / 2;
	float y = bar_rect(a).h + 30, th = (float)a->text_h;
	d2_text(x, y, "SYN-DIRSTAT", accent_text(t));
	d2_text(x + (float)a->char_w * 13, y, "where the space has gone: pick a disk or a network share", rgb(t->text));
	y += th * 2;
	if (a->message[0]) {
		d2_text_fit(x, y, w, a->message, rgb(t->urgent));
		y += th * 2;
	}
	float rh = th * 2 + 22;
	for (int i = 0; i < a->ndisks; i++, y += rh + 8) {
		const Disk *d = &a->disks[i];
		Rect r = {x, y, w, rh};
		d2_rect(r.x, r.y, r.w, r.h, in_rect(r, a->mx, a->my) ? rgb(t->panel_hover) : rgb(t->panel));
		d2_frame(r.x, r.y, r.w, r.h, 1, rgb(t->border));
		char used[32], total[32], line[128];
		format_size(d->total - d->free, used, sizeof used);
		format_size(d->total, total, sizeof total);
		snprintf(line, sizeof line, "%s of %s used", used, total);
		float lw = (float)d2_text_width(line);
		d2_text_fit(x + 10, y + 7, w * 0.35f, d->path, rgb(t->text));
		d2_text_fit(x + 10 + w * 0.36f, y + 7, w * 0.62f - lw - 30, d->label, mix(rgb(t->text), rgb(t->panel), 0.45f));
		if (d->remote) {
			d2_text(x + w - lw - 20 - (float)a->char_w * 9, y + 7, "NETWORK", rgb(t->urgent));
		}
		d2_text(x + w - lw - 10, y + 7, line, rgb(t->text));
		float full = d->total ? (float)(d->total - d->free) / (float)d->total : 0;
		d2_rect(x + 10, y + th + 13, w - 20, th - 2, rgb(t->bg));
		d2_rect(x + 10, y + th + 13, (w - 20) * full, th - 2, full > 0.9f ? rgb(t->urgent) : rgb(t->accent));
	}
	if (!a->ndisks) {
		d2_text(x, y, "No disks found. Type a path in the bar above.", rgb(t->text));
	}
}

static void draw_scanning(const App *a) {
	const Theme *t = theme(a);
	char size[32], line[256];
	format_size(a->progress.bytes, size, sizeof size);
	snprintf(line, sizeof line, "%llu files, %s so far.  Esc stops.", (unsigned long long)a->progress.files, size);
	float y = (float)a->h / 2 - (float)a->text_h * 2;
	d2_text(((float)a->w - (float)d2_text_width("Scanning")) / 2, y, "Scanning", accent_text(t));
	d2_text_fit(40, y + (float)a->text_h * 1.5f, (float)a->w - 80, a->scan_path, rgb(t->text));
	d2_text(((float)a->w - (float)d2_text_width(line)) / 2, y + (float)a->text_h * 3, line, rgb(t->text));
}

/* The list's columns from the right edge: where each starts, and its width. */
enum { COL_NAME, COL_SIZE, COL_SHARE, COL_FILES, COL_CHANGED, COL_COUNT };

static void columns(const App *a, Rect cols[COL_COUNT]) {
	Rect l = list_rect(a);
	static const int CHARS[COL_COUNT] = {0, 11, 14, 9, 11};
	float x = l.x + l.w - 6;
	for (int c = COL_COUNT - 1; c > COL_NAME; c--) {
		float w = (float)(CHARS[c] * a->char_w);
		x -= w;
		cols[c] = (Rect){x, l.y, w, row_h(a)};
	}
	cols[COL_NAME] = (Rect){l.x + 4, l.y, x - l.x - 8, row_h(a)};
}

static void text_right(Rect col, float y, const char *s, SDL_Color c) {
	d2_text(col.x + col.w - (float)d2_text_width(s) - 4, y, s, c);
}

static void draw_list(const App *a) {
	const Theme *t = theme(a);
	Rect l = list_rect(a), cols[COL_COUNT];
	columns(a, cols);
	float rh = row_h(a);
	d2_rect(l.x, l.y, l.w, l.h, rgb(t->bg));
	d2_rect(l.x + l.w, l.y, 1, l.h, rgb(t->border));

	/* the header: click a column to sort by it, again to turn the order round */
	static const char *HEAD[COL_COUNT] = {"Name", "Size", "Of parent", "Files", "Changed"};
	static const SortKey KEY[COL_COUNT] = {SORT_NAME, SORT_SIZE, SORT_SIZE, SORT_FILES, SORT_CHANGED};
	d2_rect(l.x, l.y, l.w, rh, rgb(t->bg_alt));
	for (int c = 0; c < COL_COUNT; c++) {
		char head[32];
		bool sorted = KEY[c] == a->sort && c != COL_SHARE;
		snprintf(head, sizeof head, "%s%s", HEAD[c], sorted ? (a->sort_desc ? " v" : " ^") : "");
		SDL_Color col = sorted ? accent_text(t) : rgb(t->text);
		if (c == COL_NAME) {
			d2_text(cols[c].x + 2, l.y + 2, head, col);
		} else {
			text_right(cols[c], l.y + 2, head, col);
		}
	}

	d2_clip(l.x, l.y + rh, l.w, l.h - rh);
	for (int i = a->scroll; i < a->nrows; i++) {
		float y = l.y + rh * (float)(i - a->scroll + 1);
		if (y >= l.y + l.h) {
			break;
		}
		const Node *n = a->rows[i].node;
		if (n == a->sel) {
			d2_rect(l.x, y, l.w, rh, rgb(t->accent_dim));
			d2_rect(l.x, y, 3, rh, rgb(t->accent));
		} else if (n == a->hover) {
			d2_rect(l.x, y, l.w, rh, rgb(t->panel));
		}
		float x = cols[COL_NAME].x + (float)(a->rows[i].depth * a->char_w * 2), ty = y + 2;
		if (n->is_dir && n->nkids) {
			d2_text(x, ty, n->open ? "-" : "+", rgb(t->text));
		}
		x += (float)a->char_w * 1.5f;
		float sq = (float)a->text_h * 0.55f;
		d2_rect(x, y + (rh - sq) / 2, sq, sq, n->is_dir ? rgb(t->accent) : kind_colour(kind_of(n->name), t));
		x += sq + (float)a->char_w * 0.75f;
		SDL_Color name = n->unread ? rgb(t->urgent) : n->is_dir ? rgb(t->text) : mix(rgb(t->text), rgb(t->bg), 0.25f);
		d2_text_fit(x, ty, cols[COL_NAME].x + cols[COL_NAME].w - x, n->name, name);

		char buf[64];
		format_size(n->size, buf, sizeof buf);
		text_right(cols[COL_SIZE], ty, buf, rgb(t->text));
		float share = n->parent && n->parent->size ? (float)n->size / (float)n->parent->size : 1;
		share = share > 1 ? 1 : share; /* a folder's own blocks are in its size and not in its children's */
		float bw = (float)a->char_w * 6;
		d2_rect(cols[COL_SHARE].x + 4, y + 4, bw, rh - 8, rgb(t->panel));
		d2_rect(cols[COL_SHARE].x + 4, y + 4, bw * share, rh - 8, rgb(t->accent));
		snprintf(buf, sizeof buf, "%.1f%%", share * 100);
		text_right(cols[COL_SHARE], ty, buf, rgb(t->text));
		snprintf(buf, sizeof buf, "%llu", (unsigned long long)n->files);
		text_right(cols[COL_FILES], ty, buf, mix(rgb(t->text), rgb(t->bg), 0.25f));
		format_date(n->mtime, buf, sizeof buf);
		text_right(cols[COL_CHANGED], ty, buf, mix(rgb(t->text), rgb(t->bg), 0.25f));
	}
	d2_unclip();

	/* where the rows shown are among them all */
	int fit = rows_fit(a);
	if (a->nrows > fit) {
		float track = l.h - rh, len = track * (float)fit / (float)a->nrows;
		d2_rect(l.x + l.w - 4, l.y + rh + track * (float)a->scroll / (float)a->nrows, 4, len < 12 ? 12 : len,
		        rgb(t->border));
	}
}

/* An item's colour as it stands now: brighter under the mouse, the
 * theme's urgent colour when it is the selection. */
static SDL_Color item_colour(const App *a, const Item *it) {
	if (it->node && it->node == a->sel && it->node != a->focus) {
		return rgb(theme(a)->urgent);
	}
	return it->node == a->hover && a->hover ? lighter(it->col, 0.3f) : it->col;
}

static void label_centred(float cx, float y, float maxw, const char *s, SDL_Color c) {
	float w = (float)d2_text_width(s);
	w = w > maxw ? maxw : w;
	d2_text_fit(cx - w / 2, y, maxw, s, c);
}

static const char *item_name(const Item *it, char *buf, int len) {
	if (it->node) {
		/* the focus is a whole path; its last part names it */
		const char *slash = it->node->parent ? NULL : strrchr(it->node->name, '/');
		return slash && slash[1] ? slash + 1 : it->node->name;
	}
	snprintf(buf, (size_t)len, "%d more", it->more);
	return buf;
}

static void draw_blocks_flat(const App *a, Rect v) {
	const Theme *t = theme(a);
	const Scene *s = &a->scene;
	const Item *sel = NULL, *hover = NULL;
	for (int i = 0; i < s->n; i++) {
		const Item *it = &s->v[i];
		float x = v.x + it->x, y = v.y + it->y;
		sel = it->node == a->sel ? it : sel;
		hover = it->node == a->hover ? it : hover;
		if (it->node->is_dir) {
			d2_rect(x, y, it->w, it->h, it->col);
			d2_frame(x, y, it->w, it->h, 1, rgb(t->border));
			if (blocks_named(it->w, it->h, a->text_h)) {
				char buf[32];
				d2_text_fit(x + 4, y + 2, it->w - 8, item_name(it, buf, sizeof buf), rgb(t->text));
			}
			continue;
		}
		/* lit from the top left, each block with a dark seam round it */
		float gap = it->w > 3 && it->h > 3 ? 0.5f : 0;
		d2_rect_shade(x + gap, y + gap, it->w - 2 * gap, it->h - 2 * gap, lighter(it->col, 0.22f), darker(it->col, 0.3f));
		if (it->w > 90 && it->h > (float)a->text_h + 6) {
			d2_text_fit(x + 4, y + 3, it->w - 8, it->node->name, ink(it->col));
		}
	}
	if (hover) {
		d2_frame(v.x + hover->x, v.y + hover->y, hover->w, hover->h, 1, rgb(t->text));
	}
	if (sel && sel->node != a->focus) {
		d2_frame(v.x + sel->x, v.y + sel->y, sel->w, sel->h, 2, rgb(t->urgent));
	}
}

/* A ball's name: inside it, with its size, where the ball is big enough;
 * beside a small child of the focus, on the side away from the focus at
 * (fx, fy), where nothing else is. */
static void ball_label(const App *a, const Item *it, float cx, float cy, float r, float fx, float fy) {
	const Theme *t = theme(a);
	char buf[32], size[32];
	const char *name = item_name(it, buf, sizeof buf);
	float th = (float)a->text_h, room = r * 1.8f;
	if (r >= th * 1.25f) {
		SDL_Color c = ink(it->col);
		format_size(it->size, size, sizeof size);
		bool both = (float)d2_text_width(size) <= room && r >= th * 1.6f;
		label_centred(cx, cy - (both ? th : th / 2), room, name, c);
		if (both) {
			label_centred(cx, cy, room, size, c);
		}
		return;
	}
	if (it->depth != 1) {
		return;
	}
	float dx = cx - fx, dy = cy - fy, len = sqrtf(dx * dx + dy * dy), maxw = (float)a->char_w * 20;
	if (len < 1) {
		return;
	}
	dx /= len;
	dy /= len;
	float ax = cx + dx * (r + 6), ay = cy + dy * (r + 6), w = (float)d2_text_width(name);
	w = w > maxw ? maxw : w;
	if (dx > 0.35f) {
		d2_text_fit(ax, ay - th / 2, maxw, name, rgb(t->text));
	} else if (dx < -0.35f) {
		d2_text_fit(ax - w, ay - th / 2, maxw, name, rgb(t->text));
	} else {
		d2_text_fit(ax - w / 2, dy < 0 ? ay - th : ay, maxw, name, rgb(t->text));
	}
}

static void draw_graph_flat(const App *a, Rect v) {
	const Theme *t = theme(a);
	const Scene *s = &a->scene;
	for (int e = 0; e < s->nedge; e += 2) {
		const Item *p = &s->v[s->edge[e]], *c = &s->v[s->edge[e + 1]];
		d2_line(v.x + p->x, v.y + p->y, v.x + c->x, v.y + c->y, c->w > 12 ? 2 : 1, rgb(t->border));
	}
	for (int i = 0; i < s->n; i++) {
		const Item *it = &s->v[i];
		SDL_Color c = item_colour(a, it);
		float x = v.x + it->x, y = v.y + it->y;
		d2_disc(x, y, it->w, lighter(c, 0.3f), darker(c, 0.25f));
		bool dir = !it->node || it->node->is_dir;
		d2_ring(x, y, it->w, dir ? 1.5f : 1, dir ? rgb(t->text) : rgb(t->border));
	}
	for (int i = 0; i < s->n; i++) {
		ball_label(a, &s->v[i], v.x + s->v[i].x, v.y + s->v[i].y, s->v[i].w, v.x + s->v[0].x, v.y + s->v[0].y);
	}
}

static void draw_solid(const App *a, Rect v) {
	const Theme *t = theme(a);
	const Scene *s = &a->scene;
	float mvp[16];
	camera_matrix(a, v, mvp);
	Solid *solids = SDL_malloc((size_t)(s->n + 1) * sizeof *solids);
	for (int i = 0; i < s->n; i++) {
		const Item *it = &s->v[i];
		SDL_Color c = item_colour(a, it);
		memcpy(solids[i].pos, it->pos, sizeof it->pos);
		memcpy(solids[i].size, it->size3, sizeof it->size3);
		memcpy(solids[i].rgba, &c, 4);
	}
	LinePoint *lines = SDL_malloc((size_t)(s->nedge + 1) * sizeof *lines);
	for (int e = 0; e < s->nedge; e++) {
		SDL_Color c = rgb(t->border);
		memcpy(lines[e].pos, s->v[s->edge[e]].pos, sizeof lines->pos);
		memcpy(lines[e].rgba, &c, 4);
	}
	d3_begin(v.x, v.y, v.w, v.h, mvp);
	d3_lines(lines, s->nedge);
	if (a->view == VIEW_BLOCKS) {
		d3_boxes(solids, s->n);
	} else {
		d3_spheres(solids, s->n);
	}
	d3_end();
	SDL_free(solids);
	SDL_free(lines);

	/* names, where each thing is on the screen */
	d2_clip(v.x, v.y, v.w, v.h);
	float fx = 0, fy = 0; /* where the focus is on the screen */
	m4_project(mvp, v3(s->v[0].pos[0], s->v[0].pos[1], s->v[0].pos[2]), v.w, v.h, &fx, &fy);
	for (int i = 0; i < s->n; i++) {
		const Item *it = &s->v[i];
		float sx, sy;
		Vec3 top = v3(it->pos[0], it->pos[1] + (it->ball ? 0 : it->size3[1] / 2), it->pos[2]);
		if (!m4_project(mvp, top, v.w, v.h, &sx, &sy)) {
			continue;
		}
		if (it->ball) {
			/* its radius on the screen, from how far away it is */
			Vec3 away = v3_sub(v3(it->pos[0], it->pos[1], it->pos[2]), camera_eye(a));
			float r = it->size3[0] / 2 / sqrtf(v3_dot(away, away)) / tanf(FOV / 2) * v.h / 2;
			ball_label(a, it, v.x + sx, v.y + sy, r, v.x + fx, v.y + fy);
		} else if (it->depth == 1 && it->w * it->h > a->scene_w * a->scene_h * 0.015f) {
			char buf[32];
			label_centred(v.x + sx, v.y + sy - (float)a->text_h - 2, (float)a->char_w * 18, item_name(it, buf, sizeof buf),
			              rgb(t->text));
		}
	}
	d2_unclip();
}

/* What the mouse is on, in a box beside it. */
static void draw_tip(const App *a, Rect v, const Item *it) {
	const Theme *t = theme(a);
	char buf[32], size[32], line[600];
	format_size(it->size, size, sizeof size);
	float share = a->focus->size ? (float)it->size / (float)a->focus->size * 100 : 0;
	snprintf(line, sizeof line, "%s   %s   %.1f%%", item_name(it, buf, sizeof buf), size, share);
	float w = (float)d2_text_width(line) + 12, h = (float)a->text_h + 8;
	float x = a->mx + 14, y = a->my + 18;
	x = x + w > v.x + v.w ? v.x + v.w - w : x;
	y = y + h > v.y + v.h ? a->my - h - 6 : y;
	d2_rect(x, y, w, h, rgb(t->bg_alt));
	d2_frame(x, y, w, h, 1, rgb(t->accent));
	d2_text(x + 6, y + 4, line, rgb(t->text));
}

static void draw_view(App *a) {
	const Theme *t = theme(a);
	Rect v = view_rect(a);
	if (a->scene_w != v.w || a->scene_h != v.h) {
		if (a->view == VIEW_BLOCKS) {
			blocks_build(&a->scene, a->focus, v.w, v.h, a->text_h, t);
		} else {
			graph_build(&a->scene, a->focus, v.w, v.h, t);
		}
		a->scene_w = v.w;
		a->scene_h = v.h;
	}
	const Item *under = in_rect(v, a->mx, a->my) && !a->dragged ? pick(a, v, a->mx, a->my) : NULL;
	if (in_rect(v, a->mx, a->my)) {
		a->hover = under ? under->node : NULL;
	}
	d2_rect(v.x, v.y, v.w, v.h, rgb(t->bg));
	if (!a->scene.n) {
		d2_text(v.x + 20, v.y + 20, "Nothing here takes any space.", rgb(t->text));
	} else if (a->solid) {
		draw_solid(a, v);
	} else if (a->view == VIEW_BLOCKS) {
		draw_blocks_flat(a, v);
	} else {
		draw_graph_flat(a, v);
	}
	if (under) {
		draw_tip(a, v, under);
	}
}

static void draw_status(const App *a) {
	const Theme *t = theme(a);
	Rect s = status_rect(a);
	d2_rect(s.x, s.y, s.w, s.h, rgb(t->bg_alt));
	d2_rect(s.x, s.y, s.w, 1, rgb(t->border));
	float ty = s.y + 5, x = 8, sq = (float)a->text_h * 0.6f;
	for (int k = 0; k < KIND_COUNT; k++) {
		float y = ty + (float)a->text_h + 4;
		d2_rect(x, y + (float)a->text_h * 0.2f, sq, sq, kind_colour(k, t));
		x += sq + 5;
		d2_text(x, y, kind_name(k), mix(rgb(t->text), rgb(t->bg_alt), 0.25f));
		x += (float)d2_text_width(kind_name(k)) + (float)a->char_w * 2;
	}
	const Node *n = a->hover ? a->hover : a->sel;
	char path[4096], size[32], date[32], line[4400];
	node_path(n, path, sizeof path);
	format_size(n->size, size, sizeof size);
	format_date(n->mtime, date, sizeof date);
	snprintf(line, sizeof line, "%s   %s   %llu files   changed %s%s", path, size, (unsigned long long)n->files, date,
	         n->unread ? "   (could not be read)" : "");
	d2_text_fit(8, ty, s.w - 16, line, rgb(t->text));
}

static void draw(App *a) {
	const Theme *t = theme(a);
	draw_begin(a->w, a->h, rgb(t->bg));
	if (a->screen == SCREEN_DISKS) {
		draw_disks(a);
	} else if (a->screen == SCREEN_SCANNING) {
		draw_scanning(a);
	} else {
		draw_list(a);
		draw_view(a);
		draw_status(a);
	}
	draw_bar(a);
	draw_end();
}

/* ======================================================================
 *   Input
 * ====================================================================== */

static void typing_start(App *a) {
	a->typing = true;
	a->entry[0] = '\0';
	SDL_StartTextInput();
}

static void typing_stop(App *a) {
	a->typing = false;
	SDL_StopTextInput();
}

static void typing_add(App *a, const char *text) {
	size_t at = strlen(a->entry);
	snprintf(a->entry + at, sizeof a->entry - at, "%s", text);
	a->entry[strcspn(a->entry, "\r\n")] = '\0';
}

static void press_button(App *a, int b) {
	bool tree = a->screen == SCREEN_TREE;
	if (b == B_DISKS && !a->thread) {
		show_disks(a);
	} else if (b == B_PATH) {
		typing_start(a);
	} else if (b == B_THEME) {
		a->theme = (a->theme + 1) % a->nthemes;
		a->scene_w = 0;
	} else if (!tree) {
		return;
	} else if (b == B_UP) {
		go_up(a);
	} else if (b == B_RESCAN) {
		char path[4096];
		node_path(a->root, path, sizeof path);
		scan_start(a, path);
	} else if (b == B_BLOCKS || b == B_GRAPH) {
		set_view(a, b == B_BLOCKS ? VIEW_BLOCKS : VIEW_GRAPH, a->solid);
	} else if (b == B_FLAT || b == B_SOLID) {
		set_view(a, a->view, b == B_SOLID);
	}
}

static void click_list(App *a, float x, float y, int clicks) {
	Rect l = list_rect(a), cols[COL_COUNT];
	columns(a, cols);
	if (y < l.y + row_h(a)) {
		static const SortKey KEY[COL_COUNT] = {SORT_NAME, SORT_SIZE, SORT_SIZE, SORT_FILES, SORT_CHANGED};
		for (int c = 0; c < COL_COUNT; c++) {
			if (x >= cols[c].x && x < cols[c].x + cols[c].w) {
				/* a new column starts with what one looks for first: the biggest, the newest, A */
				a->sort_desc = KEY[c] == a->sort ? !a->sort_desc : KEY[c] != SORT_NAME;
				a->sort = KEY[c];
				rows_build(a);
			}
		}
		return;
	}
	int i = a->scroll + (int)((y - l.y) / row_h(a)) - 1;
	if (i < 0 || i >= a->nrows) {
		return;
	}
	Node *n = a->rows[i].node;
	float mark = cols[COL_NAME].x + (float)(a->rows[i].depth * a->char_w * 2);
	if (n->is_dir && (clicks == 2 || (x >= mark - 4 && x < mark + (float)a->char_w * 1.5f))) {
		n->open = !n->open;
	}
	select_node(a, n);
	if (clicks == 2 && n->is_dir && n->nkids) {
		set_focus(a, n);
	}
}

static void mouse_down(App *a, const SDL_MouseButtonEvent *e) {
	float x = (float)e->x * a->dpi, y = (float)e->y * a->dpi;
	if (a->typing) {
		typing_stop(a);
	}
	if (in_rect(bar_rect(a), x, y)) {
		Rect b[B_COUNT];
		bar_layout(a, b);
		for (int i = 0; i < B_COUNT && e->button == SDL_BUTTON_LEFT; i++) {
			if (in_rect(b[i], x, y)) {
				press_button(a, i);
				return;
			}
		}
		return;
	}
	if (a->screen == SCREEN_DISKS && e->button == SDL_BUTTON_LEFT) {
		/* the same rows draw_disks lays out */
		float w = (float)a->w - 80 > 900 ? 900 : (float)a->w - 80, rx = ((float)a->w - w) / 2, th = (float)a->text_h;
		float ry = bar_rect(a).h + 30 + th * 2 + (a->message[0] ? th * 2 : 0), rh = th * 2 + 22;
		for (int i = 0; i < a->ndisks; i++, ry += rh + 8) {
			if (in_rect((Rect){rx, ry, w, rh}, x, y)) {
				scan_start(a, a->disks[i].path);
				return;
			}
		}
		return;
	}
	if (a->screen != SCREEN_TREE) {
		return;
	}
	if (e->button == SDL_BUTTON_RIGHT) {
		go_up(a);
	} else if (e->button == SDL_BUTTON_LEFT && in_rect(list_rect(a), x, y)) {
		click_list(a, x, y, e->clicks);
	} else if (e->button == SDL_BUTTON_LEFT && in_rect(view_rect(a), x, y)) {
		a->pressed = true;
		a->dragged = false;
		a->press_x = x;
		a->press_y = y;
	}
}

static void mouse_up(App *a, const SDL_MouseButtonEvent *e) {
	if (e->button != SDL_BUTTON_LEFT || !a->pressed) {
		return;
	}
	a->pressed = false;
	if (a->dragged) {
		a->dragged = false;
		return;
	}
	Rect v = view_rect(a);
	const Item *it = pick(a, v, a->press_x, a->press_y);
	if (!it || !it->node) {
		return;
	}
	Node *n = it->node;
	if (e->clicks == 2) {
		go_into(a, n);
	} else {
		select_node(a, n);
	}
}

static void mouse_move(App *a, const SDL_MouseMotionEvent *e) {
	float x = (float)e->x * a->dpi, y = (float)e->y * a->dpi;
	if (a->pressed && a->solid) {
		if (fabsf(x - a->press_x) + fabsf(y - a->press_y) > 4) {
			a->dragged = true;
		}
		if (a->dragged) {
			a->yaw -= (x - a->mx) * 0.006f;
			a->pitch += (y - a->my) * 0.006f;
			a->pitch = a->pitch < 0.05f ? 0.05f : a->pitch > 1.5f ? 1.5f : a->pitch;
		}
	}
	a->mx = x;
	a->my = y;
	if (a->screen == SCREEN_TREE && in_rect(list_rect(a), x, y)) {
		int i = a->scroll + (int)((y - list_rect(a).y) / row_h(a)) - 1;
		a->hover = i >= a->scroll && i < a->nrows ? a->rows[i].node : NULL;
	}
}

static void wheel(App *a, const SDL_MouseWheelEvent *e) {
	if (a->screen != SCREEN_TREE) {
		return;
	}
	if (in_rect(list_rect(a), a->mx, a->my)) {
		a->scroll -= e->y * 3;
		rows_build(a); /* which keeps the scroll within the rows */
	} else if (a->solid) {
		a->dist *= e->y > 0 ? 0.88f : 1.14f;
		a->dist = a->dist < 0.15f ? 0.15f : a->dist > 6 ? 6 : a->dist;
	}
}

static void key_typing(App *a, const SDL_KeyboardEvent *e) {
	SDL_Keycode k = e->keysym.sym;
	if (k == SDLK_ESCAPE) {
		typing_stop(a);
	} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
		typing_stop(a);
		scan_start(a, a->entry);
	} else if (k == SDLK_BACKSPACE) {
		size_t n = strlen(a->entry);
		while (n > 0 && (a->entry[n - 1] & 0xC0) == 0x80) {
			n--;
		}
		a->entry[n ? n - 1 : 0] = '\0';
	} else if (k == SDLK_v && (e->keysym.mod & KMOD_CTRL)) {
		char *clip = SDL_GetClipboardText();
		typing_add(a, clip);
		SDL_free(clip);
	}
}

static void key_tree(App *a, const SDL_KeyboardEvent *e) {
	SDL_Keycode k = e->keysym.sym;
	int row = row_of(a, a->sel), page = rows_fit(a) - 1;
	Node *n = a->sel;
	if (k == SDLK_TAB) {
		set_view(a, a->view == VIEW_BLOCKS ? VIEW_GRAPH : VIEW_BLOCKS, a->solid);
	} else if (k == SDLK_SPACE) {
		set_view(a, a->view, !a->solid);
	} else if (k == SDLK_BACKSPACE) {
		go_up(a);
	} else if (k == SDLK_r || k == SDLK_F5) {
		press_button(a, B_RESCAN);
	} else if (k == SDLK_o) {
		/* in the file manager, the folder itself or the one the file is in */
		char path[4096];
		node_path(n->is_dir ? n : n->parent, path, sizeof path);
		SDL_OpenURL(path);
	} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
		if (n->is_dir && n->nkids) {
			set_focus(a, n);
		}
	} else if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) {
		int to = row + (k == SDLK_UP ? -1 : k == SDLK_DOWN ? 1 : k == SDLK_PAGEUP ? -page : page);
		to = to < 0 ? 0 : to >= a->nrows ? a->nrows - 1 : to;
		select_node(a, a->rows[to].node);
	} else if (k == SDLK_RIGHT && n->is_dir) {
		n->open = true;
		rows_build(a);
	} else if (k == SDLK_LEFT) {
		if (n->is_dir && n->open) {
			n->open = false;
			rows_build(a);
		} else if (n->parent) {
			select_node(a, n->parent);
		}
	}
}

static void key_down(App *a, const SDL_KeyboardEvent *e) {
	SDL_Keycode k = e->keysym.sym;
	if (a->typing) {
		key_typing(a, e);
	} else if (k == SDLK_l && (e->keysym.mod & KMOD_CTRL)) {
		typing_start(a);
	} else if (k == SDLK_t) {
		int step = e->keysym.mod & KMOD_SHIFT ? a->nthemes - 1 : 1;
		a->theme = (a->theme + step) % a->nthemes;
		a->scene_w = 0;
	} else if (k == SDLK_ESCAPE) {
		if (a->screen == SCREEN_SCANNING) {
			a->progress.cancel = true;
		} else if (a->screen == SCREEN_TREE) {
			show_disks(a);
		}
	} else if (a->screen == SCREEN_TREE) {
		key_tree(a, e);
	}
}

static void handle(App *a, const SDL_Event *e) {
	switch (e->type) {
	case SDL_QUIT:
		a->quit = true;
		break;
	case SDL_KEYDOWN:
		key_down(a, &e->key);
		break;
	case SDL_TEXTINPUT:
		if (a->typing) {
			typing_add(a, e->text.text);
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		mouse_down(a, &e->button);
		break;
	case SDL_MOUSEBUTTONUP:
		mouse_up(a, &e->button);
		break;
	case SDL_MOUSEMOTION:
		mouse_move(a, &e->motion);
		break;
	case SDL_MOUSEWHEEL:
		wheel(a, &e->wheel);
		break;
	case SDL_DROPFILE:
		scan_start(a, e->drop.file);
		SDL_free(e->drop.file);
		break;
	default:
		break;
	}
}

/* ====================================================================== */

/* Said on stderr, and on Windows, where a window program has no
 * terminal, in a box. */
static int fail(const char *what) {
	fprintf(stderr, "syn-dirstat: %s: %s\n", what, SDL_GetError());
#ifdef _WIN32
	char msg[1024];
	snprintf(msg, sizeof msg, "%s:\n%s\n\nSYN-DIRSTAT needs OpenGL 3.3 or newer.", what, SDL_GetError());
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SYN-DIRSTAT could not start", msg, NULL);
#endif
	return 1;
}

int app_run(const AppOptions *opt) {
	static App app;
	App *a = &app;
	a->opt = opt;
	a->nthemes = themes_load(&a->themes);
	a->theme = theme_start(a->themes, a->nthemes, opt->theme);
	if (a->theme < 0) {
		fprintf(stderr, "syn-dirstat: no theme named %s\n", opt->theme ? opt->theme : "SYN-OS-RED");
		return 1;
	}
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		return fail("Starting the video system");
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	a->win = SDL_CreateWindow("SYN-DIRSTAT", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
	                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!a->win) {
		return fail("Opening the window");
	}
	if (!SDL_GL_CreateContext(a->win)) {
		return fail("Creating an OpenGL 3.3 context");
	}
	SDL_GL_SetSwapInterval(1);
	int ww, wh;
	SDL_GetWindowSize(a->win, &ww, &wh);
	SDL_GL_GetDrawableSize(a->win, &a->w, &a->h);
	a->dpi = (float)a->w / (float)ww;
	if (!draw_init((int)(16 * roundf(a->dpi)))) {
		SDL_SetError("the graphics driver or the font could not be used");
		return fail("Setting up the drawing");
	}
	a->text_h = d2_text_height();
	a->char_w = d2_text_width("0");
	a->sort = SORT_SIZE;
	a->sort_desc = true;
	set_view(a, opt->graph ? VIEW_GRAPH : VIEW_BLOCKS, opt->solid);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	SDL_StopTextInput();
	show_disks(a);
	if (opt->path) {
		scan_start(a, opt->path);
	}

	while (!a->quit) {
		SDL_Event e;
		/* nothing moves by itself but a scan's counters */
		if (SDL_WaitEventTimeout(&e, a->screen == SCREEN_SCANNING ? 100 : 1000)) {
			do {
				handle(a, &e);
			} while (SDL_PollEvent(&e));
		}
		if (a->thread && SDL_AtomicGet(&a->scan_done)) {
			scan_finish(a);
			if (opt->shot && !a->root) {
				fprintf(stderr, "syn-dirstat: %s\n", a->message);
				return 1;
			}
		}
		SDL_GL_GetDrawableSize(a->win, &a->w, &a->h);
		draw(a);
		if (opt->shot && a->screen != SCREEN_SCANNING && ++a->settled == 2) {
			return draw_shot(opt->shot) ? 0 : 1;
		}
		SDL_GL_SwapWindow(a->win);
	}
	if (a->thread) {
		a->progress.cancel = true;
		SDL_WaitThread(a->thread, NULL);
	}
	SDL_Quit();
	return 0;
}
