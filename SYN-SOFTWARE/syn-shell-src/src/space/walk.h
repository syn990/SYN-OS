/* ------------------------------------------------------------------------
 *   Walk mode: the disk as a place to walk about in. The folder you are
 *   in is a room, its biggest folders doorways in its walls (and one back
 *   up), its files blocks standing on the floor, as tall as they are big,
 *   to climb and jump on; you are a small figure, seen from behind or
 *   through its eyes.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_WALK_H
#define SYN_SHELL_SPACE_WALK_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "scan.h"
#include "theme.h"

typedef struct Walk Walk;

/* what a click or a key asks of the window */
enum { WALK_NONE, WALK_EXIT };

/* What a thing in /dev is, by what it looks like in the room. */
enum { WALK_DEV_NONE, WALK_DEV_DISK, WALK_DEV_TTY, WALK_DEV_SOUND, WALK_DEV_GPU, WALK_DEV_INPUT, WALK_DEV_VOID, WALK_DEV_CHAR };

/* Pictures for the blocks, from the host (SYN-SHELL's Qt side): a picture
 * file's own image, a code or text file's first lines as a screen, any
 * other file's icon. Fills a malloc'd RGBA image, top row first, that the
 * caller frees; false for none. NULL (the standalone window): plain blocks.
 * kind is kinds.c's, dev a WALK_DEV_ (WALK_DEV_NONE for a file). Called
 * with the GL context current, a couple of times a frame at most. */
extern bool (*walk_picture)(const char *path, int kind, int dev, unsigned char **rgba, int *w, int *h);

/* The keys held, by scancode: NULL for SDL's own, or the host's array
 * when the view is drawn inside SYN-SHELL's window. */
extern const Uint8 *walk_keys;

Walk *walk_new(void);
bool walk_on(const Walk *w);
/* Into the room of folder dir (a file: the folder it is in), standing by
 * near if it is one of the room's files; call holding the tree. */
void walk_enter(Walk *w, Node *dir, const Node *near);
/* Out again: the folder you were in, for the map to show. */
Node *walk_leave(Walk *w);
Node *walk_room(const Walk *w);

/* A step of dt seconds: the keys held, gravity, the doorways; holding the tree. */
void walk_update(Walk *w, float dt);
/* Into the rectangle (window pixels), in the theme; back is the sky's
 * colour (its alpha for a see-through window); mx, my the mouse. */
void walk_draw(Walk *w, float x, float y, float vw, float vh, const Theme *t, SDL_Color back, float mx, float my);

int walk_key(Walk *w, SDL_Keycode k);
int walk_mouse_down(Walk *w, float x, float y, int button);
void walk_mouse_up(Walk *w, int button);
/* the mouse moved this many pixels (it turns you while captured) */
void walk_mouse_move(Walk *w, float dx, float dy);
/* The window holds the mouse (a click in the room took it, as in a game;
 * Esc gives it back): the host keeps the pointer locked while it is. */
bool walk_captured(const Walk *w);
void walk_release(Walk *w);
/* The locked mouse moved this much, in its own counts (unaccelerated). */
void walk_look(Walk *w, float dx, float dy);
void walk_wheel(Walk *w, int notches);

/* ---- using the things in the room ----
 * E (or F) on the file you face and are close to asks the host to use it:
 * play it, show it, open it. walk_used hands over what was asked for since
 * last time (its path, kinds.c's kind, a WALK_DEV_), once. */
bool walk_used(Walk *w, char *path, int len, int *kind, int *dev);
/* The host plays this file now (NULL: nothing); whether it can play at all
 * (called once by a host that uses things: till then E does nothing). */
void walk_set_playing(Walk *w, const char *path);
void walk_set_player(Walk *w, bool there);
/* The window takes the mouse again, as a click in the room would. */
void walk_capture(Walk *w);
/* What E would do now, as the room shows it ("play track01.flac"); false for nothing. */
bool walk_target(const Walk *w, char *out, int len);
/* To that file's block in this room, on a side with room to stand, facing it; false if it has none. */
bool walk_go_to(Walk *w, const Node *n);

#endif
