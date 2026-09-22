/* ------------------------------------------------------------------------
 *   An Uplink-style DTMF dialpad AND admin auth prompt: a native Wayland
 *   popup (xdg-shell), sized exactly to its content and drawn with
 *   hand-rolled pixel/bitmap-font routines rather than cairo/pango, to
 *   avoid pulling in their ~50MB transitive dependency chain for what's
 *   just rectangles and glyphs. Colors come from syn_theme_load() (shared
 *   with syn-connect/syn-crypter) — only its palette-reading half; this
 *   isn't an ncurses UI.
 *
 *   Three modes, chosen by argv, one process lifetime each (this binary
 *   is always launched fresh per prompt, never re-used across modes):
 *
 *     (no args)          DTMF dialpad — the original mode. Clickable or
 *                        typeable (numpad, top row, A-D); beeps each
 *                        digit immediately by asking syn-bar-core to
 *                        play it (TONE-RAW, see syn_bar_notify.h — this
 *                        binary has no PulseAudio access of its own) and
 *                        redials the whole sequence back on Enter. Typing any
 *                        character outside the DTMF alphabet (e.g. '.'
 *                        or most letters) still appends silently — see
 *                        type_char() — so this doubles as a general
 *                        IP/hostname prompt, not just a phone-style pad.
 *                        Numpad mapping: 0-9 -> digits, '/' -> '*'. a-d
 *                        (either case) reach the DTMF 1633Hz column,
 *                        which has no natural numpad key of its own.
 *
 *     --password [title] A centered, bordered password-only box (no
 *                        keypad grid) — masked input, Enter submits one
 *                        line to stdout, Esc/q cancels with none.
 *
 *     --login [title]    Same box shape, username field above a masked
 *                        password field, Tab/Enter moves focus down.
 *                        Submits two lines to stdout (username, then
 *                        password) on Enter from the password field.
 *
 *     --exec -- doas <command> [args...]
 *                        Unified auth-and-exec: shows the --password
 *                        screen in-process, then forkpty()s the given
 *                        command, feeding the collected password to its
 *                        pty the moment doas's own prompt appears on the
 *                        other end (doas refuses piped/non-tty input —
 *                        confirmed empirically — so this is a real
 *                        pseudo-terminal, not a pipe). Exits with the
 *                        child's real exit status. When doas needs no
 *                        password (nopass rule, or a live persist
 *                        timestamp) there's nothing to collect: no
 *                        screen, the command is exec'd directly on this
 *                        terminal.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-UPLINK-DIALPAD
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "font5x7.h"
#include "syn_bar_notify.h"
#include "syn_theme.h"
#include "xdg-shell-client-protocol.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DTMF_SECONDS 0.15
#define READBACK_GAP_MS 90

/* Password/login field feedback (KEY_CLICK on keystroke/backspace,
 * SUCCESS on submit) is asked from syn-bar-core by name — see
 * syn_bar_notify_meaning() calls below and syn_tone_vocab.h's own
 * comment on KEY_CLICK for why it's one fixed pair regardless of which
 * character was typed. */
#define SEQUENCE_MAX 32
#define GRID_ROWS 4
#define GRID_COLS 4
#define KEY_SIZE 56
#define KEY_GAP 10
#define MARGIN 18
#define TITLE_H 26
#define READOUT_H 30
#define STATUS_H 22
#define SYS_ROW_H 34
#define SYS_ROW_LABEL_H 16
#define GLYPH_SCALE_TITLE 3
#define GLYPH_SCALE_KEY 4
#define GLYPH_SCALE_SMALL 2
#define GLYPH_SCALE_SYS 2

struct dtmf_key {
	char digit;
	double lo, hi;
	int x, y, w, h;
};

/* Standard DTMF keypad frequency table (ITU-T Q.23), full 4x4 grid, laid
 * out to match a real phone pad's row/col positions. x/y filled in once
 * the window size is known (layout_keys() below). */
static struct dtmf_key keys[] = {
	{'1', 697.0, 1209.0, 0, 0, 0, 0}, {'2', 697.0, 1336.0, 0, 0, 0, 0}, {'3', 697.0, 1477.0, 0, 0, 0, 0}, {'A', 697.0, 1633.0, 0, 0, 0, 0},
	{'4', 770.0, 1209.0, 0, 0, 0, 0}, {'5', 770.0, 1336.0, 0, 0, 0, 0}, {'6', 770.0, 1477.0, 0, 0, 0, 0}, {'B', 770.0, 1633.0, 0, 0, 0, 0},
	{'7', 852.0, 1209.0, 0, 0, 0, 0}, {'8', 852.0, 1336.0, 0, 0, 0, 0}, {'9', 852.0, 1477.0, 0, 0, 0, 0}, {'C', 852.0, 1633.0, 0, 0, 0, 0},
	{'*', 941.0, 1209.0, 0, 0, 0, 0}, {'0', 941.0, 1336.0, 0, 0, 0, 0}, {'#', 941.0, 1477.0, 0, 0, 0, 0}, {'D', 941.0, 1633.0, 0, 0, 0, 0},
};
#define NUM_KEYS (sizeof(keys) / sizeof(keys[0]))

struct sys_tone {
	const char *label;
	double lo, hi; /* hi <= 0 means a single tone, not DTMF-style dual */
	double seconds;
	int x, y, w, h;
};

/* syn-bar-core's own signal tones (syn-bar-core-src/src/main.c), exposed
 * here too so this pad can represent the whole Uplink-style tone set, not
 * just the 12/16-key DTMF pad. */
static struct sys_tone sys_tones[] = {
	{"2600", 2600.0, 0.0, 0.15, 0, 0, 0, 0},   /* SSH inbound alert */
	{"CONN", 1900.0, 0.0, 0.2, 0, 0, 0, 0},    /* relay connect */
	{"FAIL", 697.0, 1209.0, 0.15, 0, 0, 0, 0}, /* relay unreachable */
	{"HANG", 941.0, 1336.0, 0.15, 0, 0, 0, 0}, /* relay disconnect */
};
#define NUM_SYS_TONES (sizeof(sys_tones) / sizeof(sys_tones[0]))

typedef enum {
	MODE_DIALPAD,
	MODE_PASSWORD,
	MODE_LOGIN,
	MODE_EXEC,
} dialpad_mode;

static dialpad_mode mode = MODE_DIALPAD;
static char mode_title[128] = "DESTINATION";

#define CRED_MAX 255
#define PASSWORD_FIELD_W 260
#define PASSWORD_FIELD_H 26
#define PASSWORD_FIELD_GAP 14
#define PASSWORD_LABEL_H 16

/* MODE_LOGIN has two fields (username, password); MODE_PASSWORD/MODE_EXEC
 * use only field 1. field_focus selects which one keystrokes go to. */
static char cred_field[2][CRED_MAX + 1];
static int cred_len[2];
static int field_focus = 0;
static int cred_x, cred_y[2], cred_w, cred_h;

static char sequence[SEQUENCE_MAX + 1];
static int sequence_len = 0;
static int highlighted_index = -1;
static int sys_highlighted_index = -1;

static int win_w, win_h;
static syn_palette palette;

/* Wayland globals */
static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wl_seat *seat;
static struct wl_keyboard *keyboard;
static struct wl_pointer *pointer;
static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;
static struct wl_buffer *buffer;
static uint32_t *shm_pixels;
static int shm_size;
static int running = 1;
static int configured = 0;

/* xkbcommon keymap state */
static struct xkb_context *xkb_ctx;
static struct xkb_keymap *xkb_keymap;
static struct xkb_state *xkb_state;

/* Pointer hit-testing needs the last known cursor position. */
static double pointer_x, pointer_y;

static char normalize(char c) {
	if (c == '/') return '*';                                /* numpad '/' -> '*' */
	if (c >= 'a' && c <= 'd') return (char)(c - 'a' + 'A');   /* a-d -> A-D */
	return c;
}

static struct dtmf_key *lookup(char digit) {
	for (size_t i = 0; i < NUM_KEYS; i++) {
		if (keys[i].digit == digit) {
			return &keys[i];
		}
	}
	return NULL;
}

static int index_of(const struct dtmf_key *key) {
	return (int)(key - keys);
}

static void sleep_ms(long ms) {
	struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
	nanosleep(&ts, NULL);
}

static void layout_keys(void) {
	int grid_w = GRID_COLS * KEY_SIZE + (GRID_COLS - 1) * KEY_GAP;
	int grid_top = MARGIN + TITLE_H + READOUT_H;
	for (size_t i = 0; i < NUM_KEYS; i++) {
		int row = (int)(i / GRID_COLS);
		int col = (int)(i % GRID_COLS);
		keys[i].x = MARGIN + col * (KEY_SIZE + KEY_GAP);
		keys[i].y = grid_top + row * (KEY_SIZE + KEY_GAP);
		keys[i].w = KEY_SIZE;
		keys[i].h = KEY_SIZE;
	}

	int grid_bottom = grid_top + GRID_ROWS * KEY_SIZE + (GRID_ROWS - 1) * KEY_GAP;
	int sys_row_y = grid_bottom + KEY_GAP + SYS_ROW_LABEL_H;
	int sys_w = (grid_w - (int)(NUM_SYS_TONES - 1) * KEY_GAP) / (int)NUM_SYS_TONES;
	for (size_t i = 0; i < NUM_SYS_TONES; i++) {
		sys_tones[i].x = MARGIN + (int)i * (sys_w + KEY_GAP);
		sys_tones[i].y = sys_row_y;
		sys_tones[i].w = sys_w;
		sys_tones[i].h = SYS_ROW_H;
	}

	win_w = MARGIN * 2 + grid_w;
	win_h = sys_row_y + SYS_ROW_H + MARGIN + STATUS_H;
}

/* Independent of layout_keys()'s DTMF-grid math — sized purely from its
 * own content (one or two labeled fields), same "compute win_w/win_h
 * from what's actually being drawn" approach as the DTMF grid. */
static void layout_password_screen(void) {
	int field_count = (mode == MODE_LOGIN) ? 2 : 1;
	int content_h = TITLE_H + (int)field_count * (PASSWORD_LABEL_H + PASSWORD_FIELD_H)
		+ (field_count - 1) * PASSWORD_FIELD_GAP;

	win_w = MARGIN * 2 + PASSWORD_FIELD_W;
	win_h = MARGIN * 2 + content_h + STATUS_H;

	cred_x = MARGIN;
	cred_w = PASSWORD_FIELD_W;
	cred_h = PASSWORD_FIELD_H;
	int y = MARGIN + TITLE_H + PASSWORD_LABEL_H;
	cred_y[0] = y;
	if (field_count == 2) {
		cred_y[1] = y + PASSWORD_FIELD_H + PASSWORD_FIELD_GAP + PASSWORD_LABEL_H;
	} else {
		cred_y[1] = cred_y[0];
	}
}

static const struct dtmf_key *hit_test(double x, double y) {
	for (size_t i = 0; i < NUM_KEYS; i++) {
		if (x >= keys[i].x && x < keys[i].x + keys[i].w &&
			y >= keys[i].y && y < keys[i].y + keys[i].h) {
			return &keys[i];
		}
	}
	return NULL;
}

static struct sys_tone *sys_hit_test(double x, double y) {
	for (size_t i = 0; i < NUM_SYS_TONES; i++) {
		if (x >= sys_tones[i].x && x < sys_tones[i].x + sys_tones[i].w &&
			y >= sys_tones[i].y && y < sys_tones[i].y + sys_tones[i].h) {
			return &sys_tones[i];
		}
	}
	return NULL;
}

/* ---- pixel/bitmap-font drawing ----------------------------------------*/
static uint32_t pack_argb(syn_rgb c) {
	return 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static void put_pixel(int x, int y, uint32_t argb) {
	if (x < 0 || y < 0 || x >= win_w || y >= win_h) {
		return;
	}
	shm_pixels[y * win_w + x] = argb;
}

static void fill_rect(int x, int y, int w, int h, uint32_t argb) {
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			put_pixel(x + col, y + row, argb);
		}
	}
}

static void stroke_rect(int x, int y, int w, int h, uint32_t argb) {
	fill_rect(x, y, w, 1, argb);
	fill_rect(x, y + h - 1, w, 1, argb);
	fill_rect(x, y, 1, h, argb);
	fill_rect(x + w - 1, y, 1, h, argb);
}

static const struct font_glyph *find_glyph(char c) {
	char up = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
	for (size_t i = 0; i < FONT5X7_COUNT; i++) {
		if (FONT5X7[i].ch == up) {
			return &FONT5X7[i];
		}
	}
	return &FONT5X7[0]; /* space, for anything unrecognized */
}

static int text_width(const char *s, int scale) {
	int len = (int)strlen(s);
	return len > 0 ? len * (FONT_W + 1) * scale - scale : 0;
}

static void draw_char(int x, int y, char c, uint32_t argb, int scale) {
	const struct font_glyph *g = find_glyph(c);
	for (int row = 0; row < FONT_H; row++) {
		for (int col = 0; col < FONT_W; col++) {
			if (g->rows[row] & (1 << (FONT_W - 1 - col))) {
				fill_rect(x + col * scale, y + row * scale, scale, scale, argb);
			}
		}
	}
}

static void draw_text(int x, int y, const char *s, uint32_t argb, int scale) {
	int cx = x;
	for (const char *p = s; *p; p++) {
		draw_char(cx, y, *p, argb, scale);
		cx += (FONT_W + 1) * scale;
	}
}

static void draw_text_centered(int box_x, int box_w, int y, const char *s, uint32_t argb, int scale) {
	int tw = text_width(s, scale);
	draw_text(box_x + (box_w - tw) / 2, y, s, argb, scale);
}

/* Renders one credential field as a masked box: label above, bordered
 * panel below with '*' per typed char (never the real characters — this
 * is the whole point of a password field) and a blinking-free caret
 * (a single trailing block) so the user can tell where typing lands
 * without needing real cursor blink timing wired into the redraw loop. */
static void draw_cred_field(int field, const char *label, bool focused) {
	uint32_t panel = pack_argb(palette.panel);
	uint32_t accent = pack_argb(palette.accent);
	uint32_t text_c = pack_argb(palette.text);
	uint32_t border = pack_argb(palette.border);

	draw_text(cred_x, cred_y[field] - PASSWORD_LABEL_H + 2, label,
		focused ? accent : border, 1);

	fill_rect(cred_x, cred_y[field], cred_w, cred_h, panel);
	stroke_rect(cred_x, cred_y[field], cred_w, cred_h, focused ? accent : border);

	char masked[CRED_MAX + 2];
	int len = cred_len[field];
	for (int i = 0; i < len; i++) {
		masked[i] = '*';
	}
	masked[len] = focused ? '_' : '\0';
	masked[len + (focused ? 1 : 0)] = '\0';
	draw_text(cred_x + 6, cred_y[field] + (cred_h - FONT_H * GLYPH_SCALE_SMALL) / 2,
		masked, text_c, GLYPH_SCALE_SMALL);
}

static void draw_password_screen(void) {
	uint32_t bg = pack_argb(palette.bg);
	uint32_t accent = pack_argb(palette.accent);
	uint32_t border = pack_argb(palette.border);

	fill_rect(0, 0, win_w, win_h, bg);
	draw_text_centered(0, win_w, MARGIN - 4, mode_title, accent, GLYPH_SCALE_KEY - 1);

	if (mode == MODE_LOGIN) {
		draw_cred_field(0, "USERNAME", field_focus == 0);
		draw_cred_field(1, "PASSWORD", field_focus == 1);
	} else {
		draw_cred_field(0, "PASSWORD", true);
	}

	int status_y = win_h - MARGIN - STATUS_H + (STATUS_H - FONT_H * GLYPH_SCALE_SMALL) / 2;
	const char *hint = (mode == MODE_LOGIN)
		? "TAB:NEXT FIELD  ENTER:SUBMIT  ESC:CANCEL"
		: "ENTER:SUBMIT  ESC:CANCEL";
	draw_text_centered(0, win_w, status_y, hint, border, GLYPH_SCALE_SMALL);

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, win_w, win_h);
	wl_surface_commit(surface);
}

static void draw(void) {
	if (mode == MODE_PASSWORD || mode == MODE_LOGIN || mode == MODE_EXEC) {
		draw_password_screen();
		return;
	}

	uint32_t bg = pack_argb(palette.bg);
	uint32_t panel = pack_argb(palette.panel);
	uint32_t accent = pack_argb(palette.accent);
	uint32_t text_c = pack_argb(palette.text);
	uint32_t border = pack_argb(palette.border);

	fill_rect(0, 0, win_w, win_h, bg);

	draw_text_centered(0, win_w, MARGIN - 4, "DESTINATION", accent, GLYPH_SCALE_TITLE);

	fill_rect(MARGIN, MARGIN + TITLE_H, win_w - MARGIN * 2, READOUT_H - 6, panel);
	/* A typed hostname (see type_char()) can be longer than the readout
	 * box is wide — the DTMF grid's own short sequences never hit this,
	 * but "syn-node-03.lan" or a dotted-quad IP can. Show the tail (most
	 * recently typed end, where the cursor conceptually is) rather than
	 * silently letting draw_text() run off the right edge into nothing,
	 * same convention as a browser address bar scrolling to the caret. */
	int avail_w = win_w - MARGIN * 2 - 12;
	const char *visible = sequence;
	while (text_width(visible, GLYPH_SCALE_SMALL) > avail_w && *visible) {
		visible++;
	}
	draw_text(MARGIN + 6, MARGIN + TITLE_H + (READOUT_H - 6 - FONT_H * GLYPH_SCALE_SMALL) / 2,
		visible, text_c, GLYPH_SCALE_SMALL);

	for (size_t i = 0; i < NUM_KEYS; i++) {
		const struct dtmf_key *k = &keys[i];
		int lit = (int)i == highlighted_index;
		fill_rect(k->x, k->y, k->w, k->h, lit ? accent : panel);
		stroke_rect(k->x, k->y, k->w, k->h, border);

		char label[2] = {k->digit, '\0'};
		int tw = text_width(label, GLYPH_SCALE_KEY);
		draw_text(k->x + (k->w - tw) / 2, k->y + (k->h - FONT_H * GLYPH_SCALE_KEY) / 2,
			label, lit ? bg : text_c, GLYPH_SCALE_KEY);
	}

	for (size_t i = 0; i < NUM_SYS_TONES; i++) {
		const struct sys_tone *t = &sys_tones[i];
		int lit = (int)i == sys_highlighted_index;
		fill_rect(t->x, t->y, t->w, t->h, lit ? accent : panel);
		stroke_rect(t->x, t->y, t->w, t->h, border);
		int tw = text_width(t->label, GLYPH_SCALE_SYS);
		draw_text(t->x + (t->w - tw) / 2, t->y + (t->h - FONT_H * GLYPH_SCALE_SYS) / 2,
			t->label, lit ? bg : text_c, GLYPH_SCALE_SYS);
	}
	draw_text_centered(MARGIN, win_w - MARGIN * 2, sys_tones[0].y - SYS_ROW_LABEL_H,
		"SIGNAL TONES", border, 1);

	int status_y = win_h - MARGIN - STATUS_H + (STATUS_H - FONT_H * GLYPH_SCALE_SMALL) / 2;
	draw_text_centered(0, win_w, status_y, "*=DOT  ENTER:DIAL  BKSP:CLEAR  Q:QUIT", border, GLYPH_SCALE_SMALL);

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, win_w, win_h);
	wl_surface_commit(surface);
}

static void create_shm_buffer(void) {
	shm_size = win_w * win_h * 4;
	char name[64];
	snprintf(name, sizeof(name), "/syn-uplink-dialpad-%d", getpid());
	int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
	if (fd < 0) {
		perror("shm_open");
		exit(1);
	}
	shm_unlink(name);
	if (ftruncate(fd, shm_size) != 0) {
		perror("ftruncate");
		close(fd);
		exit(1);
	}
	shm_pixels = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm_pixels == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, shm_size);
	buffer = wl_shm_pool_create_buffer(pool, 0, win_w, win_h, win_w * 4, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
}

static void press_key(const struct dtmf_key *key) {
	/* '*' has no real use in an IP/hostname, and there's no dedicated
	 * '.' key on the DTMF grid — reaching for '*' as a dot stand-in is
	 * the natural (and historically common, e.g. old phone-menu IP
	 * entry) thing to do, so honor that: insert a literal '.' into the
	 * dialed sequence while still playing/highlighting the real '*' key
	 * that was actually pressed. redial_sequence()'s per-character tone
	 * replay looks up each stored char against keys[] — '.' won't
	 * match, so it's silently skipped on replay, same as any other
	 * typed-not-dialed character already is. */
	char stored = key->digit == '*' ? '.' : key->digit;
	if (sequence_len < SEQUENCE_MAX) {
		sequence[sequence_len++] = stored;
		sequence[sequence_len] = '\0';
	}
	highlighted_index = index_of(key);
	draw();
	syn_bar_notify_raw(key->lo, key->hi, DTMF_SECONDS);
	sleep_ms((long)(DTMF_SECONDS * 1000));
	highlighted_index = -1;
	draw();
}

static void press_sys_tone(const struct sys_tone *tone) {
	sys_highlighted_index = (int)(tone - sys_tones);
	draw();
	syn_bar_notify_raw(tone->lo, tone->hi, tone->seconds);
	sleep_ms((long)(tone->seconds * 1000));
	sys_highlighted_index = -1;
	draw();
}

static void backspace_digit(void) {
	if (sequence_len > 0) {
		sequence[--sequence_len] = '\0';
		draw();
	}
}

/* Appends any printable, non-DTMF character straight to the readout —
 * no tone, no key-grid highlight, since there's no key to light up for
 * '.', most letters, '-', etc. This is what makes the dialpad a real
 * IP/hostname prompt (e.g. "192.168.1.50", "syn-node-03.lan") rather
 * than being limited to its 16-key DTMF alphabet: the phone-style grid
 * stays exactly as-is for mouse clicks and digit/A-D/star/hash tones,
 * typing anything else on a real keyboard just works. */
static void type_char(char c) {
	if (c < 0x20 || c == 0x7f) {
		return; /* control character — not something a host/IP needs */
	}
	if (sequence_len < SEQUENCE_MAX) {
		sequence[sequence_len++] = c;
		sequence[sequence_len] = '\0';
		draw();
	}
}

/* ---- MODE_PASSWORD/MODE_LOGIN/MODE_EXEC input handling ----------------*/
static void cred_type_char(char c) {
	if (c < 0x20 || c == 0x7f) {
		return;
	}
	if (cred_len[field_focus] < CRED_MAX) {
		cred_field[field_focus][cred_len[field_focus]++] = c;
		cred_field[field_focus][cred_len[field_focus]] = '\0';
		draw();
		syn_bar_notify_meaning("KEY_CLICK");
	}
}

static void cred_backspace(void) {
	if (cred_len[field_focus] > 0) {
		cred_field[field_focus][--cred_len[field_focus]] = '\0';
		draw();
		syn_bar_notify_meaning("KEY_CLICK");
	}
}

static void cred_next_field(void) {
	if (mode == MODE_LOGIN) {
		field_focus = 1 - field_focus;
		draw();
	}
}

/* Called from keyboard_key() on Enter (MODE_PASSWORD/MODE_EXEC always;
 * MODE_LOGIN only once the password field itself has focus — Enter on
 * the username field just advances focus, matching the "Tab or Enter
 * moves focus down" contract). Prints one line per field to stdout
 * (username first if MODE_LOGIN) and stops the event loop; MODE_EXEC
 * intercepts this via running_exec_after_submit rather than exiting
 * immediately, since it still has real work to do (the forkpty/doas
 * dance in main()). */
static int submit_pending = 0;

static void cred_submit(void) {
	if (mode == MODE_LOGIN && field_focus == 0) {
		cred_next_field();
		return;
	}
	if (mode == MODE_LOGIN) {
		printf("%s\n%s\n", cred_field[0], cred_field[1]);
	} else {
		printf("%s\n", cred_field[0]);
	}
	fflush(stdout);
	syn_bar_notify_meaning("SUCCESS");
	if (mode == MODE_EXEC) {
		submit_pending = 1; /* main()'s loop breaks out to do the forkpty/doas work */
	}
	running = 0;
}

/* Plays the readback, then hands the dialed sequence to the caller via
 * stdout and exits — a caller (e.g. syn-relay's syn_dialpad_prompt())
 * popen()s this binary and reads one line back, same contract as a rofi
 * -dmenu prompt. q/Esc exit with no output, treated as a cancel. */
static void redial_sequence(void) {
	for (int i = 0; i < sequence_len; i++) {
		struct dtmf_key *key = lookup(sequence[i]);
		if (key) {
			highlighted_index = index_of(key);
			draw();
			syn_bar_notify_raw(key->lo, key->hi, DTMF_SECONDS);
			wl_display_flush(display);
			sleep_ms((long)(DTMF_SECONDS * 1000));
			highlighted_index = -1;
			draw();
			wl_display_flush(display);
			sleep_ms(READBACK_GAP_MS);
			wl_display_dispatch_pending(display);
		}
	}
	printf("%s\n", sequence);
	fflush(stdout);
	running = 0;
}

/* ---- xdg_wm_base ------------------------------------------------------ */
static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {.ping = wm_base_ping};

/* ---- xdg_surface -------------------------------------------------------*/
static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
	(void)data;
	xdg_surface_ack_configure(xs, serial);
	if (!configured) {
		configured = 1;
		create_shm_buffer();
		draw();
	}
}
static const struct xdg_surface_listener xdg_surface_listener = {.configure = xdg_surface_configure};

/* ---- xdg_toplevel ------------------------------------------------------*/
static void toplevel_configure(void *data, struct xdg_toplevel *tl, int32_t w, int32_t h,
		struct wl_array *states) {
	(void)data; (void)tl; (void)w; (void)h; (void)states;
	/* Fixed-size dialog — the compositor's suggested size is ignored; we
	 * always present at the content-computed win_w/win_h from layout_keys(). */
}
static void toplevel_close(void *data, struct xdg_toplevel *tl) {
	(void)data; (void)tl;
	running = 0;
}
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

/* ---- wl_keyboard -------------------------------------------------------*/
static void keyboard_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int32_t fd, uint32_t size) {
	(void)data; (void)kb;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map_str == MAP_FAILED) {
		close(fd);
		return;
	}
	struct xkb_keymap *new_keymap = xkb_keymap_new_from_string(
		xkb_ctx, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_str, size);
	close(fd);
	if (!new_keymap) {
		return;
	}
	struct xkb_state *new_state = xkb_state_new(new_keymap);
	if (xkb_state) xkb_state_unref(xkb_state);
	if (xkb_keymap) xkb_keymap_unref(xkb_keymap);
	xkb_keymap = new_keymap;
	xkb_state = new_state;
}
static void keyboard_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
		struct wl_surface *surf, struct wl_array *keys_pressed) {
	(void)data; (void)kb; (void)serial; (void)surf; (void)keys_pressed;
}
static void keyboard_leave(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surf) {
	(void)data; (void)kb; (void)serial; (void)surf;
}
static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state) {
	(void)data; (void)kb; (void)serial; (void)time;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !xkb_state) {
		return;
	}
	xkb_keycode_t keycode = key + 8; /* evdev-to-xkb offset */
	xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, keycode);

	if (sym == XKB_KEY_Escape || (sym == XKB_KEY_q && mode == MODE_DIALPAD) || (sym == XKB_KEY_Q && mode == MODE_DIALPAD)) {
		running = 0;
		return;
	}

	if (mode == MODE_PASSWORD || mode == MODE_LOGIN || mode == MODE_EXEC) {
		if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
			cred_submit();
			return;
		}
		if (sym == XKB_KEY_Tab) {
			cred_next_field();
			return;
		}
		if (sym == XKB_KEY_BackSpace) {
			cred_backspace();
			return;
		}
		char buf[8];
		int n = xkb_state_key_get_utf8(xkb_state, keycode, buf, sizeof(buf));
		if (n == 1) {
			cred_type_char(buf[0]);
		}
		return;
	}

	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		redial_sequence();
		return;
	}
	if (sym == XKB_KEY_BackSpace) {
		backspace_digit();
		return;
	}

	char buf[8];
	int n = xkb_state_key_get_utf8(xkb_state, keycode, buf, sizeof(buf));
	if (n == 1) {
		struct dtmf_key *pressed = lookup(normalize(buf[0]));
		if (pressed) {
			press_key(pressed); /* digit/A-D/star/hash — tone + key-grid highlight */
		} else {
			type_char(buf[0]); /* '.', other letters, '-', etc. — silent append */
		}
	}
}
static void keyboard_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
		uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	(void)data; (void)kb; (void)serial;
	if (xkb_state) {
		xkb_state_update_mask(xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
	}
}
static void keyboard_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay) {
	(void)data; (void)kb; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

/* ---- wl_pointer ----------------------------------------------------- */
static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf,
		wl_fixed_t sx, wl_fixed_t sy) {
	(void)data; (void)p; (void)serial; (void)surf;
	pointer_x = wl_fixed_to_double(sx);
	pointer_y = wl_fixed_to_double(sy);
}
static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf) {
	(void)data; (void)p; (void)serial; (void)surf;
}
static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	(void)data; (void)p; (void)time;
	pointer_x = wl_fixed_to_double(sx);
	pointer_y = wl_fixed_to_double(sy);
}
static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
		uint32_t button, uint32_t state) {
	(void)data; (void)p; (void)serial; (void)time;
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}
	const struct dtmf_key *pressed = hit_test(pointer_x, pointer_y);
	if (pressed) {
		press_key(pressed);
		return;
	}
	struct sys_tone *sys_pressed = sys_hit_test(pointer_x, pointer_y);
	if (sys_pressed) {
		press_sys_tone(sys_pressed);
	}
}
static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value) {
	(void)data; (void)p; (void)time; (void)axis; (void)value;
}
static void pointer_frame(void *data, struct wl_pointer *p) { (void)data; (void)p; }
static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) { (void)data; (void)p; (void)source; }
static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis) { (void)data; (void)p; (void)time; (void)axis; }
static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t discrete) { (void)data; (void)p; (void)axis; (void)discrete; }
static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

/* ---- wl_seat ---------------------------------------------------------- */
static void seat_capabilities(void *data, struct wl_seat *s, uint32_t caps) {
	(void)data;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
		keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}
static void seat_name(void *data, struct wl_seat *s, const char *name) { (void)data; (void)s; (void)name; }
static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

/* ---- wl_registry -------------------------------------------------------*/
static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
		const char *interface, uint32_t version) {
	(void)data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(reg, name, &wl_seat_interface, 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	}
	(void)version;
}
static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)data; (void)reg; (void)name;
}
static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

/* argv[exec_argv_start..] once MODE_EXEC is selected — the real command
 * to run under doas, e.g. {"doas", "systemctl", "restart", "foo", NULL}. */
static char **exec_argv;

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [--password [title] | --login [title] | --exec -- <command> [args...]]\n"
		"  (no args)         DTMF dialpad\n"
		"  --password [title] masked password prompt, one line on stdout\n"
		"  --login [title]    username+password prompt, two lines on stdout\n"
		"  --exec -- <cmd>    show password prompt, then run <cmd> under doas,\n"
		"                     feeding the password to its real pty prompt\n",
		argv0);
}

static bool parse_args(int argc, char **argv) {
	if (argc < 2) {
		return true; /* MODE_DIALPAD, default */
	}
	if (strcmp(argv[1], "--password") == 0) {
		mode = MODE_PASSWORD;
		if (argc >= 3) snprintf(mode_title, sizeof(mode_title), "%s", argv[2]);
		else snprintf(mode_title, sizeof(mode_title), "AUTHENTICATE");
		return true;
	}
	if (strcmp(argv[1], "--login") == 0) {
		mode = MODE_LOGIN;
		if (argc >= 3) snprintf(mode_title, sizeof(mode_title), "%s", argv[2]);
		else snprintf(mode_title, sizeof(mode_title), "AUTHENTICATE");
		return true;
	}
	if (strcmp(argv[1], "--exec") == 0) {
		if (argc < 4 || strcmp(argv[2], "--") != 0) {
			return false; /* need: --exec -- <command> [args...] */
		}
		mode = MODE_EXEC;
		snprintf(mode_title, sizeof(mode_title), "AUTHENTICATE");
		exec_argv = &argv[3]; /* already NULL-terminated, same array as argv itself */
		return true;
	}
	return false;
}

/* True when doas will run a command without asking for a password at
 * all — a nopass rule (e.g. "permit nopass :wheel") or a still-valid
 * persist timestamp. run_under_doas_pty() below would then wait forever
 * for a prompt that never comes, relaying neither the child's output nor
 * this terminal's input — a blank, dead screen — so --exec skips the
 * password screen and hands the real terminal straight to doas instead. */
static bool doas_needs_no_password(void) {
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
		}
		execlp("doas", "doas", "-n", "true", (char *)NULL);
		_exit(127);
	}
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Runs `exec_argv` (already doas-prefixed by the caller — e.g.
 * {"doas", "systemctl", ...}) inside a pty, and feeds `password` to it
 * the moment doas's own prompt text appears on the child's output side.
 * Modeled directly on syn-iso-builder's run_mkarchiso() (syn_build_
 * runner.c) for the forkpty()+select()-loop shape; the write-to-pty-on-
 * prompt-detection part is new (that existing code only ever reads from
 * its child). doas refuses a piped/non-tty password outright (confirmed
 * empirically: "Operation not permitted" even with a correct password
 * piped to stdin) — a real pty is the only thing that satisfies its
 * readpassphrase() call, which is why this can't just be `popen()`. */
static int run_under_doas_pty(char **argv_to_exec, const char *password) {
	/* Ensures default SIGCHLD disposition so this function's own
	 * waitpid(pid, ...) calls below can reap the doas child directly. */
	struct sigaction old_sa;
	struct sigaction default_sa = {.sa_handler = SIG_DFL};
	sigemptyset(&default_sa.sa_mask);
	sigaction(SIGCHLD, &default_sa, &old_sa);

	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		ws.ws_row = 24;
		ws.ws_col = 80;
		ws.ws_xpixel = ws.ws_ypixel = 0;
	}

	int master_fd;
	pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
	if (pid < 0) {
		perror("syn-uplink-dialpad: forkpty");
		sigaction(SIGCHLD, &old_sa, NULL);
		return 1;
	}
	if (pid == 0) {
		execvp(argv_to_exec[0], argv_to_exec);
		fprintf(stderr, "syn-uplink-dialpad: failed to exec %s\n", argv_to_exec[0]);
		_exit(127);
	}

	char buf[512];
	char seen[1024] = "";
	int password_sent = 0;

	/* Phase 1: watch silently for doas's own password prompt and answer
	 * it with what the graphical screen already collected — the user
	 * never sees doas's raw text prompt, only this program's. */
	while (!password_sent) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(master_fd, &rfds);
		struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
		int rc = select(master_fd + 1, &rfds, NULL, NULL, &tv);
		if (rc < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (rc == 0) {
			int status;
			if (waitpid(pid, &status, WNOHANG) == pid) {
				sigaction(SIGCHLD, &old_sa, NULL);
				return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
			}
			continue;
		}
		ssize_t n = read(master_fd, buf, sizeof(buf) - 1);
		if (n <= 0) {
			break; /* child closed the pty (exited) before ever prompting */
		}
		buf[n] = '\0';

		/* doas's prompt is "doas (user@host) password: " with no
		 * trailing newline, always written as one short chunk on a
		 * fresh pty — just accumulate into a fixed buffer and look for
		 * the stable substring. If something unexpected floods output
		 * before ever prompting (shouldn't happen — doas always prompts
		 * before running anything, including re-execing a long-lived
		 * program like syn-iso-builder), drop the buffer once it's
		 * nearly full rather than overflow. */
		size_t seen_len = strlen(seen);
		if (seen_len + (size_t)n >= sizeof(seen) - 1) {
			seen[0] = '\0';
			seen_len = 0;
		}
		strncat(seen, buf, sizeof(seen) - 1 - seen_len);
		if (strstr(seen, "password:")) {
			char line[CRED_MAX + 2];
			snprintf(line, sizeof(line), "%s\n", password);
			write(master_fd, line, strlen(line));
			password_sent = 1;
		}
	}

	/* Phase 2: once authenticated, this may be a short one-shot command
	 * (cmd_stream_app-style: exits almost immediately, output relayed
	 * via stderr below) or a full re-exec'd interactive program (e.g.
	 * syn-iso-builder re-execing its whole ncurses TUI under doas) that
	 * needs real, ongoing terminal control — arrow keys, resizes, its
	 * own raw-mode input, all of it — for the rest of its life. There's
	 * no way to tell which in advance, so this always does a real
	 * bidirectional relay (this process's stdin -> the child's pty,
	 * the pty's output -> this process's stdout) until the child exits,
	 * exactly what `script`/`tmux` do — a one-shot command just produces
	 * a very short-lived relay before naturally hitting EOF. */
	struct termios orig_termios;
	int stdin_is_tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &orig_termios) == 0;
	if (stdin_is_tty) {
		struct termios raw = orig_termios;
		cfmakeraw(&raw);
		tcsetattr(STDIN_FILENO, TCSANOW, &raw);
	}

	int child_alive = 1;
	while (child_alive) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(master_fd, &rfds);
		FD_SET(STDIN_FILENO, &rfds);
		int maxfd = master_fd > STDIN_FILENO ? master_fd : STDIN_FILENO;
		int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (rc < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (FD_ISSET(master_fd, &rfds)) {
			ssize_t n = read(master_fd, buf, sizeof(buf));
			if (n <= 0) {
				child_alive = 0;
			} else {
				fwrite(buf, 1, (size_t)n, stdout);
				fflush(stdout);
			}
		}
		if (child_alive && FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
			if (n > 0) {
				write(master_fd, buf, (size_t)n);
			}
		}
	}

	if (stdin_is_tty) {
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
	}

	int status;
	int rc = 1;
	if (waitpid(pid, &status, 0) == pid) {
		rc = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}
	sigaction(SIGCHLD, &old_sa, NULL);
	return rc;
}

int main(int argc, char **argv) {
	if (!parse_args(argc, argv)) {
		print_usage(argv[0]);
		return 1;
	}

	if (mode == MODE_EXEC && doas_needs_no_password()) {
		execvp(exec_argv[0], exec_argv);
		fprintf(stderr, "syn-uplink-dialpad: failed to exec %s\n", exec_argv[0]);
		return 127;
	}

	syn_theme_load(&palette);
	if (mode == MODE_DIALPAD) {
		layout_keys();
	} else {
		layout_password_screen();
	}

	xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "syn-uplink-dialpad: cannot connect to Wayland display\n");
		return 1;
	}

	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!compositor || !shm || !wm_base) {
		fprintf(stderr, "syn-uplink-dialpad: missing required Wayland globals\n");
		return 1;
	}

	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(xdg_toplevel, &toplevel_listener, NULL);
	xdg_toplevel_set_app_id(xdg_toplevel, "syn-uplink-dialpad");
	xdg_toplevel_set_title(xdg_toplevel, mode == MODE_DIALPAD ? "DESTINATION" : mode_title);
	xdg_toplevel_set_min_size(xdg_toplevel, win_w, win_h);
	xdg_toplevel_set_max_size(xdg_toplevel, win_w, win_h);
	wl_surface_commit(surface);

	while (running && wl_display_dispatch(display) != -1) {
	}

	if (buffer) wl_buffer_destroy(buffer);
	if (shm_pixels) munmap(shm_pixels, shm_size);
	if (xdg_toplevel) xdg_toplevel_destroy(xdg_toplevel);
	if (xdg_surface) xdg_surface_destroy(xdg_surface);
	if (surface) wl_surface_destroy(surface);
	if (xkb_state) xkb_state_unref(xkb_state);
	if (xkb_keymap) xkb_keymap_unref(xkb_keymap);
	if (xkb_ctx) xkb_context_unref(xkb_ctx);
	wl_display_disconnect(display);

	if (mode == MODE_EXEC && submit_pending) {
		int rc = run_under_doas_pty(exec_argv, cred_field[0]);
		memset(cred_field[0], 0, sizeof(cred_field[0])); /* don't leave the password sitting in memory longer than needed */
		return rc;
	}
	return 0;
}
