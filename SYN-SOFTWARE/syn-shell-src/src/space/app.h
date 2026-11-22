/* ------------------------------------------------------------------------
 *   The window: the disks to pick from, the scan's progress, then the
 *   list beside the view (blocks or graph, flat or solid).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_APP_H
#define SYN_SHELL_SPACE_APP_H

#include <stdbool.h>

typedef struct {
	const char *path;  /* what to scan at once; NULL to start at the disks */
	const char *theme; /* a theme's name; NULL for the system's */
	bool graph;        /* start in the node graph, not the blocks */
	bool solid;        /* start in 3D */
	const char *shot;  /* write the first finished picture here (PPM) and leave */
	int shot_at;       /* with shot: the picture this many ms after the start instead, mid-scan or not */
	int levels;        /* the DEPTH slider to start at: 1 .. 11; 0 for all */
	float lift;        /* the HEIGHT slider to start at, 0 .. 3; below 0 for as made (1) */
	bool walk;         /* walk mode in the folder scanned, once its tree is up */
	float spin;        /* the SPIN slider to start at, times SYN-TREK's speed, 0 .. 3; below 0 for 1 */
} AppOptions;

/* Runs the window until it is closed; the program's exit status. */
int app_run(const AppOptions *opt);

/* ---- Inside SYN-SHELL's window: no window of its own --------------------
 *
 * SYN-SHELL draws the view in its right-hand column (a QOpenGLWidget, whose
 * GL context is current for space_gl_init and space_frame), hands it the
 * mouse and keys as SDL events, and shows the tree in its left column. All
 * from SYN-SHELL's one thread; the scan runs in its own. */

#include <SDL2/SDL.h>
#include "scan.h"

typedef struct App App;
enum { SPACE_FOCUS = 1, SPACE_SELECT = 2, SPACE_USE = 4 };

App *space_new(void);
/* In the GL context, which is current; dpi is pixels per mouse unit. */
bool space_gl_init(App *a, void *(*getproc)(const char *), float dpi);
/* SYN-SHELL is in this folder: the view shows it, from the tree scanned
 * already if it is in it, otherwise from a new scan of it. */
void space_show(App *a, const char *dir);
/* SYN-SHELL's cursor is on this. */
void space_select(App *a, const char *path);
/* The mouse (in mouse units) and keys over the view. */
void space_event(App *a, const SDL_Event *e);
/* One frame, w x h pixels, into the current GL context's framebuffer. */
void space_frame(App *a, int w, int h, float dpi);
/* The scan's news without drawing, while the view is not shown. */
void space_tick(App *a);
/* Whether it wants another frame soon (moving, scanning, news to give). */
bool space_busy(const App *a);
/* The corner render: on, only the view, 3D and turning, with no bar,
 * buttons, sliders or status and nothing to press; off, back to the view
 * it had. */
void space_set_mini(App *a, bool on);
bool space_walking(const App *a);
/* Walking, the room has the mouse (a click took it, Esc gives it back):
 * the host locks the pointer while this is true, hands its movement to
 * space_look, and space_release when the window loses the keys. */
bool space_captured(const App *a);
void space_release(App *a);
void space_look(App *a, float dx, float dy);
bool space_live(const App *a);
/* What the user did in the view since last asked: SPACE_FOCUS (went to
 * the folder in focus: SYN-SHELL follows), SPACE_SELECT (picked sel) and
 * SPACE_USE (E on a file while walking: space_used says which). */
int space_changes(App *a, char *focus, char *sel, int len);
/* After SPACE_USE: the file to use (play, show, open), its kind (kinds.c's)
 * and what it is in /dev (a WALK_DEV_; 0 for a file). */
const char *space_used(const App *a, int *kind, int *dev);
/* The host plays this file now (NULL: nothing), shown in the room; and
 * whether it has anything to play with at all. */
void space_set_playing(App *a, const char *path);
void space_set_player(App *a, bool there);
/* The room takes the mouse again (an overlay over it has closed). */
void space_capture(App *a);
/* What E would do now ("play track01.flac"); false for nothing. */
bool space_target(const App *a, char *out, int len);
/* Walking: to the file at path, in its room, facing it. */
void space_walk_to(App *a, const char *path);

/* The tree, for SYN-SHELL's left column. Its nodes stay put until the tree
 * is dropped (drop is called first, in SYN-SHELL's thread); a node's
 * children change while it is scanned, so read them holding the lock. */
void space_lock(App *a);
void space_unlock(App *a);
Node *space_root(const App *a);
unsigned space_generation(const App *a); /* changes with each new tree */
Node *space_find(const App *a, const char *path); /* holding the lock */
void space_on_drop(App *a, void (*drop)(void *), void *host);
/* The node's colour in the view: its kind's, a folder's accent. */
void space_colour(const App *a, const Node *n, unsigned char rgb[3]);
/* Stops the scan, before the window goes. */
void space_quit(App *a);

#endif
