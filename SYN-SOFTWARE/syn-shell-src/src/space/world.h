/* ------------------------------------------------------------------------
 *   The world walk mode walks in: the scan's root as a city, made from
 *   the tree alone, so the same tree always gives the same city. The
 *   root's folders are its buildings on a grid of streets round a market
 *   square, where the root's own files are; each building as big across
 *   as its folder is big, a floor for each level of folders under it.
 *   The ground floor is the folder's own room; on each floor above, the
 *   folders a level deeper, each room over its parent's, stairs up to it
 *   from there. Only where things are: no drawing, no GL.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_WORLD_H
#define SYN_SHELL_SPACE_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "scan.h"

/* in metres; y up, the street at 0 */
#define FLOOR_H 3.6f     /* floor to floor */
#define SLAB 0.25f       /* a floor's thickness, under its top */
#define WALL_IN 0.1f     /* half an inside wall: each room has its own half */
#define WALL_OUT 0.3f    /* the outside walls */
#define MAX_FLOORS 12
#define MIN_ROOM 4.4f    /* a room's longer side at least: stairs and a landing */
#define MIN_ROOM_W 3.6f  /* ... and its shorter */
#define STEPS 9          /* stairs: so many steps up a floor */
#define STEP_GO 0.3f     /* each this deep */
#define STAIR_W 1.2f
#define STAIR_RUN (STEPS * STEP_GO)
#define ROOM_DOOR_W 1.4f /* a doorway between rooms */
#define ROOM_DOOR_H 2.6f
#define FRONT_DOOR_W 2.0f /* a building's way in from the street */
#define FRONT_DOOR_H 2.8f
#define PAVE 3.0f        /* the pavement round each block of the city */
#define KERB 0.15f

typedef struct {
	float x0, z0, x1, z1;
} WRect;

/* how a building looks, from what is mostly in it */
enum { STYLE_OFFICE, STYLE_GLASS, STYLE_WAREHOUSE, STYLE_LIBRARY, STYLE_RECORDS, STYLE_DATA, STYLE_GALLERY, STYLE_COUNT };

/* sides of a rectangle, and walls of a room: 0 -z, 1 +x, 2 +z, 3 -x */

typedef struct {
	Node *node;     /* the folder; a store room's: its first folder */
	int building;
	int parent;     /* the room on the floor below it stands over, -1 on the ground floor */
	int floor;
	WRect r;        /* to the middle of its walls (inside ones), or the outside of them */
	bool store;     /* folders too small for rooms of their own, together; no rooms over it */
	int store0, nstore; /* its folders, in World.stored */
	/* how it is reached from its parent's room: stairs there, inside this
	 * room's rectangle, rising from their bottom end toward up (a side) */
	WRect stair;
	int up;
	unsigned seed;
} WRoom;

typedef struct {
	Node *node;
	char name[64];
	uint64_t size;
	int kind;       /* kinds.c's, of what is mostly in it; -1 nothing */
	int style;
	unsigned seed;
	WRect r;        /* the footprint: the outside of its walls */
	WRect cell;     /* the block of the city it stands on, pavement round it */
	int floors;
	int front;      /* the side its door is on, toward the square */
	float door;     /* the door's middle along that side */
	int room0, nroom; /* its rooms in World.room, the ground floor first, then floor by floor */
	int crew;       /* a crew member's office (their home, World.extra), its index + 1; 0 not */
} WBuilding;

typedef struct {
	WRect r;
	bool avenue;  /* the wide ones */
	bool along_x; /* runs along x (else along z) */
	int n;        /* its number, for its name */
} WRoad;

typedef struct {
	Node *root;
	char name[128];
	WBuilding *b;
	int nb, b_cap;
	WRoom *room;
	int nroom, room_cap;
	Node **stored;
	int nstored, stored_cap;
	WRoad *road;
	int nroad, road_cap;
	WRect square; /* the market square in the middle: the root's own files */
	float edge;   /* the wall round the city, this far out each way */
	int more;     /* folders of the root too small for a building */
	/* what it was made from, to tell when the tree has grown enough to make it again */
	int made_kids;
	uint64_t made_size;
	/* the floor, in square metres, the furniture of folder d's own files
	 * wants (walk.c's arrangement); NULL: rooms by bytes alone */
	float (*want)(const Node *d);
	/* trees from outside the root that stand in the city too, at its
	 * edge (the crew's homes: their offices), each its own building */
	Node *extra[16];
	int nextra;
} World;

/* Makes the world of the tree under root (root's parent NULL); call
 * holding the tree. Again, over what it had. */
void world_build(World *w, Node *root);
void world_free(World *w);
/* Whether the tree has grown enough since to make it again. */
bool world_stale(const World *w);
/* The room n is in: its own, or the store room holding it or the folder
 * above it that is; -1 for none (the root, or not in the tree). */
int world_room_of(const World *w, const Node *n);
/* The building at (x, z) (inside its walls, by in), -1 none. */
int world_building_at(const World *w, float x, float z, float in);
/* The middles of the windows of building b along side f, floor fl, from
 * a0 to a1 along it (x on sides 0 and 2, z on 1 and 3): the same for the
 * inside of a room and the outside of the building. Fills at most max. */
int world_windows(const World *w, int b, int fl, int f, float a0, float a1, float *mid, int max);
/* The windows' width and their sill and head over the floor, for b's style. */
void world_window_size(const World *w, int b, int fl, float *width, float *y0, float *y1);
/* The doorway between rooms a and b (on the same floor, side by side, of
 * one parent): the side of a it is in and its middle along that side;
 * false for none. */
bool world_doorway(const World *w, int a, int b, int *side, float *mid);

static inline float wr_w(WRect r) { return r.x1 - r.x0; }
static inline float wr_d(WRect r) { return r.z1 - r.z0; }

#endif
