/* ------------------------------------------------------------------------
 *   What a view shows of the folder in focus: a list of items, each a
 *   folder or file with its flat shape and its solid one, so the same
 *   picture is drawn in 2D or 3D and clicked through either way.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIRSTAT_SCENE_H
#define SYN_DIRSTAT_SCENE_H

#include <SDL2/SDL.h>

#include "scan.h"
#include "theme.h"

typedef struct {
	Node *node;    /* NULL for the node that stands for a folder's small things together */
	int more;      /* how many things that node stands for */
	uint64_t size; /* the node's size, or theirs together */
	int depth;     /* levels below the focus */
	bool ball;     /* a graph node; otherwise a block */
	/* flat, in pixels inside the view: a block's rectangle, or a ball's
	 * centre (x, y) and radius (w) */
	float x, y, w, h;
	/* solid: the centre and the whole size, the view about one unit across */
	float pos[3], size3[3];
	SDL_Color col;
} Item;

typedef struct {
	Item *v;
	int n, cap;
	int *edge; /* pairs of items joined by a line: parent, child */
	int nedge, edge_cap;
} Scene;

void scene_clear(Scene *s);
Item *scene_add(Scene *s);
void scene_join(Scene *s, int parent, int child);

/* The block map: the focus folder's space cut into rectangles, a folder's
 * inside its own, each with the area its size is of the whole; in 3D the
 * folders are platforms and the files stand on them. */
void blocks_build(Scene *s, Node *focus, float w, float h, int text_h, const Theme *t);
/* Whether a folder's block that size has the strip with its name. */
static inline bool blocks_named(float w, float h, int text_h) { return w > 70 && h > (float)text_h * 2.5f; }

/* The node graph: the focus in the middle, its biggest children round it
 * and theirs round them, every ball's area its size. */
void graph_build(Scene *s, Node *focus, float w, float h, const Theme *t);

/* ---- colours (kinds.c) ---- */

enum { KIND_COUNT = 9 };
/* What kind of file the name's ending says it is, 0 .. KIND_COUNT-1. */
int kind_of(const char *name);
const char *kind_name(int kind);
/* The kind's colour, leaning toward the theme's accent so it sits with it. */
SDL_Color kind_colour(int kind, const Theme *t);

static inline SDL_Color rgb(Rgb c) { return (SDL_Color){c.r, c.g, c.b, 255}; }

static inline SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
	return (SDL_Color){(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t),
	                   (Uint8)(a.b + (b.b - a.b) * t), 255};
}

#endif
