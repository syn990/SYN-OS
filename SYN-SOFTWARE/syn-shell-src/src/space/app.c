/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "app.h"

#include <sys/stat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "disks.h"
#include "draw.h"
#include "fileops.h"
#include "gl.h"
#include "icons.h"
#include "scan.h"
#include "scene.h"
#include "theme.h"
#include "vmath.h"
#include "walk.h"

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

/* the list's right-click menu */
enum { M_COPY, M_CUT, M_PASTE, M_RENAME, M_NEWDIR, M_DELETE, M_PURGE, M_COUNT };
typedef enum { EDIT_NONE, EDIT_RENAME, EDIT_NEWDIR } Edit;

/* SDL3's SDL_WINDOW_TRANSPARENT. SDL2 has no such flag, but sdl2-compat
 * (SDL2 on SDL3, as SYN-OS has it) hands the window's flags on to SDL3,
 * which then gives the window an alpha the compositor honours; SDL2
 * proper ignores the bit. */
#define WINDOW_SEE_THROUGH 0x40000000u

typedef struct App {
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

	/* the scan in its thread. It is live: the tree is on screen from its
	 * first folder and grows as it is walked; tree_lock is held by the
	 * scan while it changes the tree and by the window for each frame. */
	SDL_Thread *thread;
	ScanProgress progress;
	SDL_atomic_t scan_done;
	char scan_path[4096], scan_err[1024];
	Node *scan_result;
	SDL_mutex *tree_lock;
	SDL_atomic_t want_tree; /* the window is waiting for the lock */
	bool live;              /* the tree on screen is still being scanned */
	Uint32 built_at;        /* when the scene was last built from the growing tree */

	Node *root, *focus, *sel, *hover;
	View view;
	bool solid;
	SortKey sort;
	bool sort_desc;
	Row *rows;
	int nrows, rows_cap, scroll;

	/* what the view draws (scene) eases toward the layout as it stands
	 * (target), so a folder found mid-scan appears small and grows, and
	 * one overtaken slides to its new place, never jumps */
	Scene scene, target, next;
	Scene shown; /* the flat view's layout as drawn, zoomed and slid */
	float scene_w, scene_h; /* the view size the scene was built for; 0 when stale */
	bool snap;              /* the next layout is shown at once, not eased into */
	Uint32 ease_until;      /* easing goes on until then, scan or no scan */
	const void **ease_key;  /* scene's items by what they show, to ease from */
	int *ease_at, ease_cap;
	float yaw, pitch, dist;
	Vec3 pan3;           /* the solid view's camera slid this far from where it looks */
	float zoom2, pan2x, pan2y; /* the flat view: drawn this many times bigger, then moved by so many pixels */
	bool spin;   /* the 3D view turns by itself after a scan, until it is dragged */
	Uint64 tick;  /* when the last frame was drawn (performance counter: walking wants better than milliseconds) */
	float dt;     /* seconds since the frame before */

	unsigned icon[ICON_COUNT]; /* the toolbar's, as textures */
	float icon_px;             /* drawn this big */

	/* Delete permanently asks first, in a box over everything */
	Node *ask_node;

	bool typing; /* a path is being typed into the bar */
	char entry[4096];

	/* the strip of sliders under the view: the see-through background,
	 * 0 (solid) .. 100; how many levels below the focus are drawn,
	 * 1 .. LEVELS_ALL (all); the 3D gap between levels, 0 (flat) .. 3
	 * times as made; the idle spin, radians a second; and the one dragged */
	float see;
	int levels;
	float lift;
	float spin_rate;
	int slide_drag; /* SL_..., or -1 */

	/* the list's right-click menu, and what it was opened on */
	bool menu_open;
	float menu_x, menu_y;
	Node *menu_node;
	char clip[4096]; /* what Copy or Cut took, by path */
	bool clip_cut;

	/* a name typed into the list: a rename of edit_node, or a new folder in it */
	Edit edit;
	Node *edit_node;
	char edit_text[256];
	bool edit_fresh; /* all of it chosen: the first key typed replaces it */

	/* a paste in its thread */
	SDL_Thread *job;
	SDL_atomic_t job_done;
	bool job_move, job_ok;
	char job_src[4096], job_dir[4096], job_out[4096], job_err[600];

	/* a line in the status bar for a while: what was done, or why not */
	char note[700];
	bool note_bad;
	Uint32 note_at;

	float mx, my;
	bool pressed, dragged; /* the left button is down in the view; it has moved since */
	float press_x, press_y;
	bool rpressed, rdragged; /* the right button is down in the view; it has moved since (a slide, not a click) */
	float rpress_x, rpress_y;
	/* walk mode: the figure in the view's corner, picked up and dropped
	 * on the map, puts you in the folder there as a room to walk about in */
	Walk *walk;
	bool fig_drag, fig_moved; /* the figure is held; it has moved since */
	float fig_x, fig_y;       /* where it was picked up */
	bool walk_asked;          /* --walk: into walk mode once there is a tree */
	int settled; /* frames drawn since the last scan ended, for --shot */
	Uint32 shot_from; /* when the window started, for --shot-at */

	/* Drawn inside SYN-SHELL's window (space_new): no window, list, disk
	 * screen or path of its own. The view follows the folder SYN-SHELL is
	 * in (space_show), and what the user does here that moves it (into a
	 * folder, up, walking through a doorway, a click) is handed back by
	 * space_changes, never the moves SYN-SHELL asked for itself. */
	bool embedded;
	/* The corner render (space_set_mini): the view alone, the whole
	 * widget, 3D and turning, nothing to press. */
	bool mini;
	Node *told_focus, *told_sel, *told_room; /* as SYN-SHELL knows them */
	bool host_up;                            /* up, from the top of the tree */
	int changes;                             /* SPACE_FOCUS | SPACE_SELECT | SPACE_USE */
	char want_focus[4096], want_sel[4096];
	char want_use[4096];                     /* E on this while walking */
	int use_kind, use_dev;
	char pending[4096];                      /* SYN-SHELL's folder, not yet reached by the live scan */
	unsigned gen;                            /* a new tree each time it goes up */
	void (*tree_drop)(void *);               /* the tree is about to be freed */
	void *host;
	Uint8 keys[SDL_NUM_SCANCODES];           /* held, for walking */
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

static Rect bar_rect(const App *a) {
	if (a->mini) {
		return (Rect){0, 0, (float)a->w, 0};
	}
	float h = (float)a->text_h + 12, ih = a->icon_px + 12;
	return (Rect){0, 0, (float)a->w, h > ih ? h : ih};
}
static Rect status_rect(const App *a) {
	if (a->mini) {
		return (Rect){0, (float)a->h, (float)a->w, 0};
	}
	float h = (float)a->text_h * 2 + 14; /* what the mouse is on, and under it what the colours mean */
	return (Rect){0, (float)a->h - h, (float)a->w, h};
}
static Rect list_rect(const App *a) {
	Rect bar = bar_rect(a), st = status_rect(a);
	float w = a->embedded ? 0 : floorf((float)a->w * 0.42f); /* in SYN-SHELL the tree is its own column */
	return (Rect){0, bar.h, w, st.y - bar.h};
}
/* the right side: the view, and under it the see-through slider */
static Rect right_rect(const App *a) {
	Rect l = list_rect(a);
	if (a->embedded) {
		return (Rect){0, l.y, (float)a->w, l.h};
	}
	return (Rect){l.w + 1, l.y, (float)a->w - l.w - 1, l.h};
}
/* the sliders under the view, two to a row */
enum { SL_SEE, SL_DEPTH, SL_HEIGHT, SL_SPIN, SL_COUNT };
#define SL_ROWS 2
#define LEVELS_ALL 12 /* the depth slider's top: every level */
#define SPIN_MADE 0.35f /* SYN-TREK's planets turn 0.35 radians a second */
#define SPIN_MOST (SPIN_MADE * 3)
#define LIFT_MOST 3.0f
static const char *SL_LABEL[SL_COUNT] = {"SEE-THROUGH", "DEPTH", "HEIGHT", "SPIN"};
static Rect slider_rect(const App *a) {
	Rect r = right_rect(a);
	float h = a->mini ? 0 : ((float)a->text_h + 10) * SL_ROWS;
	return (Rect){r.x, r.y + r.h - h, r.w, h};
}
/* one slider's cell in the strip */
static Rect slider_cell(const App *a, int i) {
	Rect s = slider_rect(a);
	float w = floorf(s.w / 2), h = s.h / SL_ROWS;
	float x = s.x + (float)(i % 2) * w;
	return (Rect){x, s.y + (float)(i / 2) * h, i % 2 ? s.w - w : w, h};
}
static Rect view_rect(const App *a) {
	Rect r = right_rect(a);
	r.h -= slider_rect(a).h;
	return r;
}
/* a slider's track inside its cell */
static Rect slider_track(const App *a, int i) {
	Rect s = slider_cell(a, i);
	float left = s.x + (float)a->char_w * 13, right = s.x + s.w - (float)a->char_w * 6;
	return (Rect){left, s.y, right - left, s.h};
}
/* the slider under a point, or -1 */
static int slider_at(const App *a, float x, float y) {
	for (int i = 0; i < SL_COUNT; i++) {
		if (in_rect(slider_cell(a, i), x, y)) {
			return i;
		}
	}
	return -1;
}
/* where a slider's knob is, 0 .. 1 along its track */
static float slider_get(const App *a, int i) {
	switch (i) {
	case SL_SEE: return a->see / 100;
	case SL_DEPTH: return (float)(a->levels - 1) / (LEVELS_ALL - 1);
	case SL_HEIGHT: return a->lift / LIFT_MOST;
	default: return a->spin_rate / SPIN_MOST;
	}
}
/* what a slider reads beside its track */
static void slider_text(const App *a, int i, char *buf, int len) {
	switch (i) {
	case SL_SEE: snprintf(buf, (size_t)len, "%d%%", (int)roundf(a->see)); break;
	case SL_DEPTH:
		if (a->levels >= LEVELS_ALL) {
			snprintf(buf, (size_t)len, "ALL");
		} else {
			snprintf(buf, (size_t)len, "%d", a->levels);
		}
		break;
	case SL_HEIGHT: snprintf(buf, (size_t)len, "%.1fx", (double)a->lift); break;
	default: snprintf(buf, (size_t)len, "%.1fx", (double)(a->spin_rate / SPIN_MADE)); break;
	}
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

/* each button's icon, the path's none */
static int button_icon(int b) {
	static const int ICON[B_COUNT] = {ICON_DISKS, ICON_UP,   ICON_RESCAN, -1,        ICON_BLOCKS,
	                                  ICON_GRAPH, ICON_FLAT, ICON_SOLID,  ICON_THEME};
	return ICON[b];
}

/* a button's width: its icon, then its label */
static float button_w(const App *a, int b) {
	char buf[64];
	float pad = (float)a->char_w;
	return (float)d2_text_width(button_label(a, b, buf, sizeof buf)) + 2 * pad + a->icon_px + pad * 0.6f;
}

/* The bar's buttons: three at the left, five at the right, the path between. */
static void bar_layout(const App *a, Rect out[B_COUNT]) {
	Rect bar = bar_rect(a);
	float x = 4, right = bar.w - 4;
	if (a->embedded) {
		/* in SYN-SHELL: RESCAN, and the four that choose the view; the
		 * disks, the path and the theme are SYN-SHELL's own */
		for (int b = 0; b < B_COUNT; b++) {
			out[b] = (Rect){0, 0, 0, 0};
		}
		float w = button_w(a, B_RESCAN);
		out[B_RESCAN] = (Rect){x, 3, w, bar.h - 6};
		for (int b = B_SOLID; b >= B_BLOCKS; b--) {
			w = button_w(a, b);
			right -= w;
			out[b] = (Rect){right, 3, w, bar.h - 6};
			right -= b == B_FLAT ? 12 : 4;
		}
		return;
	}
	for (int b = B_DISKS; b <= B_RESCAN; b++) {
		float w = button_w(a, b);
		out[b] = (Rect){x, 3, w, bar.h - 6};
		x += w + 4;
	}
	for (int b = B_THEME; b >= B_BLOCKS; b--) {
		float w = button_w(a, b);
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
	/* a new folder is laid out afresh: shown whole, where it is, at the zoom it had in 3D */
	a->pan3 = (Vec3){0, 0, 0};
	a->zoom2 = 1;
	a->pan2x = a->pan2y = 0;
	a->focus = dir;
	a->hover = NULL;
	a->scene_w = 0;
	a->snap = true;
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
	} else if (a->embedded) {
		a->host_up = true; /* above the tree: SYN-SHELL goes up, and the tree is scanned again from there */
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
	a->pan3 = v3(0, 0, 0);
	a->zoom2 = 1;
	a->pan2x = a->pan2y = 0;
}

static void set_view(App *a, View v, bool solid) {
	a->view = v;
	a->solid = solid;
	a->scene_w = 0;
	a->snap = true;
	a->hover = NULL;
	camera_reset(a);
}

/* ======================================================================
 *   The files themselves: the right-click menu's actions, and the tree
 *   kept true to them without scanning everything again
 * ====================================================================== */

static void say(App *a, bool bad, const char *fmt, const char *what) {
	snprintf(a->note, sizeof a->note, fmt, what);
	a->note_bad = bad;
	a->note_at = SDL_GetTicks();
}

static int by_size_desc(const void *pa, const void *pb) {
	const Node *x = *(Node *const *)pa, *y = *(Node *const *)pb;
	if (x->size != y->size) {
		return x->size < y->size ? 1 : -1;
	}
	return strcmp(x->name, y->name);
}

/* sizes and file counts from dir up to the root changed by (size, files),
 * and each folder's children back in size order */
static void tree_change(Node *dir, uint64_t size, uint64_t files, bool add) {
	for (Node *p = dir; p; p = p->parent) {
		p->size = add ? p->size + size : p->size - size;
		p->files = add ? p->files + files : p->files - files;
		qsort(p->kids, (size_t)p->nkids, sizeof *p->kids, by_size_desc);
	}
}

/* The node for a whole path, or NULL where the tree has none. */
static Node *node_find(const App *a, const char *path) {
	if (!a->root) {
		return NULL;
	}
	const char *rp = a->root->name;
	size_t n = strlen(rp);
	if (strncmp(path, rp, n) || (path[n] && path[n] != '/' && rp[n - 1] != '/')) {
		return NULL;
	}
	Node *at = a->root;
	const char *p = path + n;
	while (*p) {
		while (*p == '/') {
			p++;
		}
		if (!*p) {
			break;
		}
		size_t len = strcspn(p, "/");
		Node *next = NULL;
		for (int i = 0; i < at->nkids && !next; i++) {
			if (strlen(at->kids[i]->name) == len && !strncmp(at->kids[i]->name, p, len)) {
				next = at->kids[i];
			}
		}
		if (!next) {
			return NULL;
		}
		at = next;
		p += len;
	}
	return at;
}

/* Takes the node out of the tree (not freed): whatever pointed into it
 * points at its folder after. */
static void walk_to_map(App *a);

static void tree_detach(App *a, Node *n) {
	walk_to_map(a); /* the room may be what goes */
	Node *dir = n->parent;
	if (!dir) {
		return;
	}
	for (int i = 0; i < dir->nkids; i++) {
		if (dir->kids[i] == n) {
			memmove(&dir->kids[i], &dir->kids[i + 1], (size_t)(dir->nkids - i - 1) * sizeof *dir->kids);
			dir->nkids--;
			break;
		}
	}
	n->parent = NULL;
	tree_change(dir, n->size, n->files, false);
	Node **at[] = {&a->sel, &a->focus, &a->menu_node, &a->edit_node};
	for (size_t i = 0; i < sizeof at / sizeof *at; i++) {
		if (*at[i] && is_under(*at[i], n)) {
			*at[i] = dir;
		}
	}
	a->hover = NULL;
	a->scene_w = 0;
	rows_build(a);
}

static void tree_attach(App *a, Node *dir, Node *n) {
	walk_to_map(a);
	dir->kids = realloc(dir->kids, (size_t)(dir->nkids + 1) * sizeof *dir->kids);
	dir->kids[dir->nkids++] = n;
	n->parent = dir;
	if (n->mtime > dir->mtime) {
		dir->mtime = n->mtime;
	}
	tree_change(dir, n->size, n->files, true);
	a->scene_w = 0;
	rows_build(a);
}

/* Takes what is at path into the tree in dir, and selects it. */
static void tree_take(App *a, Node *dir, const char *path) {
	char err[1200];
	Node *n = scan_one(path, err, sizeof err);
	if (!n) {
		say(a, true, "%s", err);
		return;
	}
	tree_attach(a, dir, n);
	select_node(a, n);
}

/* the folder a paste or a new folder goes into: the node itself, or its folder */
static Node *target_dir(Node *n) { return n->is_dir ? n : n->parent; }

static bool menu_enabled(const App *a, int item) {
	const Node *n = a->menu_node;
	if (!n || a->thread) {
		return false; /* the tree is still being built: nothing is changed under the scan */
	}
	bool root = n == a->root;
	switch (item) {
	case M_CUT:
	case M_RENAME:
	case M_DELETE:
	case M_PURGE:
		return !root;
	case M_PASTE:
		return a->clip[0] && !a->job;
	default:
		return true;
	}
}

static int paste_thread(void *arg) {
	App *a = arg;
	a->job_ok = fo_paste(a->job_src, a->job_dir, a->job_move, a->job_out, sizeof a->job_out, a->job_err, sizeof a->job_err);
	SDL_AtomicSet(&a->job_done, 1);
	SDL_Event wake = {.type = SDL_USEREVENT};
	SDL_PushEvent(&wake);
	return 0;
}

/* The paste has ended: the tree takes in what it made, and for a cut
 * gives up what it moved. */
static void paste_finish(App *a) {
	SDL_WaitThread(a->job, NULL);
	a->job = NULL;
	if (!a->job_ok) {
		say(a, true, "Paste failed: %s", a->job_err);
		return;
	}
	if (a->job_move) {
		Node *old = node_find(a, a->job_src);
		if (old && old != a->root) {
			tree_detach(a, old);
			scan_free(old);
		}
		a->clip[0] = '\0'; /* what was cut is now where it went */
	}
	Node *dir = node_find(a, a->job_dir);
	if (dir) {
		tree_take(a, dir, a->job_out);
	}
	const char *slash = strrchr(a->job_out, '/');
	say(a, false, a->job_move ? "Moved here: %s" : "Pasted: %s", slash ? slash + 1 : a->job_out);
}

static void edit_start(App *a, Edit kind, Node *n) {
	a->edit = kind;
	a->edit_node = n;
	snprintf(a->edit_text, sizeof a->edit_text, "%s", kind == EDIT_RENAME ? n->name : "New Folder");
	a->edit_fresh = true;
	if (kind == EDIT_NEWDIR) {
		n->open = true;
	}
	select_node(a, n); /* opens the list down to it and scrolls to it */
	int row = row_of(a, n) + (kind == EDIT_NEWDIR ? 1 : 0), fit = rows_fit(a);
	if (row >= a->scroll + fit) {
		a->scroll = row - fit + 1;
	}
	SDL_StartTextInput();
}

static void edit_stop(App *a) {
	a->edit = EDIT_NONE;
	a->edit_node = NULL;
	SDL_StopTextInput();
}

static void edit_commit(App *a) {
	Node *n = a->edit_node;
	Edit kind = a->edit;
	char text[256];
	snprintf(text, sizeof text, "%s", a->edit_text);
	edit_stop(a);
	if (!n) {
		return;
	}
	char path[4096], out[4400], err[1200];
	node_path(n, path, sizeof path);
	if (kind == EDIT_RENAME) {
		if (!fo_rename(path, text, out, sizeof out, err, sizeof err)) {
			say(a, true, "Rename failed: %s", err);
			return;
		}
		free(n->name);
		n->name = strdup(text);
		a->scene_w = 0;
		rows_build(a);
		say(a, false, "Renamed to %s", text);
	} else {
		if (!fo_mkdir(path, text, out, sizeof out, err, sizeof err)) {
			say(a, true, "New folder failed: %s", err);
			return;
		}
		tree_take(a, n, out);
		say(a, false, "New folder: %s", text);
	}
}

static void menu_do(App *a, int item) {
	Node *n = a->menu_node;
	a->menu_open = false;
	if (!n) {
		return;
	}
	if (a->thread) {
		say(a, true, "%s", "Wait for the scan to finish: the files are left alone while it counts");
		return;
	}
	if (!menu_enabled(a, item)) {
		return;
	}
	char path[4096];
	node_path(n, path, sizeof path);
	switch (item) {
	case M_COPY:
	case M_CUT:
		snprintf(a->clip, sizeof a->clip, "%s", path);
		a->clip_cut = item == M_CUT;
		say(a, false, item == M_CUT ? "Cut: %s  (right-click a folder, then Paste)" : "Copied: %s  (right-click a folder, then Paste)",
		    n->name);
		break;
	case M_PASTE: {
		Node *dir = target_dir(n);
		node_path(dir, a->job_dir, sizeof a->job_dir);
		snprintf(a->job_src, sizeof a->job_src, "%s", a->clip);
		a->job_move = a->clip_cut;
		/* a cut put back where it came from is already there */
		const char *slash = strrchr(a->job_src, '/');
		size_t at = slash ? (size_t)(slash - a->job_src) : 0;
		if (a->job_move && strlen(a->job_dir) == (at ? at : 1) && !strncmp(a->job_src, a->job_dir, at ? at : 1)) {
			say(a, false, "%s is already in this folder", slash ? slash + 1 : a->job_src);
			break;
		}
		SDL_AtomicSet(&a->job_done, 0);
		a->job = SDL_CreateThread(paste_thread, "paste", a);
		say(a, false, "Pasting %s ...", slash ? slash + 1 : a->job_src);
		break;
	}
	case M_RENAME:
		edit_start(a, EDIT_RENAME, n);
		break;
	case M_NEWDIR:
		edit_start(a, EDIT_NEWDIR, target_dir(n));
		break;
	case M_DELETE: {
		char err[1200];
		if (!fo_trash(path, err, sizeof err)) {
			say(a, true, "Not moved to Trash: %s", err);
			break;
		}
		char name[1024];
		snprintf(name, sizeof name, "%s", n->name);
		tree_detach(a, n);
		scan_free(n);
		say(a, false, "Moved to Trash: %s", name);
		break;
	}
	case M_PURGE:
		a->ask_node = n; /* the box asks; ask_answer does it */
		break;
	default:
		break;
	}
}

static void menu_open_on(App *a, Node *n, float x, float y) {
	a->menu_open = true;
	a->menu_node = n;
	a->menu_x = x;
	a->menu_y = y;
}

/* The answer to "Delete permanently?": yes deletes it for good. */
static void ask_answer(App *a, bool yes) {
	Node *n = a->ask_node;
	a->ask_node = NULL;
	if (!yes || !n || n == a->root || a->thread) {
		if (n && !yes) {
			say(a, false, "Kept: %s", n->name);
		}
		return;
	}
	char path[4096], err[1200], name[1024];
	node_path(n, path, sizeof path);
	snprintf(name, sizeof name, "%s", n->name);
	if (!fo_remove(path, err, sizeof err)) {
		/* some of it may have gone: the folder's sizes are read again */
		say(a, true, "Not all deleted: %s", err);
		Node *dir = n->parent;
		tree_detach(a, n);
		scan_free(n);
		struct stat st;
		if (dir && stat(path, &st) == 0) {
			tree_take(a, dir, path);
		}
		return;
	}
	tree_detach(a, n);
	scan_free(n);
	say(a, false, "Deleted permanently: %s", name);
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

/* the scan's hold on the tree, between finding a thing and hanging it on */
static void scan_lock(void *arg) {
	App *a = arg;
	SDL_LockMutex(a->tree_lock);
}

static void scan_unlock(void *arg) {
	App *a = arg;
	SDL_UnlockMutex(a->tree_lock);
	if (SDL_AtomicGet(&a->want_tree)) {
		SDL_Delay(1); /* the window waits for a frame: let it in */
	}
}

/* the window's hold on the tree, for a frame */
static void tree_hold(App *a) {
	SDL_AtomicSet(&a->want_tree, 1);
	SDL_LockMutex(a->tree_lock);
	SDL_AtomicSet(&a->want_tree, 0);
}

static void tree_let_go(App *a) { SDL_UnlockMutex(a->tree_lock); }

static void scan_start(App *a, const char *path) {
	if (a->thread || !path[0]) {
		return;
	}
	if (a->edit) {
		edit_stop(a);
	}
	a->menu_open = false;
	a->ask_node = NULL;
	if (walk_on(a->walk)) {
		walk_leave(a->walk); /* its rooms are the tree that goes now */
	}
	if (a->root) {
		if (a->tree_drop) {
			a->tree_drop(a->host);
		}
		scan_free(a->root);
		a->root = a->focus = a->sel = a->hover = a->menu_node = NULL;
		a->told_focus = a->told_sel = a->told_room = NULL;
		scene_clear(&a->scene);
		scene_clear(&a->target);
		scene_clear(&a->shown);
	}
	snprintf(a->scan_path, sizeof a->scan_path, "%s", path);
	a->progress = (ScanProgress){0};
	a->progress.lock = scan_lock;
	a->progress.unlock = scan_unlock;
	a->progress.lock_arg = a;
	a->message[0] = '\0';
	a->live = false;
	a->spin = false;
	SDL_AtomicSet(&a->scan_done, 0);
	a->screen = SCREEN_SCANNING;
	a->thread = SDL_CreateThread(scan_thread, "scan", a);
}

/* The scan has put up its first folder: the tree is shown from now on,
 * growing as the scan goes. Called holding the tree. */
static void scan_show(App *a) {
	if (!a->thread || a->root || !a->progress.root) {
		return;
	}
	a->root = a->progress.root;
	a->gen++;
	a->root->open = true;
	a->sel = a->root;
	a->scroll = 0;
	a->live = true;
	a->built_at = 0;
	set_focus(a, a->root);
	rows_build(a);
	a->screen = SCREEN_TREE;
}

/* Called not holding the tree: the scan may be waiting for it to end. */
static void scan_finish(App *a) {
	SDL_WaitThread(a->thread, NULL);
	a->thread = NULL;
	bool was_live = a->root != NULL;
	a->live = false;
	if (!a->scan_result) {
		if (walk_on(a->walk)) {
			walk_leave(a->walk);
		}
		/* a live scan leaves what it built: it goes, and with it everything pointing in */
		if (a->progress.root) {
			if (a->tree_drop) {
				a->tree_drop(a->host);
			}
			scan_free(a->progress.root);
			a->gen++;
		}
		a->told_focus = a->told_sel = a->told_room = NULL;
		a->progress.root = NULL;
		a->root = a->focus = a->sel = a->hover = a->menu_node = a->edit_node = a->ask_node = NULL;
		scene_clear(&a->scene);
		scene_clear(&a->target);
		snprintf(a->message, sizeof a->message, "%s", a->scan_err);
		a->ndisks = disks_list(a->disks, 32);
		a->screen = SCREEN_DISKS;
		return;
	}
	if (a->root != a->scan_result) {
		a->gen++;
	}
	a->root = a->scan_result;
	a->progress.root = NULL;
	if (!was_live) {
		a->root->open = true;
		a->sel = a->root;
		a->scroll = 0;
		set_focus(a, a->root);
	} else {
		/* where one has gone while it scanned stays; the last sizes ease in */
		a->scene_w = 0;
		a->ease_until = SDL_GetTicks() + 1500;
	}
	rows_build(a);
	a->screen = SCREEN_TREE;
	a->spin = true; /* the 3D view turns until it is dragged */
}

static void show_disks(App *a) {
	if (walk_on(a->walk)) {
		walk_leave(a->walk);
	}
	a->ndisks = disks_list(a->disks, 32);
	a->screen = SCREEN_DISKS;
}

/* ======================================================================
 *   The solid view's camera and what the mouse is on
 * ====================================================================== */

static Vec3 camera_target(const App *a) {
	return v3_add(a->view == VIEW_BLOCKS ? v3(0, 0.06f, 0) : v3(0, -0.2f, 0), a->pan3);
}

static Vec3 camera_eye(const App *a) {
	Vec3 back = v3(cosf(a->pitch) * sinf(a->yaw), sinf(a->pitch), cosf(a->pitch) * cosf(a->yaw));
	return v3_add(camera_target(a), v3_scale(back, a->dist * (a->mini ? 0.78f : 1))); /* the corner: closer in */
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
		/* the point in the layout, before the zoom and the slide */
		float x = (mx - view.x - a->pan2x) / a->zoom2, y = (my - view.y - a->pan2y) / a->zoom2;
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

static const char *MENU_LABEL[M_COUNT] = {"Copy", "Cut", "Paste", "Rename", "New Folder", "Move to Trash",
                                          "Delete permanently"};
static const char *MENU_KEY[M_COUNT] = {"Ctrl+C", "Ctrl+X", "Ctrl+V", "F2", "Ctrl+Shift+N", "Del", "Shift+Del"};

static float menu_item_h(const App *a) { return (float)a->text_h + 10; }

/* the menu's box, kept inside the window */
static Rect menu_rect(const App *a) {
	float w = 0;
	for (int i = 0; i < M_COUNT; i++) {
		float iw = (float)(d2_text_width(MENU_LABEL[i]) + d2_text_width(MENU_KEY[i]));
		w = iw > w ? iw : w;
	}
	w += (float)a->char_w * 7;
	float h = menu_item_h(a) * M_COUNT + 4;
	float x = a->menu_x, y = a->menu_y;
	x = x + w > (float)a->w ? (float)a->w - w : x;
	y = y + h > (float)a->h ? (float)a->h - h : y;
	return (Rect){x < 0 ? 0 : x, y < 0 ? 0 : y, w, h};
}

static int menu_item_at(const App *a, float x, float y) {
	Rect m = menu_rect(a);
	if (!in_rect(m, x, y)) {
		return -1;
	}
	int i = (int)((y - m.y - 2) / menu_item_h(a));
	return i < 0 ? 0 : i >= M_COUNT ? M_COUNT - 1 : i;
}

/* ======================================================================
 *   Zoom and slide: the buttons in the view's corner, the wheel, the
 *   right button held and dragged
 * ====================================================================== */

enum { VB_ZOOMIN, VB_ZOOMOUT, VB_RESET, VB_COUNT };
static const int VB_ICON[VB_COUNT] = {ICON_ZOOMIN, ICON_ZOOMOUT, ICON_RESET};
static const char *VB_TIP[VB_COUNT] = {"Zoom in (+)", "Zoom out (-)", "Reset the view (0)"};

/* the buttons, one above the other in the view's top right corner */
static void view_buttons(const App *a, Rect out[VB_COUNT]) {
	Rect v = view_rect(a);
	float s = a->icon_px + 12, gap = 4;
	s = s < (float)a->text_h + 10 ? (float)a->text_h + 10 : s;
	for (int i = 0; i < VB_COUNT; i++) {
		out[i] = (Rect){v.x + v.w - s - 8, v.y + 8 + (s + gap) * (float)i, s, s};
	}
}

static int view_button_at(const App *a, float x, float y) {
	if (a->screen != SCREEN_TREE) {
		return -1;
	}
	Rect b[VB_COUNT];
	view_buttons(a, b);
	for (int i = 0; i < VB_COUNT; i++) {
		if (in_rect(b[i], x, y)) {
			return i;
		}
	}
	return -1;
}

/* Zooms by f (above 1 nearer) about the point (px, py) on the screen: in
 * 3D the camera comes nearer what it looks at; in 2D the picture grows
 * and the point under the mouse stays under it. */
static void zoom_by(App *a, float f, float px, float py) {
	if (a->solid) {
		a->dist /= f;
		a->dist = a->dist < 0.15f ? 0.15f : a->dist > 6 ? 6 : a->dist;
		return;
	}
	Rect v = view_rect(a);
	float z = a->zoom2 * f;
	z = z < 0.25f ? 0.25f : z > 60 ? 60 : z;
	float sx = (px - v.x - a->pan2x) / a->zoom2, sy = (py - v.y - a->pan2y) / a->zoom2;
	a->zoom2 = z;
	a->pan2x = px - v.x - sx * z;
	a->pan2y = py - v.y - sy * z;
}

/* Slides the view by (dx, dy) pixels, so what is under the mouse goes
 * with it: in 3D sideways and up or down as the camera sees it. */
static void slide_by(App *a, float dx, float dy) {
	if (!a->solid) {
		a->pan2x += dx;
		a->pan2y += dy;
		return;
	}
	Rect v = view_rect(a);
	Vec3 eye = camera_eye(a), f = v3_norm(v3_sub(camera_target(a), eye));
	Vec3 side = v3_norm(v3_cross(f, v3(0, 1, 0))), up = v3_cross(side, f);
	/* a pixel's width at the distance of what the camera looks at */
	float per = 2 * a->dist * tanf(FOV / 2) / v.h;
	a->pan3 = v3_add(a->pan3, v3_add(v3_scale(side, -dx * per), v3_scale(up, dy * per)));
	float far = 3; /* never slid off into nothing */
	a->pan3.x = a->pan3.x < -far ? -far : a->pan3.x > far ? far : a->pan3.x;
	a->pan3.y = a->pan3.y < -far ? -far : a->pan3.y > far ? far : a->pan3.y;
	a->pan3.z = a->pan3.z < -far ? -far : a->pan3.z > far ? far : a->pan3.z;
}

static void view_reset(App *a) {
	camera_reset(a);
}

static void press_view_button(App *a, int b) {
	Rect v = view_rect(a);
	if (b == VB_ZOOMIN || b == VB_ZOOMOUT) {
		zoom_by(a, b == VB_ZOOMIN ? 1.25f : 0.8f, v.x + v.w / 2, v.y + v.h / 2);
	} else if (b == VB_RESET) {
		view_reset(a);
	}
}

/* ======================================================================
 *   Walk mode: the figure in the view's bottom right corner
 * ====================================================================== */

static Rect fig_rect(const App *a) {
	Rect b[VB_COUNT];
	view_buttons(a, b);
	Rect v = view_rect(a);
	float s = b[0].w;
	return (Rect){v.x + v.w - s - 8, v.y + v.h - s - 8, s, s};
}

static bool fig_at(const App *a, float x, float y) {
	/* a folder whose things take no space (/dev) can still be walked into */
	bool walkable = a->scene.n || (a->focus && a->focus->nkids);
	return a->screen == SCREEN_TREE && walkable && !walk_on(a->walk) && in_rect(fig_rect(a), x, y);
}

/* into walk mode in the folder of what is under (x, y), or the focus */
static void walk_start(App *a, float x, float y, bool dropped) {
	Rect v = view_rect(a);
	const Item *it = dropped && in_rect(v, x, y) ? pick(a, v, x, y) : NULL;
	Node *n = it && it->node ? it->node : a->focus;
	if (!n) {
		return;
	}
	a->pressed = a->dragged = a->rpressed = a->rdragged = false;
	a->menu_open = false;
	walk_enter(a->walk, n, NULL);
	SDL_StopTextInput();
}

/* out of walk mode, the map showing the folder you were walking in */
static void walk_to_map(App *a) {
	if (!walk_on(a->walk)) {
		return;
	}
	Node *room = walk_leave(a->walk);
	if (room && room != a->focus) {
		set_focus(a, room);
		select_node(a, room);
	}
}

/* the figure: a head and a body, drawn with the flat shapes */
static void draw_figure(const App *a, float cx, float cy, float s, bool lit) {
	const Theme *t = theme(a);
	SDL_Color c = lit ? rgb(t->accent) : rgb(t->text);
	d2_disc(cx, cy - s * 0.24f, s * 0.13f, lighter(c, 0.2f), c);
	d2_rect(cx - s * 0.14f, cy - s * 0.08f, s * 0.28f, s * 0.26f, c);
	d2_rect(cx - s * 0.13f, cy + s * 0.18f, s * 0.1f, s * 0.2f, c);
	d2_rect(cx + s * 0.03f, cy + s * 0.18f, s * 0.1f, s * 0.2f, c);
	d2_rect(cx - s * 0.24f, cy - s * 0.07f, s * 0.08f, s * 0.22f, c);
	d2_rect(cx + s * 0.16f, cy - s * 0.07f, s * 0.08f, s * 0.22f, c);
}

static void draw_fig_button(const App *a) {
	const Theme *t = theme(a);
	Rect r = fig_rect(a);
	bool on = in_rect(r, a->mx, a->my) && !a->fig_drag;
	d2_rect(r.x, r.y, r.w, r.h, on ? rgb(t->panel_hover) : rgb(t->panel));
	d2_frame(r.x, r.y, r.w, r.h, 1, on ? rgb(t->accent) : rgb(t->border));
	if (!a->fig_drag) {
		draw_figure(a, r.x + r.w / 2, r.y + r.h / 2, r.h, on);
	}
	if (on) {
		const char *tip = "Drag me onto the map to walk there (click: walk here)";
		float w = (float)d2_text_width(tip) + 12, h = (float)a->text_h + 8;
		float x = r.x - w - 6, y = r.y + (r.h - h) / 2;
		d2_rect(x, y, w, h, rgb(t->bg_alt));
		d2_frame(x, y, w, h, 1, rgb(t->accent));
		d2_text(x + 6, y + 4, tip, rgb(t->text));
	}
}

/* ======================================================================
 *   Drawing
 * ====================================================================== */

static void button(const App *a, Rect r, const char *label, int icon, bool active) {
	const Theme *t = theme(a);
	SDL_Color fill = active ? rgb(t->accent) : in_rect(r, a->mx, a->my) ? rgb(t->panel_hover) : rgb(t->panel);
	SDL_Color fg = active ? ink(fill) : rgb(t->text);
	d2_rect(r.x, r.y, r.w, r.h, fill);
	d2_frame(r.x, r.y, r.w, r.h, 1, rgb(t->border));
	float x = r.x + (float)a->char_w;
	if (icon >= 0) {
		d2_icon(a->icon[icon], x, r.y + (r.h - a->icon_px) / 2, a->icon_px, fg);
		x += a->icon_px + (float)a->char_w * 0.6f;
	}
	d2_text(x, r.y + (r.h - (float)a->text_h) / 2, label, fg);
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
		if (i == B_PATH || b[i].w <= 0) {
			continue;
		}
		bool active = (i == B_DISKS && a->screen == SCREEN_DISKS) ||
		              (tree && ((i == B_BLOCKS && a->view == VIEW_BLOCKS) || (i == B_GRAPH && a->view == VIEW_GRAPH) ||
		                        (i == B_FLAT && !a->solid) || (i == B_SOLID && a->solid)));
		button(a, b[i], button_label(a, i, buf, sizeof buf), button_icon(i), active);
	}
	Rect p = b[B_PATH];
	if (p.w <= 0) {
		return;
	}
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

/* A folder in SYN-SHELL's own window: this same program, run again
 * without --space, so it opens there and not in whatever xdg-open picks. */
static void open_in_shell(const char *path) {
#ifdef _WIN32
	SDL_OpenURL(path);
#else
	/* through a child that leaves at once, so the window is nobody's to
	 * wait on (fileops.c waits on its own children: SIGCHLD stays as is) */
	char *argv[] = {"syn-shell", (char *)path, NULL};
	pid_t mid = fork();
	if (mid == 0) {
		pid_t pid;
		_exit(posix_spawn(&pid, "/proc/self/exe", NULL, NULL, argv, environ) == 0 ? 0 : 1);
	}
	int status = 1;
	if (mid > 0) {
		while (waitpid(mid, &status, 0) < 0 && errno == EINTR) {
		}
	}
	if (mid < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		SDL_OpenURL(path);
	}
#endif
}

static void draw_disks(const App *a) {
	const Theme *t = theme(a);
	float w = (float)a->w - 80 > 900 ? 900 : (float)a->w - 80, x = ((float)a->w - w) / 2;
	float y = bar_rect(a).h + 30, th = (float)a->text_h;
	d2_text(x, y, "SYN-SHELL SPACE", accent_text(t));
	d2_text(x + (float)a->char_w * 17, y, "where the space has gone: pick a disk or a network share", rgb(t->text));
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
		/* a folder the scan is still in: its size is so far, and counting */
		text_right(cols[COL_SIZE], ty, buf, a->live && !n->done ? accent_text(t) : rgb(t->text));
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
	for (int i = 0; i < s->n && !a->mini; i++) { /* the corner is too small for names */
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
	snprintf(line, sizeof line, "%s   %s   %.1f%%%s", item_name(it, buf, sizeof buf), size, share,
	         a->live && it->node && !it->node->done ? "   counting" : "");
	float w = (float)d2_text_width(line) + 12, h = (float)a->text_h + 8;
	float x = a->mx + 14, y = a->my + 18;
	x = x + w > v.x + v.w ? v.x + v.w - w : x;
	y = y + h > v.y + v.h ? a->my - h - 6 : y;
	d2_rect(x, y, w, h, rgb(t->bg_alt));
	d2_frame(x, y, w, h, 1, rgb(t->accent));
	d2_text(x + 6, y + 4, line, rgb(t->text));
}

/* ---- the growing tree, eased into ---- */

/* Puts back in size order the children of every folder something under
 * which has grown since: the scan only appends, the view wants the
 * biggest first. Called holding the tree. */
static void live_sort(Node *n) {
	if (!n->dirty) {
		return;
	}
	n->dirty = false;
	qsort(n->kids, (size_t)n->nkids, sizeof *n->kids, by_size_desc);
	for (int i = 0; i < n->nkids; i++) {
		if (n->kids[i]->is_dir) {
			live_sort(n->kids[i]);
		}
	}
}

static void scene_copy(Scene *to, const Scene *from) {
	scene_clear(to);
	for (int i = 0; i < from->n; i++) {
		*scene_add(to) = from->v[i];
	}
	for (int e = 0; e < from->nedge; e += 2) {
		scene_join(to, from->edge[e], from->edge[e + 1]);
	}
}

/* what an item shows, the same from one layout to the next; the graph's
 * one "more" ball has no node */
static const void *item_key(const Item *it) { return it->node ? (const void *)it->node : (const void *)&item_key; }

static unsigned key_hash(const void *k) { return (unsigned)(((uintptr_t)k >> 4) * 2654435761u); }

static float ease_to(float from, float to, float k) { return from + (to - from) * k; }

/* One frame's step of the scene toward the target, k of the way: an item
 * already shown moves and grows from where it was, a new one grows out
 * of nothing at its place. Returns how far the furthest still has to go,
 * in pixels. */
static float ease_step(App *a, float k) {
	const Scene *from = &a->scene, *to = &a->target;
	int cap = 64;
	while (cap < from->n * 2) {
		cap *= 2;
	}
	if (cap > a->ease_cap) {
		a->ease_key = SDL_realloc(a->ease_key, (size_t)cap * sizeof *a->ease_key);
		a->ease_at = SDL_realloc(a->ease_at, (size_t)cap * sizeof *a->ease_at);
		a->ease_cap = cap;
	}
	memset(a->ease_key, 0, (size_t)cap * sizeof *a->ease_key);
	for (int i = 0; i < from->n; i++) {
		const void *key = item_key(&from->v[i]);
		unsigned h = key_hash(key) & (unsigned)(cap - 1);
		while (a->ease_key[h] && a->ease_key[h] != key) {
			h = (h + 1) & (unsigned)(cap - 1);
		}
		a->ease_key[h] = key;
		a->ease_at[h] = i;
	}
	scene_copy(&a->next, to);
	float furthest = 0;
	for (int i = 0; i < a->next.n; i++) {
		Item *it = &a->next.v[i];
		const Item *t = &to->v[i];
		const void *key = item_key(t);
		unsigned h = key_hash(key) & (unsigned)(cap - 1);
		while (a->ease_key[h] && a->ease_key[h] != key) {
			h = (h + 1) & (unsigned)(cap - 1);
		}
		Item was;
		if (a->ease_key[h]) {
			was = from->v[a->ease_at[h]];
		} else {
			/* new: nothing yet, at its own middle */
			was = *t;
			was.w = was.h = 0;
			if (!t->ball) {
				was.x = t->x + t->w / 2;
				was.y = t->y + t->h / 2;
			}
			was.size3[0] = was.size3[1] = was.size3[2] = 0;
			if (!t->ball) {
				was.pos[1] = t->pos[1] - t->size3[1] / 2;
			}
		}
		it->x = ease_to(was.x, t->x, k);
		it->y = ease_to(was.y, t->y, k);
		it->w = ease_to(was.w, t->w, k);
		it->h = ease_to(was.h, t->h, k);
		for (int c = 0; c < 3; c++) {
			it->pos[c] = ease_to(was.pos[c], t->pos[c], k);
			it->size3[c] = ease_to(was.size3[c], t->size3[c], k);
		}
		float off = fabsf(t->x - it->x) + fabsf(t->y - it->y) + fabsf(t->w - it->w) + fabsf(t->h - it->h);
		furthest = off > furthest ? off : furthest;
	}
	Scene swap = a->scene;
	a->scene = a->next;
	a->next = swap;
	return furthest;
}

/* Whether the view is moving by itself, so frames are drawn without a wait. */
static bool moving(const App *a) {
	if (a->screen != SCREEN_TREE) {
		return false;
	}
	if (walk_on(a->walk) || a->fig_drag) {
		return true; /* every frame while you walk */
	}
	return a->live || !SDL_TICKS_PASSED(SDL_GetTicks(), a->ease_until) || (a->spin && a->solid && !a->pressed && !a->rpressed);
}

/* Only the levels the depth slider asks for: the deeper items dropped, and
 * the lines to them; what stays keeps its place and its true size. */
static void keep_levels(Scene *s, int levels) {
	if (levels >= LEVELS_ALL || !s->n) {
		return;
	}
	int *to = malloc(sizeof *to * (size_t)s->n), n = 0;
	if (!to) {
		return;
	}
	for (int i = 0; i < s->n; i++) {
		if (s->v[i].depth <= levels) {
			to[i] = n;
			s->v[n++] = s->v[i];
		} else {
			to[i] = -1;
		}
	}
	s->n = n;
	/* nedge counts ints, two to a line (see scene_join) */
	int e = 0;
	for (int k = 0; k + 1 < s->nedge; k += 2) {
		int p = to[s->edge[k]], c = to[s->edge[k + 1]];
		if (p >= 0 && c >= 0) {
			s->edge[e++] = p;
			s->edge[e++] = c;
		}
	}
	s->nedge = e;
	free(to);
}

/* The view's layout brought up to date, and a step of easing into it.
 * Called holding the tree: this is all of drawing that walks it. */
static void view_update(App *a) {
	if (a->screen != SCREEN_TREE) {
		return;
	}
	const Theme *t = theme(a);
	Rect v = view_rect(a);
	Uint32 now = SDL_GetTicks();
	bool stale = a->scene_w != v.w || a->scene_h != v.h;
	bool window_changed = stale && a->scene_w != 0;
	/* while it scans, the layout is made again from the tree as it stands: the graph ten
	 * times a second; the block map, whose rows shuffle as sizes change, three times, each
	 * change settling quickly, so it reads as a shape filling in and not a churn */
	Uint32 every = a->view == VIEW_BLOCKS ? 300 : 100;
	if (stale || (a->live && now - a->built_at >= every)) {
		if (a->live) {
			live_sort(a->root);
			rows_build(a);
		}
		if (a->view == VIEW_BLOCKS) {
			blocks_build(&a->target, a->focus, v.w, v.h, a->text_h, a->lift, t);
		} else {
			graph_build(&a->target, a->focus, v.w, v.h, a->lift, t);
		}
		keep_levels(&a->target, a->levels);
		a->built_at = now;
		bool easing = a->live || !SDL_TICKS_PASSED(now, a->ease_until);
		/* a new size of window, a new folder or view: shown as it is; the scan's growth: eased */
		if (!easing || a->snap || window_changed) {
			scene_copy(&a->scene, &a->target);
		}
		a->snap = false;
		a->scene_w = v.w;
		a->scene_h = v.h;
	}
	if (a->live || !SDL_TICKS_PASSED(now, a->ease_until)) {
		/* the same at any frame rate: the graph glides, the block map settles in about 0.15 s */
		float k = 1 - expf(-a->dt * (a->view == VIEW_BLOCKS ? 20.0f : 8.0f));
		float left = ease_step(a, k < 0.02f ? 0.02f : k > 1 ? 1 : k);
		if (!a->live && left < 0.5f) {
			scene_copy(&a->scene, &a->target);
			a->ease_until = now;
		}
	}
}

/* the zoom and reset buttons, solid over the view, and what the one under the mouse does */
static void draw_view_buttons(const App *a, int hover) {
	const Theme *t = theme(a);
	Rect b[VB_COUNT];
	view_buttons(a, b);
	for (int i = 0; i < VB_COUNT; i++) {
		Rect r = b[i];
		bool on = i == hover;
		d2_rect(r.x, r.y, r.w, r.h, on ? rgb(t->panel_hover) : rgb(t->panel));
		d2_frame(r.x, r.y, r.w, r.h, 1, on ? rgb(t->accent) : rgb(t->border));
		d2_icon(a->icon[VB_ICON[i]], r.x + (r.w - a->icon_px) / 2, r.y + (r.h - a->icon_px) / 2, a->icon_px,
		        on ? accent_text(t) : rgb(t->text));
	}
	if (hover >= 0) {
		const char *tip = VB_TIP[hover];
		float w = (float)d2_text_width(tip) + 12, h = (float)a->text_h + 8;
		Rect r = b[hover];
		float x = r.x - w - 6, y = r.y + (r.h - h) / 2;
		d2_rect(x, y, w, h, rgb(t->bg_alt));
		d2_frame(x, y, w, h, 1, rgb(t->accent));
		d2_text(x + 6, y + 4, tip, rgb(t->text));
	}
}

/* Draws the view. Not holding the tree while it scans: the nodes the
 * scene points at stay where they are, only their counts go up. */
static void draw_view(App *a) {
	const Theme *t = theme(a);
	Rect v = view_rect(a);
	if (walk_on(a->walk)) {
		SDL_Color sky = rgb(t->bg);
		sky.a = (Uint8)roundf(255 * (1 - a->see / 100));
		walk_draw(a->walk, v.x, v.y, v.w, v.h, t, sky, a->mx, a->my);
		return;
	}
	int vb = a->mini ? -1 : view_button_at(a, a->mx, a->my);
	const Item *under = !a->mini && in_rect(v, a->mx, a->my) && !a->dragged && !a->rdragged && vb < 0 ? pick(a, v, a->mx, a->my) : NULL;
	if (a->mini) {
		a->hover = NULL;
	} else if (in_rect(v, a->mx, a->my)) {
		a->hover = under ? under->node : NULL;
	}
	/* the background only goes see-through; what is drawn on it stays solid */
	SDL_Color back = rgb(t->bg);
	back.a = (Uint8)roundf(255 * (1 - a->see / 100));
	d2_rect(v.x, v.y, v.w, v.h, back);
	if (!a->scene.n) {
		d2_text(v.x + 20, v.y + 20, a->live ? "Scanning..." : "Nothing here takes any space.", rgb(t->text));
		if (!a->live && !a->mini && a->focus && a->focus->nkids) {
			d2_text(v.x + 20, v.y + 20 + (float)a->text_h * 1.5f, "Walk in to see what is here: the figure, bottom right.",
			        mix(rgb(t->text), rgb(t->bg), 0.35f));
		}
	} else if (a->solid) {
		draw_solid(a, v);
	} else {
		/* flat: the layout drawn zoomed and slid, kept inside the view */
		bool moved = a->zoom2 != 1 || a->pan2x != 0 || a->pan2y != 0;
		Scene layout = a->scene;
		if (moved) {
			scene_copy(&a->shown, &a->scene);
			for (int i = 0; i < a->shown.n; i++) {
				Item *it = &a->shown.v[i];
				it->x = it->x * a->zoom2 + a->pan2x;
				it->y = it->y * a->zoom2 + a->pan2y;
				it->w *= a->zoom2;
				it->h *= a->zoom2;
			}
			a->scene = a->shown;
		}
		d2_clip(v.x, v.y, v.w, v.h);
		if (a->view == VIEW_BLOCKS) {
			draw_blocks_flat(a, v);
		} else {
			draw_graph_flat(a, v);
		}
		d2_unclip();
		if (moved) {
			a->shown = a->scene; /* the copy keeps its memory for the next frame */
			a->scene = layout;
		}
	}
	if (a->mini) {
		/* the corner: which folder, how big, over the turning blocks */
		if (a->focus) {
			char size[32], line[300];
			format_size(a->focus->size, size, sizeof size);
			const char *name = a->focus->name, *slash = strrchr(name, '/');
			name = slash && slash[1] ? slash + 1 : name; /* the top of the tree is named by its whole path */
			snprintf(line, sizeof line, "%s  %s%s", name, size, a->live ? "  ..." : "");
			d2_text_fit(v.x + 6, v.y + 4, v.w - 12, line, mix(rgb(t->text), rgb(t->bg), 0.2f));
		}
		return;
	}
	if (a->scene.n) {
		draw_view_buttons(a, vb);
	}
	if (a->scene.n || (a->focus && a->focus->nkids)) {
		draw_fig_button(a);
	}
	if (under) {
		draw_tip(a, v, under);
	}
	if (a->fig_drag && a->fig_moved) {
		/* the figure held, over where it would land */
		float s = fig_rect(a).h * 1.3f;
		draw_figure(a, a->mx, a->my - s * 0.2f, s, true);
	}
}

static void draw_menu(const App *a) {
	const Theme *t = theme(a);
	Rect m = menu_rect(a);
	float ih = menu_item_h(a);
	d2_rect(m.x + 3, m.y + 3, m.w, m.h, (SDL_Color){0, 0, 0, 110}); /* a shadow */
	d2_rect(m.x, m.y, m.w, m.h, rgb(t->bg_alt));
	d2_frame(m.x, m.y, m.w, m.h, 1, rgb(t->accent));
	int hover = menu_item_at(a, a->mx, a->my);
	for (int i = 0; i < M_COUNT; i++) {
		float y = m.y + 2 + ih * (float)i;
		bool on = menu_enabled(a, i);
		if (i == M_RENAME || i == M_DELETE) {
			d2_rect(m.x + 6, y, m.w - 12, 1, rgb(t->border));
		}
		if (i == hover && on) {
			d2_rect(m.x + 1, y + 1, m.w - 2, ih - 2, rgb(t->accent_dim));
			d2_rect(m.x + 1, y + 1, 3, ih - 2, rgb(t->accent));
		}
		SDL_Color c = !on ? mix(rgb(t->text), rgb(t->bg_alt), 0.6f) : i == M_PURGE ? rgb(t->urgent) : rgb(t->text);
		float ty = y + (ih - (float)a->text_h) / 2;
		d2_text(m.x + (float)a->char_w * 2, ty, MENU_LABEL[i], c);
		d2_text(m.x + m.w - (float)a->char_w * 2 - (float)d2_text_width(MENU_KEY[i]), ty, MENU_KEY[i],
		        mix(c, rgb(t->bg_alt), 0.4f));
	}
}

/* the name being typed, in place in the list: over the row being renamed,
 * or as the first row in the folder getting a new one */
static void draw_edit(const App *a) {
	const Theme *t = theme(a);
	int row = row_of(a, a->edit_node);
	if (row < 0) {
		return;
	}
	int depth = a->rows[row].depth;
	if (a->edit == EDIT_NEWDIR) {
		row++;
		depth++;
	}
	Rect l = list_rect(a), cols[COL_COUNT];
	columns(a, cols);
	float rh = row_h(a), y = l.y + rh * (float)(row - a->scroll + 1);
	if (row < a->scroll || y + rh > l.y + l.h) {
		return;
	}
	float x = cols[COL_NAME].x + (float)(depth * a->char_w * 2) + (float)a->char_w * 1.5f;
	float w = l.x + l.w - 6 - x;
	if (a->edit == EDIT_NEWDIR) {
		d2_rect(l.x, y, l.w, rh, rgb(t->bg));
		float sq = (float)a->text_h * 0.55f;
		d2_rect(x, y + (rh - sq) / 2, sq, sq, rgb(t->accent));
		x += sq + (float)a->char_w * 0.75f;
		w -= sq + (float)a->char_w * 0.75f;
	}
	d2_rect(x - 3, y, w + 3, rh, rgb(t->bg));
	d2_frame(x - 3, y, w + 3, rh, 1, rgb(t->accent));
	float tw = (float)d2_text_width(a->edit_text);
	if (a->edit_fresh && a->edit_text[0]) {
		d2_rect(x, y + 2, tw > w - 6 ? w - 6 : tw, rh - 4, rgb(t->accent_dim));
	}
	char shown[300];
	snprintf(shown, sizeof shown, "%s%s", a->edit_text, a->edit_fresh ? "" : "_");
	d2_text_fit(x, y + 2, w - 6, shown, rgb(t->text));
	const char *hint = a->edit == EDIT_NEWDIR ? "new folder: Enter makes it, Esc stops" : "Enter renames, Esc stops";
	float hw = (float)d2_text_width(hint);
	if (tw + hw + (float)a->char_w * 4 < w) {
		d2_text(x + w - hw - 6, y + 2, hint, mix(rgb(t->text), rgb(t->bg), 0.5f));
	}
}

static void draw_sliders(const App *a) {
	const Theme *t = theme(a);
	Rect s = slider_rect(a);
	d2_rect(s.x, s.y, s.w, s.h, rgb(t->bg_alt));
	d2_rect(s.x, s.y, s.w, 1, rgb(t->border));
	d2_rect(slider_cell(a, 1).x, s.y + 4, 1, s.h - 8, rgb(t->border)); /* between the two columns */
	for (int i = 0; i < SL_COUNT; i++) {
		Rect c = slider_cell(a, i), tr = slider_track(a, i);
		bool lit = a->slide_drag == i || (a->slide_drag < 0 && in_rect(c, a->mx, a->my));
		float ty = c.y + (c.h - (float)a->text_h) / 2;
		d2_text(c.x + (float)a->char_w, ty, SL_LABEL[i], lit ? accent_text(t) : rgb(t->text));
		float mid = c.y + c.h / 2, at = tr.x + tr.w * slider_get(a, i);
		d2_rect(tr.x, mid - 1, tr.w, 3, rgb(t->panel));
		d2_rect(tr.x, mid - 1, at - tr.x, 3, rgb(t->accent));
		int ticks = i == SL_DEPTH ? LEVELS_ALL - 1 : 4; /* depth: one a level; the rest: each quarter */
		for (int k = 0; k <= ticks; k++) {
			d2_rect(tr.x + tr.w * (float)k / (float)ticks, mid - 4, 1, 9, rgb(t->border));
		}
		float kw = (float)a->char_w * 1.2f, kh = c.h - 8;
		d2_rect(at - kw / 2, c.y + 4, kw, kh, a->slide_drag == i ? lighter(rgb(t->accent), 0.3f) : rgb(t->accent));
		d2_frame(at - kw / 2, c.y + 4, kw, kh, 1, rgb(t->text));
		char val[16];
		slider_text(a, i, val, sizeof val);
		d2_text(c.x + c.w - (float)a->char_w - (float)d2_text_width(val), ty, val, rgb(t->text));
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
	if (a->job || (a->note[0] && SDL_GetTicks() - a->note_at < 8000)) {
		d2_text_fit(8, ty, s.w - 16, a->note, a->note_bad ? rgb(t->urgent) : accent_text(t));
		return;
	}
	if (a->live) {
		char so_far[32], live[300];
		format_size(a->progress.bytes, so_far, sizeof so_far);
		snprintf(live, sizeof live, "SCANNING  %llu files, %s so far.  Sizes in this colour are still counting.  Esc stops.",
		         (unsigned long long)a->progress.files, so_far);
		d2_text_fit(8, ty, s.w - 16, live, accent_text(t));
		return;
	}
	d2_text_fit(8, ty, s.w - 16, line, rgb(t->text));
}

/* "Delete permanently?": the box, and its two buttons */
static Rect ask_rect(const App *a) {
	float w = (float)a->char_w * 64, h = (float)a->text_h * 8.5f;
	w = w > (float)a->w - 20 ? (float)a->w - 20 : w;
	return (Rect){((float)a->w - w) / 2, ((float)a->h - h) / 2, w, h};
}

static void ask_buttons(const App *a, Rect *yes, Rect *no) {
	Rect r = ask_rect(a);
	float bw = (float)a->char_w * 22, bh = (float)a->text_h + 12, y = r.y + r.h - bh - 12;
	*yes = (Rect){r.x + r.w / 2 - bw - 8, y, bw, bh};
	*no = (Rect){r.x + r.w / 2 + 8, y, bw, bh};
}

static void draw_ask(const App *a) {
	const Theme *t = theme(a);
	const Node *n = a->ask_node;
	d2_rect(0, 0, (float)a->w, (float)a->h, (SDL_Color){0, 0, 0, 150});
	Rect r = ask_rect(a), yes, no;
	ask_buttons(a, &yes, &no);
	d2_rect(r.x, r.y, r.w, r.h, rgb(t->bg_alt));
	d2_frame(r.x, r.y, r.w, r.h, 2, rgb(t->urgent));
	float x = r.x + (float)a->char_w * 2, y = r.y + 12, th = (float)a->text_h, room = r.w - (float)a->char_w * 4;
	d2_text(x, y, "DELETE PERMANENTLY?", rgb(t->urgent));
	char path[4096], size[32], line[200];
	node_path(n, path, sizeof path);
	format_size(n->size, size, sizeof size);
	d2_text_fit(x, y + th * 1.5f, room, path, rgb(t->text));
	if (n->is_dir) {
		snprintf(line, sizeof line, "%s in %llu files, the folder and everything in it", size, (unsigned long long)n->files);
	} else {
		snprintf(line, sizeof line, "%s", size);
	}
	d2_text_fit(x, y + th * 2.7f, room, line, rgb(t->text));
	d2_text_fit(x, y + th * 3.9f, room, "It does not go to the Trash and cannot be got back.", mix(rgb(t->text), rgb(t->bg_alt), 0.25f));
	bool on_yes = in_rect(yes, a->mx, a->my), on_no = in_rect(no, a->mx, a->my);
	d2_rect(yes.x, yes.y, yes.w, yes.h, on_yes ? rgb(t->urgent) : rgb(t->panel));
	d2_frame(yes.x, yes.y, yes.w, yes.h, 1, rgb(t->urgent));
	d2_rect(no.x, no.y, no.w, no.h, on_no ? rgb(t->panel_hover) : rgb(t->panel));
	d2_frame(no.x, no.y, no.w, no.h, 1, rgb(t->border));
	const char *ys = "Y  YES, DELETE", *ns = "N  NO, KEEP IT";
	d2_text(yes.x + (yes.w - (float)d2_text_width(ys)) / 2, yes.y + 6, ys, on_yes ? ink(rgb(t->urgent)) : rgb(t->urgent));
	d2_text(no.x + (no.w - (float)d2_text_width(ns)) / 2, no.y + 6, ns, rgb(t->text));
}

static void draw(App *a) {
	const Theme *t = theme(a);
	/* cleared see-through: everything but the view's background paints itself solid */
	draw_begin(a->w, a->h, (SDL_Color){0, 0, 0, 0});
	if (a->screen == SCREEN_DISKS && a->embedded) {
		d2_rect(0, 0, (float)a->w, (float)a->h, rgb(t->bg));
		const char *say_ = a->message[0] ? a->message : "Nothing scanned yet";
		d2_text_fit(20, (float)a->h / 2, (float)a->w - 40, say_, a->message[0] ? rgb(t->urgent) : rgb(t->text));
	} else if (a->screen == SCREEN_DISKS) {
		d2_rect(0, 0, (float)a->w, (float)a->h, rgb(t->bg));
		draw_disks(a);
	} else if (a->screen == SCREEN_SCANNING) {
		d2_rect(0, 0, (float)a->w, (float)a->h, rgb(t->bg));
		draw_scanning(a);
	} else {
		if (!a->embedded) {
			draw_list(a);
		}
		if (a->edit && !a->embedded) {
			draw_edit(a);
		}
		draw_view(a);
		if (!a->mini) {
			draw_sliders(a);
			draw_status(a);
		}
	}
	if (!a->mini) {
		draw_bar(a);
	}
	if (a->menu_open && a->screen == SCREEN_TREE) {
		draw_menu(a);
	}
	if (a->ask_node && a->screen == SCREEN_TREE) {
		draw_ask(a);
	}
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
	if (a->embedded && (b == B_DISKS || b == B_PATH || b == B_THEME)) {
		return;
	}
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

/* a slider put at k, 0 .. 1 along its track; the layout made again when
 * what it shows changes */
static void slider_put(App *a, int i, float k) {
	k = k < 0 ? 0 : k > 1 ? 1 : k;
	if (i == SL_SEE) {
		a->see = roundf(k * 100);
	} else if (i == SL_SPIN) {
		a->spin_rate = roundf(k * SPIN_MOST / SPIN_MADE * 10) / 10 * SPIN_MADE; /* tenths of SYN-TREK's speed */
	} else {
		int levels = a->levels;
		float lift = a->lift;
		if (i == SL_DEPTH) {
			a->levels = 1 + (int)roundf(k * (LEVELS_ALL - 1));
		} else {
			a->lift = roundf(k * LIFT_MOST * 10) / 10;
		}
		if (levels != a->levels || lift != a->lift) {
			a->scene_w = 0; /* made again next frame, shown at once */
			a->snap = true;
		}
	}
}
static void slider_drag_to(App *a, int i, float x) {
	Rect tr = slider_track(a, i);
	slider_put(a, i, (x - tr.x) / tr.w);
}
/* a notch of the wheel: one step */
static void slider_nudge(App *a, int i, int notches) {
	float step = i == SL_SEE ? 0.05f
	           : i == SL_DEPTH ? 1.0f / (LEVELS_ALL - 1)
	           : i == SL_HEIGHT ? 0.1f / LIFT_MOST
	                            : 0.1f * SPIN_MADE / SPIN_MOST;
	slider_put(a, i, slider_get(a, i) + step * (float)notches + step * 0.01f * (float)notches);
}

/* the row under a point in the list, or -1 */
static int list_row_at(const App *a, float y) {
	Rect l = list_rect(a);
	int i = a->scroll + (int)((y - l.y) / row_h(a)) - 1;
	return y >= l.y + row_h(a) && i >= 0 && i < a->nrows ? i : -1;
}

static void mouse_down(App *a, const SDL_MouseButtonEvent *e) {
	float x = (float)e->x * a->dpi, y = (float)e->y * a->dpi;
	if (a->ask_node) {
		/* the box has the window until it is answered */
		Rect yes, no;
		ask_buttons(a, &yes, &no);
		if (e->button == SDL_BUTTON_LEFT && in_rect(yes, x, y)) {
			ask_answer(a, true);
		} else if (e->button == SDL_BUTTON_LEFT && in_rect(no, x, y)) {
			ask_answer(a, false);
		}
		return;
	}
	if (a->typing) {
		typing_stop(a);
	}
	if (a->menu_open) {
		/* any click shuts the menu; on one of its items, it does that */
		int item = e->button == SDL_BUTTON_LEFT ? menu_item_at(a, x, y) : -1;
		bool inside = in_rect(menu_rect(a), x, y);
		a->menu_open = false;
		if (item >= 0) {
			menu_do(a, item);
		}
		if (inside || e->button == SDL_BUTTON_LEFT) {
			return;
		}
	}
	if (a->edit) {
		edit_commit(a); /* a click away keeps the name, as a file manager does */
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
	if (walk_on(a->walk) && in_rect(view_rect(a), x, y)) {
		if (walk_mouse_down(a->walk, x, y, e->button) == WALK_EXIT) {
			walk_to_map(a);
		}
		return;
	}
	if (e->button == SDL_BUTTON_LEFT && fig_at(a, x, y)) {
		a->fig_drag = true;
		a->fig_moved = false;
		a->fig_x = x;
		a->fig_y = y;
		return;
	}
	int vb = view_button_at(a, x, y);
	if (vb >= 0) {
		if (e->button == SDL_BUTTON_LEFT) {
			press_view_button(a, vb);
		}
		return;
	}
	if (e->button == SDL_BUTTON_RIGHT && in_rect(list_rect(a), x, y)) {
		/* the row's menu; below the rows, the menu of the whole tree's folder */
		int i = list_row_at(a, y);
		Node *n = i >= 0 ? a->rows[i].node : a->root;
		if (i >= 0) {
			select_node(a, n);
		}
		menu_open_on(a, n, x, y);
	} else if (e->button == SDL_BUTTON_RIGHT && in_rect(view_rect(a), x, y)) {
		/* held and dragged it slides the view; let go where it was pressed, it goes up a folder as before */
		a->rpressed = true;
		a->rdragged = false;
		a->rpress_x = x;
		a->rpress_y = y;
	} else if (e->button == SDL_BUTTON_RIGHT) {
		go_up(a);
	} else if (e->button == SDL_BUTTON_LEFT && slider_at(a, x, y) >= 0) {
		a->slide_drag = slider_at(a, x, y);
		slider_drag_to(a, a->slide_drag, x);
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
	if (e->button == SDL_BUTTON_LEFT) {
		a->slide_drag = -1;
	}
	if (walk_on(a->walk)) {
		walk_mouse_up(a->walk, e->button);
		return;
	}
	if (e->button == SDL_BUTTON_LEFT && a->fig_drag) {
		float x = (float)e->x * a->dpi, y = (float)e->y * a->dpi;
		a->fig_drag = false;
		if (!a->fig_moved) {
			walk_start(a, x, y, false); /* a click: walk in the folder shown */
		} else if (in_rect(view_rect(a), x, y) && !in_rect(fig_rect(a), x, y)) {
			walk_start(a, x, y, true);
		}
		return;
	}
	if (e->button == SDL_BUTTON_RIGHT && a->rpressed) {
		bool click = !a->rdragged;
		a->rpressed = a->rdragged = false;
		if (click && a->screen == SCREEN_TREE) {
			go_up(a);
		}
		return;
	}
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
	if (walk_on(a->walk)) {
		walk_mouse_move(a->walk, (float)e->xrel * a->dpi, (float)e->yrel * a->dpi);
		if (SDL_GetRelativeMouseMode()) {
			return; /* the pointer stays where the look began */
		}
		a->mx = x;
		a->my = y;
		return;
	}
	if (a->fig_drag && fabsf(x - a->fig_x) + fabsf(y - a->fig_y) > 4) {
		a->fig_moved = true;
	}
	if (a->slide_drag >= 0) {
		slider_drag_to(a, a->slide_drag, x);
	}
	if (a->pressed && a->solid) {
		if (fabsf(x - a->press_x) + fabsf(y - a->press_y) > 4) {
			a->dragged = true;
			a->spin = false; /* taken in hand: it stays where it is put */
		}
		if (a->dragged) {
			a->yaw -= (x - a->mx) * 0.006f;
			a->pitch += (y - a->my) * 0.006f;
			a->pitch = a->pitch < 0.05f ? 0.05f : a->pitch > 1.5f ? 1.5f : a->pitch;
		}
	}
	if (a->rpressed) {
		if (fabsf(x - a->rpress_x) + fabsf(y - a->rpress_y) > 4) {
			a->rdragged = true;
			a->spin = false; /* taken in hand: it stays where it is put */
		}
		if (a->rdragged) {
			slide_by(a, x - a->mx, y - a->my);
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
	if (slider_at(a, a->mx, a->my) >= 0) {
		slider_nudge(a, slider_at(a, a->mx, a->my), e->y);
	} else if (in_rect(slider_rect(a), a->mx, a->my)) {
		/* between the cells: nothing */
	} else if (in_rect(list_rect(a), a->mx, a->my)) {
		a->scroll -= e->y * 3;
		rows_build(a); /* which keeps the scroll within the rows */
	} else if (walk_on(a->walk) && in_rect(view_rect(a), a->mx, a->my)) {
		walk_wheel(a->walk, e->y);
	} else if (in_rect(view_rect(a), a->mx, a->my) && e->y) {
		/* nearer or further in 3D as before; in 2D about the mouse */
		zoom_by(a, e->y > 0 ? 1 / 0.88f : 1 / 1.14f, a->mx, a->my);
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

static void edit_add(App *a, const char *text) {
	if (a->edit_fresh) {
		a->edit_text[0] = '\0';
		a->edit_fresh = false;
	}
	size_t at = strlen(a->edit_text);
	snprintf(a->edit_text + at, sizeof a->edit_text - at, "%s", text);
	a->edit_text[strcspn(a->edit_text, "\r\n")] = '\0';
}

static void key_edit(App *a, const SDL_KeyboardEvent *e) {
	SDL_Keycode k = e->keysym.sym;
	if (k == SDLK_ESCAPE) {
		edit_stop(a);
	} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
		edit_commit(a);
	} else if (k == SDLK_BACKSPACE) {
		if (a->edit_fresh) {
			a->edit_text[0] = '\0';
			a->edit_fresh = false;
			return;
		}
		size_t n = strlen(a->edit_text);
		while (n > 0 && (a->edit_text[n - 1] & 0xC0) == 0x80) {
			n--;
		}
		a->edit_text[n ? n - 1 : 0] = '\0';
	} else if (k == SDLK_RIGHT || k == SDLK_END || k == SDLK_LEFT || k == SDLK_HOME) {
		a->edit_fresh = false; /* keep the name and type on its end */
	} else if (k == SDLK_v && (e->keysym.mod & KMOD_CTRL)) {
		char *clip = SDL_GetClipboardText();
		edit_add(a, clip);
		SDL_free(clip);
	} else if (k == SDLK_a && (e->keysym.mod & KMOD_CTRL)) {
		a->edit_fresh = true;
	}
}

static void key_tree(App *a, const SDL_KeyboardEvent *e) {
	SDL_Keycode k = e->keysym.sym;
	int row = row_of(a, a->sel), page = rows_fit(a) - 1;
	Node *n = a->sel;
	bool ctrl = e->keysym.mod & KMOD_CTRL, shift = e->keysym.mod & KMOD_SHIFT;
	/* the menu's own keys, on the selection */
	int item = ctrl && k == SDLK_c ? M_COPY : ctrl && k == SDLK_x ? M_CUT : ctrl && k == SDLK_v ? M_PASTE
	         : k == SDLK_F2 ? M_RENAME : ctrl && shift && k == SDLK_n ? M_NEWDIR
	         : shift && k == SDLK_DELETE ? M_PURGE : k == SDLK_DELETE ? M_DELETE : -1;
	if (item >= 0 && n && !a->embedded) { /* in SYN-SHELL its own keys do that, on its own columns */
		a->menu_node = n;
		menu_do(a, item);
		return;
	}
	if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) {
		press_view_button(a, VB_ZOOMIN);
	} else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
		press_view_button(a, VB_ZOOMOUT);
	} else if (k == SDLK_0 || k == SDLK_KP_0) {
		press_view_button(a, VB_RESET);
	} else if (k == SDLK_TAB) {
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
		if (a->embedded) {
			set_focus(a, n->is_dir ? n : n->parent); /* SYN-SHELL follows */
		} else {
			open_in_shell(path);
		}
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
	if (a->ask_node) {
		if (k == SDLK_y) {
			ask_answer(a, true);
		} else if (k == SDLK_n || k == SDLK_ESCAPE) {
			ask_answer(a, false);
		}
		return;
	}
	if (a->edit) {
		key_edit(a, e);
	} else if (a->menu_open && k == SDLK_ESCAPE) {
		a->menu_open = false;
	} else if (a->typing) {
		key_typing(a, e);
	} else if (walk_on(a->walk) && k != SDLK_t) {
		if (walk_key(a->walk, k) == WALK_EXIT) {
			walk_to_map(a);
		}
	} else if (a->embedded && k == SDLK_ESCAPE) {
		if (a->thread) {
			a->progress.cancel = true;
		}
	} else if (a->embedded && (k == SDLK_t || k == SDLK_l)) {
		/* the theme is the desktop's, the path SYN-SHELL's */
	} else if (k == SDLK_l && (e->keysym.mod & KMOD_CTRL)) {
		typing_start(a);
	} else if (k == SDLK_t) {
		int step = e->keysym.mod & KMOD_SHIFT ? a->nthemes - 1 : 1;
		a->theme = (a->theme + step) % a->nthemes;
		a->scene_w = 0;
	} else if (k == SDLK_ESCAPE) {
		if (a->thread) {
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
		if (a->edit) {
			edit_add(a, e->text.text);
		} else if (a->typing) {
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
	fprintf(stderr, "syn-shell: %s: %s\n", what, SDL_GetError());
#ifdef _WIN32
	char msg[1024];
	snprintf(msg, sizeof msg, "%s:\n%s\n\nSYN-SHELL SPACE needs OpenGL 3.3 or newer.", what, SDL_GetError());
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SYN-SHELL SPACE could not start", msg, NULL);
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
		fprintf(stderr, "syn-shell: no theme named %s\n", opt->theme ? opt->theme : "SYN-OS-RED");
		return 1;
	}
#ifndef _WIN32
	/* Native Wayland first. sdl2-compat (SDL2 on SDL3) puts X11 ahead of
	 * Wayland, so a copy started without SDL_VIDEODRIVER (systemd-run, a
	 * launcher with a bare environment) came up under XWayland, where its
	 * clicks went missing or arrived late (18 Nov: GRAPH and 2D/3D did
	 * nothing on William's screen). A driver the user names still wins. */
	if (!SDL_getenv("SDL_VIDEODRIVER") && SDL_getenv("WAYLAND_DISPLAY")) {
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");
	}
#endif
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		return fail("Starting the video system");
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifndef _WIN32
	/* a window with an alpha, so the view's background can be see-through */
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	a->win = SDL_CreateWindow("SYN-SHELL SPACE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
	                          flags | WINDOW_SEE_THROUGH);
	if (!a->win) {
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
	}
#endif
	if (!a->win) {
		a->win = SDL_CreateWindow("SYN-SHELL SPACE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, flags);
	}
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
	/* the icons at 24 pixels, or 48 on a screen with twice the pixels, never taller than the text by much */
	bool big = a->dpi >= 1.5f;
	for (int i = 0; i < ICON_COUNT; i++) {
		a->icon[i] = big ? d2_icon_make(ICON_48[i], 48, 48) : d2_icon_make(ICON_24[i], 24, 24);
	}
	a->icon_px = big ? 48 : 24;
	if (a->icon_px > (float)a->text_h * 1.4f) {
		a->icon_px = roundf((float)a->text_h * 1.4f);
	}
	a->tree_lock = SDL_CreateMutex();
	if (!a->tree_lock) {
		return fail("Making the tree's lock");
	}
	a->sort = SORT_SIZE;
	a->sort_desc = true;
	a->see = 10;
	a->levels = opt->levels >= 1 && opt->levels < LEVELS_ALL ? opt->levels : LEVELS_ALL;
	a->lift = opt->lift >= 0 ? (opt->lift > LIFT_MOST ? LIFT_MOST : opt->lift) : 1;
	a->spin_rate = opt->spin >= 0 ? (opt->spin > 3 ? SPIN_MOST : opt->spin * SPIN_MADE) : SPIN_MADE;
	a->slide_drag = -1;
	a->walk = walk_new();
	a->walk_asked = opt->walk;
	set_view(a, opt->graph ? VIEW_GRAPH : VIEW_BLOCKS, opt->solid);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	SDL_StopTextInput();
	show_disks(a);
	a->shot_from = SDL_GetTicks();
	if (opt->path) {
		scan_start(a, opt->path);
	}

	a->tick = SDL_GetPerformanceCounter();
	while (!a->quit) {
		SDL_Event e;
		bool got;
		if (moving(a)) {
			got = SDL_PollEvent(&e); /* the frame rate is the screen's, through the swap */
		} else {
			/* nothing moves by itself but a scan's counters */
			got = SDL_WaitEventTimeout(&e, a->screen == SCREEN_SCANNING || a->job ? 100 : a->note[0] ? 500 : 1000);
		}
		/* not holding the tree: the scan may be waiting for it to end */
		if (a->thread && SDL_AtomicGet(&a->scan_done)) {
			scan_finish(a);
			if (opt->shot && !a->root) {
				fprintf(stderr, "syn-shell: %s\n", a->message);
				return 1;
			}
		}
		tree_hold(a);
		if (got) {
			do {
				handle(a, &e);
			} while (SDL_PollEvent(&e));
		}
		scan_show(a);
		if (a->job && SDL_AtomicGet(&a->job_done)) {
			paste_finish(a);
		}
		Uint64 now = SDL_GetPerformanceCounter();
		a->dt = (float)((double)(now - a->tick) / (double)SDL_GetPerformanceFrequency());
		a->dt = a->dt > 0.1f ? 0.1f : a->dt; /* after a wait, no leap */
		a->tick = now;
		if (a->spin && a->solid && !a->pressed && !a->rpressed && !a->live && a->screen == SCREEN_TREE) {
			a->yaw += a->spin_rate * a->dt; /* SPIN slider; as made, SYN-TREK's 0.35 radians a second */
		}
		if (a->walk_asked && a->screen == SCREEN_TREE && a->focus) {
			a->walk_asked = false;
			walk_enter(a->walk, a->focus, NULL);
		}
		walk_update(a->walk, a->dt);
		SDL_GL_GetDrawableSize(a->win, &a->w, &a->h);
		{
			/* pixels per mouse unit, again each frame: the window may have
			 * been resized or moved to a screen with another scale */
			int mw, mh;
			SDL_GetWindowSize(a->win, &mw, &mh);
			if (mw > 0) {
				a->dpi = (float)a->w / (float)mw;
			}
		}
		view_update(a);
		tree_let_go(a);
		draw(a);
		/* --shot: the finished tree, or with --shot-at, the picture that many ms into the scan */
		if (opt->shot) {
			bool ready = opt->shot_at ? a->screen == SCREEN_TREE && a->root &&
			                                SDL_TICKS_PASSED(now, a->shot_from + (Uint32)opt->shot_at)
			                          : !a->thread && a->screen != SCREEN_SCANNING && SDL_TICKS_PASSED(now, a->ease_until) && ++a->settled == 2;
			if (ready) {
				return draw_shot(opt->shot) ? 0 : 1;
			}
		}
		SDL_GL_SwapWindow(a->win);
	}
	if (a->thread) {
		a->progress.cancel = true;
		SDL_WaitThread(a->thread, NULL);
	}
	if (a->job) {
		SDL_WaitThread(a->job, NULL); /* a copy half made is worse than a wait */
	}
	SDL_Quit();
	return 0;
}

/* ======================================================================
 *   Inside SYN-SHELL's window (app.h, space_*): no SDL window, events or
 *   loop of its own. SYN-SHELL draws it in its right-hand column, hands it
 *   the mouse and keys as SDL events, and shows its tree in the left one.
 * ====================================================================== */

App *space_new(void) {
	App *a = SDL_calloc(1, sizeof *a);
	if (!a) {
		return NULL;
	}
	static const AppOptions none = {.lift = -1, .spin = -1};
	a->opt = &none;
	a->embedded = true;
	a->nthemes = themes_load(&a->themes);
	a->theme = theme_start(a->themes, a->nthemes, NULL);
	a->theme = a->theme < 0 ? 0 : a->theme;
	a->tree_lock = SDL_CreateMutex();
	a->sort = SORT_SIZE;
	a->sort_desc = true;
	a->see = 0; /* solid: SYN-SHELL's window has its own see-through */
	a->levels = LEVELS_ALL;
	a->lift = 1;
	a->spin_rate = SPIN_MADE;
	a->slide_drag = -1;
	a->walk = walk_new();
	walk_keys = a->keys;
	set_view(a, VIEW_BLOCKS, false);
	a->screen = SCREEN_DISKS;
	a->tick = SDL_GetPerformanceCounter();
	/* until the GL context is there to measure the font: the layout only needs these to be sane */
	a->w = a->h = 100;
	a->dpi = 1;
	a->text_h = 16;
	a->char_w = 8;
	a->icon_px = 24;
	return a;
}

bool space_gl_init(App *a, void *(*getproc)(const char *), float dpi) {
	gl_getproc = getproc;
	a->dpi = dpi;
	if (!draw_init((int)(16 * roundf(dpi < 1 ? 1 : dpi)))) {
		return false;
	}
	a->text_h = d2_text_height();
	a->char_w = d2_text_width("0");
	bool big = dpi >= 1.5f;
	for (int i = 0; i < ICON_COUNT; i++) {
		a->icon[i] = big ? d2_icon_make(ICON_48[i], 48, 48) : d2_icon_make(ICON_24[i], 24, 24);
	}
	a->icon_px = big ? 48 : 24;
	if (a->icon_px > (float)a->text_h * 1.4f) {
		a->icon_px = roundf((float)a->text_h * 1.4f);
	}
	return true;
}

/* Stops a running scan and waits for it; not holding the tree. What it had
 * built stays as the tree, for scan_start to free. */
static void scan_stop(App *a) {
	if (!a->thread) {
		return;
	}
	a->progress.cancel = true;
	SDL_WaitThread(a->thread, NULL);
	a->thread = NULL;
	a->live = false;
	if (!a->root) {
		a->root = a->scan_result ? a->scan_result : a->progress.root;
	}
	a->progress.root = NULL;
	a->scan_result = NULL;
}

static void path_clean(const char *in, char *out, size_t len) {
	snprintf(out, len, "%s", in);
	size_t n = strlen(out);
	while (n > 1 && out[n - 1] == '/') {
		out[--n] = '\0';
	}
}

/* Whether path is the tree's top or under it, on the same file system (a
 * disk mounted inside is not walked, so it is scanned on its own). */
static bool in_tree(const App *a, const char *path) {
	if (!a->root) {
		return false;
	}
	const char *rp = a->root->name;
	size_t n = strlen(rp);
	if (strncmp(path, rp, n) || (path[n] && path[n] != '/' && rp[n - 1] != '/')) {
		return false;
	}
#ifndef _WIN32
	struct stat s1, s2;
	if (stat(rp, &s1) == 0 && stat(path, &s2) == 0 && s1.st_dev != s2.st_dev) {
		return false;
	}
#endif
	return true;
}

static void told_now(App *a) {
	a->told_focus = a->focus;
	a->told_sel = a->sel;
	a->told_room = walk_on(a->walk) ? walk_room(a->walk) : NULL;
}

void space_show(App *a, const char *dir) {
	char path[4096];
	path_clean(dir, path, sizeof path);
	tree_hold(a);
	Node *n = node_find(a, path);
	bool inside = in_tree(a, path);
	if (n && n->is_dir && inside) {
		a->pending[0] = '\0';
		if (walk_on(a->walk) && walk_room(a->walk) != n) {
			walk_enter(a->walk, n, NULL); /* walking: into that room */
		}
		if (n != a->focus) {
			set_focus(a, n);
		}
		told_now(a);
		tree_let_go(a);
		return;
	}
	if (inside && a->thread) {
		snprintf(a->pending, sizeof a->pending, "%s", path); /* the scan will get there */
		tree_let_go(a);
		return;
	}
	bool same = a->thread && !strcmp(a->scan_path, path);
	tree_let_go(a);
	if (same) {
		return;
	}
	scan_stop(a);
	a->pending[0] = '\0';
	scan_start(a, path);
}

void space_select(App *a, const char *path) {
	char p[4096];
	path_clean(path, p, sizeof p);
	tree_hold(a);
	Node *n = node_find(a, p);
	if (n && n != a->sel) {
		a->sel = n;
		a->told_sel = n;
	}
	tree_let_go(a);
}

/* What the user did here since last asked, in place of SDL's loop. */
static void report(App *a) {
	if (!a->root) {
		return;
	}
	if (a->host_up) {
		a->host_up = false;
		char top[4096];
		snprintf(top, sizeof top, "%s", a->root->name);
		char *slash = strrchr(top, '/');
		if (slash && slash != top) {
			*slash = '\0';
		} else if (slash) {
			slash[1] = '\0';
		}
		snprintf(a->want_focus, sizeof a->want_focus, "%s", top);
		a->changes |= SPACE_FOCUS;
	}
	Node *room = walk_on(a->walk) ? walk_room(a->walk) : NULL;
	/* a room from outside the tree (a crew member's office) is the walk's
	 * own: SYN-SHELL is not sent there, which would leave this tree */
	const Node *top = room;
	while (top && top->parent) {
		top = top->parent;
	}
	if (room && top != a->root) {
		a->told_room = room;
	}
	if (room && room != a->told_room) {
		node_path(room, a->want_focus, sizeof a->want_focus);
		a->changes |= SPACE_FOCUS;
	} else if (a->focus && a->focus != a->told_focus) {
		node_path(a->focus, a->want_focus, sizeof a->want_focus);
		a->changes |= SPACE_FOCUS;
	}
	if (a->sel && a->sel != a->told_sel) {
		node_path(a->sel, a->want_sel, sizeof a->want_sel);
		a->changes |= SPACE_SELECT;
	}
	if (walk_used(a->walk, a->want_use, sizeof a->want_use, &a->use_kind, &a->use_dev)) {
		a->changes |= SPACE_USE;
	}
	told_now(a);
}

void space_event(App *a, const SDL_Event *e) {
	if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
		SDL_Scancode sc = e->key.keysym.scancode;
		if (sc > 0 && sc < SDL_NUM_SCANCODES) {
			a->keys[sc] = e->type == SDL_KEYDOWN;
		}
	}
	if (e->type == SDL_KEYUP) {
		return;
	}
	tree_hold(a);
	handle(a, e);
	report(a);
	tree_let_go(a);
}

/* The scan's end and its first folder, and SYN-SHELL's folder once the
 * scan reaches it; holding the tree after. */
static void tick_hold(App *a) {
	if (a->thread && SDL_AtomicGet(&a->scan_done)) {
		scan_finish(a);
	}
	tree_hold(a);
	scan_show(a);
	if (a->pending[0]) {
		Node *n = node_find(a, a->pending);
		if (n && n->is_dir) {
			set_focus(a, n);
			a->pending[0] = '\0';
		} else if (!a->thread) {
			a->pending[0] = '\0';
		}
	}
	Uint64 now = SDL_GetPerformanceCounter();
	a->dt = (float)((double)(now - a->tick) / (double)SDL_GetPerformanceFrequency());
	a->dt = a->dt > 0.1f ? 0.1f : a->dt;
	a->tick = now;
}

void space_tick(App *a) {
	Node *focus = a->focus;
	tick_hold(a);
	if (a->focus != focus) {
		told_now(a); /* the scan's doing, not the user's */
	}
	tree_let_go(a);
}

void space_frame(App *a, int w, int h, float dpi) {
	a->w = w;
	a->h = h;
	a->dpi = dpi > 0 ? dpi : 1;
	Node *focus = a->focus;
	tick_hold(a);
	if (a->focus != focus) {
		told_now(a);
	}
	if (a->spin && a->solid && !a->pressed && !a->rpressed && (!a->live || a->mini) && a->screen == SCREEN_TREE) {
		a->yaw += a->spin_rate * a->dt; /* the corner turns while it grows too */
	}
	walk_update(a->walk, a->dt);
	report(a); /* a doorway walked through */
	view_update(a);
	tree_let_go(a);
	draw(a);
}

void space_set_mini(App *a, bool on) {
	if (on == a->mini) {
		return;
	}
	tree_hold(a);
	a->mini = on;
	a->pressed = a->rpressed = false;
	a->dragged = a->rdragged = false;
	if (on && !walk_on(a->walk)) {
		set_view(a, VIEW_BLOCKS, true); /* made big it stays as it is, turning, to be changed there */
	}
	if (on) {
		a->spin = true;
	}
	tree_let_go(a);
}

bool space_busy(const App *a) {
	return moving(a) || a->thread || a->pending[0] || a->changes;
}

bool space_walking(const App *a) { return walk_on(a->walk); }
bool space_captured(const App *a) { return walk_captured(a->walk); }
void space_release(App *a) { walk_release(a->walk); }
void space_look(App *a, float dx, float dy) { walk_look(a->walk, dx, dy); }
bool space_live(const App *a) { return a->thread != NULL; }

int space_changes(App *a, char *focus, char *sel, int len) {
	int c = a->changes;
	a->changes = 0;
	if (c & SPACE_FOCUS) {
		snprintf(focus, (size_t)len, "%s", a->want_focus);
	}
	if (c & SPACE_SELECT) {
		snprintf(sel, (size_t)len, "%s", a->want_sel);
	}
	return c;
}

const char *space_used(const App *a, int *kind, int *dev) {
	*kind = a->use_kind;
	*dev = a->use_dev;
	return a->want_use;
}

void space_set_playing(App *a, const char *path) { walk_set_playing(a->walk, path); }
void space_set_player(App *a, bool there) { walk_set_player(a->walk, there); }
void space_capture(App *a) { walk_capture(a->walk); }
bool space_target(const App *a, char *out, int len) { return walk_on(a->walk) && walk_target(a->walk, out, len); }

void space_walk_to(App *a, const char *path) {
	char p[4096];
	path_clean(path, p, sizeof p);
	tree_hold(a);
	Node *n = node_find(a, p);
	if (n && n->is_dir) {
		walk_enter(a->walk, n, NULL); /* a folder: walking, in its room */
	} else if (n && !n->is_dir && n->parent && walk_on(a->walk)) {
		if (walk_room(a->walk) != n->parent) {
			walk_enter(a->walk, n->parent, NULL);
		}
		walk_go_to(a->walk, n);
	}
	report(a);
	tree_let_go(a);
}

void space_lock(App *a) { tree_hold(a); }
void space_unlock(App *a) { tree_let_go(a); }
Node *space_root(const App *a) { return a->root; }
unsigned space_generation(const App *a) { return a->gen; }
Node *space_find(const App *a, const char *path) {
	char p[4096];
	path_clean(path, p, sizeof p);
	return node_find(a, p);
}

void space_on_drop(App *a, void (*drop)(void *), void *host) {
	a->tree_drop = drop;
	a->host = host;
}

void space_colour(const App *a, const Node *n, unsigned char rgb_out[3]) {
	const Theme *t = theme(a);
	SDL_Color c = n->unread ? rgb(t->urgent) : n->is_dir ? accent_text(t) : kind_colour(kind_of(n->name), t);
	rgb_out[0] = c.r;
	rgb_out[1] = c.g;
	rgb_out[2] = c.b;
}

void space_quit(App *a) {
	scan_stop(a);
}
