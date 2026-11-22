/* ------------------------------------------------------------------------
 *   The block map, a squarified treemap (Bruls, Huizing and van Wijk,
 *   2000): a folder's children are laid in rows along its shorter side,
 *   a row closed as soon as another child would make its rectangles
 *   less square.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <math.h>

#include "scene.h"

/* a folder smaller than this, either way, is drawn whole and not opened */
#define OPEN_MIN 7.0f
/* a platform's thickness and the tallest file, in the solid view's units */
#define SLAB 0.012f
#define TALLEST 0.45f

typedef struct {
	Scene *s;
	const Theme *t;
	float w, h;      /* the view */
	int text_h;
	float lift;      /* the gap between levels in 3D, times SLAB */
	uint64_t whole;  /* the focus's size */
} Build;

static void solid(const Build *b, Item *it) {
	float unit = 1.0f / (b->w > b->h ? b->w : b->h);
	bool dir = it->node->is_dir;
	float tall = dir ? SLAB : TALLEST * sqrtf((float)it->size / (float)b->whole);
	if (tall < SLAB / 2) {
		tall = SLAB / 2;
	}
	/* a folder is the platform at its depth, and what is in it, one deeper,
	 * starts where that platform ends */
	/* flat still keeps a sliver between levels, so nested platforms never fight */
	float base = (float)it->depth * SLAB * (b->lift > 0.05f ? b->lift : 0.05f);
	it->pos[0] = (it->x + it->w / 2 - b->w / 2) * unit;
	it->pos[1] = base + tall / 2;
	it->pos[2] = (it->y + it->h / 2 - b->h / 2) * unit;
	it->size3[0] = it->w * unit;
	it->size3[1] = tall;
	it->size3[2] = it->h * unit;
}

static void place(const Build *b, Node *n, float x, float y, float w, float h, int depth);

/* how far from square the worst rectangle of a row is, the row `sum` in
 * area laid along a side `side` long, its largest and smallest given */
static float worst(float sum, float largest, float smallest, float side) {
	float s2 = side * side, sum2 = sum * sum;
	float a = s2 * largest / sum2, c = sum2 / (s2 * smallest);
	return a > c ? a : c;
}

static void inside(const Build *b, Node *dir, float x, float y, float w, float h, int depth) {
	uint64_t total = 0;
	for (int i = 0; i < dir->nkids; i++) {
		total += dir->kids[i]->size;
	}
	if (!total) {
		return;
	}
	float per_byte = w * h / (float)total;
	int i = 0;
	while (i < dir->nkids && w > 0.5f && h > 0.5f) {
		float side = w < h ? w : h;
		float first = (float)dir->kids[i]->size * per_byte;
		if (first < 1.0f) {
			break; /* the rest are smaller than a pixel */
		}
		float sum = first;
		int j = i + 1;
		while (j < dir->nkids) {
			float next = (float)dir->kids[j]->size * per_byte;
			if (next <= 0 || worst(sum + next, first, next, side) >
			                         worst(sum, first, (float)dir->kids[j - 1]->size * per_byte, side)) {
				break;
			}
			sum += next;
			j++;
		}
		float thick = sum / side, at = 0;
		for (int k = i; k < j; k++) {
			float len = (float)dir->kids[k]->size * per_byte / thick;
			if (w >= h) {
				place(b, dir->kids[k], x, y + at, thick, len, depth);
			} else {
				place(b, dir->kids[k], x + at, y, len, thick, depth);
			}
			at += len;
		}
		if (w >= h) {
			x += thick;
			w -= thick;
		} else {
			y += thick;
			h -= thick;
		}
		i = j;
	}
}

static void place(const Build *b, Node *n, float x, float y, float w, float h, int depth) {
	Item *it = scene_add(b->s);
	it->node = n;
	it->size = n->size;
	it->depth = depth;
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	if (!n->is_dir) {
		it->col = kind_colour(kind_of(n->name), b->t);
		solid(b, it);
		return;
	}
	/* deeper folders a little lighter, so the nesting reads */
	it->col = mix(rgb(b->t->panel), rgb(b->t->panel_hover), depth > 4 ? 1.0f : (float)depth / 4);
	solid(b, it);
	if (w < OPEN_MIN || h < OPEN_MIN) {
		return;
	}
	/* a border, and where there is room the strip its name is written on */
	float pad = 2, head = blocks_named(w, h, b->text_h) ? (float)b->text_h + 2 : 0;
	inside(b, n, x + pad, y + pad + head, w - 2 * pad, h - 2 * pad - head, depth + 1);
}

void blocks_build(Scene *s, Node *focus, float w, float h, int text_h, float lift, const Theme *t) {
	scene_clear(s);
	if (!focus->size) {
		return;
	}
	Build b = {s, t, w, h, text_h, lift, focus->size};
	place(&b, focus, 0, 0, w, h, 0);
}
