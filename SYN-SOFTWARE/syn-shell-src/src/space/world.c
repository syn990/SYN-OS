/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene.h"

#define MAX_BUILDINGS 400
#define STREET_W 8.0f   /* a street, kerb to kerb */
#define AVENUE_W 14.0f  /* and an avenue */
#define RING_ROAD 12.0f /* the road round the last blocks, inside the wall */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static unsigned seed_of(const char *s) {
	unsigned h = 2166136261u;
	for (; *s; s++) {
		h = (h ^ (unsigned char)*s) * 16777619u;
	}
	return h;
}

#define GROW(v, n, cap, first)                                                \
	do {                                                                      \
		if ((n) == (cap)) {                                                   \
			(cap) = (cap) ? (cap) * 2 : (first);                              \
			(v) = realloc((v), (size_t)(cap) * sizeof *(v));                  \
		}                                                                     \
	} while (0)

/* biggest first, then by name: the same order every time */
static int by_size(const void *pa, const void *pb) {
	const Node *a = *(Node *const *)pa, *b = *(Node *const *)pb;
	if (a->size != b->size) {
		return a->size < b->size ? 1 : -1;
	}
	return strcmp(a->name, b->name);
}

/* the folders in d, biggest first; malloc'd, n of them */
static Node **folders(const Node *d, int *n) {
	Node **v = malloc((size_t)(d->nkids + 1) * sizeof *v);
	*n = 0;
	for (int i = 0; i < d->nkids; i++) {
		if (d->kids[i]->is_dir) {
			v[(*n)++] = d->kids[i];
		}
	}
	qsort(v, (size_t)*n, sizeof *v, by_size);
	return v;
}

/* the kind most of what is in d is, by size, a level or two down; -1 none */
static void tally(const Node *d, uint64_t *by, int levels, int *budget) {
	for (int i = 0; i < d->nkids && *budget > 0; i++, (*budget)--) {
		const Node *k = d->kids[i];
		if (!k->is_dir) {
			by[kind_of(k->name)] += k->size + 1;
		} else if (levels > 0) {
			tally(k, by, levels - 1, budget);
		}
	}
}

static int mostly(const Node *d) {
	uint64_t by[KIND_COUNT] = {0};
	int budget = 600, best = -1;
	tally(d, by, 2, &budget);
	for (int k = 0; k < KIND_COUNT; k++) {
		if (by[k] && (best < 0 || by[k] > by[best])) {
			best = k;
		}
	}
	return best;
}

static int style_of(int kind) {
	switch (kind) {
	case 0:
	case 1: return STYLE_GALLERY;
	case 2: return STYLE_RECORDS;
	case 3:
	case 7: return STYLE_WAREHOUSE;
	case 4: return STYLE_LIBRARY;
	case 5: return STYLE_GLASS;
	case 6: return STYLE_DATA;
	default: return STYLE_OFFICE;
	}
}

/* The rectangle cut in two by weight, and again, till each has its own:
 * a treemap that keeps the pieces near square. */
static void split(WRect r, const double *wt, int n, WRect *out) {
	if (n <= 0) {
		return;
	}
	if (n == 1) {
		out[0] = r;
		return;
	}
	double total = 0, run = 0;
	for (int i = 0; i < n; i++) {
		total += wt[i];
	}
	int k = 1;
	for (; k < n - 1; k++) {
		if (run + wt[k - 1] >= total / 2) {
			break;
		}
		run += wt[k - 1];
	}
	run = 0;
	for (int i = 0; i < k; i++) {
		run += wt[i];
	}
	float share = total > 0 ? (float)(run / total) : 0.5f;
	if (wr_w(r) >= wr_d(r)) {
		float xm = r.x0 + wr_w(r) * share;
		split((WRect){r.x0, r.z0, xm, r.z1}, wt, k, out);
		split((WRect){xm, r.z0, r.x1, r.z1}, wt + k, n - k, out + k);
	} else {
		float zm = r.z0 + wr_d(r) * share;
		split((WRect){r.x0, r.z0, r.x1, zm}, wt, k, out);
		split((WRect){r.x0, zm, r.x1, r.z1}, wt + k, n - k, out + k);
	}
}

static bool roomy(WRect r) {
	float a = wr_w(r), b = wr_d(r);
	return fminf(a, b) >= MIN_ROOM_W && fmaxf(a, b) >= MIN_ROOM;
}

static bool overlap(WRect a, WRect b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.z0 < b.z1 && b.z0 < a.z1; }

static WRect grow(WRect r, float by) { return (WRect){r.x0 - by, r.z0 - by, r.x1 + by, r.z1 + by}; }

static WRoom *room_new(World *w) {
	GROW(w->room, w->nroom, w->room_cap, 256);
	WRoom *r = &w->room[w->nroom++];
	memset(r, 0, sizeof *r);
	r->parent = -1;
	r->up = -1;
	return r;
}

/* Stairs up into room c from its parent's room below: inside c, along its
 * longer side, their foot toward the middle of the parent's room and a
 * landing at the top, clear of what must stay clear there (the parent's
 * own stairwell, the way in from the street). */
static void stairs_for(WRoom *c, WRect parent, const WRect *avoid, int navoid) {
	WRect r = c->r;
	float pcx = (parent.x0 + parent.x1) / 2, pcz = (parent.z0 + parent.z1) / 2;
	float cx = (r.x0 + r.x1) / 2, cz = (r.z0 + r.z1) / 2;
	bool first_x = wr_w(r) >= wr_d(r);
	WRect fallback = {0};
	bool have = false;
	for (int pass = 0; pass < 2; pass++) {
		bool along_x = pass ? !first_x : first_x;
		float len = along_x ? wr_w(r) : wr_d(r), wid = along_x ? wr_d(r) : wr_w(r);
		if (len < 0.6f + STAIR_RUN + 1.0f || wid < STAIR_W + 0.6f) {
			continue;
		}
		/* the foot toward the parent's middle first, then the other end */
		bool hi_first = along_x ? pcx > cx : pcz > cz;
		for (int e = 0; e < 2; e++) {
			bool hi = e ? !hi_first : hi_first;
			/* across: in the middle, then to either side */
			float lo_w = (along_x ? r.z0 : r.x0) + 0.5f, hi_w = (along_x ? r.z1 : r.x1) - 0.5f - STAIR_W;
			float mid_w = ((along_x ? r.z0 + r.z1 : r.x0 + r.x1) - STAIR_W) / 2;
			float across[3] = {mid_w, lo_w, hi_w};
			for (int a = 0; a < 3; a++) {
				float s0 = across[a];
				WRect s;
				int up;
				if (along_x) {
					float x1 = hi ? r.x1 - 0.6f : r.x0 + 0.6f + STAIR_RUN;
					s = (WRect){x1 - STAIR_RUN, s0, x1, s0 + STAIR_W};
					up = hi ? 3 : 1;
				} else {
					float z1 = hi ? r.z1 - 0.6f : r.z0 + 0.6f + STAIR_RUN;
					s = (WRect){s0, z1 - STAIR_RUN, s0 + STAIR_W, z1};
					up = hi ? 0 : 2;
				}
				if (!have) {
					fallback = s;
					c->up = up;
					have = true;
				}
				bool clear = true;
				for (int i = 0; i < navoid && clear; i++) {
					clear = !overlap(grow(s, 0.5f), avoid[i]);
				}
				if (clear) {
					c->stair = s;
					c->up = up;
					return;
				}
			}
		}
	}
	c->stair = have ? fallback : (WRect){cx - 0.6f, cz - STAIR_RUN / 2, cx + 0.6f, cz + STAIR_RUN / 2};
	if (!have) {
		c->up = 0;
	}
}

/* The floor folder d's room wants: its own files' furniture with a
 * walkway round the walls, and at least what the rooms over it want
 * between them (they stand on its footprint), levels floors up. */
static float need_of(const World *w, const Node *d, int levels) {
	float own = w->want ? w->want(d) : 0;
	float a = own > 0 ? powf(sqrtf(own * 1.6f) + 6.0f, 2) : MIN_ROOM * MIN_ROOM_W * 1.3f;
	if (levels > 0) {
		float up = 0;
		for (int i = 0; i < d->nkids; i++) {
			if (d->kids[i]->is_dir) {
				up += need_of(w, d->kids[i], levels - 1);
			}
		}
		a = fmaxf(a, up * 1.1f);
	}
	return a;
}

/* The rooms over room p, on the floor above: its folders cut from its
 * rectangle by size, those too small together in a store room. */
static void rooms_over(World *w, int p, int fl, const WRect *avoid, int navoid) {
	WRoom par = w->room[p];
	int n;
	Node **kids = folders(par.node, &n);
	if (!n) {
		free(kids);
		return;
	}
	float area = wr_w(par.r) * wr_d(par.r);
	int m = (int)(area / (MIN_ROOM * MIN_ROOM_W * 1.3f));
	m = m > n ? n : m;
	double *wt = malloc((size_t)(n + 2) * sizeof *wt), *need = malloc((size_t)(n + 2) * sizeof *need);
	for (int i = 0; i < n && w->want; i++) {
		need[i] = need_of(w, kids[i], MAX_FLOORS - 1 - fl);
	}
	WRect *rect = malloc((size_t)(n + 2) * sizeof *rect);
	int items = 0;
	for (;; m--) {
		/* by the floor each wants, not its bytes: a folder of many small
		 * files gets the room to stand them in */
		double kept = 0, rest = 0;
		for (int i = 0; i < n; i++) {
			*(i < m ? &kept : &rest) += w->want ? need[i] : (double)kids[i]->size + 1;
		}
		double mean = m ? kept / m : 1;
		for (int i = 0; i < m; i++) {
			double s = w->want ? need[i] : (double)kids[i]->size + 1;
			wt[i] = s < mean * 0.3 ? mean * 0.3 : s;
		}
		items = m;
		if (m < n) {
			wt[items++] = rest > mean * 0.7 ? rest : mean * 0.7;
		}
		split(par.r, wt, items, rect);
		bool ok = true;
		for (int i = 0; i < items && ok; i++) {
			ok = roomy(rect[i]);
		}
		if (ok || m == 0) {
			break;
		}
	}
	for (int i = 0; i < items; i++) {
		WRoom *c = room_new(w);
		c->building = par.building;
		c->parent = p;
		c->floor = fl;
		c->r = rect[i];
		if (i < m) {
			c->node = kids[i];
		} else {
			c->store = true;
			c->node = kids[m];
			c->store0 = w->nstored;
			for (int j = m; j < n; j++) {
				GROW(w->stored, w->nstored, w->stored_cap, 64);
				w->stored[w->nstored++] = kids[j];
			}
			c->nstore = n - m;
		}
		c->seed = seed_of(c->node->name) * 2654435761u + (unsigned)fl;
		stairs_for(c, par.r, avoid, navoid);
	}
	free(wt);
	free(need);
	free(rect);
	free(kids);
}

/* A building's rooms, floor by floor, up to MAX_FLOORS. */
static void rooms_of(World *w, int bi) {
	WBuilding *b = &w->b[bi];
	b->room0 = w->nroom;
	WRoom *g = room_new(w);
	g->node = b->node;
	g->building = bi;
	g->r = b->r;
	g->seed = b->seed;
	/* clear on the ground floor: in front of the door */
	WRect door;
	float d = b->door;
	switch (b->front) {
	case 0: door = (WRect){d - 1.8f, b->r.z0, d + 1.8f, b->r.z0 + 3.0f}; break;
	case 1: door = (WRect){b->r.x1 - 3.0f, d - 1.8f, b->r.x1, d + 1.8f}; break;
	case 2: door = (WRect){d - 1.8f, b->r.z1 - 3.0f, d + 1.8f, b->r.z1}; break;
	default: door = (WRect){b->r.x0, d - 1.8f, b->r.x0 + 3.0f, d + 1.8f}; break;
	}
	int from = w->nroom - 1, to = w->nroom;
	b->floors = 1;
	for (int fl = 1; fl < MAX_FLOORS && from < to; fl++) {
		for (int p = from; p < to; p++) {
			if (w->room[p].store) {
				continue;
			}
			/* what stays clear in p: its own stairwell, or on the ground floor the door */
			WRect avoid = fl == 1 ? door : w->room[p].stair;
			rooms_over(w, p, fl, &avoid, 1);
		}
		from = to;
		to = w->nroom;
		if (to > from) {
			b->floors = fl + 1;
		}
	}
	b->nroom = w->nroom - b->room0;
}

/* ---- the city ---- */

typedef struct {
	int i, j;
	int ring;
	float angle;
} Cell;

static int by_ring(const void *pa, const void *pb) {
	const Cell *a = pa, *b = pb;
	if (a->ring != b->ring) {
		return a->ring - b->ring;
	}
	return a->angle < b->angle ? -1 : a->angle > b->angle;
}

/* the road between columns (or rows) i and i + 1: an avenue next to the square and every third out */
static bool avenue_at(int i) {
	int m = abs(i) < abs(i + 1) ? abs(i) : abs(i + 1);
	return m % 3 == 0;
}

static float road_at(int i) { return avenue_at(i) ? AVENUE_W : STREET_W; }

static void road_add(World *w, WRect r, bool avenue, bool along_x, int n) {
	GROW(w->road, w->nroad, w->road_cap, 32);
	w->road[w->nroad++] = (WRoad){r, avenue, along_x, n};
}

void world_build(World *w, Node *root) {
	w->root = root;
	w->nb = w->nroom = w->nstored = w->nroad = 0;
	w->more = 0;
	w->made_kids = root->nkids;
	w->made_size = root->size;
	/* the root's name is its whole path: the city goes by its last part */
	const char *base = strrchr(root->name, '/');
	snprintf(w->name, sizeof w->name, "%s", base && base[1] ? base + 1 : root->name);

	int nd;
	Node **dirs = folders(root, &nd);
	if (nd > MAX_BUILDINGS - w->nextra) {
		w->more = nd - (MAX_BUILDINGS - w->nextra);
		nd = MAX_BUILDINGS - w->nextra;
	}
	/* the extra trees last: the outer ring, where the city meets the rest */
	int nown = nd;
	dirs = realloc(dirs, (size_t)(nd + w->nextra + 1) * sizeof *dirs);
	for (int i = 0; i < w->nextra; i++) {
		dirs[nd++] = w->extra[i];
	}
	int nfiles = 0;
	for (int i = 0; i < root->nkids; i++) {
		nfiles += !root->kids[i]->is_dir && root->kids[i]->size;
	}

	/* the buildings' footprints: across as the square root of how big each
	 * is beside the biggest, from 10 m to 60, a little longer one way or
	 * the other by its name */
	double big = nd ? (double)dirs[0]->size + 1 : 1;
	for (int i = 0; i < nd; i++) {
		GROW(w->b, w->nb, w->b_cap, 64);
		WBuilding *b = &w->b[w->nb++];
		memset(b, 0, sizeof *b);
		b->node = dirs[i];
		snprintf(b->name, sizeof b->name, "%s", dirs[i]->name);
		b->size = dirs[i]->size;
		b->seed = seed_of(dirs[i]->name);
		b->kind = mostly(dirs[i]);
		b->style = style_of(b->kind);
		b->crew = i >= nown ? i - nown + 1 : 0;
		float side = roundf(clampf(10 + 50 * (float)sqrt(((double)dirs[i]->size + 1) / big), 10, 60));
		/* and at least the floor its rooms want, up to 90 m across */
		if (w->want) {
			side = fmaxf(side, roundf(clampf(sqrtf(need_of(w, dirs[i], MAX_FLOORS - 1)), 10, 90)));
		}
		float lean = 0.85f + 0.3f * (float)(b->seed % 1000) / 1000.0f;
		b->r = (WRect){0, 0, roundf(side * lean), roundf(side / lean)};
		b->r.x1 = fmaxf(b->r.x1, 10);
		b->r.z1 = fmaxf(b->r.z1, 10);
	}

	/* the cells of a grid, spiralling out from the square in the middle;
	 * the biggest buildings nearest it */
	int g = 0;
	while ((2 * g + 1) * (2 * g + 1) - 1 < w->nb) {
		g++;
	}
	g = g < 1 ? 1 : g;
	int side = 2 * g + 1, ncell = 0;
	Cell *cell = malloc((size_t)(side * side) * sizeof *cell);
	for (int j = -g; j <= g; j++) {
		for (int i = -g; i <= g; i++) {
			if (i || j) {
				int ring = abs(i) > abs(j) ? abs(i) : abs(j);
				cell[ncell++] = (Cell){i, j, ring, atan2f((float)i, (float)-j)};
			}
		}
	}
	qsort(cell, (size_t)ncell, sizeof *cell, by_ring);
	float sq = roundf(clampf(16 + sqrtf((float)nfiles) * 2.2f, 22, 50));

	/* each column as wide as its widest building and each row as deep as
	 * its deepest, with the pavement round; the square's in the middle */
	float *colw = calloc((size_t)side, sizeof *colw), *rowd = calloc((size_t)side, sizeof *rowd);
	colw[g] = sq + 2 * PAVE;
	rowd[g] = sq + 2 * PAVE;
	for (int k = 0; k < w->nb; k++) {
		WBuilding *b = &w->b[k];
		int i = cell[k].i, j = cell[k].j;
		b->front = abs(i) > abs(j) ? (i > 0 ? 3 : 1) : (j > 0 ? 0 : 2);
		colw[i + g] = fmaxf(colw[i + g], wr_w(b->r) + 2 * PAVE + 2);
		rowd[j + g] = fmaxf(rowd[j + g], wr_d(b->r) + 2 * PAVE + 2);
	}
	for (int i = 0; i < side; i++) {
		colw[i] = fmaxf(colw[i], 16 + 2 * PAVE);
		rowd[i] = fmaxf(rowd[i], 16 + 2 * PAVE);
	}
	float *x0 = malloc((size_t)side * sizeof *x0), *z0 = malloc((size_t)side * sizeof *z0);
	x0[g] = -colw[g] / 2;
	z0[g] = -rowd[g] / 2;
	for (int i = g + 1; i < side; i++) {
		x0[i] = x0[i - 1] + colw[i - 1] + road_at(i - 1 - g);
		z0[i] = z0[i - 1] + rowd[i - 1] + road_at(i - 1 - g);
	}
	for (int i = g - 1; i >= 0; i--) {
		x0[i] = x0[i + 1] - road_at(i - g) - colw[i];
		z0[i] = z0[i + 1] - road_at(i - g) - rowd[i];
	}
	float ex = fmaxf(-x0[0], x0[side - 1] + colw[side - 1]), ez = fmaxf(-z0[0], z0[side - 1] + rowd[side - 1]);
	w->edge = fmaxf(ex, ez) + RING_ROAD;
	w->square = (WRect){x0[g] + PAVE, z0[g] + PAVE, x0[g] + colw[g] - PAVE, z0[g] + rowd[g] - PAVE};

	/* the roads between: along z between the columns, along x between the rows, out to the wall */
	for (int i = 0; i + 1 < side; i++) {
		float a = x0[i] + colw[i], b = x0[i + 1];
		road_add(w, (WRect){a, -w->edge, b, w->edge}, avenue_at(i - g), false, i + 1);
		a = z0[i] + rowd[i];
		b = z0[i + 1];
		road_add(w, (WRect){-w->edge, a, w->edge, b}, avenue_at(i - g), true, i + 1);
	}
	/* and the ring road inside the wall */
	road_add(w, (WRect){-w->edge, -w->edge, w->edge, -ez}, true, true, 0);
	road_add(w, (WRect){-w->edge, ez, w->edge, w->edge}, true, true, side);
	road_add(w, (WRect){-w->edge, -ez, -ex, ez}, true, false, 0);
	road_add(w, (WRect){ex, -ez, w->edge, ez}, true, false, side);

	/* each building on its block, its front to the street toward the square, the door in the middle of it */
	for (int k = 0; k < w->nb; k++) {
		WBuilding *b = &w->b[k];
		int i = cell[k].i + g, j = cell[k].j + g;
		b->cell = (WRect){x0[i], z0[j], x0[i] + colw[i], z0[j] + rowd[j]};
		float bw = wr_w(b->r), bd = wr_d(b->r), cx = (b->cell.x0 + b->cell.x1) / 2, cz = (b->cell.z0 + b->cell.z1) / 2;
		float bx = cx - bw / 2, bz = cz - bd / 2;
		switch (b->front) {
		case 0: bz = b->cell.z0 + PAVE + 1; break;
		case 1: bx = b->cell.x1 - PAVE - 1 - bw; break;
		case 2: bz = b->cell.z1 - PAVE - 1 - bd; break;
		default: bx = b->cell.x0 + PAVE + 1; break;
		}
		b->r = (WRect){roundf(bx), roundf(bz), roundf(bx) + bw, roundf(bz) + bd};
		b->door = b->front == 0 || b->front == 2 ? (b->r.x0 + b->r.x1) / 2 : (b->r.z0 + b->r.z1) / 2;
		rooms_of(w, k);
	}
	free(cell);
	free(colw);
	free(rowd);
	free(x0);
	free(z0);
	free(dirs);
}

void world_free(World *w) {
	free(w->b);
	free(w->room);
	free(w->stored);
	free(w->road);
	memset(w, 0, sizeof *w);
}

bool world_stale(const World *w) {
	if (!w->root) {
		return false;
	}
	uint64_t a = w->root->size, b = w->made_size, d = a > b ? a - b : b - a;
	return w->root->nkids != w->made_kids || d > b / 24;
}

int world_room_of(const World *w, const Node *n) {
	if (!n || !w->root) {
		return -1;
	}
	/* its building: the folder over it that is the root's */
	const Node *top = n;
	while (top->parent && top->parent != w->root) {
		top = top->parent;
	}
	if (top->parent != w->root) {
		return -1;
	}
	int bi = -1;
	for (int i = 0; i < w->nb && bi < 0; i++) {
		bi = w->b[i].node == top ? i : -1;
	}
	if (bi < 0) {
		return -1;
	}
	const WBuilding *b = &w->b[bi];
	for (const Node *a = n; a && a != w->root; a = a->parent) {
		for (int i = b->room0; i < b->room0 + b->nroom; i++) {
			const WRoom *r = &w->room[i];
			if (!r->store && r->node == a) {
				return i;
			}
			for (int j = 0; r->store && j < r->nstore; j++) {
				if (w->stored[r->store0 + j] == a) {
					return i;
				}
			}
		}
	}
	return b->room0;
}

int world_building_at(const World *w, float x, float z, float in) {
	for (int i = 0; i < w->nb; i++) {
		WRect r = w->b[i].r;
		if (x > r.x0 + in && x < r.x1 - in && z > r.z0 + in && z < r.z1 - in) {
			return i;
		}
	}
	return -1;
}

void world_window_size(const World *w, int b, int fl, float *width, float *y0, float *y1) {
	switch (w->b[b].style) {
	case STYLE_GLASS: *width = 1.5f, *y0 = 0.3f, *y1 = 3.1f; break;
	case STYLE_WAREHOUSE: *width = 1.4f, *y0 = 2.1f, *y1 = 3.0f; break;
	case STYLE_LIBRARY: *width = 1.1f, *y0 = 0.7f, *y1 = 3.0f; break;
	case STYLE_RECORDS:
		if (fl == 0) {
			*width = 2.4f, *y0 = 0.4f, *y1 = 2.7f;
		} else {
			*width = 1.3f, *y0 = 1.0f, *y1 = 2.6f;
		}
		break;
	case STYLE_DATA: *width = 0.7f, *y0 = 1.2f, *y1 = 2.8f; break;
	case STYLE_GALLERY: *width = 2.2f, *y0 = 0.8f, *y1 = 2.9f; break;
	default: *width = 1.5f, *y0 = 1.0f, *y1 = 2.6f; break;
	}
}

static float window_pitch(const World *w, int b, int fl) {
	switch (w->b[b].style) {
	case STYLE_GLASS: return 1.7f;
	case STYLE_WAREHOUSE: return 4.0f;
	case STYLE_LIBRARY: return 2.6f;
	case STYLE_RECORDS: return fl == 0 ? 3.0f : 2.4f;
	case STYLE_DATA: return 4.5f;
	case STYLE_GALLERY: return 3.4f;
	default: return 2.6f;
	}
}

int world_windows(const World *w, int bi, int fl, int f, float a0, float a1, float *mid, int max) {
	const WBuilding *b = &w->b[bi];
	float width, y0, y1, pitch = window_pitch(w, bi, fl);
	world_window_size(w, bi, fl, &width, &y0, &y1);
	float start = f == 0 || f == 2 ? b->r.x0 : b->r.z0, end = f == 0 || f == 2 ? b->r.x1 : b->r.z1;
	int n = 0, count = (int)((end - start) / pitch);
	/* evenly along the whole side, so a side's windows line up floor over floor */
	float step = count > 0 ? (end - start) / (float)count : pitch;
	for (int m = 0; m < count && n < max; m++) {
		float c = start + step * ((float)m + 0.5f);
		if (c - width / 2 < a0 + 0.45f || c + width / 2 > a1 - 0.45f) {
			continue;
		}
		if (fl == 0 && f == b->front && fabsf(c - b->door) < FRONT_DOOR_W / 2 + width / 2 + 0.4f) {
			continue;
		}
		mid[n++] = c;
	}
	return n;
}

bool world_doorway(const World *w, int a, int b, int *side, float *mid) {
	const WRoom *A = &w->room[a], *B = &w->room[b];
	if (a == b || A->floor != B->floor || A->parent != B->parent || A->building != B->building) {
		return false;
	}
	const float e = 0.01f, need = ROOM_DOOR_W + 1.2f;
	float lo, hi;
	if (fabsf(A->r.x1 - B->r.x0) < e || fabsf(A->r.x0 - B->r.x1) < e) {
		lo = fmaxf(A->r.z0, B->r.z0);
		hi = fminf(A->r.z1, B->r.z1);
		*side = fabsf(A->r.x1 - B->r.x0) < e ? 1 : 3;
	} else if (fabsf(A->r.z1 - B->r.z0) < e || fabsf(A->r.z0 - B->r.z1) < e) {
		lo = fmaxf(A->r.x0, B->r.x0);
		hi = fminf(A->r.x1, B->r.x1);
		*side = fabsf(A->r.z1 - B->r.z0) < e ? 2 : 0;
	} else {
		return false;
	}
	if (hi - lo < need) {
		return false;
	}
	*mid = (lo + hi) / 2;
	return true;
}
