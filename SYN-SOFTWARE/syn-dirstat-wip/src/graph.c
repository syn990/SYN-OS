/* ------------------------------------------------------------------------
 *   The node graph: the focus folder in the middle, its biggest children
 *   on a ring round it and each of theirs fanned out behind it. A ball's
 *   area is its size on one scale across the picture, so a glance says
 *   which is biggest; going into a ball makes it the middle.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <math.h>

#include "scene.h"

#define PI 3.14159265f
#define RING_MAX 14   /* children shown singly; the rest are one "more" ball */
#define FAN_MAX 6     /* of each child's own */
#define GAP 0.10f     /* between balls, the focus's radius being 1 */
#define SMALLEST 0.07f
#define FAN_FROM 0.30f /* a child at least this big shows its own */
#define DROP 0.22f     /* how reach_out each level hangs below the last in 3D */

typedef struct {
	Scene *s;
	const Theme *t;
	uint64_t whole;
} Build;

static float radius(const Build *b, uint64_t size) {
	float r = sqrtf((float)size / (float)b->whole);
	return r < SMALLEST ? SMALLEST : r;
}

/* Layout is in units of the focus's radius, round the origin; fit() turns
 * it into the view's pixels and the solid picture afterwards. */
static int ball(const Build *b, Node *n, uint64_t size, int more, int depth, float x, float y, float r) {
	Item *it = scene_add(b->s);
	it->node = n;
	it->more = more;
	it->size = size;
	it->depth = depth;
	it->ball = true;
	it->x = x;
	it->y = y;
	it->w = r;
	if (!n) {
		it->col = rgb(b->t->border);
	} else if (n->is_dir) {
		/* the bigger its share of the picture, the nearer the accent */
		it->col = mix(rgb(b->t->panel_hover), rgb(b->t->accent), sqrtf((float)size / (float)b->whole));
	} else {
		it->col = kind_colour(kind_of(n->name), b->t);
	}
	return b->s->n - 1;
}

static int fan_count(const Build *b, const Node *n) {
	if (!n || !n->is_dir || radius(b, n->size) < FAN_FROM) {
		return 0;
	}
	int count = 0;
	while (count < n->nkids && count < FAN_MAX &&
	       sqrtf((float)n->kids[count]->size / (float)b->whole) >= SMALLEST * 0.6f) {
		count++;
	}
	return count;
}

/* how much room a ring child takes with its fan behind it */
static float reach(const Build *b, const Node *n, float r) {
	return fan_count(b, n) ? r + GAP + 2 * radius(b, n->kids[0]->size) : r;
}

static void fan(const Build *b, int parent, float angle) {
	/* by value: adding balls may move the list */
	Node *n = b->s->v[parent].node;
	float px = b->s->v[parent].x, py = b->s->v[parent].y, pr = b->s->v[parent].w;
	int count = fan_count(b, n);
	float wide[FAN_MAX], total = 0;
	for (int i = 0; i < count; i++) {
		float r = radius(b, n->kids[i]->size);
		wide[i] = 2 * atanf((r + GAP / 4) / (pr + GAP + r));
		total += wide[i];
	}
	float at = angle - total / 2;
	for (int i = 0; i < count; i++) {
		float r = radius(b, n->kids[i]->size), a = at + wide[i] / 2, d = pr + GAP + r;
		scene_join(b->s, parent, ball(b, n->kids[i], n->kids[i]->size, 0, 2, px + cosf(a) * d, py + sinf(a) * d, r));
		at += wide[i];
	}
}

static void fit(const Build *b, float w, float h) {
	float bound = 1;
	for (int i = 0; i < b->s->n; i++) {
		const Item *it = &b->s->v[i];
		float reach_out = sqrtf(it->x * it->x + it->y * it->y) + it->w;
		bound = reach_out > bound ? reach_out : bound;
	}
	float px = (w < h ? w : h) / 2 / (bound * 1.04f), unit = 0.5f / bound;
	for (int i = 0; i < b->s->n; i++) {
		Item *it = &b->s->v[i];
		it->pos[0] = it->x * unit;
		it->pos[1] = -(float)it->depth * DROP;
		it->pos[2] = it->y * unit;
		it->size3[0] = it->size3[1] = it->size3[2] = 2 * it->w * unit;
		it->x = w / 2 + it->x * px;
		it->y = h / 2 + it->y * px;
		it->w *= px;
	}
}

void graph_build(Scene *s, Node *focus, float w, float h, const Theme *t) {
	scene_clear(s);
	Build b = {s, t, focus->size ? focus->size : 1};
	ball(&b, focus, focus->size, 0, 0, 0, 0, 1);

	int shown = focus->nkids < RING_MAX ? focus->nkids : RING_MAX;
	uint64_t rest = 0;
	for (int i = shown; i < focus->nkids; i++) {
		rest += focus->kids[i]->size;
	}
	int count = shown + (focus->nkids > shown);
	if (!count) {
		fit(&b, w, h);
		return;
	}
	/* each child's radius and the room it takes on the ring */
	float r[RING_MAX + 1], room[RING_MAX + 1], round = 0, furthest = 0;
	for (int i = 0; i < count; i++) {
		Node *n = i < shown ? focus->kids[i] : NULL;
		r[i] = radius(&b, n ? n->size : rest);
		room[i] = 2 * reach(&b, n, r[i]) + GAP * 1.6f;
		round += room[i];
		furthest = room[i] > furthest ? room[i] : furthest;
	}
	/* wide enough to clear the focus, and long enough round for them all */
	float ring = 1 + GAP * 2 + furthest / 2;
	if (round / (2 * PI) > ring) {
		ring = round / (2 * PI);
	}
	float at = -PI / 2 - room[0] / round * PI; /* the biggest at the top */
	for (int i = 0; i < count; i++) {
		Node *n = i < shown ? focus->kids[i] : NULL;
		float a = at + room[i] / round * PI;
		int id = ball(&b, n, n ? n->size : rest, n ? 0 : focus->nkids - shown, 1, cosf(a) * ring, sinf(a) * ring, r[i]);
		scene_join(s, 0, id);
		fan(&b, id, a);
		at += room[i] / round * 2 * PI;
	}
	fit(&b, w, h);
}
