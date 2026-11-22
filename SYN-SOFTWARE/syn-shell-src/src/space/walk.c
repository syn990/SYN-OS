/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* nftw's FTW_ACTIONRETVAL */
#endif
#include "walk.h"

#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <ftw.h>
#include <sys/statvfs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "draw.h"
#include "scene.h"
#include "vmath.h"
#include "world.h"

/* a room, in metres: its floor at 0 (each floor of a building is made at
 * its own level, so what stands on it stands on 0), its ceiling at WALL_H */
#define WALL_H (FLOOR_H - SLAB)
#define WALKWAY 1.2f     /* the clear floor round a room's walls */
#define MAX_FILES 300    /* a room's biggest files get blocks; the rest are counted on the sign */
#define FLOOR_FILES 900  /* and a whole floor's */

/* you */
#define BODY_R 0.3f      /* half your width */
#define BODY_H 1.75f
#define EYE_H 1.6f
#define STEP_UP 0.45f    /* what you walk up onto without a jump */
#define GRAVITY 22.0f
#define JUMP_V 7.8f      /* about 1.4 m up */
#define WALK_V 4.5f
#define RUN_V 9.0f
#define ACCEL_GROUND 48.0f /* how fast you get up to speed, and stop, on your feet */
#define ACCEL_AIR 12.0f    /* ... and how much you can steer in the air */
#define FALL_MULT 1.7f     /* down faster than up: a jump with weight to it */
#define SHORT_HOP 2.6f     /* Space let go on the way up: the jump ends sooner */
#define COYOTE 0.12f       /* a jump still counts this long after walking off an edge */
#define JUMP_EARLY 0.15f   /* ... and Space this long before landing jumps on landing */
#define LOOK_RAD 0.0022f   /* turn per mouse count, the mouse held by the window */
#define FOV_WALK 1.15f

typedef struct {
	float lo[3], hi[3];
} Box;

typedef struct {
	char name[256];
	uint64_t size;
	int kind;
	const Node *node;
	Box b;
	int dev;            /* WALK_DEV_: a disk, a terminal ... in /dev; 0 a file */
	unsigned seed;      /* from its name: the same furniture each time */
	unsigned tex;       /* its picture (walk_picture), 0 none yet */
	bool tried;         /* asked for already */
	float aspect;       /* the picture's width over its height */
	float activity;     /* a disk: reading or writing just now, 1 .. 0 */
	unsigned long long io; /* a disk: sectors read and written, last looked */
	signed char exe;    /* a file of no known kind: 1 it runs (a computer), -1 not, 0 not looked yet */
	signed char front;  /* the side it faces: 0 -z, 1 +x, 2 +z, 3 -x, set where the room is arranged */
} Block;

/* On a room's walls: its windows and doorways, for the furniture to keep
 * clear of. wall: 0 the -z side, 1 +x, 2 +z, 3 -x; along: x on 0 and 2,
 * z on 1 and 3, in the floor's own frame. */
enum { DECO_WINDOW, DECO_FRAME, DECO_SCREEN, DECO_DOOR };
typedef struct {
	int type, wall;
	float along, width, y0, y1;
	int block; /* a frame's picture, a screen's code: the block it shows */
} Deco;
#define MAX_DECO 256
#define MAX_RUGS 32 /* under the lounges' music */

/* Outside: the root's folders as a city (world.c lays it out). */
#define CITY_FENCE 2.4f  /* the wall round the city: higher than a jump */
#define CITY_NEAR 32.0f  /* the city's boxes this near you are what you can run into (the camera's reach too) */
#define LOD_NEAR 140.0f  /* buildings nearer than this have their windows one by one; further, a band a floor */
#define LOD_MOVE 30.0f   /* ... and the city's boxes are made again when you have gone this far */
static const SDL_Color LAMP_LIGHT = {255, 214, 160, 255}; /* street lamps, lit doors */
/* the city's boxes, by how they are drawn */
enum { CB_GROUND, CB_BODY, CB_LIT, CB_LAMP, CB_BALL, CB_COUNT };
/* the floor you are on, by how it is drawn */
enum { IB_WALL, IB_LIT, IB_LAMP, IB_COUNT };

typedef struct {
	float d;
	int i;
} Near;

/* what the room is drawn from each frame, by how it is drawn */
typedef struct {
	Solid *v;
	int n, cap;
} Bag;
enum {
	BAG_WALL, BAG_BODY, BAG_LIT, BAG_BALL, BAG_LAMP, BAG_BALL_LAMP, BAG_GLOW, BAG_BALL_GLOW,
	BAG_FIG_DARK, BAG_FIG_LAMP, BAG_FIG_GLOW, BAG_FIG_BOX, BAG_CREW_BOX, BAG_CREW_BALL, BAG_CREW_LAMP,
	BAG_CAR_BOX, BAG_CAR_BALL, BAG_CAR_LAMP, BAG_CAR_GLASS, BAG_CAR_DOOR, BAG_CAR_DGLASS, BAG_CAR_FL, BAG_CAR_FR, BAG_COUNT
};
typedef struct {
	unsigned tex;
	bool lamp;
	Quad3 q;
} Pic;

/* a piece of the floor you are on, for drawing: where, and what it is */
enum { IR_WALL, IR_OUTWALL, IR_FLOOR, IR_CEIL, IR_STEP, IR_RAIL, IR_FRAME, IR_DOOR, IR_FRONT, IR_LAMP, IR_BOARD, IR_PLAQUE, IR_DARK };
typedef struct {
	Box b;
	int role;
} Piece;

/* a name where something is, drawn over it when near: in a room (room
 * >= 0, shown from that room) or out in the street */
typedef struct {
	Vec3 p;
	char text[96];
	int room;
	int look; /* 0 plain, 1 a way on (stairs, doorways), 2 the way out, 3 a street's name */
} Sign;

/* a plaque on a store room's board: E on it takes the lift to its folder */
typedef struct {
	Box b;
	Node *to;
	char text[96];
} Plaque;

struct Walk {
	bool on;
	Node *room; /* the folder of the room you are in; out in the street, the root */
	char path[1024], name[256];
	int more_dirs, more_files; /* in the room but too small for a room or a block */

	World world;
	int bld;     /* the building you are in, -1 out in the street */
	int floor;   /* its floor you are on; all here is in that floor's frame, its floor at 0 */
	float base;  /* that floor's height over the street */
	int at;      /* the room you are in, in world.room; -1 none */
	int *froom;  /* the rooms on this floor */
	int nfroom, froom_cap;
	Node *pick;  /* in a store room: the folder the lift is at */
	Plaque *plaque;
	int nplaque, plaque_cap;
	int plaque_at; /* the plaque you face, -1 none */

	Block *block;
	int nblock, block_cap;
	float *bmid; /* each block's room's middle (x, z): its front faces that way */
	int bmid_cap;
	Box *solid; /* what you cannot walk through: walls, panes, floors, stairs, the blocks */
	int nsolid, solid_cap;
	int nwall; /* solid[0 .. nwall) are walls and lintels */
	int nwin;  /* solid[nwall .. nwin) are the window panes: seen through, not walked through */
	int nstruct; /* solid[nwin .. nstruct) floors, stairs and rails; then the blocks */
	WRect *ups;  /* the stairs up from this floor: the holes in its ceiling */
	int nups, ups_cap;

	/* you: where your feet are, how fast you rise, which way you look and face */
	float px, py, pz, vy;
	float hvx, hvz; /* how fast you go along the floor */
	bool ground;
	float coyote, jump_early; /* seconds left: off an edge, Space pressed before landing */
	float yaw, pitch, body_yaw, stride, moving;
	int step_n; /* steps heard, by the stride */
	/* the crew's figures this frame: those following you, resting at home, their workers at the desk */
#define FIG_MAX 24
	Bag fbox[FIG_MAX], fball[FIG_MAX], flamp[FIG_MAX];
	struct {
		int crew, kind; /* FIG_FOLLOW, FIG_REST, FIG_WORK */
		float x, y, z, yaw;
	} fig[FIG_MAX];
	int nfig;
	int crew_frame; /* the building and floor they last caught you up in */
	/* the car: parked at the square, or you in it */
	bool car_ok;
	int car_state;  /* CAR_OUT, CAR_GETTING_IN, CAR_SHUTTING, CAR_DRIVING, CAR_GETTING_OUT, CAR_LEFT */
	float car_x, car_z, car_yaw, car_v, car_steer, car_door, car_t, car_throttle, car_cam_x, car_cam_y, car_cam_z;
	bool car_chase; /* the view from behind it, not your seat */
	bool car_brake;
	bool first; /* through your own eyes */
	float dist; /* the camera behind you */
	bool look;  /* the mouse turns you */
	bool captured; /* the window holds the mouse, as a game does: a click takes it, Esc gives it back */
	Vec3 follow;   /* where the camera follows: you, smoothed, so a step up or a landing is not a jolt */
	bool follow_ok;
	float cam_len; /* the camera's distance behind you as last drawn (walls pull it in at once, it eases back) */
	float cam_dt;  /* the last step's seconds, for that easing */
	float fade; /* seconds of fading in left, after a lift */
	bool latch; /* the keys that carried you do nothing till let go */
	float wish_x, wish_z; /* the way the keys ask you to go */

	float vx, vy0, vw, vh; /* the view as last drawn, for clicks */
	struct WalkUse *use;   /* what you can use with E, what is playing: see "using things" */

	/* a tree still being scanned: the world made again when it has grown enough */
	float since_built;

	Solid *buf;
	int buf_cap;
	LinePoint *lines;
	int lines_cap;

	Deco deco[MAX_DECO];
	int ndeco;
	Box rug[MAX_RUGS];
	unsigned rug_seed[MAX_RUGS];
	int nrug;
	Bag bag[BAG_COUNT];
	Pic *pic;
	int npic, pic_cap;
	unsigned *dead; /* textures of blocks gone, freed when the GL context is next current */
	int ndead, dead_cap;
	float time;     /* seconds walking, for what moves by itself */
	float io_wait;  /* till the disks' activity is read again */

	/* the floor you are on: its pieces when built, made into boxes in the
	 * theme's colours when next drawn, not every frame */
	Piece *piece;
	int npiece, piece_cap;
	Sign *sign;
	int nsign, sign_cap;
	Bag ibag[IB_COUNT];
	Quad3 *iglass;
	int niglass, iglass_cap;
	bool floor_dirty;
	Theme floor_theme;

	/* The city: made into boxes from the world, once, and again when you
	 * have gone far (the buildings near you in more detail), never the
	 * tree. Each building's boxes together, so the one you are in is left
	 * out: cb_from[b * CB_COUNT + bag] is where b's start in that bag. */
	Bag cbag[CB_COUNT];
	int *cb_from;
	int cb_from_cap;
	Quad3 *glass;
	int nglass, glass_cap;
	Sign *fsign; /* names out in the distance (the rest of the disk), read from any way off */
	int nfsign, fsign_cap;
	bool beyond_had; /* the city was made with the disk measured */
	Sign *csign; /* the buildings' and the streets' names */
	int ncsign, csign_cap;
	bool city_dirty;
	Theme city_theme;
	SDL_Color city_back;
	float lod_x, lod_z;
	Box *csolid; /* what can be run into out there: pavements, walls, the wall round it */
	int ncsolid, csolid_cap;
	Box *cprop;  /* lamp posts and tree trunks, made with the city's boxes (near you) */
	int ncprop, cprop_cap;
	Box *cnear;  /* those near you, gathered when you have gone a few metres */
	int ncnear, cnear_cap;
	float near_x, near_z;
	bool near_ok;
	Near *ctag;  /* the city's names by how near: kept, not made each frame */
	int ctag_cap;
};

bool (*walk_picture)(const char *path, int kind, int dev, unsigned char **rgba, int *w, int *h);

static void tex_drop(Walk *w, unsigned tex) {
	if (!tex) {
		return;
	}
	if (w->ndead == w->dead_cap) {
		w->dead_cap = w->dead_cap ? w->dead_cap * 2 : 64;
		w->dead = SDL_realloc(w->dead, (size_t)w->dead_cap * sizeof *w->dead);
	}
	w->dead[w->ndead++] = tex;
}

static unsigned name_seed(const char *s) {
	unsigned h = 2166136261u;
	for (; *s; s++) {
		h = (h ^ (unsigned char)*s) * 16777619u;
	}
	return h;
}

/* What a size-0 thing in /dev is; a disk's size from /sys. */
static int dev_kind(const char *dir, const char *name, uint64_t *size) {
	char path[1400];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	struct stat st;
	*size = 0;
	if (lstat(path, &st) != 0) {
		return WALK_DEV_NONE;
	}
	if (S_ISBLK(st.st_mode)) {
		char sys[600];
		snprintf(sys, sizeof sys, "/sys/class/block/%s/size", name);
		FILE *f = fopen(sys, "r");
		unsigned long long sectors = 0;
		if (f) {
			if (fscanf(f, "%llu", &sectors) != 1) {
				sectors = 0;
			}
			fclose(f);
		}
		*size = sectors * 512ull;
		return WALK_DEV_DISK;
	}
	if (!S_ISCHR(st.st_mode)) {
		return WALK_DEV_NONE;
	}
	const char *d = strrchr(dir, '/');
	d = d ? d + 1 : dir;
#define IS(p) (!strncmp(name, p, strlen(p)))
	if (IS("tty") || IS("console") || IS("vcs") || IS("ptmx") || !strcmp(d, "pts")) {
		return WALK_DEV_TTY;
	}
	if (!strcmp(d, "snd") || IS("dsp") || IS("audio") || IS("pcm") || IS("midi") || IS("seq")) {
		return WALK_DEV_SOUND;
	}
	if (!strcmp(d, "dri") || IS("card") || IS("renderD") || IS("fb") || IS("nvidia") || IS("kfd")) {
		return WALK_DEV_GPU;
	}
	if (!strcmp(d, "input") || IS("event") || IS("mouse") || IS("mice") || IS("js") || IS("uinput") || IS("hidraw")) {
		return WALK_DEV_INPUT;
	}
	if (!strcmp(name, "null") || !strcmp(name, "zero") || !strcmp(name, "full") || !strcmp(name, "random") ||
	    !strcmp(name, "urandom")) {
		return WALK_DEV_VOID;
	}
#undef IS
	return WALK_DEV_CHAR;
}

static const char *dev_name(int dev) {
	static const char *N[] = {"file", "disk", "terminal", "sound", "graphics", "input", "the void", "device"};
	return dev >= 0 && dev <= WALK_DEV_CHAR ? N[dev] : "device";
}

/* A device has no size of its own to stand by: one for how big it looks. */
static uint64_t dev_looks(int dev, uint64_t real) {
	switch (dev) {
	case WALK_DEV_DISK: return real > 4096 ? real : 4096;
	case WALK_DEV_TTY: return 3u << 20;
	case WALK_DEV_SOUND: return 6u << 20;
	case WALK_DEV_GPU: return 256u << 10;
	case WALK_DEV_INPUT: return 48u << 10;
	case WALK_DEV_VOID: return 128u << 10;
	default: return 160u << 10;
	}
}

const Uint8 *walk_keys;

Walk *walk_new(void) { return SDL_calloc(1, sizeof(Walk)); }
bool walk_on(const Walk *w) { return w && w->on; }
Node *walk_room(const Walk *w) { return w->room; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void solid_add(Walk *w, Box b) {
	if (w->nsolid == w->solid_cap) {
		w->solid_cap = w->solid_cap ? w->solid_cap * 2 : 256;
		w->solid = SDL_realloc(w->solid, (size_t)w->solid_cap * sizeof *w->solid);
	}
	w->solid[w->nsolid++] = b;
}

/* What can be run into from where you are: the floor's solids (out in the
 * street, the square's things) and, out there, the city's near you. */
static int solid_n(const Walk *w) { return w->nsolid + (w->bld < 0 ? w->ncnear : 0); }
static const Box *solid_i(const Walk *w, int i) { return i < w->nsolid ? &w->solid[i] : &w->cnear[i - w->nsolid]; }

static Box box(float x0, float y0, float z0, float x1, float y1, float z1) {
	return (Box){{x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, z0 < z1 ? z0 : z1},
	             {x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0, z0 < z1 ? z1 : z0}};
}

/* a rectangle of the world from y0 up to y1 */
static Box rbox(WRect r, float y0, float y1) { return box(r.x0, y0, r.z0, r.x1, y1, r.z1); }
static WRect inset(WRect r, float by) { return (WRect){r.x0 + by, r.z0 + by, r.x1 - by, r.z1 - by}; }
static bool in_rect(WRect r, float x, float z) { return x > r.x0 && x < r.x1 && z > r.z0 && z < r.z1; }

typedef struct {
	Node *n;
	uint64_t looks, size; /* as big as it is drawn; as it is */
	int dev;
} Thing;

static int by_looks(const void *pa, const void *pb) {
	uint64_t a = ((const Thing *)pa)->looks, b = ((const Thing *)pb)->looks;
	return a < b ? 1 : a > b ? -1 : 0;
}

static bool overlaps_you(const Walk *w, const Box *b, float x, float y, float z) {
	(void)w;
	return x + BODY_R > b->lo[0] && x - BODY_R < b->hi[0] && z + BODY_R > b->lo[2] && z - BODY_R < b->hi[2] &&
	       y + BODY_H > b->lo[1] && y + 0.01f < b->hi[1];
}

/* how high the floor is under you: the top of whatever you stand over
 * that is not above your feet (out in the street, the road at least) */
static float floor_under(const Walk *w, float x, float y, float z) {
	float top = w->bld < 0 ? -KERB : -2 * FLOOR_H;
	for (int i = 0, n = solid_n(w); i < n; i++) {
		const Box *b = solid_i(w, i);
		if (x + BODY_R * 0.8f > b->lo[0] && x - BODY_R * 0.8f < b->hi[0] && z + BODY_R * 0.8f > b->lo[2] &&
		    z - BODY_R * 0.8f < b->hi[2] && b->hi[1] <= y + 0.05f && b->hi[1] > top) {
			top = b->hi[1];
		}
	}
	return top;
}

/* ---- the floor you are on ---- */

static void piece_add(Walk *w, Box b, int role) {
	if (w->npiece == w->piece_cap) {
		w->piece_cap = w->piece_cap ? w->piece_cap * 2 : 512;
		w->piece = SDL_realloc(w->piece, (size_t)w->piece_cap * sizeof *w->piece);
	}
	w->piece[w->npiece++] = (Piece){b, role};
}

static void sign_add(Walk *w, Vec3 p, int room, int look, const char *text) {
	if (w->nsign == w->sign_cap) {
		w->sign_cap = w->sign_cap ? w->sign_cap * 2 : 64;
		w->sign = SDL_realloc(w->sign, (size_t)w->sign_cap * sizeof *w->sign);
	}
	Sign *s = &w->sign[w->nsign++];
	s->p = p;
	s->room = room;
	s->look = look;
	snprintf(s->text, sizeof s->text, "%s", text);
}

static Block *block_new(Walk *w) {
	if (w->nblock == w->block_cap) {
		w->block_cap = w->block_cap ? w->block_cap * 2 : 128;
		w->block = SDL_realloc(w->block, (size_t)w->block_cap * sizeof *w->block);
		w->bmid = SDL_realloc(w->bmid, (size_t)w->block_cap * 2 * sizeof *w->bmid);
	}
	Block *b = &w->block[w->nblock++];
	*b = (Block){0};
	return b;
}

/* What is in folder d to stand in its room, biggest first, at most most;
 * in /dev the devices too, which have no size. Malloc'd. */
static int things_of(Walk *w, Node *d, Thing **out, int most) {
	char path[1024];
	node_path(d, path, sizeof path);
	bool in_dev = !strncmp(path, "/dev", 4) && (path[4] == '\0' || path[4] == '/');
	Thing *v = SDL_malloc((size_t)(d->nkids + 1) * sizeof *v);
	int n = 0;
	for (int i = 0; i < d->nkids; i++) {
		Node *k = d->kids[i];
		uint64_t real;
		int dev;
		if (k->is_dir) {
			continue;
		} else if (k->size) {
			v[n++] = (Thing){k, k->size, k->size, WALK_DEV_NONE};
		} else if (in_dev && (dev = dev_kind(path, k->name, &real)) != WALK_DEV_NONE) {
			v[n++] = (Thing){k, dev_looks(dev, real), real, dev};
		} else {
			w->more_files++;
		}
	}
	qsort(v, (size_t)n, sizeof *v, by_looks);
	if (n > most) {
		w->more_files += n - most;
		n = most;
	}
	*out = v;
	return n;
}

/* ---- arranging a room: where each file stands, and which way it faces ---- */

#define EDGE 0.9f      /* groups out in the room stand at least this far off the walls */
#define AISLE 1.7f     /* ... and this far from what stands against them, and rows from rows */
#define WALL_OFF 0.08f /* a piece against a wall stands this far off it */

static float rnd(unsigned *s);
static bool runs(Block *k);

/* the groups a room's files fall into, by kind, each arranged its own way */
enum { ZN_DESKS, ZN_CODE, ZN_PCS, ZN_SERVERS, ZN_MUSIC, ZN_PICS, ZN_BOOKS, ZN_STORE, ZN_VIDEO, ZN_DEVICES, ZN_COUNT };

/* How each group stands out in the room: in rows back to back (desks
 * facing each other, stacks, racks) or face to face across an aisle (a
 * gallery), each row at most len long and most pieces, gap apart along it,
 * so many pairs of rows to a group, the rows running north and south
 * (turn); or round a rug (lounge). */
typedef struct {
	bool lounge, face, turn;
	float len, gap;
	int most, pairs;
} Way;
static const Way GROUP[ZN_COUNT] = {
	[ZN_DESKS] = {false, false, false, 7, 0.35f, 3, 2},
	[ZN_CODE] = {false, false, false, 7, 0.35f, 3, 2},
	[ZN_PCS] = {false, false, false, 7, 0.3f, 3, 2},
	[ZN_SERVERS] = {false, false, true, 6.5f, 0.02f, 99, 3},
	[ZN_MUSIC] = {true, false, false, 4.5f, 0.35f, 3, 1},
	[ZN_PICS] = {false, true, true, 8, 0.7f, 6, 2},
	[ZN_BOOKS] = {false, false, true, 6.5f, 0.02f, 99, 3},
	[ZN_STORE] = {false, false, false, 5.5f, 0.12f, 99, 2},
	[ZN_VIDEO] = {false, false, false, 6, 0.6f, 4, 1},
	[ZN_DEVICES] = {false, false, false, 5, 0.4f, 6, 2},
};
#define LOUNGES 3 /* lounges in a room at most; more music stands in rows, as in a shop */
static const Way MUSIC_ROWS = {false, false, false, 6, 0.3f, 4, 2};

typedef struct {
	int zone;
	float w, d, h; /* across its front, deep, tall */
	bool wall;     /* it belongs against a wall */
	bool done;
	Box b;         /* where it stands: in its group's own frame, then in the room */
	signed char f; /* the way it faces */
} Spot;

/* A file's piece: as wide and deep as what furnish() makes of it, its
 * group, and whether it goes against a wall. Wider as the file is bigger,
 * as tall as it was always (k->b's top). */
static void piece_of(Block *k, Spot *p) {
	float H = k->b.hi[1], t = clampf((H - 0.3f) / 2.5f, 0, 1), w = 0.8f, d = 0.6f;
	int style = k->dev ? 100 + k->dev : runs(k) ? 6 : k->kind, zone = ZN_DEVICES;
	bool wall = false;
	unsigned s = k->seed; /* drawn as make_picture, make_audio and make_archive draw it first */
	switch (style) {
	case 0: /* an easel, a table of paints beside most; or a picture on a stand */
		w = fmaxf(H * 0.8f, 0.3f) + 0.1f + (rnd(&s) < 0.6f ? 0.75f : 0);
		d = clampf(0.35f + H * 0.35f, 0.6f, 1.3f);
		zone = ZN_PICS;
		break;
	case 1: /* a set; a screen on a cabinet; a cinema screen and its projector */
		if (H < 0.9f) {
			w = clampf(H * 1.4f + 0.1f, 0.6f, 1.3f);
			d = clampf(H + 0.08f, 0.45f, 1.0f);
		} else if (H < 1.7f) {
			w = clampf(1.2f + (H - 0.9f) * 1.2f, 1.2f, 2.2f);
			d = 0.6f;
		} else {
			w = clampf(1.2f + H * 0.7f, 2.6f, 3.4f);
			d = 2.0f;
		}
		zone = ZN_VIDEO;
		wall = true;
		break;
	case 2:
	case 100 + WALK_DEV_SOUND: /* a boombox or a turntable; a hi-fi; a jukebox */
		if (H < 0.9f) {
			bool boom = rnd(&s) < 0.5f;
			w = clampf(H * 2.2f + 0.1f, 0.8f, 2.0f);
			d = boom ? clampf(H * 0.7f + 0.06f, 0.35f, 0.7f) : 0.6f;
		} else if (H < 1.75f) {
			w = clampf(1.4f + (H - 0.9f) * 1.4f, 1.4f, 2.6f);
			d = 0.6f;
		} else {
			w = clampf(H * 0.55f, 0.6f, 1.1f) + 2 * fminf(H * 0.3f, 0.6f) + 0.3f;
			d = 0.85f;
		}
		zone = ZN_MUSIC;
		break;
	case 3: { /* a crate; filing cabinets; crates on a pallet */
		float r = rnd(&s);
		if (H < 0.75f || (r < 0.35f && H < 1.4f)) {
			w = d = clampf(0.5f + H * 0.4f, 0.55f, 1.0f);
		} else if (r < 0.7f) {
			w = 0.5f * (float)(1 + (int)(t * 3.5f));
			d = 0.74f;
		} else {
			w = clampf(1.0f + t * 1.2f, 1.1f, 2.0f);
			d = 1.2f;
		}
		zone = ZN_STORE;
		wall = true;
		break;
	}
	case 4: /* a desk; tall, a bookcase */
		if (H < 1.15f) {
			w = 1.0f + (H - 0.3f) * 0.9f;
			d = 0.75f;
			zone = ZN_DESKS;
		} else {
			w = clampf(0.9f + (H - 1.15f) * 0.9f, 0.9f, 2.4f);
			d = 0.42f;
			zone = ZN_BOOKS;
			wall = true;
		}
		break;
	case 5:
	case 100 + WALK_DEV_TTY: /* a terminal; a desk of screens with its chair */
		if (k->dev || H < 0.55f) {
			w = clampf(H * 1.25f + 0.1f, 0.6f, 1.4f);
			d = clampf(H * 1.1f / 0.62f, 0.55f, 1.3f);
		} else {
			w = clampf(1.1f + t * 1.4f, 1.1f, 2.2f);
			d = H > 0.8f ? 1.3f : 0.8f;
		}
		zone = ZN_CODE;
		break;
	case 6: /* towers; a desk with a computer; racks */
		if (H < 0.8f) {
			w = (float)(H > 0.6f ? 2 : 1) * (fmaxf(H * 0.5f, 0.22f) + 0.12f) + 0.04f;
			d = fmaxf(0.4f, H * 1.1f + 0.04f);
			wall = true;
			zone = ZN_PCS;
		} else if (H < 1.5f) {
			w = clampf(1.2f + (H - 0.8f) * 0.7f, 1.2f, 1.7f);
			d = 0.9f;
			zone = ZN_PCS;
		} else {
			int n = 1 + (int)((H - 1.5f) / 0.45f);
			w = 0.7f * (float)(n > 3 ? 3 : n);
			d = 1.05f;
			zone = ZN_SERVERS;
		}
		break;
	case 7:
	case 100 + WALK_DEV_DISK: /* a drive with its lid off; an enclosure of bays */
		if (H < 0.75f) {
			w = d = clampf(0.3f + H * 0.5f, 0.4f, 0.7f);
		} else {
			w = clampf(0.5f + t * 0.8f, 0.5f, 1.3f);
			d = 0.7f;
		}
		zone = ZN_SERVERS;
		break;
	case 8: /* boxes */
		w = clampf(0.6f + t * 1.2f, 0.6f, 1.6f);
		d = 0.6f;
		zone = ZN_STORE;
		wall = true;
		break;
	default: /* the rest of /dev */
		w = clampf(0.5f + t * 0.6f, 0.5f, 1.0f);
		d = w * 0.8f;
		break;
	}
	*p = (Spot){zone, w, d, H, wall, false, {{0, 0, 0}, {0, 0, 0}}, 0};
}

/* two floors overlap, or come nearer than m */
static bool meets(const Box *a, const Box *b, float m) {
	return a->lo[0] < b->hi[0] + m && b->lo[0] < a->hi[0] + m && a->lo[2] < b->hi[2] + m && b->lo[2] < a->hi[2] + m;
}

typedef struct {
	Box room;
	const Box *clear; /* floor kept free: doorways, the way in */
	int nclear;
	const Deco *deco;
	int ndeco;
	Box *put; /* the pieces placed so far */
	int nput;
	Box *grp; /* the groups out in the room, whole */
	int ngrp;
	float cur; /* how far round the walls the last piece stood: from the north-west corner, east, south, west, north */
} Plan;

/* Free floor for b: in the room, off what is kept clear and what is placed,
 * and where it is against a wall not so tall it hides a window or a
 * picture on it. */
static bool fits(const Plan *pl, const Box *b, float h) {
	const Box *R = &pl->room;
	if (b->lo[0] < R->lo[0] - 0.001f || b->hi[0] > R->hi[0] + 0.001f || b->lo[2] < R->lo[2] - 0.001f || b->hi[2] > R->hi[2] + 0.001f) {
		return false;
	}
	for (int i = 0; i < pl->nclear; i++) {
		if (meets(b, &pl->clear[i], 0)) {
			return false;
		}
	}
	for (int i = 0; i < pl->nput; i++) {
		if (meets(b, &pl->put[i], 0)) {
			return false;
		}
	}
	for (int i = 0; i < pl->ngrp; i++) {
		if (meets(b, &pl->grp[i], AISLE)) {
			return false;
		}
	}
	float off[4] = {b->lo[2] - R->lo[2], R->hi[0] - b->hi[0], R->hi[2] - b->hi[2], b->lo[0] - R->lo[0]};
	for (int i = 0; i < pl->ndeco; i++) {
		const Deco *d = &pl->deco[i];
		bool ns = d->wall == 0 || d->wall == 2;
		float a0 = ns ? b->lo[0] : b->lo[2], a1 = ns ? b->hi[0] : b->hi[2];
		if (off[d->wall] < 0.5f && a0 < d->along + d->width / 2 + 0.12f && a1 > d->along - d->width / 2 - 0.12f && h > d->y0 - 0.05f) {
			return false;
		}
	}
	return true;
}

/* where a piece w wide and d deep stands with its back to wall s, a along
 * it the way round the walls go, out off it */
static Box against(const Box *R, int s, float a, float w, float d, float out, signed char *f) {
	float x0 = R->lo[0], x1 = R->hi[0], z0 = R->lo[2], z1 = R->hi[2];
	switch (s) {
	case 0: *f = 2; return box(x0 + a, 0, z0 + out, x0 + a + w, 0, z0 + out + d);
	case 1: *f = 3; return box(x1 - out - d, 0, z0 + a, x1 - out, 0, z0 + a + w);
	case 2: *f = 0; return box(x1 - a - w, 0, z1 - out - d, x1 - a, 0, z1 - out);
	default: *f = 1; return box(x0 + out, 0, z1 - a - w, x0 + out + d, 0, z1 - a);
	}
}

static void place(Plan *pl, Spot *p, Box b, signed char f) {
	p->b = b;
	p->f = f;
	p->done = true;
	pl->put[pl->nput++] = b;
}

/* The piece against the walls at the first free floor round from the last,
 * at most reach further on; gap after it. */
static bool wall_put(Plan *pl, Spot *p, float gap, float reach) {
	const Box *R = &pl->room;
	float L[4] = {R->hi[0] - R->lo[0], R->hi[2] - R->lo[2], R->hi[0] - R->lo[0], R->hi[2] - R->lo[2]};
	float start[5] = {0, L[0], L[0] + L[1], L[0] + L[1] + L[2], L[0] + L[1] + L[2] + L[3]};
	float c = pl->cur, stop = c + reach, all = start[4];
	while (c < stop && c < all) {
		int s = 0;
		while (s < 3 && c >= start[s + 1]) {
			s++;
		}
		float a = c - start[s];
		if (a + p->w > L[s]) {
			if (s == 3) {
				break; /* no wall left */
			}
			c = start[s + 1]; /* round the corner: always on, never stuck by rounding */
			continue;
		}
		signed char f;
		Box b = against(R, s, a, p->w, p->d, WALL_OFF, &f);
		if (fits(pl, &b, p->h)) {
			place(pl, p, b, f);
			pl->cur = c + p->w + gap;
			return true;
		}
		c += 0.2f;
	}
	return false;
}

/* a group out in the room: its pieces (in order[first ..]) in its own frame */
typedef struct {
	int first, n;
	float w, d; /* its floor: w east and west, d north and south */
	bool rug;
	Box mat;    /* the rug, in its frame */
	float x, z; /* its north-west corner in the room */
	bool placed;
} Group;

/* the group turned a quarter: its rows running north and south */
static void group_turn(Spot *pc, const int *order, Group *g) {
	for (int i = 0; i < g->n; i++) {
		Spot *p = &pc[order[g->first + i]];
		Box b = p->b;
		p->b = box(b.lo[2], 0, b.lo[0], b.hi[2], 0, b.hi[0]);
		p->f = (signed char)(3 - p->f);
	}
	float t = g->w;
	g->w = g->d;
	g->d = t;
	Box m = g->mat;
	g->mat = box(m.lo[2], 0, m.lo[0], m.hi[2], 0, m.hi[0]);
}

/* One group from the head of list, as rows: pairs back to back (their
 * fronts out), or face to face across an aisle. How many it took. */
static int group_rows(Spot *pc, const int *list, int n, const Way *way, Group *g, int *order, int *no) {
	bool face = way->face;
	float gap = way->gap, z = 0, wide = 0, after = face ? 0.6f : AISLE;
	int used = 0, rows[8][2], nrow = 0; /* each row: its first in order[], how many */
	float rlen[8];
	g->first = *no;
	g->n = 0;
	g->rug = false;
	for (int pair = 0; pair < way->pairs && used < n; pair++) {
		int row[2][64], nr[2] = {0, 0};
		float len[2] = {0, 0}, dep[2] = {0, 0};
		while (used < n) {
			const Spot *p = &pc[list[used]];
			bool ok[2];
			for (int r = 0; r < 2; r++) {
				ok[r] = nr[r] < 64 && nr[r] < way->most && (nr[r] == 0 || len[r] + gap + p->w <= way->len);
			}
			if (!ok[0] && !ok[1]) {
				break;
			}
			int r = ok[0] && (!ok[1] || len[0] <= len[1]) ? 0 : 1;
			len[r] += (nr[r] ? gap : 0) + p->w;
			dep[r] = fmaxf(dep[r], p->d);
			row[r][nr[r]++] = list[used++];
		}
		float inner = nr[1] ? (face ? 2.2f : 0.04f) : 0;
		for (int r = 0; r < 2; r++) {
			if (!nr[r]) {
				continue;
			}
			rows[nrow][0] = *no;
			rows[nrow][1] = nr[r];
			rlen[nrow++] = len[r];
			wide = fmaxf(wide, len[r]);
			float a = 0;
			for (int i = 0; i < nr[r]; i++) {
				Spot *p = &pc[row[r][i]];
				float z0, z1;
				if (!face) { /* the backs meet along the middle */
					z0 = r == 0 ? z + dep[0] - p->d : z + dep[0] + inner;
					p->f = r == 0 ? 0 : 2;
				} else { /* the fronts face across the aisle */
					z0 = r == 0 ? z : z + dep[0] + inner + dep[1] - p->d;
					p->f = r == 0 ? 2 : 0;
				}
				z1 = z0 + p->d;
				p->b = box(a, 0, z0, a + p->w, 0, z1);
				a += p->w + gap;
				order[(*no)++] = row[r][i];
				g->n++;
			}
		}
		z += dep[0] + inner + dep[1] + after;
	}
	/* each row in the middle of the longest */
	for (int r = 0; r < nrow; r++) {
		float sh = (wide - rlen[r]) / 2;
		for (int i = 0; i < rows[r][1]; i++) {
			Box *b = &pc[order[rows[r][0] + i]].b;
			b->lo[0] += sh;
			b->hi[0] += sh;
		}
	}
	g->w = wide;
	g->d = z - after;
	if (way->turn) {
		group_turn(pc, order, g);
	}
	return used;
}

/* One group from the head of list round a rug, facing it: at the far
 * side, the left, the right in turn, then the near side; three a side. */
static int group_lounge(Spot *pc, const int *list, int n, Group *g, int *order, int *no) {
	static const int turn[12] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 3, 3, 3}; /* the near side, open, till the rest are full */
	int side[4][3], ns[4] = {0, 0, 0, 0}, used = 0; /* north, west, east, south */
	float len[4] = {0, 0, 0, 0}, dep[4] = {0, 0, 0, 0}, gap = GROUP[ZN_MUSIC].gap;
	for (; used < n && used < 12; used++) {
		int s = turn[used];
		const Spot *p = &pc[list[used]];
		len[s] += (ns[s] ? gap : 0) + p->w;
		dep[s] = fmaxf(dep[s], p->d);
		side[s][ns[s]++] = list[used];
	}
	float rx = fmaxf(fmaxf(len[0], len[3]), 2.0f) + 1.6f, rz = fmaxf(fmaxf(len[1], len[2]), 2.0f) + 1.6f;
	float ox = dep[1] + 0.15f, oz = dep[0] + 0.15f;
	g->first = *no;
	g->n = 0;
	for (int s = 0; s < 4; s++) {
		bool ns_side = s == 0 || s == 3;
		float a = ns_side ? ox + (rx - len[s]) / 2 : oz + (rz - len[s]) / 2;
		for (int i = 0; i < ns[s]; i++) {
			Spot *p = &pc[side[s][i]];
			switch (s) {
			case 0: p->b = box(a, 0, oz - 0.15f - p->d, a + p->w, 0, oz - 0.15f); p->f = 2; break;
			case 1: p->b = box(ox - 0.15f - p->d, 0, a, ox - 0.15f, 0, a + p->w); p->f = 1; break;
			case 2: p->b = box(ox + rx + 0.15f, 0, a, ox + rx + 0.15f + p->d, 0, a + p->w); p->f = 3; break;
			default: p->b = box(a, 0, oz + rz + 0.15f, a + p->w, 0, oz + rz + 0.15f + p->d); p->f = 0; break;
			}
			a += p->w + gap;
			order[(*no)++] = side[s][i];
			g->n++;
		}
	}
	g->w = ox + rx + 0.15f + dep[2];
	g->d = oz + rz + 0.15f + dep[3];
	g->rug = true;
	g->mat = box(ox + 0.2f, 0, oz + 0.2f, ox + rx - 0.2f, 0, oz + rz - 0.2f);
	return used;
}

/* a group's floor at x, z free: in the room off the walls, off what is
 * kept clear, an aisle from what stands at the walls, gap from the other groups */
static bool group_fits(const Plan *pl, const Group *g, const Group *all, int ng, float x, float z, float gap) {
	const Box *R = &pl->room;
	Box b = box(x, 0, z, x + g->w, 0, z + g->d);
	if (x < R->lo[0] + EDGE || z < R->lo[2] + EDGE || x + g->w > R->hi[0] - EDGE || z + g->d > R->hi[2] - EDGE) {
		return false;
	}
	for (int i = 0; i < pl->nclear; i++) {
		if (meets(&b, &pl->clear[i], 0.3f)) {
			return false;
		}
	}
	for (int i = 0; i < pl->nput; i++) {
		if (meets(&b, &pl->put[i], AISLE)) {
			return false;
		}
	}
	for (int i = 0; i < ng; i++) {
		const Group *o = &all[i];
		if (o != g && o->placed) {
			Box ob = box(o->x, 0, o->z, o->x + o->w, 0, o->z + o->d);
			if (meets(&b, &ob, gap)) {
				return false;
			}
		}
	}
	return true;
}

/* The groups on the floor in turn, each at the first place from the
 * north-west it fits, gap from the rest; how many found a place. */
static int pack(const Plan *pl, Group *g, int ng, float gap) {
	const Box *R = &pl->room;
	float step = clampf(fmaxf(R->hi[0] - R->lo[0], R->hi[2] - R->lo[2]) / 64, 0.25f, 1.0f);
	int got = 0;
	for (int i = 0; i < ng; i++) {
		g[i].placed = false;
	}
	for (int i = 0; i < ng; i++) {
		for (float z = R->lo[2] + EDGE; z + g[i].d <= R->hi[2] - EDGE && !g[i].placed; z += step) {
			for (float x = R->lo[0] + EDGE; x + g[i].w <= R->hi[0] - EDGE; x += step) {
				if (group_fits(pl, &g[i], g, ng, x, z, gap)) {
					g[i].x = x;
					g[i].z = z;
					g[i].placed = true;
					got++;
					break;
				}
			}
		}
	}
	return got;
}

/* About the floor a folder's own files want, in square metres, for the
 * world to size its room to them: their pieces with the aisles between
 * (a lounge's rug too), the biggest MAX_FILES as the room stands them. */
static float folder_want(const Node *d) {
	Thing *v = SDL_malloc((size_t)(d->nkids + 1) * sizeof *v);
	int n = 0;
	for (int i = 0; i < d->nkids; i++) {
		const Node *k = d->kids[i];
		if (!k->is_dir && k->size) {
			v[n++] = (Thing){(Node *)k, k->size, k->size, WALK_DEV_NONE};
		}
	}
	if (n > MAX_FILES) {
		qsort(v, (size_t)n, sizeof *v, by_looks);
		n = MAX_FILES;
	}
	float want = 0;
	int music = 0;
	for (int i = 0; i < n; i++) {
		Block k = {0};
		Spot p;
		snprintf(k.name, sizeof k.name, "%s", v[i].n->name);
		k.kind = kind_of(k.name);
		k.node = v[i].n;
		k.seed = name_seed(k.name);
		float t = clampf((float)(log10((double)v[i].looks / 4096.0) / 6.0), 0, 1);
		k.b.hi[1] = 0.3f + 2.5f * t;
		piece_of(&k, &p);
		want += (p.w + 0.35f) * (p.d + 1.0f) * (p.zone == ZN_MUSIC && music++ < LOUNGES * 12 ? 2.4f : 1);
	}
	SDL_free(v);
	return want;
}

/* Furnishes the rectangle room (its floor inside the walls) with files:
 * some pieces against the walls (the library, storage, screens, computers,
 * easels in the windows' light), the rest out in the room in groups of
 * their kind (desks facing each other, racks and stacks in rows, music
 * round a rug, a gallery), open floor between. keep_clear stays free;
 * walls_deco says where the windows and pictures are, which nothing tall
 * stands in front of. Each placed file is a block, facing its way, and
 * a solid; the rest are counted in w->more_files, and how many returned. */
static int furnish_room(Walk *w, Box room, const Thing *files, int nf, const Box *keep_clear, int nclear, const Deco *walls_deco, int ndeco) {
	if (nf <= 0) {
		return 0;
	}
	Block *tmp = SDL_calloc((size_t)nf, sizeof *tmp);
	Spot *pc = SDL_calloc((size_t)nf, sizeof *pc);
	Group *g = SDL_calloc((size_t)nf, sizeof *g);
	int *list = SDL_malloc((size_t)nf * sizeof *list), *rest = SDL_malloc((size_t)nf * sizeof *rest), *order = SDL_malloc((size_t)nf * sizeof *order);
	Plan pl = {room, keep_clear, nclear, walls_deco, ndeco, SDL_malloc((size_t)nf * sizeof(Box)), 0, SDL_malloc((size_t)nf * sizeof(Box)), 0, 0};
	for (int i = 0; i < nf; i++) {
		Block *k = &tmp[i];
		snprintf(k->name, sizeof k->name, "%s", files[i].n->name);
		k->size = files[i].size;
		k->kind = kind_of(files[i].n->name);
		k->node = files[i].n;
		k->dev = files[i].dev;
		k->seed = name_seed(k->name);
		/* one scale in every room: 4 KiB and under knee high, 4 GiB and over 2.8 m */
		float t = clampf((float)(log10((double)files[i].looks / 4096.0) / 6.0), 0, 1);
		k->b = box(0, 0, 0, 0, 0.3f + 2.5f * t, 0);
		piece_of(k, &pc[i]);
	}
	/* each group's files by name, so the same file stands in the same place */
	int from[ZN_COUNT + 1] = {0}, n = 0;
	for (int z = 0; z < ZN_COUNT; z++) {
		from[z] = n;
		for (int i = 0; i < nf; i++) {
			if (pc[i].zone != z) {
				continue;
			}
			int j = n++;
			for (; j > from[z] && strcmp(tmp[list[j - 1]].name, tmp[i].name) > 0; j--) {
				list[j] = list[j - 1];
			}
			list[j] = i;
		}
	}
	from[ZN_COUNT] = n;

	/* pictures on their easels in each window's light, backs to it */
	for (int i = 0, at = from[ZN_PICS]; i < ndeco && at < from[ZN_PICS + 1]; i++) {
		const Deco *d = &walls_deco[i];
		if (d->type != DECO_WINDOW) {
			continue;
		}
		int take = 0;
		float span = 0;
		while (at + take < from[ZN_PICS + 1] && span + pc[list[at + take]].w + (take ? 0.3f : 0) <= d->width + 0.6f) {
			span += pc[list[at + take]].w + (take ? 0.3f : 0);
			take++;
		}
		float a = d->along - span / 2, out = 0.55f;
		for (int j = 0; j < take; j++, at++) {
			Spot *p = &pc[list[at]];
			signed char f;
			Box b;
			switch (d->wall) {
			case 0: b = box(a, 0, room.lo[2] + out, a + p->w, 0, room.lo[2] + out + p->d); f = 2; break;
			case 1: b = box(room.hi[0] - out - p->d, 0, a, room.hi[0] - out, 0, a + p->w); f = 3; break;
			case 2: b = box(a, 0, room.hi[2] - out - p->d, a + p->w, 0, room.hi[2] - out); f = 0; break;
			default: b = box(room.lo[0] + out, 0, a, room.lo[0] + out + p->d, 0, a + p->w); f = 1; break;
			}
			if (fits(&pl, &b, 0)) {
				place(&pl, p, b, f);
			}
			a += p->w + 0.3f;
		}
	}
	/* round the walls: the library, the screens, storage, the computers */
	static const int walls[] = {ZN_BOOKS, ZN_VIDEO, ZN_STORE, ZN_PCS};
	for (int i = 0; i < (int)(sizeof walls / sizeof *walls); i++) {
		int z = walls[i];
		bool any = false;
		for (int j = from[z]; j < from[z + 1]; j++) {
			Spot *p = &pc[list[j]];
			if (!p->done && p->wall) {
				any |= wall_put(&pl, p, GROUP[z].gap, 14);
			}
		}
		if (any) {
			pl.cur += 1.2f; /* a gap between one kind and the next */
		}
	}
	/* the rest in groups of their kind out in the room */
	int ng = 0, no = 0;
	for (int z = 0; z < ZN_COUNT; z++) {
		int m = 0;
		for (int j = from[z]; j < from[z + 1]; j++) {
			if (!pc[list[j]].done) {
				rest[m++] = list[j];
			}
		}
		for (int at = 0, lounges = 0; at < m;) {
			Group *gr = &g[ng++];
			if (GROUP[z].lounge && lounges++ < LOUNGES) {
				at += group_lounge(pc, rest + at, m - at, gr, order, &no);
			} else {
				at += group_rows(pc, rest + at, m - at, GROUP[z].lounge ? &MUSIC_ROWS : &GROUP[z], gr, order, &no);
			}
		}
	}
	/* wide apart where there is the floor for it, closer where not */
	static const float gaps[] = {3.6f, 2.8f, 2.2f, AISLE};
	float gap = AISLE;
	for (int i = 0; i < 4; i++) {
		if (pack(&pl, g, ng, gaps[i]) == ng || i == 3) {
			gap = gaps[i];
			break;
		}
	}
	/* then all of them moved toward the middle as far as they go */
	float bx0 = 1e9f, bz0 = 1e9f, bx1 = -1e9f, bz1 = -1e9f;
	for (int i = 0; i < ng; i++) {
		if (g[i].placed) {
			bx0 = fminf(bx0, g[i].x);
			bz0 = fminf(bz0, g[i].z);
			bx1 = fmaxf(bx1, g[i].x + g[i].w);
			bz1 = fmaxf(bz1, g[i].z + g[i].d);
		}
	}
	if (bx1 > bx0) {
		float dx = ((room.lo[0] + room.hi[0]) - (bx0 + bx1)) / 2, dz = ((room.lo[2] + room.hi[2]) - (bz0 + bz1)) / 2;
		for (float k = 1; k > 0.1f; k /= 2) {
			bool ok = true;
			for (int i = 0; i < ng && ok; i++) {
				g[i].x += dx * k;
				g[i].z += dz * k;
			}
			for (int i = 0; i < ng && ok; i++) {
				ok = !g[i].placed || group_fits(&pl, &g[i], g, ng, g[i].x, g[i].z, gap);
			}
			if (ok) {
				break;
			}
			for (int i = 0; i < ng; i++) {
				g[i].x -= dx * k;
				g[i].z -= dz * k;
			}
		}
	}
	(void)gap;
	for (int i = 0; i < ng; i++) {
		const Group *gr = &g[i];
		if (!gr->placed) {
			continue;
		}
		for (int j = 0; j < gr->n; j++) {
			Spot *p = &pc[order[gr->first + j]];
			Box b = p->b;
			p->b = box(b.lo[0] + gr->x, 0, b.lo[2] + gr->z, b.hi[0] + gr->x, 0, b.hi[2] + gr->z);
			p->done = true;
		}
		pl.grp[pl.ngrp++] = box(gr->x, 0, gr->z, gr->x + gr->w, 0, gr->z + gr->d);
		if (gr->rug && w->nrug < MAX_RUGS) {
			Box m = gr->mat;
			w->rug_seed[w->nrug] = tmp[order[gr->first]].seed;
			w->rug[w->nrug++] = box(m.lo[0] + gr->x, 0, m.lo[2] + gr->z, m.hi[0] + gr->x, 0.012f, m.hi[2] + gr->z);
		}
	}
	/* what found no place out in the room: against a wall if there is still one free */
	pl.cur = 0;
	for (int j = 0; j < n; j++) {
		Spot *p = &pc[list[j]];
		if (!p->done && !wall_put(&pl, p, 0.15f, 10)) {
			pl.cur += 10;
		}
	}
	/* the blocks, biggest first, as the files came */
	int left = 0;
	for (int i = 0; i < nf; i++) {
		const Spot *p = &pc[i];
		if (!p->done) {
			w->more_files++;
			left++;
			continue;
		}
		Block *k = block_new(w);
		*k = tmp[i];
		k->b = box(p->b.lo[0], 0, p->b.lo[2], p->b.hi[0], p->h, p->b.hi[2]);
		k->front = p->f;
		solid_add(w, k->b);
	}
	SDL_free(pl.put);
	SDL_free(pl.grp);
	SDL_free(order);
	SDL_free(rest);
	SDL_free(list);
	SDL_free(g);
	SDL_free(pc);
	SDL_free(tmp);
	return left;
}

/* About the floor files want, in square metres, for sizing a room to them. */

/* whether side s of room r is on its building's outside */
static bool outer_side(const World *wd, const WRoom *r, int s) {
	WRect b = wd->b[r->building].r, q = r->r;
	const float e = 0.01f;
	switch (s) {
	case 0: return fabsf(q.z0 - b.z0) < e;
	case 1: return fabsf(q.x1 - b.x1) < e;
	case 2: return fabsf(q.z1 - b.z1) < e;
	default: return fabsf(q.x0 - b.x0) < e;
	}
}

static float side_t(const World *wd, const WRoom *r, int s) { return outer_side(wd, r, s) ? WALL_OUT : WALL_IN; }

/* the inside of room r's walls */
static WRect room_inside(const World *wd, const WRoom *r) {
	return (WRect){r->r.x0 + side_t(wd, r, 3), r->r.z0 + side_t(wd, r, 0), r->r.x1 - side_t(wd, r, 1), r->r.z1 - side_t(wd, r, 2)};
}

/* along side s of rectangle q, from depth d0 to d1 in from it, a0 to a1 along, y0 to y1 up */
static Box side_box(WRect q, int s, float d0, float d1, float a0, float a1, float y0, float y1) {
	switch (s) {
	case 0: return box(a0, y0, q.z0 + d0, a1, y1, q.z0 + d1);
	case 1: return box(q.x1 - d1, y0, a0, q.x1 - d0, y1, a1);
	case 2: return box(a0, y0, q.z1 - d1, a1, y1, q.z1 - d0);
	default: return box(q.x0 + d0, y0, a0, q.x0 + d1, y1, a1);
	}
}

/* a point d in from side s of q, a along it, y up */
static Vec3 side_at(WRect q, int s, float d, float a, float y) {
	switch (s) {
	case 0: return v3(a, y, q.z0 + d);
	case 1: return v3(q.x1 - d, y, a);
	case 2: return v3(a, y, q.z1 - d);
	default: return v3(q.x0 + d, y, a);
	}
}

typedef struct {
	float a0, a1, y0, y1;
	int kind; /* 0 a doorway, 1 a window, 2 the way in from the street */
	int to;   /* a doorway's room on the other side */
} Hole;

static int by_hole(const void *pa, const void *pb) {
	float a = ((const Hole *)pa)->a0, b = ((const Hole *)pb)->a0;
	return a < b ? -1 : a > b;
}

/* the name a room goes by on signs */
static void room_label(const Walk *w, int ri, char *out, size_t n) {
	const WRoom *r = &w->world.room[ri];
	if (r->store) {
		snprintf(out, n, "STORE: %d small folders", r->nstore);
	} else {
		snprintf(out, n, "%.80s", r->node->name);
	}
}

/* Room ri's walls, cut for its doorways, its windows (a sill under, a
 * lintel over, and a pane between that keeps you in) and on the ground
 * floor the way in; into the solids (the panes into panes, to go after
 * the walls) and the pieces, the doorways' names into the signs. */
static void room_walls(Walk *w, int ri, Box **panes, int *npane, int *pane_cap) {
	const World *wd = &w->world;
	const WRoom *r = &wd->room[ri];
	const WBuilding *b = &wd->b[r->building];
	char label[96], text[128];
	for (int s = 0; s < 4; s++) {
		bool out = outer_side(wd, r, s);
		float t = out ? WALL_OUT : WALL_IN;
		float a0 = s == 0 || s == 2 ? r->r.x0 : r->r.z0, a1 = s == 0 || s == 2 ? r->r.x1 : r->r.z1;
		Hole hole[96];
		int nh = 0;
		for (int k = 0; k < w->nfroom && nh < 40; k++) {
			int side;
			float mid;
			if (world_doorway(wd, ri, w->froom[k], &side, &mid) && side == s) {
				hole[nh++] = (Hole){mid - ROOM_DOOR_W / 2, mid + ROOM_DOOR_W / 2, 0, ROOM_DOOR_H, 0, w->froom[k]};
			}
		}
		if (out) {
			if (r->floor == 0 && s == b->front) {
				hole[nh++] = (Hole){b->door - FRONT_DOOR_W / 2, b->door + FRONT_DOOR_W / 2, 0, FRONT_DOOR_H, 2, -1};
			}
			float mid[48], ww, y0, y1;
			world_window_size(wd, r->building, r->floor, &ww, &y0, &y1);
			int n = world_windows(wd, r->building, r->floor, s, a0, a1, mid, 48);
			for (int i = 0; i < n && nh < 96; i++) {
				hole[nh++] = (Hole){mid[i] - ww / 2, mid[i] + ww / 2, y0, fminf(y1, WALL_H - 0.15f), 1, -1};
			}
		}
		qsort(hole, (size_t)nh, sizeof *hole, by_hole);
		int role = out ? IR_OUTWALL : IR_WALL;
		float from = a0;
		for (int i = 0; i < nh; i++) {
			const Hole *h = &hole[i];
			if (h->a0 > from) {
				Box p = side_box(r->r, s, 0, t, from, h->a0, 0, WALL_H);
				solid_add(w, p);
				piece_add(w, p, role);
			}
			if (h->y0 > 0) {
				Box p = side_box(r->r, s, 0, t, h->a0, h->a1, 0, h->y0);
				solid_add(w, p);
				piece_add(w, p, role);
			}
			Box p = side_box(r->r, s, 0, t, h->a0, h->a1, h->y1, WALL_H);
			solid_add(w, p);
			piece_add(w, p, role);
			from = h->a1;
			/* its frame on this side, and what it is to the furniture */
			int fr = h->kind == 1 ? IR_FRAME : h->kind == 2 ? IR_FRONT : IR_DOOR;
			float f = h->kind == 1 ? 0.08f : 0.07f;
			piece_add(w, side_box(r->r, s, t - 0.02f, t + 0.04f, h->a0 - f, h->a0, h->y0, h->y1), fr);
			piece_add(w, side_box(r->r, s, t - 0.02f, t + 0.04f, h->a1, h->a1 + f, h->y0, h->y1), fr);
			piece_add(w, side_box(r->r, s, t - 0.02f, t + 0.04f, h->a0 - f, h->a1 + f, h->y1, h->y1 + f), fr);
			if (h->kind == 1) {
				piece_add(w, side_box(r->r, s, t - 0.02f, t + 0.16f, h->a0 - f, h->a1 + f, h->y0 - f, h->y0), fr);
				if (*npane == *pane_cap) {
					*pane_cap = *pane_cap ? *pane_cap * 2 : 128;
					*panes = SDL_realloc(*panes, (size_t)*pane_cap * sizeof **panes);
				}
				(*panes)[(*npane)++] = side_box(r->r, s, t / 2 - 0.02f, t / 2 + 0.02f, h->a0, h->a1, h->y0, h->y1);
			}
			if (w->ndeco < MAX_DECO) {
				w->deco[w->ndeco++] = (Deco){h->kind == 1 ? DECO_WINDOW : DECO_DOOR, s, (h->a0 + h->a1) / 2, h->a1 - h->a0, h->y0, h->y1, -1};
			}
			if (h->kind == 0) {
				room_label(w, h->to, label, sizeof label);
				sign_add(w, side_at(r->r, s, t + 0.1f, (h->a0 + h->a1) / 2, h->y1 + 0.35f), ri, 1, label);
			} else if (h->kind == 2) {
				snprintf(text, sizeof text, "OUT: %.80s", wd->name);
				sign_add(w, side_at(r->r, s, t + 0.1f, (h->a0 + h->a1) / 2, h->y1 + 0.4f), ri, 2, text);
				/* a light over the way out, inside */
				piece_add(w, side_box(r->r, s, t, t + 0.06f, h->a0 + 0.2f, h->a1 - 0.2f, h->y1 + 0.2f, h->y1 + 0.45f), IR_FRONT);
			}
		}
		if (from < a1) {
			Box p = side_box(r->r, s, 0, t, from, a1, 0, WALL_H);
			solid_add(w, p);
			piece_add(w, p, role);
		}
		/* a dark skirting along its foot */
		piece_add(w, side_box(r->r, s, t, t + 0.03f, a0, a1, 0, 0.12f), IR_DARK);
	}
}

/* Rectangle q from y0 to y1 with holes cut in it, as boxes: cut on a grid
 * by the holes' edges, a row's cells joined where none is a hole. */
static void slab(Walk *w, WRect q, const WRect *hole, int nh, float y0, float y1, int role, bool solid) {
	float xs[66], zs[66];
	int nx = 0, nz = 0;
	xs[nx++] = q.x0;
	xs[nx++] = q.x1;
	zs[nz++] = q.z0;
	zs[nz++] = q.z1;
	for (int i = 0; i < nh && nx < 64; i++) {
		xs[nx++] = clampf(hole[i].x0, q.x0, q.x1);
		xs[nx++] = clampf(hole[i].x1, q.x0, q.x1);
		zs[nz++] = clampf(hole[i].z0, q.z0, q.z1);
		zs[nz++] = clampf(hole[i].z1, q.z0, q.z1);
	}
	for (int i = 1; i < nx; i++) {
		for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) {
			float s = xs[j];
			xs[j] = xs[j - 1];
			xs[j - 1] = s;
		}
	}
	for (int i = 1; i < nz; i++) {
		for (int j = i; j > 0 && zs[j] < zs[j - 1]; j--) {
			float s = zs[j];
			zs[j] = zs[j - 1];
			zs[j - 1] = s;
		}
	}
	for (int j = 0; j + 1 < nz; j++) {
		if (zs[j + 1] - zs[j] < 0.01f) {
			continue;
		}
		float zm = (zs[j] + zs[j + 1]) / 2, run = 0;
		bool open = false;
		for (int i = 0; i + 1 < nx; i++) {
			float xm = (xs[i] + xs[i + 1]) / 2;
			bool cut = false;
			for (int k = 0; k < nh && !cut; k++) {
				cut = in_rect(hole[k], xm, zm);
			}
			if (!cut && !open) {
				run = xs[i];
				open = true;
			}
			if ((cut || i + 2 == nx) && open) {
				Box b = box(run, y0, zs[j], cut ? xs[i] : xs[i + 1], y1, zs[j + 1]);
				if (b.hi[0] - b.lo[0] > 0.01f) {
					if (solid) {
						solid_add(w, b);
					}
					piece_add(w, b, role);
				}
				open = false;
			}
		}
	}
}

/* The steps of stairs s, rising toward side up, from y0 a floor up. */
static void steps(Walk *w, WRect s, int up, float y0, bool solid) {
	float rise = FLOOR_H / STEPS;
	for (int i = 0; i < STEPS; i++) {
		WRect st = s;
		float a = (float)i * STEP_GO, b = (float)(i + 1) * STEP_GO;
		switch (up) {
		case 0: st.z1 = s.z1 - a, st.z0 = s.z1 - b; break;
		case 1: st.x0 = s.x0 + a, st.x1 = s.x0 + b; break;
		case 2: st.z0 = s.z0 + a, st.z1 = s.z0 + b; break;
		default: st.x1 = s.x1 - a, st.x0 = s.x1 - b; break;
		}
		Box bx = rbox(st, y0, y0 + rise * (float)(i + 1));
		if (solid) {
			solid_add(w, bx);
		}
		piece_add(w, bx, IR_STEP);
	}
}

/* rails round a stairwell's top: along its sides and its foot, open at the top of the steps */
static void rails(Walk *w, WRect s, int up) {
	float t = 0.06f, h = 1.0f;
	Box r[3];
	if (up == 1 || up == 3) {
		r[0] = box(s.x0, 0, s.z0 - t, s.x1, h, s.z0);
		r[1] = box(s.x0, 0, s.z1, s.x1, h, s.z1 + t);
		r[2] = up == 1 ? box(s.x0 - t, 0, s.z0 - t, s.x0, h, s.z1 + t) : box(s.x1, 0, s.z0 - t, s.x1 + t, h, s.z1 + t);
	} else {
		r[0] = box(s.x0 - t, 0, s.z0, s.x0, h, s.z1);
		r[1] = box(s.x1, 0, s.z0, s.x1 + t, h, s.z1);
		r[2] = up == 2 ? box(s.x0 - t, 0, s.z0 - t, s.x1 + t, h, s.z0) : box(s.x0 - t, 0, s.z1, s.x1 + t, h, s.z1 + t);
	}
	for (int i = 0; i < 3; i++) {
		solid_add(w, r[i]);
		piece_add(w, r[i], IR_RAIL);
	}
}

/* the far end of stairs s, where they come out on the floor above; and the foot */
static Vec3 stair_end(WRect s, int up, bool top, float beyond) {
	float cx = (s.x0 + s.x1) / 2, cz = (s.z0 + s.z1) / 2;
	int f = top ? up : (up + 2) % 4;
	switch (f) {
	case 0: return v3(cx, 0, s.z0 - beyond);
	case 1: return v3(s.x1 + beyond, 0, cz);
	case 2: return v3(cx, 0, s.z1 + beyond);
	default: return v3(s.x0 - beyond, 0, cz);
	}
}

/* the area at the far end of stairs s (top, or foot), so deep, as wide as they are and a little more */
static Box stair_clear(WRect s, int up, bool top, float deep) {
	Vec3 e = stair_end(s, up, top, 0);
	int f = top ? up : (up + 2) % 4;
	float hw = STAIR_W / 2 + 0.4f;
	switch (f) {
	case 0: return box(e.x - hw, 0, e.z - deep, e.x + hw, WALL_H, e.z);
	case 1: return box(e.x, 0, e.z - hw, e.x + deep, WALL_H, e.z + hw);
	case 2: return box(e.x - hw, 0, e.z, e.x + hw, WALL_H, e.z + deep);
	default: return box(e.x - deep, 0, e.z - hw, e.x, WALL_H, e.z + hw);
	}
}

/* whether the lift's folder belongs to store room r: one of its folders, or under one */
static bool pick_in(const World *wd, const WRoom *r, const Node *pick) {
	for (const Node *a = pick; a; a = a->parent) {
		for (int j = 0; j < r->nstore; j++) {
			if (wd->stored[r->store0 + j] == a) {
				return true;
			}
		}
	}
	return false;
}

static void plaque_add(Walk *w, Box b, Node *to, const char *text) {
	if (w->nplaque == w->plaque_cap) {
		w->plaque_cap = w->plaque_cap ? w->plaque_cap * 2 : 32;
		w->plaque = SDL_realloc(w->plaque, (size_t)w->plaque_cap * sizeof *w->plaque);
	}
	Plaque *p = &w->plaque[w->nplaque++];
	p->b = b;
	p->to = to;
	snprintf(p->text, sizeof p->text, "%s", text);
}

/* A store room's board, on the wall ahead as you come up its stairs: a
 * plaque for each of its folders, and for those in the folder the lift is
 * at, and back up out of it; E on one takes the lift there. */
static void store_board(Walk *w, int ri, Box *clear) {
	const World *wd = &w->world;
	const WRoom *r = &wd->room[ri];
	int s = r->up < 0 ? 0 : r->up;
	float t = side_t(wd, r, s) + 0.02f;
	float a0 = s == 0 || s == 2 ? r->r.x0 : r->r.z0, a1 = s == 0 || s == 2 ? r->r.x1 : r->r.z1, mid = (a0 + a1) / 2;
	float bw = fminf(2.6f, a1 - a0 - 1.2f), top = 2.75f, row = 0.17f;
	Node *list[20];
	char text[20][96];
	int n = 0;
	for (int j = 0; j < r->nstore && n < 12; j++) {
		Node *k = wd->stored[r->store0 + j];
		list[n] = k;
		snprintf(text[n++], sizeof text[0], "%s%.60s", k == w->pick ? "* " : "", k->name);
	}
	if (w->pick && w->pick->parent && pick_in(wd, r, w->pick->parent) && n < 20) {
		list[n] = w->pick->parent;
		snprintf(text[n++], sizeof text[0], "< back to %.60s", w->pick->parent->name);
	}
	for (int i = 0; w->pick && i < w->pick->nkids && n < 20; i++) {
		Node *k = w->pick->kids[i];
		if (k->is_dir) {
			list[n] = k;
			snprintf(text[n++], sizeof text[0], "> %.60s", k->name);
		}
	}
	float bottom = top - row * (float)n - 0.1f;
	piece_add(w, side_box(r->r, s, t, t + 0.05f, mid - bw / 2 - 0.1f, mid + bw / 2 + 0.1f, bottom - 0.08f, top + 0.35f), IR_BOARD);
	sign_add(w, side_at(r->r, s, t + 0.1f, mid, top + 0.45f), ri, 1, "DIRECTORY  (E on a name: the lift)");
	for (int i = 0; i < n; i++) {
		float y = top - row * (float)(i + 1);
		Box p = side_box(r->r, s, t + 0.05f, t + 0.08f, mid - bw / 2, mid + bw / 2, y + 0.02f, y + row - 0.02f);
		piece_add(w, p, IR_PLAQUE);
		plaque_add(w, p, list[i], text[i]);
	}
	*clear = side_box(r->r, s, 0, 1.8f, mid - bw / 2 - 0.5f, mid + bw / 2 + 0.5f, 0, WALL_H);
}

/* The textures blocks had, kept for the same files on a floor built again. */
typedef struct {
	const Node *node;
	unsigned tex;
	bool tried;
	float aspect;
	unsigned long long io;
} Kept;

/* Builds the floor you are on, in its own frame (its floor at 0): fl of
 * building bld, or (bld -1) the street, where the square's things are.
 * Walls, panes, floors, stairs and rails, then the blocks of each room's
 * files; never the city, which stays as it is. */
static void build_floor(Walk *w, int bld, int fl) {
	World *wd = &w->world;
	int nkept = w->nblock;
	Kept *kept = SDL_malloc((size_t)(nkept + 1) * sizeof *kept);
	for (int i = 0; i < nkept; i++) {
		kept[i] = (Kept){w->block[i].node, w->block[i].tex, w->block[i].tried, w->block[i].aspect, w->block[i].io};
	}
	w->bld = bld;
	w->floor = bld < 0 ? 0 : fl;
	w->base = (float)w->floor * FLOOR_H;
	w->nblock = w->nsolid = w->nwall = w->nwin = w->nstruct = 0;
	w->npiece = w->nsign = w->nplaque = w->ndeco = w->nfroom = w->nups = w->nrug = 0;
	w->more_dirs = w->more_files = 0;
	w->plaque_at = -1;
	w->floor_dirty = true;
	w->near_ok = false;

	if (bld < 0) {
		/* the market square: the root's own files on it, round its fountain */
		Thing *t;
		int n = things_of(w, wd->root, &t, MAX_FILES);
		WRect q = inset(wd->square, 1.0f);
		float cx = (q.x0 + q.x1) / 2, cz = (q.z0 + q.z1) / 2;
		Box keep[3] = {box(cx - 4.5f, 0, cz - 4.5f, cx + 4.5f, 3, cz + 4.5f), box(cx - 1.5f, 0, q.z0, cx + 1.5f, 3, q.z1),
		               box(q.x0, 0, cz - 1.5f, q.x1, 3, cz + 1.5f)};
		solid_add(w, box(cx - 3.2f, 0, cz - 3.2f, cx + 3.2f, 0.55f, cz + 3.2f)); /* the fountain's basin */
		w->nwall = w->nwin = w->nstruct = 0;
		furnish_room(w, rbox(q, 0, WALL_H), t, n, keep, 3, NULL, 0);
		for (int i = 0; i < w->nblock; i++) {
			w->bmid[2 * i] = cx;
			w->bmid[2 * i + 1] = cz;
		}
		SDL_free(t);
	} else {
		const WBuilding *b = &wd->b[bld];
		for (int i = b->room0; i < b->room0 + b->nroom; i++) {
			if (wd->room[i].floor == fl) {
				if (w->nfroom == w->froom_cap) {
					w->froom_cap = w->froom_cap ? w->froom_cap * 2 : 32;
					w->froom = SDL_realloc(w->froom, (size_t)w->froom_cap * sizeof *w->froom);
				}
				w->froom[w->nfroom++] = i;
			}
		}
		if (SDL_getenv("SYN_WORLD_DUMP")) {
			for (int k = 0; k < w->nfroom; k++) {
				const WRoom *r = &wd->room[w->froom[k]];
				fprintf(stderr, "room %d %s floor %d parent %d r %.1f %.1f %.1f %.1f stair %.1f %.1f %.1f %.1f up %d\n", w->froom[k], r->node->name, r->floor,
				        r->parent, r->r.x0, r->r.z0, r->r.x1, r->r.z1, r->stair.x0, r->stair.z0, r->stair.x1, r->stair.z1, r->up);
			}
		}
		/* the walls first, then the panes, as the camera and the names want them */
		Box *panes = NULL;
		int npane = 0, pane_cap = 0;
		int *deco_from = SDL_malloc((size_t)(w->nfroom + 1) * sizeof *deco_from);
		for (int k = 0; k < w->nfroom; k++) {
			deco_from[k] = w->ndeco;
			room_walls(w, w->froom[k], &panes, &npane, &pane_cap);
		}
		deco_from[w->nfroom] = w->ndeco;
		w->nwall = w->nsolid;
		for (int i = 0; i < npane; i++) {
			solid_add(w, panes[i]);
		}
		SDL_free(panes);
		w->nwin = w->nsolid;
		char text[128], label[96];
		/* what must stay clear in each room, for its files after */
		enum { MAX_CLEAR = 80 };
		Box *clears = SDL_malloc((size_t)(w->nfroom + 1) * MAX_CLEAR * sizeof *clears);
		int *nclears = SDL_malloc((size_t)(w->nfroom + 1) * sizeof *nclears);
		for (int k = 0; k < w->nfroom; k++) {
			int ri = w->froom[k];
			const WRoom *r = &wd->room[ri];
			/* its floor, cut where its own stairs come up; its ceiling, cut where the stairs up to the rooms over it go */
			WRect up[64];
			int nup = 0;
			for (int c = b->room0; c < b->room0 + b->nroom && nup < 64; c++) {
				if (wd->room[c].parent == ri) {
					up[nup++] = wd->room[c].stair;
				}
			}
			slab(w, r->r, fl > 0 ? &r->stair : NULL, fl > 0, -SLAB, 0, IR_FLOOR, true);
			slab(w, r->r, up, nup, WALL_H, WALL_H + SLAB, IR_CEIL, false);
			/* lights in its ceiling, one every five metres or so each way */
			float cx = (r->r.x0 + r->r.x1) / 2, cz = (r->r.z0 + r->r.z1) / 2;
			int lx = (int)(wr_w(r->r) / 5) + 1, lz = (int)(wr_d(r->r) / 5) + 1;
			lx = lx > 6 ? 6 : lx;
			lz = lz > 6 ? 6 : lz;
			for (int i = 0; i < lx; i++) {
				for (int j = 0; j < lz; j++) {
					float x = r->r.x0 + wr_w(r->r) * ((float)i + 0.5f) / (float)lx, z = r->r.z0 + wr_d(r->r) * ((float)j + 0.5f) / (float)lz;
					piece_add(w, box(x - 0.45f, WALL_H - 0.05f, z - 0.45f, x + 0.45f, WALL_H, z + 0.45f), IR_LAMP);
				}
			}
			Box *clear = clears + k * MAX_CLEAR;
			int nclear = 0;
			/* the stairs up from it into each room over it */
			for (int c = b->room0; c < b->room0 + b->nroom; c++) {
				const WRoom *u = &wd->room[c];
				if (u->parent != ri) {
					continue;
				}
				steps(w, u->stair, u->up, 0, true);
				if (w->nups == w->ups_cap) {
					w->ups_cap = w->ups_cap ? w->ups_cap * 2 : 32;
					w->ups = SDL_realloc(w->ups, (size_t)w->ups_cap * sizeof *w->ups);
				}
				w->ups[w->nups++] = u->stair;
				/* the ceiling of the room up there, seen up the stairs */
				piece_add(w, rbox(inset(u->stair, -0.5f), FLOOR_H + WALL_H - 0.05f, FLOOR_H + WALL_H + 0.1f), IR_CEIL);
				Vec3 foot = stair_end(u->stair, u->up, false, 0.4f);
				room_label(w, c, label, sizeof label);
				snprintf(text, sizeof text, "UP: %.80s", label);
				sign_add(w, v3(foot.x, 2.5f, foot.z), ri, 1, text);
				if (nclear < 78) {
					clear[nclear++] = rbox(inset(u->stair, -0.3f), 0, WALL_H);
					clear[nclear++] = stair_clear(u->stair, u->up, false, 1.6f);
				}
			}
			/* its own stairs, coming up from the room below: the steps, the rails, the floor down there */
			if (fl > 0) {
				steps(w, r->stair, r->up, -FLOOR_H, true);
				rails(w, r->stair, r->up);
				piece_add(w, rbox(inset(r->stair, -1.0f), -FLOOR_H - SLAB, -FLOOR_H), IR_FLOOR);
				Vec3 top = stair_end(r->stair, r->up, true, 0.3f);
				room_label(w, r->parent, label, sizeof label);
				snprintf(text, sizeof text, "DOWN: %.80s", label);
				sign_add(w, v3(top.x, 1.5f, top.z), ri, 1, text);
				clear[nclear++] = rbox(inset(r->stair, -0.5f), 0, WALL_H);
				clear[nclear++] = stair_clear(r->stair, r->up, true, 1.8f);
			}
			/* the way in from the street, and the doorways */
			if (fl == 0) {
				float d = b->door;
				WRect dr = b->front == 0 ? (WRect){d - 1.6f, b->r.z0, d + 1.6f, b->r.z0 + 3.2f}
				         : b->front == 1 ? (WRect){b->r.x1 - 3.2f, d - 1.6f, b->r.x1, d + 1.6f}
				         : b->front == 2 ? (WRect){d - 1.6f, b->r.z1 - 3.2f, d + 1.6f, b->r.z1}
				                         : (WRect){b->r.x0, d - 1.6f, b->r.x0 + 3.2f, d + 1.6f};
				clear[nclear++] = rbox(dr, 0, WALL_H);
			}
			for (int j = 0; j < w->nfroom && nclear < 78; j++) {
				int side;
				float mid;
				if (world_doorway(wd, ri, w->froom[j], &side, &mid)) {
					clear[nclear++] = side_box(r->r, side, 0, 1.6f, mid - ROOM_DOOR_W / 2 - 0.3f, mid + ROOM_DOOR_W / 2 + 0.3f, 0, WALL_H);
				}
			}
			if (r->store) {
				if (!w->pick || !pick_in(wd, r, w->pick)) {
					w->pick = r->node;
				}
				store_board(w, ri, &clear[nclear++]);
			}
			room_label(w, ri, label, sizeof label);
			sign_add(w, v3(cx, WALL_H - 0.45f, cz), ri, 0, r->store ? w->pick->name : label);
			nclears[k] = nclear;
		}
		/* then each room's files, standing in it, after all the floors and stairs */
		w->nstruct = w->nsolid;
		for (int k = 0; k < w->nfroom; k++) {
			const WRoom *r = &wd->room[w->froom[k]];
			float cx = (r->r.x0 + r->r.x1) / 2, cz = (r->r.z0 + r->r.z1) / 2;
			Node *what = r->store ? w->pick : r->node;
			Thing *t;
			int most = FLOOR_FILES - w->nblock;
			int n = things_of(w, what, &t, most < MAX_FILES ? (most > 0 ? most : 0) : MAX_FILES);
			int first = w->nblock;
			furnish_room(w, rbox(room_inside(wd, r), 0, WALL_H), t, n, clears + k * MAX_CLEAR, nclears[k], w->deco + deco_from[k],
			             deco_from[k + 1] - deco_from[k]);
			for (int i = first; i < w->nblock; i++) {
				w->bmid[2 * i] = cx;
				w->bmid[2 * i + 1] = cz;
			}
			SDL_free(t);
		}
		SDL_free(clears);
		SDL_free(nclears);
		SDL_free(deco_from);
	}
	for (int i = 0; i < w->nblock; i++) {
		for (int j = 0; j < nkept; j++) {
			if (kept[j].node == w->block[i].node) {
				w->block[i].tex = kept[j].tex;
				w->block[i].tried = kept[j].tried;
				w->block[i].aspect = kept[j].aspect;
				w->block[i].io = kept[j].io;
				kept[j].tex = 0; /* taken */
				break;
			}
		}
	}
	for (int j = 0; j < nkept; j++) {
		tex_drop(w, kept[j].tex);
	}
	SDL_free(kept);
}

/* the room on this floor at (x, z), -1 none (a doorway, a wall) */
static int room_at(const Walk *w, float x, float z) {
	for (int k = 0; k < w->nfroom; k++) {
		if (in_rect(w->world.room[w->froom[k]].r, x, z)) {
			return w->froom[k];
		}
	}
	return -1;
}

/* You are in room ri now (-1: out in the street): its folder is the room
 * the map, the HUD and E go by. */
static void set_room(Walk *w, int ri) {
	const World *wd = &w->world;
	w->at = ri;
	Node *n = ri < 0 ? wd->root : wd->room[ri].store && w->pick ? w->pick : wd->room[ri].node;
	w->room = n;
	node_path(n, w->path, sizeof w->path);
	snprintf(w->name, sizeof w->name, "%s", n->parent ? n->name : w->path);
}

/* Standing still at (x, z) facing yaw, after a lift, the street's square, a door. */
static void city_near(Walk *w);
static void beyond_ask(const Node *root);
static void sfx_play(bool on);
static bool car_covers(const Walk *w, float x, float z, float r);
static bool car_use(Walk *w);
static bool car_in(const Walk *w);
static void car_camera(Walk *w, Vec3 *eye, Vec3 *at);
static void sfx_engine(float on, float speed01, float throttle);
static void crew_into(World *wd, const Node *root);
static bool crew_here(const Node *root);
static void crew_move(Walk *w, float dt);
static void crew_test(Walk *w);
static bool crew_ready(void);
static void stand(Walk *w, float x, float z, float yaw) {
	w->px = x;
	w->pz = z;
	if (w->bld < 0) {
		w->near_ok = false;
		city_near(w);
	}
	w->yaw = w->body_yaw = yaw;
	w->py = floor_under(w, x, STEP_UP, z);
	w->vy = w->hvx = w->hvz = 0;
	w->coyote = w->jump_early = 0;
	w->ground = true;
	w->follow_ok = false;
	w->cam_len = 0;
}

/* the way side f faces, as a yaw: 0 -z ... */
static float yaw_of(int f) {
	static const float Y[4] = {0, -(float)M_PI / 2, (float)M_PI, (float)M_PI / 2};
	return Y[f & 3];
}

/* just inside building b's door (in > 0), or that far out in front of it, facing the way you would go */
static void at_door(Walk *w, int b, float in) {
	const WBuilding *B = &w->world.b[b];
	float x = B->door, z = B->door;
	switch (B->front) {
	case 0: z = B->r.z0 + in; break;
	case 1: x = B->r.x1 - in; break;
	case 2: z = B->r.z1 - in; break;
	default: x = B->r.x0 + in; break;
	}
	stand(w, x, z, yaw_of(in > 0 ? (B->front + 2) % 4 : B->front));
}

/* at the top of the stairs up into room ri, facing on into it */
static void at_landing(Walk *w, int ri) {
	const WRoom *r = &w->world.room[ri];
	Vec3 e = stair_end(r->stair, r->up, true, 0.9f);
	stand(w, e.x, e.z, yaw_of(r->up));
}

/* in the market square, on its south side, looking over it */
static void in_square(Walk *w) {
	WRect q = w->world.square;
	stand(w, (q.x0 + q.x1) / 2, q.z1 - 2.5f, 0);
}

static void csolid_add(Walk *w, Box b) {
	if (w->ncsolid == w->csolid_cap) {
		w->csolid_cap = w->csolid_cap ? w->csolid_cap * 2 : 256;
		w->csolid = SDL_realloc(w->csolid, (size_t)w->csolid_cap * sizeof *w->csolid);
	}
	w->csolid[w->ncsolid++] = b;
}

/* What can be run into out in the street: each building's outside walls,
 * a gap for its door; the pavements; the square's fountain; the wall
 * round the city. Made with the world. */
static void city_solids(Walk *w) {
	const World *wd = &w->world;
	w->ncsolid = 0;
	w->near_ok = false;
	for (int i = 0; i < wd->nb; i++) {
		const WBuilding *b = &wd->b[i];
		float H = (float)b->floors * FLOOR_H;
		WRect q = b->r;
		for (int s = 0; s < 4; s++) {
			float a0 = s == 0 || s == 2 ? q.x0 : q.z0, a1 = s == 0 || s == 2 ? q.x1 : q.z1;
			if (s == b->front) {
				csolid_add(w, side_box(q, s, 0, WALL_OUT, a0, b->door - FRONT_DOOR_W / 2, 0, H));
				csolid_add(w, side_box(q, s, 0, WALL_OUT, b->door + FRONT_DOOR_W / 2, a1, 0, H));
				csolid_add(w, side_box(q, s, 0, WALL_OUT, b->door - FRONT_DOOR_W / 2, b->door + FRONT_DOOR_W / 2, FRONT_DOOR_H, H));
			} else {
				csolid_add(w, side_box(q, s, 0, WALL_OUT, a0, a1, 0, H));
			}
		}
		csolid_add(w, rbox(b->cell, -1, 0));
	}
	csolid_add(w, rbox(inset(wd->square, -PAVE), -1, 0));
	float cx = (wd->square.x0 + wd->square.x1) / 2, cz = (wd->square.z0 + wd->square.z1) / 2;
	csolid_add(w, box(cx - 3.2f, 0, cz - 3.2f, cx + 3.2f, 0.55f, cz + 3.2f));
	float E = wd->edge;
	csolid_add(w, box(-E - 0.5f, -1, -E - 0.5f, E + 0.5f, CITY_FENCE, -E));
	csolid_add(w, box(-E - 0.5f, -1, E, E + 0.5f, CITY_FENCE, E + 0.5f));
	csolid_add(w, box(-E - 0.5f, -1, -E, -E, CITY_FENCE, E));
	csolid_add(w, box(E, -1, -E, E + 0.5f, CITY_FENCE, E));
}

/* The world of the tree dir is in, made if it is not the one there is,
 * and you in dir's room (by near, one of its files, if given). */
static void place_in(Walk *w, Node *dir) {
	World *wd = &w->world;
	int r = world_room_of(wd, dir);
	if (r < 0) {
		build_floor(w, -1, 0);
		set_room(w, -1);
		in_square(w);
		return;
	}
	const WRoom *R = &wd->room[r];
	if (R->store) {
		w->pick = dir;
	}
	build_floor(w, R->building, R->floor);
	set_room(w, r);
	if (R->floor == 0) {
		at_door(w, R->building, 1.8f);
	} else {
		at_landing(w, r);
	}
}

void walk_enter(Walk *w, Node *dir, const Node *near) {
	if (!dir) {
		return;
	}
	if (!dir->is_dir) {
		near = dir;
		dir = dir->parent;
	}
	if (!dir) {
		return;
	}
	Node *root = dir;
	while (root->parent) {
		root = root->parent;
	}
	if (w->world.root != root || world_stale(&w->world)) {
		w->world.want = folder_want;
		crew_into(&w->world, root);
		world_build(&w->world, root);
		beyond_ask(root);
		city_solids(w);
		w->city_dirty = true;
		w->since_built = 0;
		for (int i = 0; SDL_getenv("SYN_WORLD_DUMP") && i < w->world.nb; i++) {
			const WBuilding *b = &w->world.b[i];
			fprintf(stderr, "building %d %s r %.0f %.0f %.0f %.0f front %d door %.1f floors %d style %d rooms %d files %d want %.0f m2\n", i, b->name, b->r.x0,
			        b->r.z0, b->r.x1, b->r.z1, b->front, b->door, b->floors, b->style, b->nroom, b->node->nkids, folder_want(b->node));
		}
	}
	w->on = true;
	sfx_play(true);
	w->dist = w->dist > 0 ? w->dist : 4.2f;
	w->look = w->captured = false;
	place_in(w, dir);
	if (near) {
		walk_go_to(w, near);
	}
	w->pitch = w->first ? 0 : 0.3f;
	w->fade = 0.35f;
}

Node *walk_leave(Walk *w) {
	walk_release(w);
	w->on = false;
	sfx_engine(0, 0, 0);
	sfx_play(false);
	return w->room;
}

/* ---- sound: steps, jumps, the car ----
 *
 * Made as it plays, no recordings: SDL's audio, a callback mixing a few
 * voices. A step is a scuff of filtered noise and a low thump; a jump, a
 * rush of cloth; a landing, a thump as hard as the fall; a car door, the
 * latch and the hinge, or the shut. The engine runs while you are in the
 * car: its firing pitch with the speed, louder on the throttle, the road
 * under it rising with the speed. Quiet when you are not walking. */

enum { SFX_STEP, SFX_JUMP, SFX_LAND, SFX_DOOR_OPEN, SFX_DOOR_SHUT, SFX_KINDS };
#define SFX_VOICES 12
typedef struct {
	int kind;
	float t, len, amp, pitch, lp, ph;
	unsigned seed;
} Voice;
static struct {
	SDL_AudioDeviceID dev;
	bool tried;
	float rate;
	SDL_SpinLock lock;
	Voice v[SFX_VOICES];
	/* the engine, set from the walk each frame */
	float eng_on, eng_speed, eng_throttle;
	/* the callback's own */
	float eng_ph, eng_ph2, eng_lp, road_lp, eng_amp, eng_f;
	unsigned noise;
} sfx;

static float sfx_noise(unsigned *s) {
	*s = *s * 1664525u + 1013904223u;
	return (float)(*s >> 8) / 8388608.0f - 1.0f;
}

static void SDLCALL sfx_mix(void *ud, Uint8 *stream, int len) {
	(void)ud;
	float *out = (float *)stream;
	int n = len / (int)(2 * sizeof(float));
	float dt = 1.0f / sfx.rate;
	SDL_AtomicLock(&sfx.lock);
	for (int i = 0; i < n; i++) {
		float mix = 0;
		for (int k = 0; k < SFX_VOICES; k++) {
			Voice *v = &sfx.v[k];
			if (v->len <= 0) {
				continue;
			}
			float t = v->t, s = 0, nz = sfx_noise(&v->seed);
			switch (v->kind) {
			case SFX_STEP: /* a scuff, and the heel's thump */
				v->lp += (nz - v->lp) * (0.18f * v->pitch);
				s = v->lp * expf(-t * 55) * 1.6f + sinf(v->ph) * expf(-t * 38) * 0.9f;
				v->ph += 2 * (float)M_PI * (70 + 30 * v->pitch) * dt;
				break;
			case SFX_JUMP: /* cloth: noise swelling and gone */
				v->lp += (nz - v->lp) * 0.12f;
				s = v->lp * sinf((float)M_PI * fminf(t / v->len, 1)) * 0.9f;
				break;
			case SFX_LAND: /* a thump, a little grit */
				v->lp += (nz - v->lp) * 0.1f;
				s = sinf(v->ph) * expf(-t * 22) * 1.3f + v->lp * expf(-t * 40);
				v->ph += 2 * (float)M_PI * (52 - 20 * t) * dt;
				break;
			case SFX_DOOR_OPEN: /* the latch, then the hinge */
				v->lp += (nz - v->lp) * 0.5f;
				s = v->lp * expf(-t * 160) * 1.2f;
				if (t > 0.04f) {
					float f = 260 + 220 * (t - 0.04f) / v->len;
					v->ph += 2 * (float)M_PI * f * dt;
					s += (fmodf(v->ph / (2 * (float)M_PI), 1) - 0.5f) * 0.18f * sinf((float)M_PI * fminf((t - 0.04f) / (v->len - 0.04f), 1)) *
					     (0.6f + 0.4f * sinf(t * 70));
				}
				break;
			case SFX_DOOR_SHUT: /* the slam and the latch */
				v->lp += (nz - v->lp) * 0.25f;
				s = sinf(v->ph) * expf(-t * 18) * 1.4f + v->lp * expf(-t * 60) * 0.8f;
				v->ph += 2 * (float)M_PI * 64 * dt;
				if (t > 0.07f && t < 0.09f) {
					s += nz * 0.6f;
				}
				break;
			}
			mix += s * v->amp;
			v->t += dt;
			if (v->t >= v->len) {
				v->len = 0;
			}
		}
		/* the engine, easing to what the walk asks */
		float want_amp = sfx.eng_on * (0.16f + 0.2f * sfx.eng_throttle + 0.1f * sfx.eng_speed);
		float want_f = 34 + 95 * sfx.eng_speed + 18 * sfx.eng_throttle;
		sfx.eng_amp += (want_amp - sfx.eng_amp) * 0.0006f;
		sfx.eng_f += (want_f - sfx.eng_f) * 0.0004f;
		if (sfx.eng_amp > 0.0005f) {
			sfx.eng_ph += sfx.eng_f * dt;
			sfx.eng_ph2 += sfx.eng_f * 0.5f * dt;
			float fire = fmodf(sfx.eng_ph, 1), half = fmodf(sfx.eng_ph2, 1);
			float e = (fire - 0.5f) * 0.8f + (half - 0.5f) * 0.5f + (fire < 0.12f ? 0.5f : 0); /* the firing, its rumble under */
			sfx.eng_lp += (e - sfx.eng_lp) * 0.08f;
			float nz = sfx_noise(&sfx.noise);
			sfx.road_lp += (nz - sfx.road_lp) * 0.05f;
			mix += sfx.eng_lp * sfx.eng_amp + sfx.road_lp * sfx.eng_on * sfx.eng_speed * 0.35f;
		}
		mix = mix / (1 + fabsf(mix)); /* never past full */
		out[2 * i] = out[2 * i + 1] = mix * 0.8f;
	}
	SDL_AtomicUnlock(&sfx.lock);
}

/* the sound opened (once) and playing, or quiet */
static void sfx_play(bool on) {
	if (!sfx.tried && on) {
		sfx.tried = true;
		if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
			SDL_AudioSpec want = {0}, have;
			want.freq = 48000;
			want.format = AUDIO_F32SYS;
			want.channels = 2;
			want.samples = 512;
			want.callback = sfx_mix;
			sfx.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
			sfx.rate = (float)have.freq;
			sfx.noise = 12345u;
			if (SDL_getenv("SYN_WORLD_DUMP")) {
				fprintf(stderr, "sound: %s\n", sfx.dev ? "open" : SDL_GetError());
			}
		}
	}
	if (sfx.dev) {
		SDL_PauseAudioDevice(sfx.dev, on ? 0 : 1);
	}
}

static void sfx_start(int kind, float amp, float pitch, float len) {
	if (!sfx.dev) {
		return;
	}
	SDL_AtomicLock(&sfx.lock);
	for (int k = 0; k < SFX_VOICES; k++) {
		Voice *v = &sfx.v[k];
		if (v->len <= 0) {
			*v = (Voice){kind, 0, len, amp, pitch, 0, 0, (unsigned)SDL_GetTicks() * 2654435761u + (unsigned)k * 97u};
			break;
		}
	}
	SDL_AtomicUnlock(&sfx.lock);
}

static void sfx_engine(float on, float speed01, float throttle) {
	sfx.eng_on = on;
	sfx.eng_speed = speed01;
	sfx.eng_throttle = throttle;
}

/* ---- moving ---- */

/* Moves you along one axis (0 x, 2 z) by d: onto what is low enough to
 * step up on, stopped by anything else. */
static bool move_axis(Walk *w, int axis, float d) {
	float x = w->px + (axis == 0 ? d : 0), z = w->pz + (axis == 2 ? d : 0), rise = w->py;
	if (!car_in(w) && car_covers(w, x, z, BODY_R) && !car_covers(w, w->px, w->pz, BODY_R)) {
		return false; /* the car, parked */
	}
	int n = solid_n(w);
	for (int i = 0; i < n; i++) {
		const Box *b = solid_i(w, i);
		if (!overlaps_you(w, b, x, w->py, z)) {
			continue;
		}
		if (b->hi[1] - w->py <= STEP_UP && (w->ground || w->coyote > 0)) { /* just off a step counts as on it */
			rise = b->hi[1] > rise ? b->hi[1] : rise;
		} else {
			return false; /* in the way */
		}
	}
	if (rise > w->py) {
		/* stepped up: nothing may be in the way up there either */
		for (int i = 0; i < n; i++) {
			if (overlaps_you(w, solid_i(w, i), x, rise, z)) {
				return false;
			}
		}
		w->py = rise;
	}
	w->px = x;
	w->pz = z;
	return true;
}

/* The city's boxes near you, for running into and for the camera, so the
 * loops that test every box test a few dozen: gathered again when you have
 * gone a few metres from where they last were. */
static void city_near(Walk *w) {
	float mx = w->px - w->near_x, mz = w->pz - w->near_z;
	if (w->near_ok && mx * mx + mz * mz < 4 * 4) {
		return;
	}
	w->near_ok = true;
	w->near_x = w->px;
	w->near_z = w->pz;
	w->ncnear = 0;
	for (int i = 0; i < w->ncsolid + w->ncprop; i++) {
		const Box *b = i < w->ncsolid ? &w->csolid[i] : &w->cprop[i - w->ncsolid];
		float dx = w->px < b->lo[0] ? b->lo[0] - w->px : w->px > b->hi[0] ? w->px - b->hi[0] : 0;
		float dz = w->pz < b->lo[2] ? b->lo[2] - w->pz : w->pz > b->hi[2] ? w->pz - b->hi[2] : 0;
		if (dx > CITY_NEAR || dz > CITY_NEAR) {
			continue;
		}
		if (w->ncnear == w->cnear_cap) {
			w->cnear_cap = w->cnear_cap ? w->cnear_cap * 2 : 64;
			w->cnear = SDL_realloc(w->cnear, (size_t)w->cnear_cap * sizeof *w->cnear);
		}
		w->cnear[w->ncnear++] = *b;
	}
}

/* whether you are inside something you could not step up onto */
static bool stuck_in(const Walk *w) {
	for (int i = 0, n = solid_n(w); i < n; i++) {
		const Box *b = solid_i(w, i);
		if (b->hi[1] > w->py + STEP_UP && overlaps_you(w, b, w->px, w->py, w->pz)) {
			return true;
		}
	}
	return false;
}

/* Out of whatever you stand in (the world made again round you): the
 * shortest way out of each, a few times over; false if still in
 * something. What you can step up on you just stand on. */
static bool step_clear(Walk *w) {
	for (int pass = 0; pass < 6; pass++) {
		bool moved = false;
		for (int i = 0, n = solid_n(w); i < n; i++) {
			const Box *b = solid_i(w, i);
			if (b->hi[1] <= w->py + STEP_UP || !overlaps_you(w, b, w->px, w->py, w->pz)) {
				continue;
			}
			float e[4] = {b->hi[0] + BODY_R - w->px, w->px - (b->lo[0] - BODY_R), b->hi[2] + BODY_R - w->pz, w->pz - (b->lo[2] - BODY_R)};
			int k = 0;
			for (int j = 1; j < 4; j++) {
				k = e[j] < e[k] ? j : k;
			}
			float push = e[k] + 0.02f;
			*(k < 2 ? &w->px : &w->pz) += k == 0 || k == 2 ? push : -push;
			moved = true;
		}
		if (!moved) {
			w->py = floor_under(w, w->px, w->py + STEP_UP, w->pz);
			return true;
		}
	}
	return false;
}

/* Through a door, up or down stairs, from room to room: the floor you are
 * on now, built if it changed (the city never), your height kept in the
 * new floor's frame so nothing jumps; true if it was built. */
static bool step_floor(Walk *w) {
	const World *wd = &w->world;
	if (w->bld < 0) {
		int b = world_building_at(wd, w->px, w->pz, WALL_OUT + 0.15f);
		if (b < 0) {
			return false;
		}
		build_floor(w, b, 0);
		set_room(w, room_at(w, w->px, w->pz) >= 0 ? room_at(w, w->px, w->pz) : wd->b[b].room0);
		return true;
	}
	const WBuilding *b = &wd->b[w->bld];
	if (w->floor == 0 && !in_rect(inset(b->r, -0.15f), w->px, w->pz)) {
		build_floor(w, -1, 0);
		set_room(w, -1);
		city_near(w);
		return true;
	}
	if (w->py > FLOOR_H - 1.0f) {
		for (int c = b->room0; c < b->room0 + b->nroom; c++) {
			const WRoom *u = &wd->room[c];
			if (u->floor == w->floor + 1 && in_rect(inset(u->stair, -0.3f), w->px, w->pz)) {
				w->py -= FLOOR_H;
				w->follow.y -= FLOOR_H;
				if (u->store && (!w->pick || !pick_in(wd, u, w->pick))) {
					w->pick = u->node;
				}
				build_floor(w, w->bld, w->floor + 1);
				set_room(w, c);
				return true;
			}
		}
	}
	if (w->floor > 0 && w->py < -2.0f) {
		w->py += FLOOR_H;
		w->follow.y += FLOOR_H;
		build_floor(w, w->bld, w->floor - 1);
		int r = room_at(w, w->px, w->pz);
		set_room(w, r >= 0 ? r : w->froom[0]);
		return true;
	}
	int r = room_at(w, w->px, w->pz);
	if (r >= 0 && r != w->at) {
		set_room(w, r);
	}
	return false;
}

/* The tree has grown enough: the world made again, you in the same room
 * as near as can be where you were in it, or out where you were. */
static void world_again(Walk *w) {
	World *wd = &w->world;
	Node *was = w->room;
	bool out = w->bld < 0;
	float fx = 0.5f, fz = 0.5f;
	if (!out && w->at >= 0) {
		WRect q = wd->room[w->at].r;
		fx = (w->px - q.x0) / wr_w(q);
		fz = (w->pz - q.z0) / wr_d(q);
	}
	wd->want = folder_want;
	crew_into(wd, wd->root);
	world_build(wd, wd->root);
	beyond_ask(wd->root);
	city_solids(w);
	w->city_dirty = true;
	if (out) {
		build_floor(w, -1, 0);
		set_room(w, -1);
		city_near(w);
		if (!step_clear(w)) {
			in_square(w);
		}
		return;
	}
	int r = world_room_of(wd, was);
	if (r < 0) {
		build_floor(w, -1, 0);
		set_room(w, -1);
		in_square(w);
		return;
	}
	const WRoom *R = &wd->room[r];
	float y = w->py;
	build_floor(w, R->building, R->floor);
	set_room(w, r);
	WRect q = room_inside(wd, R);
	w->px = clampf(R->r.x0 + fx * wr_w(R->r), q.x0 + BODY_R, q.x1 - BODY_R);
	w->pz = clampf(R->r.z0 + fz * wr_d(R->r), q.z0 + BODY_R, q.z1 - BODY_R);
	w->py = clampf(y, 0, WALL_H - BODY_H);
	if (!step_clear(w)) {
		R->floor ? at_landing(w, r) : at_door(w, R->building, 1.8f);
	}
	w->follow_ok = false;
}

static Vec3 look_dir(const Walk *w);
static void figure(Bag *bx, Bag *ball, Bag *lamp, float x, float y, float z, float mv, float run, float stride, float time, SDL_Color hood, SDL_Color glow, bool flicker, int pose);
static float cam_clear(const Walk *w, Vec3 p, Vec3 d);
static void bag_put(Bag *g, float x, float y, float z, float sx, float sy, float sz, SDL_Color c);

/* ---- the car ----
 *
 * Parked at the edge of the market square, right-hand drive. E at its
 * driver's door: the door swings open on its hinge, you get in and it
 * shuts behind you; you see from your seat (V: from behind it). W and S
 * are the throttle and the brake (and reverse), A and D steer, Shift
 * gives it more, Space is the handbrake; E stops it and lets you out.
 * It runs into what you would: the buildings, the wall, posts and trees,
 * bumping off. Built facing -z at (car_x, car_z), drawn turned by car_yaw. */

enum { CAR_OUT, CAR_GETTING_IN, CAR_SHUTTING, CAR_DRIVING, CAR_GETTING_OUT, CAR_LEFT };
#define CAR_HALF_W 0.93f
#define CAR_HALF_L 2.2f
#define CAR_BASE 2.7f   /* between the axles */
#define CAR_DOOR_OPEN 1.15f

static void car_local(const Walk *w, float lx, float lz, float *x, float *z) {
	float c = cosf(w->car_yaw), s = sinf(w->car_yaw);
	*x = w->car_x + c * lx + s * lz;
	*z = w->car_z - s * lx + c * lz;
}

static bool car_in(const Walk *w) { return w->car_ok && w->car_state != CAR_OUT && w->car_state != CAR_LEFT; }

/* where it waits: the square's east edge, nose north */
static void car_park(Walk *w) {
	WRect q = w->world.square;
	w->car_x = q.x1 - 2.2f;
	w->car_z = (q.z0 + q.z1) / 2 + 3;
	w->car_yaw = 0;
	w->car_v = w->car_steer = w->car_door = 0;
	w->car_state = CAR_OUT;
	w->car_ok = true;
}

/* whether (x, z) is within the car's body, grown by r */
static bool car_covers(const Walk *w, float x, float z, float r) {
	if (!w->car_ok || w->bld >= 0) {
		return false;
	}
	float dx = x - w->car_x, dz = z - w->car_z, c = cosf(w->car_yaw), s = sinf(w->car_yaw);
	float lx = c * dx - s * dz, lz = s * dx + c * dz;
	return fabsf(lx) < CAR_HALF_W + r && fabsf(lz) < CAR_HALF_L + r;
}

/* near enough its driver's door to get in */
static bool car_by_door(const Walk *w) {
	if (!w->car_ok || w->car_state != CAR_OUT || w->bld >= 0) {
		return false;
	}
	float dx, dz;
	car_local(w, CAR_HALF_W + 0.5f, 0, &dx, &dz);
	dx -= w->px;
	dz -= w->pz;
	return dx * dx + dz * dz < 1.8f * 1.8f;
}

/* E: in, or out once it has all but stopped */
static bool car_use(Walk *w) {
	if (car_by_door(w)) {
		w->car_state = CAR_GETTING_IN;
		w->car_t = 0;
		w->car_chase = false;
		w->hvx = w->hvz = 0;
		sfx_start(SFX_DOOR_OPEN, 0.7f, 1, 0.45f);
		return true;
	}
	if (w->car_ok && w->car_state == CAR_DRIVING) {
		if (fabsf(w->car_v) < 2.0f) {
			w->car_state = CAR_GETTING_OUT;
			w->car_t = 0;
			sfx_start(SFX_DOOR_OPEN, 0.7f, 1, 0.45f);
		}
		return true;
	}
	return false;
}

/* one circle of the car against the boxes round it: pushed out; how hard it hit */
static float car_bump(Walk *w, float lz, float r) {
	float cx, cz, hit = 0;
	car_local(w, 0, lz, &cx, &cz);
	for (int i = 0, n = solid_n(w); i < n; i++) {
		const Box *b = solid_i(w, i);
		if (b->hi[1] < 0.35f || b->lo[1] > 1.4f) {
			continue; /* under its sills, over its roof */
		}
		float nx = clampf(cx, b->lo[0], b->hi[0]), nz = clampf(cz, b->lo[2], b->hi[2]);
		float dx = cx - nx, dz = cz - nz, d = sqrtf(dx * dx + dz * dz);
		if (d >= r) {
			continue;
		}
		if (d < 1e-4f) { /* its middle inside: out the nearest side */
			float e[4] = {cx - b->lo[0], b->hi[0] - cx, cz - b->lo[2], b->hi[2] - cz};
			int k = 0;
			for (int j = 1; j < 4; j++) {
				k = e[j] < e[k] ? j : k;
			}
			dx = k == 0 ? -1.0f : k == 1 ? 1.0f : 0;
			dz = k == 2 ? -1.0f : k == 3 ? 1.0f : 0;
			d = 0;
		} else {
			dx /= d;
			dz /= d;
		}
		w->car_x += dx * (r - d);
		w->car_z += dz * (r - d);
		cx += dx * (r - d);
		cz += dz * (r - d);
		hit = fmaxf(hit, fabsf(w->car_v));
	}
	return hit;
}

static void car_update(Walk *w, float dt, const Uint8 *k) {
	if (!w->car_ok) {
		return;
	}
	w->car_t += dt;
	float throttle = 0;
	w->car_brake = false;
	switch (w->car_state) {
	case CAR_GETTING_IN:
		w->car_door = clampf(w->car_t / 0.45f, 0, 1) * CAR_DOOR_OPEN;
		if (w->car_t >= 0.6f) {
			w->car_state = CAR_SHUTTING;
			w->car_t = 0;
			w->first = true; /* from the seat */
			w->yaw = w->car_yaw;
			w->pitch = -0.05f;
		}
		break;
	case CAR_SHUTTING:
		w->car_door = (1 - clampf(w->car_t / 0.35f, 0, 1)) * CAR_DOOR_OPEN;
		if (w->car_t >= 0.35f) {
			w->car_state = CAR_DRIVING;
			w->car_door = 0;
			sfx_start(SFX_DOOR_SHUT, 0.8f, 1, 0.35f);
		}
		break;
	case CAR_GETTING_OUT:
		w->car_v *= 1 - clampf(dt * 4, 0, 1);
		w->car_door = clampf(w->car_t / 0.4f, 0, 1) * CAR_DOOR_OPEN;
		if (w->car_t >= 0.55f) {
			float x, z;
			car_local(w, CAR_HALF_W + 0.75f, 0.1f, &x, &z);
			w->car_state = CAR_LEFT;
			w->car_t = 0;
			w->first = false;
			w->pitch = 0.3f;
			w->cam_len = 0;
			stand(w, x, z, w->car_yaw - (float)M_PI / 2);
			w->follow_ok = false;
		}
		break;
	case CAR_LEFT:
		w->car_door = (1 - clampf(w->car_t / 0.35f, 0, 1)) * CAR_DOOR_OPEN;
		if (w->car_t >= 0.35f) {
			w->car_state = CAR_OUT;
			w->car_door = 0;
			sfx_start(SFX_DOOR_SHUT, 0.8f, 1, 0.35f);
		}
		break;
	case CAR_DRIVING: {
		float ahead = (float)((k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]));
		float side = (float)(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]);
		bool boost = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT], hand = k[SDL_SCANCODE_SPACE];
		float top = boost ? 34 : 22, v = w->car_v;
		if (ahead > 0) {
			v += (v < 0 ? 16 : boost ? 11 : 7) * dt;
			throttle = 1;
		} else if (ahead < 0) {
			if (v > 0.3f) {
				v -= 16 * dt; /* braking */
				w->car_brake = true;
			} else {
				v -= 5 * dt; /* reverse */
				throttle = 0.6f;
			}
		}
		v -= v * 0.12f * dt + (v > 0 ? 1.0f : v < 0 ? -1.0f : 0) * 0.8f * dt; /* air and the road */
		if (fabsf(v) < 0.05f && ahead == 0) {
			v = 0;
		}
		if (hand) {
			v *= 1 - clampf(dt * 1.8f, 0, 1);
			w->car_brake = true;
		}
		v = clampf(v, -7, top);
		float want = -side * 0.55f / (1 + fabsf(v) / 16); /* less lock the faster */
		w->car_steer += (want - w->car_steer) * clampf(dt * 6, 0, 1);
		float turn = v * tanf(w->car_steer) / CAR_BASE * (hand ? 1.7f : 1);
		w->car_yaw += turn * dt;
		w->yaw += turn * dt; /* the view turns with it */
		w->car_v = v;
		break;
	}
	default:
		break;
	}
	/* rolling, whatever the state */
	float c = cosf(w->car_yaw), s = sinf(w->car_yaw);
	w->car_x += -s * w->car_v * dt;
	w->car_z += -c * w->car_v * dt;
	if (w->car_v != 0) {
		float hit = fmaxf(car_bump(w, -1.3f, 1.0f), car_bump(w, 1.3f, 1.0f));
		if (hit > 0) {
			if (hit > 3) {
				sfx_start(SFX_LAND, clampf(hit / 20, 0.2f, 1), 1, 0.35f);
			}
			w->car_v *= -0.25f;
		}
	}
	w->car_throttle += (throttle - w->car_throttle) * clampf(dt * 5, 0, 1);
	sfx_engine(car_in(w) ? 1.0f : 0.0f, clampf(fabsf(w->car_v) / 34, 0, 1), w->car_throttle);
	if (car_in(w) && w->car_state != CAR_LEFT) {
		/* you go where it goes: the city's boxes round you, the world's LOD */
		w->px = w->car_x;
		w->pz = w->car_z;
		w->py = 0;
		w->hvx = w->hvz = w->vy = 0;
		w->ground = true;
		city_near(w);
	}
}

/* the camera from the seat, or from behind it */
static void car_camera(Walk *w, Vec3 *eye, Vec3 *at) {
	Vec3 f = look_dir(w);
	if (!w->car_chase) {
		float x, z;
		car_local(w, 0.38f, 0.2f, &x, &z);
		*eye = v3(x, 1.2f, z);
		*at = v3_add(*eye, f);
		return;
	}
	float x, z;
	car_local(w, 0, 6.5f, &x, &z);
	Vec3 want = v3(x, 2.6f, z);
	float k = 1 - expf(-w->cam_dt * 6);
	if (w->car_cam_y <= 0) {
		w->car_cam_x = want.x;
		w->car_cam_y = want.y;
		w->car_cam_z = want.z;
	}
	w->car_cam_x += (want.x - w->car_cam_x) * k;
	w->car_cam_y += (want.y - w->car_cam_y) * k;
	w->car_cam_z += (want.z - w->car_cam_z) * k;
	*at = v3(w->car_x, 1.2f, w->car_z);
	Vec3 back = v3(w->car_cam_x - at->x, w->car_cam_y - at->y, w->car_cam_z - at->z);
	float t = cam_clear(w, *at, back);
	*eye = v3_add(*at, v3_scale(back, t));
}

/* the car into its bags, for this frame */
static void car_fill(Walk *w, const Theme *t) {
	if (!w->car_ok || w->bld >= 0) {
		return;
	}
	Bag *bx = &w->bag[BAG_CAR_BOX], *ball = &w->bag[BAG_CAR_BALL], *lamp = &w->bag[BAG_CAR_LAMP], *glass = &w->bag[BAG_CAR_GLASS];
	float x = w->car_x, z = w->car_z;
	SDL_Color paint = mix(rgb(t->accent), (SDL_Color){24, 26, 30, 255}, 0.55f), dark = {16, 17, 19, 255}, trim = {120, 124, 130, 255},
	          seat = {30, 28, 26, 255}, dash = {22, 23, 26, 255}, tyre = {14, 14, 15, 255}, rim = {150, 154, 160, 255};
	SDL_Color glassc = {150, 175, 200, 70};
#define CB(lx, y0, lz, sx, sy, sz, c) bag_put(bx, x + (lx), (y0) + (sy) / 2, z + (lz), sx, sy, sz, c)
	/* the body: sills, the shell, bonnet and boot, bumpers */
	CB(0, 0.28f, 0, 1.86f, 0.12f, 4.3f, dark);
	CB(-CAR_HALF_W + 0.03f, 0.4f, 0.65f, 0.06f, 0.6f, 2.5f, paint); /* the near side, rear of its door (its own door is the driver's) */
	CB(CAR_HALF_W - 0.03f, 0.4f, 1.3f, 0.06f, 0.6f, 1.2f, paint);  /* behind the driver's door */
	CB(-CAR_HALF_W + 0.03f, 0.4f, -0.55f, 0.06f, 0.6f, 1.1f, paint);
	CB(0, 0.4f, -1.55f, 1.86f, 0.55f, 1.3f, paint);  /* the front: bonnet and wings */
	CB(0, 0.4f, 1.85f, 1.86f, 0.55f, 0.75f, paint);  /* the back */
	CB(0, 0.4f, 0.35f, 1.7f, 0.08f, 2.6f, dark);     /* the floor of the cabin */
	CB(0, 0.3f, -2.17f, 1.9f, 0.22f, 0.12f, dark);   /* bumpers */
	CB(0, 0.3f, 2.17f, 1.9f, 0.22f, 0.12f, dark);
	/* the cabin: pillars, the roof, windscreen and windows (glass) */
	CB(0, 1.38f, 0.25f, 1.7f, 0.06f, 1.7f, paint);   /* roof */
	for (int i = 0; i < 2; i++) {
		float sx = i ? 1.0f : -1.0f;
		CB(sx * 0.82f, 0.95f, -0.62f, 0.06f, 0.45f, 0.08f, paint); /* A pillars */
		CB(sx * 0.82f, 0.95f, 1.08f, 0.06f, 0.45f, 0.14f, paint);  /* C pillars */
	}
	bag_put(glass, x, 1.17f, z - 0.66f, 1.6f, 0.42f, 0.03f, glassc);    /* windscreen */
	bag_put(glass, x, 1.17f, z + 1.12f, 1.55f, 0.38f, 0.03f, glassc);   /* rear window */
	bag_put(glass, x - 0.86f, 1.17f, z + 0.25f, 0.02f, 0.4f, 1.6f, glassc); /* near side windows */
	/* inside: seats, the dashboard, its lights, the wheel, the mirror */
	for (int i = 0; i < 2; i++) {
		float sx = i ? 0.38f : -0.38f;
		CB(sx, 0.48f, 0.2f, 0.5f, 0.14f, 0.5f, seat);
		CB(sx, 0.48f, 0.48f, 0.5f, 0.62f, 0.12f, seat);
		CB(sx, 1.1f, 0.5f, 0.26f, 0.18f, 0.1f, seat); /* headrests */
	}
	CB(0, 0.48f, 1.0f, 1.5f, 0.14f, 0.5f, seat); /* the back seat */
	CB(0, 0.48f, 1.25f, 1.5f, 0.55f, 0.12f, seat);
	CB(0, 0.75f, -0.58f, 1.66f, 0.22f, 0.3f, dash);
	CB(0, 0.5f, -0.25f, 0.24f, 0.3f, 0.5f, dash); /* the console between the seats */
	bag_put(lamp, x + 0.38f, 0.95f, z - 0.45f, 0.34f, 0.08f, 0.01f, mix(rgb(t->accent), (SDL_Color){255, 255, 255, 255}, 0.3f)); /* the cluster */
	bag_put(lamp, x, 0.9f, z - 0.46f, 0.24f, 0.13f, 0.01f, mix(rgb(t->accent), (SDL_Color){20, 30, 40, 255}, 0.5f)); /* the screen */
	float wy = 1.0f, wz = -0.36f, wr = 0.17f;  /* the steering wheel, a ring of pieces, before the driver */
	for (int i = 0; i < 12; i++) {
		float a = (float)i / 12 * 2 * (float)M_PI + w->car_steer * 2.5f;
		bag_put(bx, x + 0.38f + cosf(a) * wr, wy + sinf(a) * wr, z + wz, 0.06f, 0.06f, 0.035f, dark);
	}
	CB(0.38f, 0.92f, -0.42f, 0.05f, 0.1f, 0.12f, dark); /* its column */
	CB(0, 1.25f, -0.55f, 0.2f, 0.05f, 0.03f, dark);     /* the mirror */
	/* lights: headlights white, tail lights red, brighter braking */
	SDL_Color head = {255, 244, 214, 255}, tail = w->car_brake ? (SDL_Color){255, 40, 30, 255} : (SDL_Color){150, 20, 18, 255};
	for (int i = 0; i < 2; i++) {
		float sx = i ? 0.68f : -0.68f;
		bag_put(lamp, x + sx, 0.72f, z - 2.21f, 0.34f, 0.12f, 0.04f, head);
		bag_put(lamp, x + sx, 0.75f, z + 2.23f, 0.32f, 0.1f, 0.04f, tail);
	}
	/* rear wheels with the body; front ones each turned by the steering, in their own bags */
	for (int i = 0; i < 2; i++) {
		float sx = i ? 0.85f : -0.85f;
		bag_put(ball, x + sx, 0.33f, z + 1.35f, 0.24f, 0.66f, 0.66f, tyre);
		bag_put(ball, x + sx * 1.02f, 0.33f, z + 1.35f, 0.22f, 0.34f, 0.34f, rim);
	}
	for (int i = 0; i < 2; i++) {
		float fx, fz;
		car_local(w, i ? 0.85f : -0.85f, -1.35f, &fx, &fz);
		Bag *fw = &w->bag[i ? BAG_CAR_FR : BAG_CAR_FL];
		bag_put(fw, fx, 0.33f, fz, 0.24f, 0.66f, 0.66f, tyre);
		bag_put(fw, fx + (i ? 0.02f : -0.02f), 0.33f, fz, 0.22f, 0.34f, 0.34f, rim);
	}
	/* the driver's door, about its hinge (the front edge, on the right) */
	float hx, hz;
	car_local(w, CAR_HALF_W - 0.03f, -0.55f, &hx, &hz);
	Bag *door = &w->bag[BAG_CAR_DOOR];
	bag_put(door, hx, 0.7f, hz + 0.55f, 0.06f, 0.6f, 1.1f, paint);
	bag_put(door, hx - 0.05f, 0.75f, hz + 0.55f, 0.04f, 0.4f, 0.95f, seat); /* its inside panel */
	bag_put(door, hx + 0.035f, 0.85f, hz + 0.85f, 0.02f, 0.03f, 0.14f, trim); /* the handle */
	bag_put(door, hx, 1.37f, hz + 0.55f, 0.05f, 0.04f, 1.1f, paint); /* the window frame */
	bag_put(&w->bag[BAG_CAR_DGLASS], hx, 1.15f, hz + 0.55f, 0.02f, 0.4f, 1.05f, glassc);
	(void)trim;
#undef CB
}

static void car_hinge(const Walk *w, float *hx, float *hz) { car_local(w, CAR_HALF_W - 0.03f, -0.55f, hx, hz); }

/* the car's bags drawn: lit, lamps or glass, as the pass asks */
static void car_draw(const Walk *w, int pass) {
	if (!w->car_ok || w->bld >= 0) {
		return;
	}
	float hx, hz, dyaw = w->car_yaw + w->car_door;
	car_hinge(w, &hx, &hz);
	if (pass == 0) {
		d3_boxes_turned(w->bag[BAG_CAR_BOX].v, w->bag[BAG_CAR_BOX].n, 0.01f, w->car_yaw, w->car_x, w->car_z);
		d3_spheres_turned(w->bag[BAG_CAR_BALL].v, w->bag[BAG_CAR_BALL].n, w->car_yaw, w->car_x, w->car_z);
		for (int i = 0; i < 2; i++) {
			float fx, fz;
			car_local(w, i ? 0.85f : -0.85f, -1.35f, &fx, &fz);
			const Bag *fw = &w->bag[i ? BAG_CAR_FR : BAG_CAR_FL];
			d3_spheres_turned(fw->v, fw->n, w->car_yaw + w->car_steer, fx, fz);
		}
		d3_boxes_turned(w->bag[BAG_CAR_DOOR].v, w->bag[BAG_CAR_DOOR].n, 0.01f, dyaw, hx, hz);
	} else if (pass == 1) {
		d3_boxes_turned(w->bag[BAG_CAR_LAMP].v, w->bag[BAG_CAR_LAMP].n, 0, w->car_yaw, w->car_x, w->car_z);
	} else {
		d3_boxes_turned(w->bag[BAG_CAR_GLASS].v, w->bag[BAG_CAR_GLASS].n, 0, w->car_yaw, w->car_x, w->car_z);
		d3_boxes_turned(w->bag[BAG_CAR_DGLASS].v, w->bag[BAG_CAR_DGLASS].n, 0, dyaw, hx, hz);
	}
}

void walk_update(Walk *w, float dt) {
	if (!w->on) {
		return;
	}
	/* SYN_SHELL_FPS=1: frames a second on stderr while walking, to measure */
	static int frames;
	static Uint32 since;
	if (SDL_getenv("SYN_SHELL_FPS")) {
		frames++;
		if (SDL_GetTicks() - since >= 1000) {
			fprintf(stderr, "walk %p fps %d room %s rooms %d blocks %d pieces %d solids %d at %.1f %.1f %.1f yaw %.2f bld %d fl %d\n", (void *)w, frames, w->path, w->nfroom, w->nblock, w->npiece, w->nsolid, w->px, w->py, w->pz, w->yaw, w->bld, w->floor);
			frames = 0;
			since = SDL_GetTicks();
		}
	}
	const Uint8 *k = walk_keys ? walk_keys : SDL_GetKeyboardState(NULL);
	float ahead = (float)((k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]));
	float right = (float)(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]);
	float turn = (float)(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]); /* E is for using things now */
	bool run = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
	if (!w->car_ok && w->bld < 0 && w->world.nb > 0) {
		car_park(w);
	}
	static bool car_tried; /* SYN_CAR_TEST=1: beside the car's door once it is parked, for trying it */
	if (!car_tried && w->car_ok && w->bld < 0 && SDL_getenv("SYN_CAR_TEST")) {
		float cx, cz;
		car_local(w, CAR_HALF_W + 0.8f, 0.6f, &cx, &cz);
		stand(w, cx, cz, w->car_yaw + 0.9f);
		car_tried = true;
	}
	car_update(w, dt, k);
	if (car_in(w) && w->car_state != CAR_LEFT) {
		w->time += dt; /* in the car: it moves, you sit */
		w->cam_dt = dt;
		w->follow = v3(w->px, w->py, w->pz);
		crew_move(w, dt);
		return;
	}
	if (w->latch) {
		w->latch = ahead != 0 || right != 0;
		ahead = right = 0;
	}
	w->yaw -= turn * 2.2f * dt;

	/* Along the floor: towards the speed the keys ask for, quickly on your
	 * feet (and to a stop when they are let go), less so in the air. */
	Vec3 f = v3(-sinf(w->yaw), 0, -cosf(w->yaw)), r = v3(cosf(w->yaw), 0, -sinf(w->yaw));
	Vec3 mv = v3_add(v3_scale(f, ahead), v3_scale(r, right));
	float len = sqrtf(v3_dot(mv, mv)), top = run ? RUN_V : WALK_V;
	w->wish_x = mv.x;
	w->wish_z = mv.z;
	if (w->bld < 0) {
		city_near(w);
	}
	float wx = len > 0 ? mv.x / len * top : 0, wz = len > 0 ? mv.z / len * top : 0;
	float dvx = wx - w->hvx, dvz = wz - w->hvz, dv = sqrtf(dvx * dvx + dvz * dvz);
	float most = (w->ground ? ACCEL_GROUND : ACCEL_AIR) * dt;
	if (dv > most) {
		dvx *= most / dv;
		dvz *= most / dv;
	}
	w->hvx += dvx;
	w->hvz += dvz;
	float speed = sqrtf(w->hvx * w->hvx + w->hvz * w->hvz);
	if (speed < 0.02f && len == 0) {
		w->hvx = w->hvz = speed = 0;
	}
	w->moving = clampf(speed / WALK_V, 0, 1);
	/* inside a wall or a block (the world made again round you mid-scan,
	 * a floor built round you on the stairs): out the shortest way, or
	 * back to the landing, the door or the square */
	if (stuck_in(w) && !step_clear(w)) {
		if (w->bld < 0) {
			in_square(w);
		} else if (w->floor > 0) {
			at_landing(w, w->at >= 0 ? w->at : w->froom[0]);
		} else {
			at_door(w, w->bld, 1.8f);
		}
		w->hvx = w->hvz = 0;
		w->follow_ok = false;
	}
	if (speed > 0) {
		/* in small steps, so a fast frame never passes through a thin wall;
		 * what you run into stops you along that way only, so you slide along it */
		int steps = 1 + (int)(speed * dt / 0.1f);
		for (int i = 0; i < steps; i++) {
			if (w->hvx != 0 && !move_axis(w, 0, w->hvx * dt / (float)steps)) {
				w->hvx = 0;
			}
			if (w->hvz != 0 && !move_axis(w, 2, w->hvz * dt / (float)steps)) {
				w->hvz = 0;
			}
		}
		if (speed > 0.4f) {
			float want = atan2f(-w->hvx, -w->hvz), diff = want - w->body_yaw;
			while (diff > (float)M_PI) {
				diff -= 2 * (float)M_PI;
			}
			while (diff < -(float)M_PI) {
				diff += 2 * (float)M_PI;
			}
			w->body_yaw += diff * clampf(dt * 12, 0, 1);
		}
		w->stride += speed * dt * 2.2f;
		int n = (int)(w->stride / (float)M_PI); /* a foot down every half stride */
		if (n != w->step_n && w->ground) {
			float run = clampf((speed - WALK_V) / (RUN_V - WALK_V), 0, 1);
			sfx_start(SFX_STEP, 0.3f + 0.35f * run, (w->bld < 0 ? 1.15f : 0.85f) + 0.2f * (float)(n % 3) / 2, 0.14f);
		}
		w->step_n = n;
	}

	/* Up and down. Space a moment before landing still jumps, and so does
	 * Space a moment after walking off an edge; let go on the way up and the
	 * jump is shorter; coming down is faster than going up. */
	w->coyote = w->ground ? COYOTE : w->coyote - dt;
	if (w->jump_early > 0 && w->coyote > 0) {
		w->vy = JUMP_V;
		w->ground = false;
		sfx_start(SFX_JUMP, 0.45f, 1, 0.24f);
		w->coyote = w->jump_early = 0;
	}
	w->jump_early -= dt;
	float g = GRAVITY;
	if (w->vy < 0) {
		g *= FALL_MULT;
	} else if (w->vy > 0 && !k[SDL_SCANCODE_SPACE]) {
		g *= SHORT_HOP;
	}
	float support = floor_under(w, w->px, w->py, w->pz);
	w->vy -= g * dt;
	w->vy = w->vy < -30 ? -30 : w->vy;
	float ny = w->py + w->vy * dt;
	if (w->vy > 0) {
		/* a lintel overhead */
		for (int i = 0, n = solid_n(w); i < n; i++) {
			const Box *b = solid_i(w, i);
			if (w->px + BODY_R > b->lo[0] && w->px - BODY_R < b->hi[0] && w->pz + BODY_R > b->lo[2] &&
			    w->pz - BODY_R < b->hi[2] && b->lo[1] >= w->py + BODY_H - 0.05f && ny + BODY_H > b->lo[1]) {
				ny = b->lo[1] - BODY_H;
				w->vy = 0;
			}
		}
	}
	if (ny <= support) {
		if (!w->ground && w->vy < -3) {
			sfx_start(SFX_LAND, clampf(-w->vy / 14, 0.2f, 1), 1, 0.32f);
		}
		ny = support;
		w->vy = 0;
		w->ground = true;
	} else {
		w->ground = false;
	}
	w->py = ny;
	w->cam_dt = dt;
	/* the camera's point follows you smoothly: quickly along the floor,
	 * a little slower up and down (steps, landings) */
	Vec3 you = v3(w->px, w->py, w->pz);
	if (!w->follow_ok) {
		w->follow = you;
		w->follow_ok = true;
	} else {
		float kxz = 1 - expf(-dt * 22), ky = 1 - expf(-dt * 12);
		w->follow.x += (you.x - w->follow.x) * kxz;
		w->follow.z += (you.z - w->follow.z) * kxz;
		w->follow.y += (you.y - w->follow.y) * ky;
	}
	if (w->fade > 0) {
		w->fade -= dt;
	}
	w->time += dt;
	/* the disks in /dev: reading or writing, from what the kernel counts */
	w->io_wait -= dt;
	for (int i = 0; i < w->nblock; i++) {
		Block *b = &w->block[i];
		if (b->dev != WALK_DEV_DISK) {
			continue;
		}
		b->activity = clampf(b->activity - dt * 2.5f, 0, 1);
		if (w->io_wait > 0) {
			continue;
		}
		char sys[600];
		snprintf(sys, sizeof sys, "/sys/class/block/%s/stat", b->name);
		FILE *f = fopen(sys, "r");
		unsigned long long v[7] = {0};
		if (f) {
			if (fscanf(f, "%llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7) {
				unsigned long long io = v[2] + v[6];
				if (b->io && io != b->io) {
					b->activity = 1;
				}
				b->io = io;
			}
			fclose(f);
		}
	}
	if (w->io_wait <= 0) {
		w->io_wait = 0.25f;
	}
	crew_test(w);
	crew_move(w, dt);
	if (step_floor(w)) {
		return;
	}
	/* mid-scan: the world again once the tree has grown enough */
	w->since_built += dt;
	if (w->since_built > 3) {
		w->since_built = 0;
		if (world_stale(&w->world) || (w->world.nextra == 0 && crew_ready() && crew_here(w->world.root))) {
			world_again(w); /* grown, or the crew's offices to stand in it now */
		}
	}
}

/* ---- input ---- */

static void buttons(const Walk *w, SDL_Rect *view_b, SDL_Rect *map_b) {
	int th = d2_text_height(), bw1 = d2_text_width("3RD PERSON (V)") + 24, bw2 = d2_text_width("BACK TO MAP (Esc)") + 24;
	int bh = th + 14, right = (int)(w->vx + w->vw) - 10, top = (int)w->vy0 + 10;
	*map_b = (SDL_Rect){right - bw2, top, bw2, bh};
	*view_b = (SDL_Rect){right - bw2 - 8 - bw1, top, bw1, bh};
}

static bool in(SDL_Rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static void use_key(Walk *w);

int walk_key(Walk *w, SDL_Keycode k) {
	if (k == SDLK_ESCAPE) {
		if (w->captured) {
			walk_release(w); /* first the mouse back, as any game; again, the map */
			return WALK_NONE;
		}
		return WALK_EXIT;
	}
	if (k == SDLK_SPACE) {
		w->jump_early = JUMP_EARLY;
	} else if ((k == SDLK_v || k == SDLK_F5) && car_in(w)) {
		w->car_chase = !w->car_chase;
		w->car_cam_y = 0;
	} else if (k == SDLK_v || k == SDLK_F5) {
		w->first = !w->first;
		w->pitch = w->first ? 0 : 0.3f;
		w->cam_len = 0;
	} else if (k == SDLK_e || k == SDLK_f) {
		if (!car_use(w)) {
			use_key(w);
		}
	}
	return WALK_NONE;
}

int walk_mouse_down(Walk *w, float x, float y, int button) {
	if (!w->captured) {
		SDL_Rect vb, mb;
		buttons(w, &vb, &mb);
		if (button == SDL_BUTTON_LEFT && in(mb, x, y)) {
			return WALK_EXIT;
		}
		if (button == SDL_BUTTON_LEFT && in(vb, x, y)) {
			w->first = !w->first;
			w->pitch = w->first ? 0 : 0.3f;
			w->cam_len = 0;
			return WALK_NONE;
		}
		/* anywhere else: the window takes the mouse and it turns you, no
		 * button to hold, till Esc gives it back */
		w->captured = w->look = true;
		SDL_SetRelativeMouseMode(SDL_TRUE);
	}
	return WALK_NONE;
}

void walk_mouse_up(Walk *w, int button) {
	(void)w;
	(void)button; /* held or not, the captured mouse keeps looking */
}

bool walk_captured(const Walk *w) { return w && w->on && w->captured; }

void walk_release(Walk *w) {
	if (w->captured) {
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}
	w->captured = w->look = false;
}

static void turn_by(Walk *w, float dyaw, float dpitch) {
	w->yaw -= dyaw;
	w->pitch += dpitch;
	w->pitch = w->first ? clampf(w->pitch, -1.45f, 1.45f) : clampf(w->pitch, -0.7f, 1.3f);
}

void walk_mouse_move(Walk *w, float dx, float dy) {
	if (w->look) {
		turn_by(w, dx * 0.0035f, dy * 0.0035f);
	}
}

void walk_look(Walk *w, float dx, float dy) {
	if (w->on) {
		turn_by(w, dx * LOOK_RAD, dy * LOOK_RAD);
	}
}

void walk_wheel(Walk *w, int notches) {
	if (!w->first) {
		w->dist = clampf(w->dist * (notches > 0 ? 0.88f : 1.14f), 1.8f, 14);
	}
}

/* ---- drawing ---- */

static SDL_Color lighten(SDL_Color c, float t) { return mix(c, (SDL_Color){255, 255, 255, 255}, t); }
static SDL_Color darken(SDL_Color c, float t) { return mix(c, (SDL_Color){0, 0, 0, 255}, t); }

static Vec3 look_dir(const Walk *w) {
	return v3(-sinf(w->yaw) * cosf(w->pitch), -sinf(w->pitch), -cosf(w->yaw) * cosf(w->pitch));
}

/* how far along o + v (0 .. 1) it first meets b grown by off; 2 for never */
static float seg_box(const float o[3], const float v[3], const Box *b, float off) {
	float enter = 0, leave = 1;
	for (int k = 0; k < 3 && enter <= leave; k++) {
		float lo = b->lo[k] - off, hi = b->hi[k] + off;
		if (fabsf(v[k]) < 1e-6f) {
			if (o[k] < lo || o[k] > hi) {
				return 2;
			}
			continue;
		}
		float t0 = (lo - o[k]) / v[k], t1 = (hi - o[k]) / v[k];
		if (t0 > t1) {
			float x = t0;
			t0 = t1;
			t1 = x;
		}
		enter = t0 > enter ? t0 : enter;
		leave = t1 < leave ? t1 : leave;
	}
	return enter <= leave ? enter : 2;
}

/* how far the camera can go back from p along d (0 .. 1): stopped by
 * walls, window panes and anything that stands higher than p, and by the
 * floor; low furniture it looks over, so it never jumps about over desks */
static float cam_clear(const Walk *w, Vec3 p, Vec3 d) {
	float o[3] = {p.x, p.y, p.z}, v[3] = {d.x, d.y, d.z}, best = 1;
	if (d.y < -1e-4f) {
		float t = (p.y - w->py - 0.35f) / -d.y; /* nor down through what you stand on */
		best = t < best ? t : best;
	}
	if (d.y > 1e-4f && w->bld >= 0) {
		/* nor up through the ceiling; up a stairwell, the one over it */
		float top = WALL_H - 0.3f;
		for (int i = 0; i < w->nups; i++) {
			top = in_rect(w->ups[i], p.x, p.z) ? FLOOR_H + WALL_H - 0.3f : top;
		}
		float t = (top - p.y) / d.y;
		best = t < best ? t : best;
	}
	for (int i = 0, n = solid_n(w); i < n; i++) {
		const Box *b = solid_i(w, i);
		if (i >= w->nwin && (b->hi[1] < p.y + 0.2f || (i < w->nstruct && w->bld >= 0))) {
			continue; /* a block lower than your head; the floors, stairs and rails */
		}
		float t = seg_box(o, v, b, 0.2f);
		best = t < best ? t : best;
		if (w->bld < 0 && b->hi[0] - b->lo[0] < 0.5f && b->hi[2] - b->lo[2] < 0.5f && b->hi[1] > 2.4f) {
			/* a tree's trunk (or a lamp post): its crown, which nothing runs into, kept out of too */
			float cx = (b->lo[0] + b->hi[0]) / 2, cz = (b->lo[2] + b->hi[2]) / 2;
			Box crown = box(cx - 1.3f, 1.9f, cz - 1.3f, cx + 1.3f, b->hi[1] + 0.4f, cz + 1.3f);
			t = seg_box(o, v, &crown, 0.1f);
			best = t < best ? t : best;
		}
	}
	return best < 0 ? 0 : best;
}

static void camera(Walk *w, Vec3 *eye, Vec3 *at) {
	if (car_in(w) && w->car_state != CAR_GETTING_IN && w->car_state != CAR_LEFT) {
		car_camera(w, eye, at);
		return;
	}
	Vec3 f = look_dir(w);
	float fy = w->follow_ok ? w->follow.y : w->py;
	if (w->first) {
		/* your own eyes, a small bob as you walk */
		float bob = w->ground ? fabsf(sinf(w->stride)) * 0.045f * w->moving : 0;
		*eye = v3(w->px, fy + EYE_H - 0.03f + bob, w->pz);
		*at = v3_add(*eye, f);
		return;
	}
	/* over your shoulder, always straight back along the way you look: a
	 * wall or something tall behind pulls it in at once (it would show
	 * through otherwise), and it eases back out once clear */
	*at = w->follow_ok ? v3(w->follow.x, fy + 1.45f, w->follow.z) : v3(w->px, w->py + 1.45f, w->pz);
	Vec3 back = v3_scale(f, -w->dist);
	float want = cam_clear(w, *at, back) * w->dist, now = w->cam_len > 0 ? w->cam_len : want;
	now = want < now ? want : now + (want - now) * (1 - expf(-w->cam_dt * 5.0f)); /* the same at any frame rate */
	w->cam_len = now;
	*eye = v3_add(*at, v3_scale(back, now / w->dist));
}

/* the labels drawn this frame, so a further one never covers a nearer */
#define MAX_TAGS 160
static SDL_FRect tags[MAX_TAGS];
static int ntags;

/* text with a box behind it, its middle at (cx, y); false, not drawn, where a label already is */
static bool tag(float cx, float y, const char *s, SDL_Color fg, SDL_Color bg, SDL_Color edge) {
	float tw = (float)d2_text_width(s), th = (float)d2_text_height();
	SDL_FRect r = {cx - tw / 2 - 5, y - 2, tw + 10, th + 4};
	for (int i = 0; i < ntags; i++) {
		const SDL_FRect *o = &tags[i];
		if (r.x < o->x + o->w && o->x < r.x + r.w && r.y < o->y + o->h && o->y < r.y + r.h) {
			return false;
		}
	}
	if (ntags < MAX_TAGS) {
		tags[ntags++] = r;
	}
	d2_rect(cx - tw / 2 - 5, y - 2, tw + 10, th + 4, bg);
	d2_frame(cx - tw / 2 - 5, y - 2, tw + 10, th + 4, 1, edge);
	d2_text(cx - tw / 2, y, s, fg);
	return true;
}

static int nearer(const void *pa, const void *pb) {
	float a = ((const Near *)pa)->d, b = ((const Near *)pb)->d;
	return a < b ? -1 : a > b;
}

/* ---- the room's things, by how each is drawn ---- */

static void bag_put(Bag *g, float x, float y, float z, float sx, float sy, float sz, SDL_Color c) {
	if (g->n == g->cap) {
		g->cap = g->cap ? g->cap * 2 : 256;
		g->v = SDL_realloc(g->v, (size_t)g->cap * sizeof *g->v);
	}
	Solid *o = &g->v[g->n++];
	o->pos[0] = x;
	o->pos[1] = y;
	o->pos[2] = z;
	o->size[0] = sx;
	o->size[1] = sy;
	o->size[2] = sz;
	memcpy(o->rgba, &c, 4);
}

static void bag_box(Walk *w, int bag, Box b, SDL_Color c) {
	bag_put(&w->bag[bag], (b.lo[0] + b.hi[0]) / 2, (b.lo[1] + b.hi[1]) / 2, (b.lo[2] + b.hi[2]) / 2, b.hi[0] - b.lo[0],
	        b.hi[1] - b.lo[1], b.hi[2] - b.lo[2], c);
}

static void pic_add(Walk *w, unsigned tex, bool lamp, float q[4][3], SDL_Color c) {
	if (w->npic == w->pic_cap) {
		w->pic_cap = w->pic_cap ? w->pic_cap * 2 : 128;
		w->pic = SDL_realloc(w->pic, (size_t)w->pic_cap * sizeof *w->pic);
	}
	Pic *p = &w->pic[w->npic++];
	p->tex = tex;
	p->lamp = lamp;
	memcpy(p->q.p, q, sizeof p->q.p);
	memcpy(p->q.rgba, &c, 4);
}

static float rnd(unsigned *s) {
	*s = *s * 1664525u + 1013904223u;
	return (float)(*s >> 8) / 16777216.0f;
}

/* A block's side, as seen from in front of it: where u = 0 is (its left
 * edge, on the floor), which way u runs, which way it faces, how wide. */
typedef struct {
	Vec3 o, r, n;
	float w;
} Face;

/* f: 0 the -z side, 1 +x, 2 +z, 3 -x; out: that far in front of it */
static Face face_of(const Box *b, int f, float out) {
	switch (f) {
	case 0: return (Face){v3(b->hi[0], 0, b->lo[2] - out), v3(-1, 0, 0), v3(0, 0, -1), b->hi[0] - b->lo[0]};
	case 1: return (Face){v3(b->hi[0] + out, 0, b->hi[2]), v3(0, 0, -1), v3(1, 0, 0), b->hi[2] - b->lo[2]};
	case 2: return (Face){v3(b->lo[0], 0, b->hi[2] + out), v3(1, 0, 0), v3(0, 0, 1), b->hi[0] - b->lo[0]};
	default: return (Face){v3(b->lo[0] - out, 0, b->lo[2]), v3(0, 0, 1), v3(-1, 0, 0), b->hi[2] - b->lo[2]};
	}
}

static Vec3 face_at(const Face *f, float u, float y) { return v3(f->o.x + f->r.x * u * f->w, y, f->o.z + f->r.z * u * f->w); }

/* a panel on a side: u0 .. u1 across it, y0 .. y1 up, from in behind its face to out in front */
static Box face_box(const Box *b, int f, float u0, float u1, float y0, float y1, float in, float out) {
	Face a = face_of(b, f, -in), c = face_of(b, f, out);
	Vec3 p0 = face_at(&a, u0, y0), p1 = face_at(&c, u1, y1);
	return box(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
}

static SDL_Color hue(float h, float s, float v) {
	h = h - floorf(h);
	float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
	r = clampf(r, 0, 1);
	g = clampf(g, 0, 1);
	b = clampf(b, 0, 1);
	return (SDL_Color){(Uint8)(255 * v * (1 - s + s * r)), (Uint8)(255 * v * (1 - s + s * g)), (Uint8)(255 * v * (1 - s + s * b)), 255};
}

/* ---- furniture: each file a thing of its kind, made of parts ---- */

/* A piece's own frame, in metres: a across its front from the left as you
 * face it, d back from its front (below 0: in front of it), y up. */
typedef struct {
	Vec3 o, r, n; /* the front's left end on the floor; to the right; out of the front */
	float w, d;   /* how wide, how deep */
} Loc;

/* what the pieces are made of, in the theme's colours; set for each file */
static struct {
	SDL_Color kc, wood, oak, metal, steel, dark, black, paper, white, accent, floor, screen;
	float time;
	float ex, ez; /* where you are, for leaving out what is too small to see from there */
	bool fine;    /* near enough for the small parts: print, keys, ports, bands on spines */
	float spines; /* books on shelves this much wider than they would be, to keep their count down */
} M;

static const SDL_Color LED_GREEN = {70, 235, 110, 255}, LED_AMBER = {250, 180, 50, 255}, LED_RED = {245, 70, 60, 255},
                       LED_BLUE = {80, 160, 255, 255}, WARM = {255, 214, 150, 255};

static void materials(const Block *k, const Theme *t, float time) {
	static const SDL_Color woods[4] = {{122, 82, 48, 255}, {152, 110, 68, 255}, {84, 56, 36, 255}, {176, 138, 96, 255}};
	M.kc = kind_colour(k->kind, t);
	M.wood = mix(woods[(k->seed * 2654435761u) >> 30], M.kc, 0.1f);
	M.oak = lighten(M.wood, 0.16f);
	M.metal = mix(rgb(t->panel), (SDL_Color){150, 156, 162, 255}, 0.55f);
	M.steel = lighten(M.metal, 0.3f);
	M.dark = mix(rgb(t->panel), (SDL_Color){34, 36, 40, 255}, 0.65f);
	M.black = darken(rgb(t->bg_alt), 0.6f);
	M.paper = (SDL_Color){236, 230, 214, 255};
	M.white = (SDL_Color){255, 255, 255, 255};
	M.accent = lighten(rgb(t->accent), 0.2f);
	M.floor = mix(rgb(t->bg_alt), rgb(t->panel_hover), 0.5f);
	M.screen = mix(rgb(t->bg), rgb(t->accent), 0.1f);
	M.time = time;
	M.spines = 1;
}

static Loc loc_of(const Box *b, int f, float depth) {
	Face F = face_of(b, f, 0);
	return (Loc){F.o, F.r, F.n, F.w, depth};
}

static Vec3 lp(const Loc *L, float a, float d, float y) {
	return v3(L->o.x + L->r.x * a - L->n.x * d, y, L->o.z + L->r.z * a - L->n.z * d);
}

/* Too small to see from where you are: a part whose middle size of its
 * three is under a couple of pixels that far away (a light, a little less;
 * a round part, which costs fifty boxes' drawing, sooner). */
static bool unseen(int bag, Vec3 p, float sx, float sy, float sz) {
	float lo = fminf(sx, fminf(sy, sz)), hi = fmaxf(sx, fmaxf(sy, sz)), mid = sx + sy + sz - lo - hi;
	float dx = p.x - M.ex, dz = p.z - M.ez, far = dx * dx + dz * dz;
	float k = bag == BAG_LAMP ? 0.0005f : bag == BAG_BALL_LAMP ? 0.002f : bag == BAG_BALL ? 0.0035f : 0.0012f;
	return mid * mid < far * k * k;
}

static void lbox(Walk *w, int bag, const Loc *L, float a0, float a1, float d0, float d1, float y0, float y1, SDL_Color c) {
	Vec3 p = lp(L, a0, d0, y0), q = lp(L, a1, d1, y1);
	if (unseen(bag, p, fabsf(q.x - p.x), fabsf(q.y - p.y), fabsf(q.z - p.z))) {
		return;
	}
	bag_box(w, bag, box(p.x, p.y, p.z, q.x, q.y, q.z), c);
}

/* round, about a, d, y: sa across, sd deep, sy tall */
static void lball(Walk *w, int bag, const Loc *L, float a, float d, float y, float sa, float sd, float sy, SDL_Color c) {
	Vec3 p = lp(L, a, d, y);
	bool ax = fabsf(L->r.x) > 0.5f;
	if (unseen(bag, p, sa, sd, sy)) {
		return;
	}
	float dx = p.x - M.ex, dz = p.z - M.ez;
	if (dx * dx + dz * dz > 12 * 12) { /* far off a small round thing looks no different as a box, and costs far less */
		bag = bag == BAG_BALL ? BAG_LIT : bag == BAG_BALL_LAMP ? BAG_LAMP : bag == BAG_BALL_GLOW ? BAG_GLOW : bag;
		sa *= 0.8f;
		sd *= 0.8f;
		sy *= 0.8f;
	}
	bag_put(&w->bag[bag], p.x, p.y, p.z, ax ? sa : sd, sy, ax ? sd : sa, c);
}

/* a picture standing up, facing out of the front, at depth d */
static void lpic(Walk *w, const Loc *L, unsigned tex, bool lamp, float a0, float a1, float y0, float y1, float d, SDL_Color c) {
	Vec3 p[4] = {lp(L, a0, d, y0), lp(L, a1, d, y0), lp(L, a1, d, y1), lp(L, a0, d, y1)};
	float q[4][3];
	for (int i = 0; i < 4; i++) {
		q[i][0] = p[i].x;
		q[i][1] = p[i].y;
		q[i][2] = p[i].z;
	}
	pic_add(w, tex, lamp, q, c);
}

/* a picture lying flat at height y, its top away from you */
static void lpic_flat(Walk *w, const Loc *L, unsigned tex, float a0, float a1, float d0, float d1, float y, SDL_Color c) {
	Vec3 p[4] = {lp(L, a0, d0, y), lp(L, a1, d0, y), lp(L, a1, d1, y), lp(L, a0, d1, y)};
	float q[4][3];
	for (int i = 0; i < 4; i++) {
		q[i][0] = p[i].x;
		q[i][1] = p[i].y;
		q[i][2] = p[i].z;
	}
	pic_add(w, tex, false, q, c);
}

static float pick(unsigned *s, float lo, float hi) { return lo + (hi - lo) * rnd(s); }

/* on for part of each beat: rate beats a second, from its own phase */
static bool blink(unsigned *s, float rate, float on) { return fmodf(M.time * rate + rnd(s) * 7, 1.0f) < on; }

/* How many pieces about want wide fit across width, 1 .. most. */
static int across(float width, float want, int most) {
	int n = (int)(width / want + 0.5f);
	return n < 1 ? 1 : n > most ? most : n;
}

/* the dark the piece throws on the floor round its foot */
static void shadow(Walk *w, const Box *b) {
	float g = 0.06f;
	bag_box(w, BAG_LIT, box(b->lo[0] - g, 0, b->lo[2] - g, b->hi[0] + g, 0.003f, b->hi[2] + g), darken(M.floor, 0.22f));
	bag_box(w, BAG_LIT, box(b->lo[0] + 0.01f, 0, b->lo[2] + 0.01f, b->hi[0] - 0.01f, 0.005f, b->hi[2] - 0.01f), darken(M.floor, 0.5f));
}

static SDL_Color book_colour(unsigned *s) {
	static const SDL_Color c[12] = {{128, 36, 40, 255},  {36, 70, 120, 255},   {40, 96, 62, 255},  {160, 116, 40, 255},
	                                {90, 50, 100, 255},  {34, 34, 38, 255},    {176, 156, 124, 255}, {60, 110, 130, 255},
	                                {186, 84, 40, 255},  {110, 30, 50, 255},   {206, 196, 176, 255}, {74, 84, 40, 255}};
	SDL_Color x = c[(int)(rnd(s) * 12) % 12];
	x = rnd(s) < 0.5f ? lighten(x, rnd(s) * 0.2f) : darken(x, rnd(s) * 0.25f);
	return mix(x, M.kc, 0.1f);
}

/* text on a page or a screen: short lines of ink, la across, from the top down */
static void ink_lines(Walk *w, int bag, const Loc *L, float a0, float a1, float d0, float d1, float y, int rows, SDL_Color c, unsigned *s) {
	if (!M.fine) {
		return;
	}
	float step = (d1 - d0) / (float)rows;
	for (int i = 0; i < rows; i++) {
		float len = rnd(s) < 0.2f ? pick(s, 0.2f, 0.5f) : pick(s, 0.75f, 1.0f);
		float z = d1 - step * ((float)i + 0.5f);
		lbox(w, bag, L, a0, a0 + (a1 - a0) * len, z - step * 0.16f, z + step * 0.16f, y, y + 0.0012f, c);
	}
}

/* a book lying flat from corner a, d at y: la across, ld deep, th thick;
 * its spine to the front, or the pages */
static void book_flat(Walk *w, const Loc *L, float a, float d, float y, float la, float ld, float th, SDL_Color c, bool spine_front) {
	float cv = fminf(0.004f, th * 0.18f);
	if (!M.fine) {
		lbox(w, BAG_LIT, L, a, a + la, d, d + ld, y, y + th, c);
		return;
	}
	lbox(w, BAG_LIT, L, a, a + la, d, d + ld, y, y + cv, c);
	lbox(w, BAG_LIT, L, a, a + la, d, d + ld, y + th - cv, y + th, c);
	if (spine_front) {
		lbox(w, BAG_LIT, L, a, a + la, d, d + cv * 1.5f, y, y + th, darken(c, 0.12f));
		lbox(w, BAG_LIT, L, a + 0.004f, a + la - 0.004f, d + cv, d + ld - 0.004f, y + cv, y + th - cv, M.paper);
		if (la > 0.08f) { /* a band across the spine */
			lbox(w, BAG_LIT, L, a + la * 0.12f, a + la * 0.18f, d - 0.001f, d + 0.004f, y + cv, y + th - cv, lighten(c, 0.45f));
		}
	} else {
		lbox(w, BAG_LIT, L, a, a + la, d + ld - cv * 1.5f, d + ld, y, y + th, darken(c, 0.12f));
		lbox(w, BAG_LIT, L, a + 0.004f, a + la - 0.004f, d + 0.004f, d + ld - cv, y + cv, y + th - cv, M.paper);
	}
}

/* books lying in a pile from corner a, d; the height of the pile back */
static float book_pile(Walk *w, const Loc *L, float a, float d, float y, float sc, int n, unsigned *s) {
	float y0 = y;
	for (int i = 0; i < n; i++) {
		float la = pick(s, 0.16f, 0.24f) * sc, ld = pick(s, 0.2f, 0.28f) * sc, th = pick(s, 0.02f, 0.05f) * sc;
		float oa = pick(s, -0.012f, 0.012f) * sc, od = pick(s, -0.01f, 0.01f) * sc;
		book_flat(w, L, a + oa, d + od, y, la, ld, th, book_colour(s), rnd(s) < 0.75f);
		y += th;
	}
	return y - y0;
}

/* an open book, its left page's corner at a, d: pages of lines, or the
 * file's own page on the right */
static void open_book(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned tex, unsigned *s) {
	float pa = 0.16f * sc, pd = 0.23f * sc;
	SDL_Color c = book_colour(s), ink = {70, 70, 78, 255};
	lbox(w, BAG_LIT, L, a - 0.006f, a + 2 * pa + 0.006f, d - 0.006f, d + pd + 0.006f, y, y + 0.004f, c);
	lbox(w, BAG_LIT, L, a, a + pa, d, d + pd, y + 0.004f, y + 0.013f, M.paper);
	lbox(w, BAG_LIT, L, a + pa, a + 2 * pa, d, d + pd, y + 0.004f, y + 0.015f, lighten(M.paper, 0.3f));
	lbox(w, BAG_LIT, L, a + pa - 0.003f, a + pa + 0.003f, d - 0.002f, d + pd + 0.002f, y + 0.004f, y + 0.0105f, darken(M.paper, 0.35f));
	ink_lines(w, BAG_LIT, L, a + pa * 0.12f, a + pa * 0.88f, d + pd * 0.08f, d + pd * 0.92f, y + 0.013f, 11, ink, s);
	if (tex) {
		lpic_flat(w, L, tex, a + pa * 1.06f, a + pa * 1.94f, d + pd * 0.04f, d + pd * 0.96f, y + 0.0155f, M.white);
	} else {
		ink_lines(w, BAG_LIT, L, a + pa * 1.12f, a + pa * 1.88f, d + pd * 0.08f, d + pd * 0.92f, y + 0.015f, 11, ink, s);
	}
	/* a ribbon to keep the place */
	lbox(w, BAG_LIT, L, a + pa * 1.5f, a + pa * 1.56f, d - 0.03f * sc, d + pd * 0.6f, y + 0.0155f, y + 0.017f, LED_RED);
}

/* a pile of paper, A4 at sc 1, the top sheet written on */
static void papers(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned *s) {
	float pa = 0.21f * sc, pd = 0.297f * sc, hh = pick(s, 0.008f, 0.045f) * sc;
	lbox(w, BAG_LIT, L, a, a + pa, d, d + pd, y, y + hh, darken(M.paper, 0.06f));
	for (int i = 0; i < 2; i++) {
		float oa = pick(s, -0.02f, 0.02f) * sc, od = pick(s, -0.015f, 0.015f) * sc;
		lbox(w, BAG_LIT, L, a + oa, a + oa + pa, d + od, d + od + pd, y + hh, y + hh + 0.002f, i ? M.white : M.paper);
		hh += 0.002f;
		if (i) {
			ink_lines(w, BAG_LIT, L, a + oa + pa * 0.12f, a + oa + pa * 0.88f, d + od + pd * 0.1f, d + od + pd * 0.9f, y + hh,
			          9, (SDL_Color){90, 90, 100, 255}, s);
		}
	}
}

/* a closed card folder, papers showing at its side, its tab at the back */
static void folder(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned *s) {
	static const SDL_Color card[4] = {{214, 184, 120, 255}, {200, 92, 70, 255}, {80, 130, 190, 255}, {110, 170, 110, 255}};
	SDL_Color c = card[(int)(rnd(s) * 4) % 4];
	float fa = 0.24f * sc, fd = 0.31f * sc, th = pick(s, 0.008f, 0.025f) * sc;
	lbox(w, BAG_LIT, L, a, a + fa, d, d + fd, y, y + 0.003f, c);
	lbox(w, BAG_LIT, L, a + 0.01f * sc, a + fa + 0.015f * sc, d + 0.012f * sc, d + fd - 0.015f * sc, y + 0.003f, y + th, M.paper);
	lbox(w, BAG_LIT, L, a, a + fa, d, d + fd, y + th, y + th + 0.003f, c);
	lbox(w, BAG_LIT, L, a + fa * 0.55f, a + fa * 0.85f, d + fd - 0.002f, d + fd + 0.022f * sc, y + th, y + th + 0.003f, c);
	lbox(w, BAG_LIT, L, a + fa * 0.18f, a + fa * 0.62f, d + fd * 0.12f, d + fd * 0.26f, y + th + 0.003f, y + th + 0.0045f, M.white);
	if (rnd(s) < 0.4f) { /* an elastic band round it */
		lbox(w, BAG_LIT, L, a - 0.002f, a + fa + 0.002f, d + fd * 0.7f, d + fd * 0.73f, y - 0.001f, y + th + 0.005f, M.black);
	}
}

/* ring binders standing in a row from a, held up by a bookend; the width back */
static float binders(Walk *w, const Loc *L, float a, float d, float y, float sc, int n, unsigned *s) {
	float a0 = a, h = 0.31f * sc, dp = 0.27f * sc;
	SDL_Color c = book_colour(s);
	for (int i = 0; i < n; i++) {
		float bw = pick(s, 0.04f, 0.075f) * sc;
		if (rnd(s) < 0.35f) {
			c = book_colour(s);
		}
		lbox(w, BAG_LIT, L, a, a + bw, d, d + dp, y, y + h, c);
		if (M.fine) {
			lbox(w, BAG_LIT, L, a + bw * 0.16f, a + bw * 0.84f, d - 0.002f, d + 0.004f, y + h * 0.5f, y + h * 0.86f, M.paper);
			lball(w, BAG_BALL, L, a + bw / 2, d - 0.001f, y + h * 0.24f, bw * 0.5f, 0.008f, bw * 0.5f, darken(c, 0.6f));
		}
		a += bw + 0.002f;
	}
	lbox(w, BAG_LIT, L, a, a + 0.004f, d + dp * 0.15f, d + dp * 0.85f, y, y + h * 0.6f, M.steel);
	lbox(w, BAG_LIT, L, a - 0.05f * sc, a + 0.004f, d + dp * 0.15f, d + dp * 0.85f, y, y + 0.003f, M.steel);
	return a + 0.004f - a0;
}

/* a mug of something, its handle to the right */
static void mug(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned *s) {
	SDL_Color c = rnd(s) < 0.5f ? M.white : hue(rnd(s), 0.5f, 0.75f);
	float r = 0.04f * sc, h = 0.095f * sc;
	lbox(w, BAG_LIT, L, a - r, a + r, d - r, d + r, y, y + h, c);
	lbox(w, BAG_LIT, L, a - r * 0.8f, a + r * 0.8f, d - r * 0.8f, d + r * 0.8f, y + h, y + h + 0.0015f, (SDL_Color){60, 36, 22, 255});
	lbox(w, BAG_LIT, L, a + r, a + r + 0.022f * sc, d - 0.006f, d + 0.006f, y + h * 0.25f, y + h * 0.8f, c);
}

/* a pot of pens and pencils */
static void pens(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned *s) {
	float r = 0.035f * sc;
	lbox(w, BAG_LIT, L, a - r, a + r, d - r, d + r, y, y + 0.1f * sc, M.dark);
	for (int i = 0; i < 4; i++) {
		float pa = a + pick(s, -0.6f, 0.6f) * r, pd = d + pick(s, -0.6f, 0.6f) * r;
		lbox(w, BAG_LIT, L, pa - 0.004f, pa + 0.004f, pd - 0.004f, pd + 0.004f, y, y + pick(s, 0.13f, 0.17f) * sc,
		     i == 0 ? LED_AMBER : hue(rnd(s), 0.6f, 0.8f));
	}
}

/* a plant in a clay pot */
static void plant(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned *s) {
	float r = 0.06f * sc, h = 0.11f * sc;
	SDL_Color clay = {176, 92, 60, 255}, leaf = {70, 140, 70, 255};
	lbox(w, BAG_LIT, L, a - r * 0.85f, a + r * 0.85f, d - r * 0.85f, d + r * 0.85f, y, y + h, clay);
	lbox(w, BAG_LIT, L, a - r, a + r, d - r, d + r, y + h * 0.8f, y + h, lighten(clay, 0.1f));
	lbox(w, BAG_LIT, L, a - r * 0.85f, a + r * 0.85f, d - r * 0.85f, d + r * 0.85f, y + h, y + h + 0.002f, (SDL_Color){50, 36, 26, 255});
	for (int i = 0; i < 6; i++) {
		float la = a + pick(s, -1, 1) * r, ld = d + pick(s, -1, 1) * r, ly = y + h + pick(s, 0.03f, 0.13f) * sc;
		float sz = pick(s, 0.05f, 0.09f) * sc;
		lball(w, BAG_BALL, L, la, ld, ly, sz, sz, sz * 0.7f, mix(leaf, (SDL_Color){120, 170, 60, 255}, rnd(s)));
	}
}

/* a desk lamp on its post, reaching forward, lit */
static void desk_lamp(Walk *w, const Loc *L, float a, float d, float y, float sc, unsigned *s) {
	SDL_Color c = rnd(s) < 0.5f ? M.dark : hue(rnd(s), 0.55f, 0.7f);
	float t = 0.009f * sc;
	lball(w, BAG_BALL, L, a, d, y + 0.012f * sc, 0.15f * sc, 0.15f * sc, 0.03f * sc, c);
	lbox(w, BAG_LIT, L, a - t, a + t, d - t, d + t, y, y + 0.38f * sc, c);
	lbox(w, BAG_LIT, L, a - t, a + t, d - 0.22f * sc, d + t, y + 0.37f * sc, y + 0.37f * sc + 2 * t, c);
	lball(w, BAG_BALL, L, a, d - 0.22f * sc, y + 0.35f * sc, 0.14f * sc, 0.14f * sc, 0.1f * sc, c);
	lball(w, BAG_BALL_LAMP, L, a, d - 0.22f * sc, y + 0.31f * sc, 0.07f * sc, 0.07f * sc, 0.03f * sc, WARM);
	SDL_Color pool = WARM;
	pool.a = 35;
	lball(w, BAG_BALL_GLOW, L, a, d - 0.22f * sc, y + 0.003f, 0.34f * sc, 0.34f * sc, 0.004f, pool);
}

/* Books, papers and files over a desk's top, a0 .. a1 across and d0 .. d1
 * deep at y: piles of books, an open book (the file's own page on it when
 * there is one), paper stacks, folders, binders, and now and then a mug, a
 * lamp, pens or a plant. busy 0 .. 1: how much of it is books and paper. */
static void clutter(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y, float busy, unsigned tex, unsigned *s) {
	float wd = a1 - a0, dp = d1 - d0;
	if (wd < 0.14f || dp < 0.14f) {
		return;
	}
	float sc = clampf(fminf(wd, dp) / 0.7f, 0.4f, 1.0f);
	int na = (int)(wd / (0.38f * sc)), nd = (int)(dp / (0.36f * sc));
	na = na < 1 ? 1 : na > 10 ? 10 : na;
	nd = nd < 1 ? 1 : nd > 5 ? 5 : nd;
	float ca = wd / (float)na, cd = dp / (float)nd;
	int book_at = busy > 0.5f ? na / 2 : -1, lamp_at = na * nd >= 4 && rnd(s) < 0.75f ? na * nd - 1 : -1;
	for (int j = 0; j < nd; j++) {
		for (int i = 0; i < na; i++) {
			float a = a0 + ca * (float)i + 0.02f * sc, d = d0 + cd * (float)j + 0.02f * sc;
			float r = rnd(s), ma = a + ca / 2, md = d + cd / 2;
			if (j == 0 && i == book_at) {
				open_book(w, L, ma - 0.16f * sc, d + 0.01f, y, sc, tex, s);
			} else if (j * na + i == lamp_at) {
				desk_lamp(w, L, ma, md + 0.06f * sc, y, sc, s);
			} else if (r < 0.22f - 0.12f * busy) {
				/* bare */
			} else if (r < 0.42f) {
				book_pile(w, L, a + pick(s, 0, 0.04f) * sc, d, y, sc, 1 + (int)(rnd(s) * (2 + 4 * busy)), s);
			} else if (r < 0.56f) {
				papers(w, L, a + pick(s, 0, 0.06f) * sc, d, y, sc, s);
			} else if (r < 0.68f) {
				int n = 1 + (int)(rnd(s) * 3);
				for (int f = 0; f < n; f++) {
					folder(w, L, a + pick(s, 0, 0.05f) * sc, d + pick(s, 0, 0.02f) * sc, y + (float)f * 0.02f * sc, sc, s);
				}
			} else if (r < 0.78f && j == nd - 1) {
				binders(w, L, a, d + cd - 0.3f * sc, y, sc, 2 + (int)(rnd(s) * 3), s);
			} else if (r < 0.86f) {
				mug(w, L, ma, md, y, sc, s);
				if (rnd(s) < 0.5f) {
					papers(w, L, a, d, y, sc * 0.8f, s);
				}
			} else if (r < 0.93f) {
				pens(w, L, ma, md, y, sc, s);
			} else {
				plant(w, L, ma, md, y, sc, s);
			}
		}
	}
}

/* A desk, its top at y over a0 .. a1 across and d0 .. d1 deep: on four legs,
 * or with drawers down one end when there is room for them. */
static void desk(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y, unsigned *s) {
	float wd = a1 - a0, dp = d1 - d0;
	float tt = clampf(y * 0.055f, 0.022f, 0.045f), lg = clampf(fminf(wd, dp) * 0.06f, 0.03f, 0.055f);
	int style = (int)(rnd(s) * 3);
	SDL_Color top = M.oak, leg = style == 2 ? M.dark : M.wood, side = darken(M.wood, 0.12f);
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, y - tt, y, top);
	lbox(w, BAG_LIT, L, a0 + 0.004f, a1 - 0.004f, d0 + 0.004f, d1 - 0.004f, y - tt - 0.012f, y - tt, side); /* the apron */
	lbox(w, BAG_LIT, L, a0 - 0.002f, a1 + 0.002f, d0 - 0.002f, d0 + 0.006f, y - tt * 0.55f, y - tt * 0.25f, darken(top, 0.18f));
	bool ped = wd > 0.85f && y > 0.42f && style != 2, right = rnd(s) < 0.5f;
	float pw = clampf(wd * 0.32f, 0.3f, 0.46f), pa0 = right ? a1 - pw : a0, pa1 = right ? a1 : a0 + pw;
	for (int c = 0; c < 4; c++) {
		float ca = c & 1 ? a1 - 0.02f - lg : a0 + 0.02f, cd = c & 2 ? d1 - 0.02f - lg : d0 + 0.02f;
		if (ped && (c & 1) == right) {
			continue;
		}
		if (style == 2) { /* a steel frame: end rails at the foot */
			lbox(w, BAG_LIT, L, ca, ca + lg, cd, cd + lg, 0.03f, y - tt - 0.012f, leg);
			if (c & 2) {
				lbox(w, BAG_LIT, L, ca - 0.005f, ca + lg + 0.005f, d0 + 0.01f, d1 - 0.01f, 0, 0.035f, leg);
			}
		} else {
			lbox(w, BAG_LIT, L, ca, ca + lg, cd, cd + lg, 0.012f, y - tt - 0.012f, leg);
			lbox(w, BAG_LIT, L, ca - 0.004f, ca + lg + 0.004f, cd - 0.004f, cd + lg + 0.004f, 0, 0.014f, darken(leg, 0.4f));
		}
	}
	if (y > 0.4f && dp > 0.3f) { /* a panel down the back */
		lbox(w, BAG_LIT, L, a0 + 0.04f, a1 - 0.04f, d1 - 0.05f, d1 - 0.035f, y * 0.42f, y - tt - 0.012f, side);
	}
	if (ped) {
		float ph = y - tt - 0.012f;
		lbox(w, BAG_LIT, L, pa0 + 0.01f, pa1 - 0.01f, d0 + 0.02f, d1 - 0.03f, 0, ph, side);
		lbox(w, BAG_LIT, L, pa0 + 0.02f, pa1 - 0.02f, d0 + 0.012f, d1 - 0.04f, 0, 0.05f, darken(M.wood, 0.45f));
		int n = (int)((ph - 0.06f) / 0.17f);
		n = n < 1 ? 1 : n > 4 ? 4 : n;
		float dh = (ph - 0.06f) / (float)n;
		for (int i = 0; i < n; i++) {
			float yb = 0.06f + dh * (float)i, yt = yb + dh, mid = (pa0 + pa1) / 2;
			lbox(w, BAG_LIT, L, pa0 + 0.016f, pa1 - 0.016f, d0 + 0.008f, d0 + 0.022f, yb + 0.005f, yt - 0.005f, top);
			lbox(w, BAG_LIT, L, pa0 + 0.04f, pa1 - 0.04f, d0 + 0.004f, d0 + 0.01f, yb + dh * 0.18f, yt - dh * 0.18f, lighten(top, 0.06f));
			lbox(w, BAG_LIT, L, mid - 0.05f, mid + 0.05f, d0 - 0.012f, d0 + 0.004f, yt - dh * 0.32f, yt - dh * 0.32f + 0.014f, M.steel);
		}
	} else if (wd > 0.6f && y > 0.35f) { /* a drawer under the top, in the middle */
		float mid = (a0 + a1) / 2, dw = fminf(wd * 0.22f, 0.3f), dh = fminf(0.09f, y * 0.15f);
		lbox(w, BAG_LIT, L, mid - dw, mid + dw, d0 + 0.01f, d0 + 0.03f, y - tt - dh, y - tt, top);
		lbox(w, BAG_LIT, L, mid - 0.04f, mid + 0.04f, d0 - 0.008f, d0 + 0.012f, y - tt - dh * 0.6f, y - tt - dh * 0.6f + 0.012f, M.steel);
	}
}

/* ---- documents ---- */

/* Shelves of books in a bookcase's open front: a0 .. a1 across, d0 .. d1
 * deep, y0 .. y1 up; standing books of every size and colour, some lying
 * in piles, binders, a gap or an ornament now and then. budget: how many
 * books are left to draw. */
static void shelves(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y0, float y1, SDL_Color carcass, int *budget, unsigned *s) {
	int n = (int)((y1 - y0) / 0.36f + 0.5f);
	n = n < 1 ? 1 : n;
	float gap = (y1 - y0) / (float)n, bt = 0.02f;
	int secs = across(a1 - a0, 0.85f, 8);
	float sw = (a1 - a0) / (float)secs;
	for (int i = 1; i < secs; i++) { /* the uprights between sections */
		float a = a0 + sw * (float)i;
		lbox(w, BAG_LIT, L, a - 0.011f, a + 0.011f, d0, d1, y0, y1, carcass);
	}
	for (int sh = 0; sh < n; sh++) {
		float y = y0 + gap * (float)sh;
		lbox(w, BAG_LIT, L, a0, a1, d0, d1, y, y + bt, carcass);
		lbox(w, BAG_LIT, L, a0, a1, d0 - 0.003f, d0 + 0.004f, y - 0.006f, y + bt, lighten(carcass, 0.08f)); /* its lip */
		float room = gap - bt - 0.03f;
		if (room < 0.05f) {
			continue;
		}
		for (int c = 0; c < secs; c++) {
			float u = a0 + sw * (float)c + 0.016f, end = a0 + sw * (float)(c + 1) - 0.016f;
			while (u < end - 0.02f && *budget > 0) {
				float r = rnd(s), dep = (d1 - d0) * pick(s, 0.72f, 0.94f);
				SDL_Color col = book_colour(s);
				if (r < 0.06f) {
					u += pick(s, 0.03f, 0.12f); /* a gap */
				} else if (r < 0.14f && end - u > 0.26f) { /* a pile lying down */
					float la = pick(s, 0.17f, 0.24f);
					float hh = book_pile(w, L, u, d0 + 0.01f, y + bt, fminf(1, room / 0.22f), 2 + (int)(rnd(s) * 3), s);
					(void)hh;
					u += la + 0.02f;
					(*budget) -= 3;
				} else if (r < 0.19f && room > 0.3f && end - u > 0.2f) {
					u += binders(w, L, u, d0 + 0.01f, y + bt, fminf(1, room / 0.33f), 2 + (int)(rnd(s) * 2), s) + 0.01f;
					(*budget) -= 3;
				} else if (r < 0.215f && end - u > 0.12f) { /* an ornament: a vase or a ball on a stand */
					float o = pick(s, 0.04f, 0.06f), oh = fminf(room * 0.7f, pick(s, 0.1f, 0.2f));
					SDL_Color oc = hue(rnd(s), 0.45f, 0.7f);
					lball(w, BAG_BALL, L, u + o, d0 + (d1 - d0) * 0.45f, y + bt + oh * 0.45f, o * 1.6f, o * 1.6f, oh * 0.9f, oc);
					lbox(w, BAG_LIT, L, u + o * 0.6f, u + o * 1.4f, d0 + (d1 - d0) * 0.45f - o * 0.4f, d0 + (d1 - d0) * 0.45f + o * 0.4f,
					     y + bt + oh * 0.8f, y + bt + oh, oc);
					u += o * 2 + 0.03f;
				} else {
					float bw = pick(s, 0.018f, 0.055f) * M.spines * (M.fine ? 1 : 2.5f), bh = room * pick(s, 0.62f, 0.95f);
					if (u + bw > end) {
						break;
					}
					lbox(w, BAG_LIT, L, u, u + bw, d0 + 0.012f, d0 + 0.012f + dep, y + bt, y + bt + bh, col);
					float band = rnd(s);
					if (M.fine) {
						lbox(w, BAG_LIT, L, u + 0.002f, u + bw - 0.002f, d0 + 0.016f, d0 + 0.01f + dep, y + bt + bh, y + bt + bh + 0.001f, M.paper);
					}
					if (band < 0.55f && M.fine) { /* bands and a label on the spine */
						SDL_Color gold = band < 0.3f ? (SDL_Color){214, 180, 90, 255} : lighten(col, 0.5f);
						lbox(w, BAG_LIT, L, u, u + bw, d0 + 0.009f, d0 + 0.013f, y + bt + bh * 0.84f, y + bt + bh * 0.87f, gold);
						lbox(w, BAG_LIT, L, u, u + bw, d0 + 0.009f, d0 + 0.013f, y + bt + bh * 0.1f, y + bt + bh * 0.13f, gold);
						lbox(w, BAG_LIT, L, u + bw * 0.2f, u + bw * 0.8f, d0 + 0.009f, d0 + 0.013f, y + bt + bh * 0.45f, y + bt + bh * 0.72f, gold);
					}
					u += bw + (rnd(s) < 0.15f ? 0.004f : 0.0008f);
					(*budget)--;
				}
			}
		}
	}
}

/* A bookcase against something: sides, a cornice, a plinth, its back, the
 * books; a0 .. a1 across, its front at d0, dp deep, h tall. */
static void bookcase(Walk *w, const Loc *L, float a0, float a1, float d0, float dp, float h, SDL_Color carcass, int *budget, unsigned *s) {
	float st = 0.025f;
	lbox(w, BAG_LIT, L, a0, a0 + st, d0, d0 + dp, 0, h, carcass);
	lbox(w, BAG_LIT, L, a1 - st, a1, d0, d0 + dp, 0, h, carcass);
	lbox(w, BAG_LIT, L, a0 - 0.012f, a1 + 0.012f, d0 - 0.018f, d0 + dp, h - 0.035f, h, lighten(carcass, 0.06f));
	lbox(w, BAG_LIT, L, a0 - 0.004f, a1 + 0.004f, d0 - 0.008f, d0 + dp, h - 0.06f, h - 0.035f, darken(carcass, 0.1f));
	lbox(w, BAG_LIT, L, a0 + st, a1 - st, d0 + 0.012f, d0 + dp, 0, 0.075f, darken(carcass, 0.3f));
	lbox(w, BAG_LIT, L, a0 + st, a1 - st, d0 + dp - 0.012f, d0 + dp, 0.075f, h - 0.06f, darken(carcass, 0.4f));
	shelves(w, L, a0 + st, a1 - st, d0, d0 + dp - 0.012f, 0.075f, h - 0.06f, carcass, budget, s);
}

/* A document: a desk with books, papers and files on it; bigger, a
 * bookcase, or deep enough, a library stack with bookcases all round it. */
static void make_documents(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	if (H >= 1.15f) {
		const Box *b = &k->b;
		SDL_Color carcass = rnd(&s) < 0.3f ? lighten(M.paper, 0.1f) : rnd(&s) < 0.5f ? darken(M.wood, 0.25f) : M.wood;
		float W = b->hi[0] - b->lo[0], D = b->hi[2] - b->lo[2], dp = fminf(0.36f, fminf(W, D) / 2);
		int budget = 900;
		if (L->d < 0.6f) { /* shallow: one bookcase, its back to the wall or to the next row's */
			M.spines = fmaxf(1, L->w * floorf((H - 0.1f) / 0.36f + 0.5f) / 0.04f / (float)budget);
			bookcase(w, L, 0, L->w, 0, L->d, H, carcass, &budget, &s);
			return;
		}
		float run = 2 * (W + D) * floorf((H - 0.1f) / 0.36f + 0.5f); /* metres of shelf */
		M.spines = fmaxf(1, run / 0.04f / (float)budget);
		/* the stack's core and its top; a bookcase on every side wide enough */
		bag_box(w, BAG_LIT, box(b->lo[0] + dp, 0, b->lo[2] + dp, b->hi[0] - dp, H - 0.06f, b->hi[2] - dp), darken(carcass, 0.5f));
		for (int f = 0; f < 4; f++) {
			Loc side = loc_of(b, f, dp);
			bool lng = f == k->front || f == (k->front + 2) % 4;
			float a0 = lng ? 0 : dp, a1 = lng ? side.w : side.w - dp;
			if (a1 - a0 > 0.3f) {
				bookcase(w, &side, a0, a1, 0, dp, H, carcass, &budget, &s);
			}
		}
		return;
	}
	/* a desk, its top at the block's top, full of books and paper, a leather
	 * mat where you would write */
	desk(w, L, 0.01f, L->w - 0.01f, 0.01f, L->d - 0.01f, H, &s);
	if (L->w > 0.7f && L->d > 0.45f) {
		float mw = fminf(L->w * 0.45f, 0.7f), mid = L->w / 2;
		lbox(w, BAG_LIT, L, mid - mw / 2, mid + mw / 2, 0.06f, 0.06f + fminf(L->d * 0.5f, 0.45f), H, H + 0.003f, (SDL_Color){40, 70, 52, 255});
	}
	clutter(w, L, 0.04f, L->w - 0.04f, 0.04f, L->d - 0.04f, H, 1.0f, k->tex, &s);
}

/* ---- consoles and computers ---- */

/* lines of text on a screen with nothing better to show, scrolling */
static void lscreen_lines(Walk *w, const Loc *L, float a0, float a1, float y0, float y1, float d, unsigned seed, SDL_Color c) {
	float lh = clampf((y1 - y0) / 16, 0.01f, 0.035f);
	int rows = (int)((y1 - y0) / lh) - 1, scroll = (int)(M.time * 2.5f);
	for (int i = 0; i < rows; i++) {
		unsigned s = seed + (unsigned)(i + scroll) * 7919u;
		float len = 0.15f + rnd(&s) * 0.75f, indent = rnd(&s) < 0.3f ? 0.1f : 0;
		float a = a0 + (a1 - a0) * (0.05f + indent), z = a + (a1 - a0) * len * (0.9f - indent);
		float y = y1 - lh * ((float)i + 1.2f);
		lbox(w, BAG_LAMP, L, a, fminf(z, a1 - 0.01f), d - 0.002f, d, y, y + lh * 0.45f, c);
	}
}

/* What a screen shows, a0 .. a1 by y0 .. y1 at depth d (its glass): the
 * file's picture, or lines of text, a cursor blinking in the corner and a
 * slow bright line running down it. */
static void screen(Walk *w, const Loc *L, float a0, float a1, float y0, float y1, float d, unsigned tex, SDL_Color text, unsigned seed) {
	unsigned s = seed;
	lbox(w, BAG_LAMP, L, a0, a1, d, d + 0.004f, y0, y1, M.screen);
	if (tex) {
		lpic(w, L, tex, true, a0, a1, y0, y1, d - 0.001f, M.white);
	} else {
		lscreen_lines(w, L, a0, a1, y0, y1, d - 0.001f, seed, text);
	}
	float sh = y1 - y0, sw = a1 - a0;
	if (blink(&s, 1.1f, 0.55f)) {
		float cy = y0 + sh * 0.07f;
		lbox(w, BAG_LAMP, L, a0 + sw * 0.05f, a0 + sw * 0.05f + fmaxf(sh * 0.035f, 0.006f), d - 0.004f, d - 0.002f, cy, cy + fmaxf(sh * 0.06f, 0.01f), text);
	}
	float sy = y1 - sh * fmodf(M.time * 0.18f + rnd(&s), 1.0f);
	SDL_Color line = text;
	line.a = 90;
	lbox(w, BAG_GLOW, L, a0, a1, d - 0.005f, d - 0.003f, fmaxf(y0, sy - sh * 0.03f), sy, line);
}

/* A flat monitor a0 .. a1 wide on its stand from y0, its top at y1, its
 * glass at d: bezel, chin, the screen, a light, the stand behind. */
static void monitor(Walk *w, const Loc *L, float a0, float a1, float d, float y0, float y1, unsigned tex, unsigned seed, SDL_Color text) {
	float pw = a1 - a0, ph = fminf((y1 - y0) * 0.8f, pw * 0.66f), pb = y1 - ph, mid = (a0 + a1) / 2;
	float bz = clampf(pw * 0.025f, 0.006f, 0.022f), th = clampf(pw * 0.05f, 0.015f, 0.035f);
	SDL_Color shell = M.black;
	lbox(w, BAG_LIT, L, a0, a1, d, d + th, pb, y1, shell);
	lbox(w, BAG_LIT, L, a0 + pw * 0.22f, a1 - pw * 0.22f, d + th, d + th * 2.2f, pb + ph * 0.2f, y1 - ph * 0.25f, darken(M.dark, 0.2f));
	lbox(w, BAG_LIT, L, mid - pw * 0.035f, mid + pw * 0.035f, d + th * 2, d + th * 2.6f, y0, pb + ph * 0.45f, M.metal);
	lbox(w, BAG_LIT, L, mid - pw * 0.17f, mid + pw * 0.17f, d - th * 0.5f, d + th * 4, y0, y0 + 0.01f, M.metal);
	lbox(w, BAG_LIT, L, mid - pw * 0.04f, mid + pw * 0.04f, d - 0.002f, d + 0.002f, pb + bz * 0.6f, pb + bz * 1.1f, M.metal);
	lbox(w, BAG_LAMP, L, a1 - bz * 3, a1 - bz * 2, d - 0.003f, d, pb + bz * 0.6f, pb + bz * 1.2f, LED_BLUE);
	screen(w, L, a0 + bz, a1 - bz, pb + bz * 1.7f, y1 - bz, d - 0.002f, tex, text, seed);
}

/* A keyboard a0 .. a1 across, d0 .. d1 deep, on y: its keys in rows, each
 * row a little higher toward the back, a long space bar. */
static void keyboard(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y, SDL_Color base, SDL_Color cap) {
	float kw = a1 - a0, kd = d1 - d0, m = fminf(kw, kd) * 0.06f;
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, y, y + 0.016f, base);
	if (!M.fine) {
		lbox(w, BAG_LIT, L, a0 + m, a1 - m, d0 + m, d1 - m, y + 0.016f, y + 0.024f, cap);
		return;
	}
	int cols = (int)(kw / 0.019f);
	cols = cols < 6 ? 6 : cols > 15 ? 15 : cols;
	float pa = (kw - 2 * m) / (float)cols, pd = (kd - 2 * m) / 5, g = fminf(pa, pd) * 0.12f;
	for (int r = 0; r < 5; r++) {
		float kd0 = d0 + m + pd * (float)r, top = y + 0.022f + 0.0015f * (float)r;
		for (int c = 0; c < cols; c++) {
			float ka0 = a0 + m + pa * (float)c, ka1 = ka0 + pa;
			if (r == 0 && c >= 3 && c < cols - 3) { /* the space bar */
				if (c != 3) {
					continue;
				}
				ka1 = a0 + m + pa * (float)(cols - 3);
			}
			bool odd = (r == 4 && c == 0) || (r == 2 && c == cols - 1);
			lbox(w, odd ? BAG_LAMP : BAG_LIT, L, ka0 + g, ka1 - g, kd0 + g, kd0 + pd - g, y + 0.016f, top, odd ? M.accent : cap);
		}
	}
}

/* a mouse on its mat, at a, d */
static void mouse(Walk *w, const Loc *L, float a, float d, float y, float sc, SDL_Color c) {
	lbox(w, BAG_LIT, L, a - 0.11f * sc, a + 0.11f * sc, d - 0.09f * sc, d + 0.09f * sc, y, y + 0.003f, M.black);
	lball(w, BAG_BALL, L, a, d, y + 0.012f * sc, 0.06f * sc, 0.1f * sc, 0.035f * sc, c);
}

/* An office chair at a, d facing the back (its desk), seat sc of a real one. */
static void chair(Walk *w, const Loc *L, float a, float d, float sc, SDL_Color c) {
	float seat = 0.47f * sc, r = 0.28f * sc, t = 0.025f * sc;
	SDL_Color frame = M.dark;
	lbox(w, BAG_LIT, L, a - r, a + r, d - t, d + t, 0.05f * sc, 0.08f * sc, frame);
	lbox(w, BAG_LIT, L, a - t, a + t, d - r, d + r, 0.05f * sc, 0.08f * sc, frame);
	for (int i = 0; i < 4; i++) {
		float wa = a + (i == 0 ? -r : i == 1 ? r : 0), wd = d + (i == 2 ? -r : i == 3 ? r : 0);
		lball(w, BAG_BALL, L, wa, wd, 0.03f * sc, 0.05f * sc, 0.05f * sc, 0.055f * sc, M.black);
	}
	lbox(w, BAG_LIT, L, a - t, a + t, d - t, d + t, 0.08f * sc, seat - 0.05f * sc, M.metal);
	lbox(w, BAG_LIT, L, a - 0.24f * sc, a + 0.24f * sc, d - 0.22f * sc, d + 0.23f * sc, seat - 0.07f * sc, seat - 0.02f * sc, frame);
	lball(w, BAG_BALL, L, a, d, seat - 0.02f * sc, 0.5f * sc, 0.48f * sc, 0.08f * sc, c);
	/* the back, toward you, on its bar */
	lbox(w, BAG_LIT, L, a - 0.025f * sc, a + 0.025f * sc, d - 0.27f * sc, d - 0.21f * sc, seat - 0.06f * sc, seat + 0.14f * sc, M.metal);
	lbox(w, BAG_LIT, L, a - 0.23f * sc, a + 0.23f * sc, d - 0.3f * sc, d - 0.24f * sc, seat + 0.1f * sc, seat + 0.58f * sc, c);
	lball(w, BAG_BALL, L, a, d - 0.255f * sc, seat + 0.34f * sc, 0.44f * sc, 0.06f * sc, 0.5f * sc, lighten(c, 0.08f));
	for (int i = -1; i <= 1; i += 2) { /* the arms */
		float aa = a + (float)i * 0.25f * sc;
		lbox(w, BAG_LIT, L, aa - 0.018f * sc, aa + 0.018f * sc, d - 0.02f * sc, d + 0.02f * sc, seat - 0.04f * sc, seat + 0.2f * sc, frame);
		lbox(w, BAG_LIT, L, aa - 0.03f * sc, aa + 0.03f * sc, d - 0.13f * sc, d + 0.12f * sc, seat + 0.2f * sc, seat + 0.235f * sc, M.black);
	}
}

/* A tower case a0 .. a1 across, d0 .. d1 deep, h tall: its front with drive
 * bays, a lit power button, a light that flickers with the disk and a grille,
 * windows down its sides with lit fans behind them. */
static void tower(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, unsigned *s) {
	bool white_case = rnd(s) < 0.3f;
	SDL_Color body = white_case ? (SDL_Color){214, 216, 220, 255} : M.black, face = white_case ? lighten(body, 0.3f) : M.dark;
	float cw = a1 - a0, cd = d1 - d0, mid = (a0 + a1) / 2, fh = h * 0.022f;
	for (int i = 0; i < 4; i++) {
		float fa = i & 1 ? a1 - cw * 0.18f : a0 + cw * 0.06f, fd = i & 2 ? d1 - cd * 0.12f : d0 + cd * 0.04f;
		lbox(w, BAG_LIT, L, fa, fa + cw * 0.12f, fd, fd + cd * 0.08f, 0, fh, M.black);
	}
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, fh, h, body);
	lbox(w, BAG_LIT, L, a0 + cw * 0.04f, a1 - cw * 0.04f, d0 - 0.008f, d0, fh + h * 0.01f, h - h * 0.01f, face);
	for (int i = 0; i < 2; i++) {
		float y = h * (0.88f - 0.075f * (float)i);
		lbox(w, BAG_LIT, L, a0 + cw * 0.12f, a1 - cw * 0.12f, d0 - 0.012f, d0 - 0.006f, y, y + h * 0.055f, darken(face, 0.15f));
	}
	lbox(w, BAG_LIT, L, a0 + cw * 0.18f, a1 - cw * 0.18f, d0 - 0.014f, d0 - 0.008f, h * 0.84f - h * 0.01f, h * 0.84f, M.black);
	lball(w, BAG_BALL_LAMP, L, mid, d0 - 0.01f, h * 0.68f, cw * 0.22f, 0.012f, cw * 0.22f, LED_BLUE);
	lball(w, BAG_BALL, L, mid, d0 - 0.012f, h * 0.68f, cw * 0.14f, 0.012f, cw * 0.14f, face);
	bool disk = blink(s, 5.3f, 0.3f) && blink(s, 0.6f, 0.6f);
	lbox(w, disk ? BAG_LAMP : BAG_LIT, L, mid - cw * 0.05f, mid + cw * 0.05f, d0 - 0.012f, d0 - 0.007f, h * 0.6f, h * 0.6f + h * 0.012f,
	     disk ? LED_AMBER : darken(LED_AMBER, 0.7f));
	for (int i = 0; i < 2; i++) { /* ports */
		lbox(w, BAG_LIT, L, mid - cw * 0.25f + cw * 0.3f * (float)i, mid - cw * 0.08f + cw * 0.3f * (float)i, d0 - 0.011f, d0 - 0.007f,
		     h * 0.55f, h * 0.565f, M.black);
	}
	for (int i = 0; i < 9; i++) { /* the grille */
		float y = h * 0.08f + (float)i * h * 0.04f;
		lbox(w, BAG_LIT, L, a0 + cw * 0.14f, a1 - cw * 0.14f, d0 - 0.011f, d0 - 0.007f, y, y + h * 0.018f, darken(face, 0.5f));
	}
	/* the windows, a fan glowing through each, the colour going round */
	SDL_Color fan = hue(M.time * 0.08f + rnd(s), 0.75f, 1);
	for (int side = 0; side < 2; side++) {
		float a = side ? a1 : a0, out = side ? 0.004f : -0.004f;
		lbox(w, BAG_LIT, L, a - 0.002f, a + 0.002f, d0 + cd * 0.12f, d1 - cd * 0.08f, h * 0.25f, h * 0.92f, darken(M.black, 0.4f));
		for (int f = 0; f < 2; f++) { /* a fan in its square frame, its ring lit, its hub */
			float fd = d0 + cd * (f ? 0.7f : 0.32f), fy = h * (f ? 0.42f : 0.72f), r = fminf(cd * 0.2f, h * 0.12f);
			lbox(w, BAG_LIT, L, a + out * 0.5f - 0.002f, a + out * 0.5f + 0.002f, fd - r * 1.15f, fd + r * 1.15f, fy - r * 1.15f, fy + r * 1.15f, M.dark);
			lball(w, BAG_BALL_LAMP, L, a + out, fd, fy, 0.004f, r * 2, r * 2, fan);
			lball(w, BAG_BALL, L, a + out * 1.5f, fd, fy, 0.004f, r * 1.75f, r * 1.75f, darken(M.dark, 0.3f));
			lbox(w, BAG_LIT, L, a + out * 2 - 0.002f, a + out * 2 + 0.002f, fd - r * 0.3f, fd + r * 0.3f, fy - r * 0.3f, fy + r * 0.3f, M.dark);
		}
	}
}

/* A rack of servers a0 .. a1 across, d0 .. d1 deep, h tall: its frame, and
 * filling it machines of one, two or four units, drive bays, switches with
 * their ports, all blinking. busy 0 .. 1: how much they blink. */
static void rack(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, float busy, unsigned *s) {
	float rw = a1 - a0, post = fminf(0.045f, rw * 0.07f);
	SDL_Color frame = darken(M.dark, 0.2f);
	/* the body inside the frame: no face of it in a frame's face, which flickers */
	lbox(w, BAG_LIT, L, a0 + 0.004f, a1 - 0.004f, d0 + 0.02f, d1 - 0.004f, 0, h - 0.004f, darken(M.black, 0.2f));
	lbox(w, BAG_LIT, L, a0, a0 + post, d0, d1, 0, h, frame);
	lbox(w, BAG_LIT, L, a1 - post, a1, d0, d1, 0, h, frame);
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, h - 0.05f, h, frame);
	lbox(w, BAG_LIT, L, a0, a1, d0 + 0.01f, d1, 0, 0.06f, frame);
	for (int i = 0; i < 2; i++) { /* vents on the top */
		float a = a0 + rw * (i ? 0.62f : 0.38f);
		lball(w, BAG_BALL, L, a, (d0 + d1) / 2, h + 0.002f, rw * 0.3f, rw * 0.3f, 0.006f, M.black);
	}
	float u = clampf(h / 22, 0.044f, 0.09f), y = 0.07f, ia0 = a0 + post, ia1 = a1 - post, iw = ia1 - ia0;
	while (y + u < h - 0.06f) {
		float r = rnd(s);
		int units = r < 0.45f ? 1 : r < 0.8f ? 2 : 4;
		if (y + u * (float)units > h - 0.06f) {
			units = 1;
		}
		float uh = u * (float)units, y0 = y + 0.002f, y1 = y + uh - 0.002f, t = rnd(s);
		if (t < 0.12f) { /* a blank */
			lbox(w, BAG_LIT, L, ia0, ia1, d0 + 0.004f, d0 + 0.012f, y0, y1, darken(M.dark, 0.3f));
		} else {
			lbox(w, BAG_LIT, L, ia0 + 0.002f, ia1 - 0.002f, d0 - 0.004f, d0 + 0.02f, y0, y1, t < 0.5f ? darken(M.metal, 0.35f) : M.dark);
			lbox(w, BAG_LIT, L, ia0 - post * 0.6f, ia0 + 0.012f, d0 - 0.008f, d0, y0, y1, M.metal); /* its ears */
			lbox(w, BAG_LIT, L, ia1 - 0.012f, ia1 + post * 0.6f, d0 - 0.008f, d0, y0, y1, M.metal);
			if (t < 0.32f) { /* a switch: ports in two rows, each with its light */
				int ports = M.fine ? (int)(iw * 0.62f / 0.022f) : 8;
				for (int p = 0; p < ports && p < 24; p++) {
					float pa = ia0 + iw * 0.06f + (float)p * 0.022f;
					for (int row = 0; row < (units > 1 ? 2 : 1) && M.fine; row++) {
						float py = y0 + (y1 - y0) * (row ? 0.55f : 0.15f);
						lbox(w, BAG_LIT, L, pa, pa + 0.016f, d0 - 0.007f, d0 - 0.003f, py, py + fminf(0.016f, u * 0.4f), M.black);
					}
					bool on = blink(s, 2 + rnd(s) * 9, 0.25f + 0.5f * busy);
					lbox(w, on ? BAG_LAMP : BAG_LIT, L, pa + 0.004f, pa + 0.012f, d0 - 0.008f, d0 - 0.004f, y1 - u * 0.18f, y1 - u * 0.08f,
					     on ? LED_GREEN : darken(LED_GREEN, 0.75f));
				}
			} else if (t < 0.62f && units > 1) { /* drive bays */
				int bays = (int)(iw * 0.8f / 0.09f);
				bays = bays < 2 ? 2 : bays > 12 ? 12 : bays;
				float bw = iw * 0.8f / (float)bays;
				for (int bi = 0; bi < bays; bi++) {
					float ba = ia0 + iw * 0.05f + bw * (float)bi;
					lbox(w, BAG_LIT, L, ba + 0.003f, ba + bw - 0.003f, d0 - 0.009f, d0 - 0.004f, y0 + 0.006f, y1 - 0.006f, darken(M.metal, 0.1f));
					if (M.fine) {
						lbox(w, BAG_LIT, L, ba + bw * 0.2f, ba + bw * 0.8f, d0 - 0.012f, d0 - 0.009f, y0 + (y1 - y0) * 0.2f, y0 + (y1 - y0) * 0.3f, M.black);
					}
					bool on = blink(s, 3 + rnd(s) * 8, 0.2f + 0.5f * busy);
					lbox(w, on ? BAG_LAMP : BAG_LIT, L, ba + bw * 0.7f, ba + bw * 0.85f, d0 - 0.011f, d0 - 0.008f, y1 - 0.014f, y1 - 0.008f,
					     on ? (rnd(s) < 0.85f ? LED_GREEN : LED_AMBER) : M.black);
				}
			} else { /* a server: vents, a label, its lights */
				for (int v = 0; v < (M.fine ? 6 : 0); v++) {
					float va = ia0 + iw * (0.06f + 0.06f * (float)v);
					lbox(w, BAG_LIT, L, va, va + iw * 0.035f, d0 - 0.007f, d0 - 0.003f, y0 + (y1 - y0) * 0.2f, y1 - (y1 - y0) * 0.2f, M.black);
				}
				lbox(w, BAG_LIT, L, ia0 + iw * 0.46f, ia0 + iw * 0.62f, d0 - 0.007f, d0 - 0.003f, y0 + (y1 - y0) * 0.35f, y1 - (y1 - y0) * 0.35f, M.paper);
				for (int l = 0; l < 3; l++) {
					bool on = blink(s, 0.6f + rnd(s) * 4 * (0.3f + busy), 0.55f);
					SDL_Color c = l == 0 ? LED_GREEN : l == 1 ? LED_AMBER : M.accent;
					float la = ia0 + iw * (0.74f + 0.06f * (float)l), ly = y0 + (y1 - y0) * 0.5f;
					lbox(w, on ? BAG_LAMP : BAG_LIT, L, la, la + iw * 0.03f, d0 - 0.009f, d0 - 0.004f, ly - 0.005f, ly + 0.005f, on ? c : darken(c, 0.75f));
				}
			}
		}
		y += uh;
	}
}

/* A desktop computer's monitor shows a desktop: the wallpaper, windows, a
 * bar along the foot, the program's icon in the middle. */
static void desktop_screen(Walk *w, const Loc *L, float a0, float a1, float y0, float y1, float d, unsigned tex, unsigned *s) {
	SDL_Color wall = hue(rnd(s), 0.45f, 0.45f);
	float sw = a1 - a0, sh = y1 - y0;
	lbox(w, BAG_LAMP, L, a0, a1, d, d + 0.004f, y0, y1, wall);
	lbox(w, BAG_LAMP, L, a0, a1, d - 0.001f, d, y0, y0 + sh * 0.07f, darken(wall, 0.6f));
	for (int i = 0; i < 2; i++) {
		float wa = a0 + sw * pick(s, 0.05f, 0.45f), ww = sw * pick(s, 0.3f, 0.45f), wy = y0 + sh * pick(s, 0.2f, 0.4f), wh = sh * pick(s, 0.3f, 0.5f);
		lbox(w, BAG_LAMP, L, wa, wa + ww, d - 0.002f - 0.001f * (float)i, d, wy, wy + wh, (SDL_Color){230, 232, 236, 255});
		lbox(w, BAG_LAMP, L, wa, wa + ww, d - 0.0025f - 0.001f * (float)i, d, wy + wh * 0.88f, wy + wh, darken(M.accent, 0.2f));
	}
	if (tex) {
		float side = fminf(sw, sh) * 0.32f, cu = (a0 + a1) / 2, cy = y0 + sh * 0.55f;
		lpic(w, L, tex, true, cu - side / 2, cu + side / 2, cy - side / 2, cy + side / 2, d - 0.005f, M.white);
	}
	float sy = y1 - sh * fmodf(M.time * 0.18f + rnd(s), 1.0f);
	SDL_Color line = M.white;
	line.a = 60;
	lbox(w, BAG_GLOW, L, a0, a1, d - 0.007f, d - 0.006f, fmaxf(y0, sy - sh * 0.03f), sy, line);
}

/* A program: a computer. Small, tower cases; middling, a desk with the
 * computer on it and its case standing by; big, racks of servers. */
static void make_program(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d;
	if (H < 0.8f) {
		int n = across(W, fmaxf(H * 0.5f, 0.22f) + 0.12f, 6);
		float cw = W / (float)n;
		for (int i = 0; i < n; i++) {
			float a = cw * (float)i;
			tower(w, L, a + cw * 0.12f, a + cw * 0.88f, 0.02f, fminf(D - 0.02f, H * 1.1f), H, &s);
		}
		return;
	}
	if (H < 1.5f) {
		float ty = clampf(H * 0.58f, 0.3f, 0.76f), dd = fminf(D - 0.02f, 0.85f);
		desk(w, L, 0.01f, W - 0.01f, D - dd, D - 0.01f, ty, &s);
		float sc = clampf(fminf(W, dd) / 0.8f, 0.4f, 1.0f);
		float cw = clampf(W * 0.22f, 0.14f, 0.24f) , ca = rnd(&s) < 0.5f ? 0.05f : W - 0.05f - cw;
		/* the case standing on the floor by the desk's end, or on it */
		if (ty > 0.45f) {
			tower(w, L, ca, ca + cw, D - dd + 0.04f, D - dd + 0.04f + fminf(dd * 0.8f, 0.5f), fminf(ty - 0.06f, cw * 2.2f), &s);
		}
		float ma0 = W * 0.22f, ma1 = W * 0.78f, md = D - dd + dd * 0.55f;
		float mw = fminf(ma1 - ma0, (H - ty) * 1.7f), mid = W / 2;
		float pw = mw, ph = fminf((H - ty) * 0.8f, pw * 0.66f), pb = H - ph, bz = clampf(pw * 0.025f, 0.006f, 0.022f);
		float th = clampf(pw * 0.05f, 0.015f, 0.035f);
		lbox(w, BAG_LIT, L, mid - pw / 2, mid + pw / 2, md, md + th, pb, H, M.black);
		lbox(w, BAG_LIT, L, mid - pw * 0.035f, mid + pw * 0.035f, md + th, md + th * 2, ty, pb + ph * 0.4f, M.metal);
		lbox(w, BAG_LIT, L, mid - pw * 0.17f, mid + pw * 0.17f, md - th, md + th * 3, ty, ty + 0.01f, M.metal);
		lbox(w, BAG_LAMP, L, mid + pw / 2 - bz * 3, mid + pw / 2 - bz * 2, md - 0.003f, md, pb + bz * 0.6f, pb + bz * 1.2f, LED_GREEN);
		desktop_screen(w, L, mid - pw / 2 + bz, mid + pw / 2 - bz, pb + bz * 1.7f, H - bz, md - 0.002f, k->tex, &s);
		float kw = fminf(0.44f * sc, W * 0.4f), kd0 = D - dd + 0.06f * sc;
		keyboard(w, L, mid - kw / 2, mid + kw / 2, kd0, kd0 + 0.15f * sc, ty, M.dark, lighten(M.dark, 0.25f));
		mouse(w, L, mid + kw / 2 + 0.1f * sc, kd0 + 0.08f * sc, ty, sc, M.dark);
		/* small speakers either side of the monitor */
		for (int i = 0; i < 2; i++) {
			float sa = i ? mid + mw / 2 + 0.05f * sc : mid - mw / 2 - 0.13f * sc;
			if (sa < 0.03f || sa + 0.08f * sc > W - 0.03f) {
				continue;
			}
			lbox(w, BAG_LIT, L, sa, sa + 0.08f * sc, md - 0.02f, md + 0.08f * sc, ty, ty + 0.16f * sc, M.dark);
			lball(w, BAG_BALL, L, sa + 0.04f * sc, md - 0.022f, ty + 0.06f * sc, 0.055f * sc, 0.008f, 0.055f * sc, M.black);
		}
		clutter(w, L, ca < W / 2 ? W * 0.72f : 0.04f, ca < W / 2 ? W - 0.04f : W * 0.28f, D - dd + 0.04f, D - dd + dd * 0.5f, ty, 0.4f, 0, &s);
		return;
	}
	/* racks, side by side, the program's icon on a card on the first */
	int n = across(W, 0.68f, 10);
	float rw = W / (float)n, busy = clampf((float)log10((double)k->size + 1) / 10, 0.2f, 1);
	for (int i = 0; i < n; i++) {
		rack(w, L, rw * (float)i + 0.01f, rw * (float)(i + 1) - 0.01f, 0.02f, D - 0.02f, H, busy, &s);
	}
	if (k->tex) {
		float side = fminf(rw * 0.35f, 0.18f);
		lpic(w, L, k->tex, false, rw * 0.5f - side / 2, rw * 0.5f + side / 2, H - 0.05f - side, H - 0.05f, -0.004f, M.white);
	}
}

/* An old terminal on its own: a deep cathode-ray case narrowing to the
 * back, its glass glowing, a keyboard in front of it. a0 .. a1 across,
 * d0 .. d1 deep, y0 .. y1 up. */
static void crt(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y0, float y1, SDL_Color shell, SDL_Color glow, unsigned tex, unsigned seed) {
	float cw = a1 - a0, ch = y1 - y0, cd = d1 - d0;
	lbox(w, BAG_LIT, L, a0, a1, d0, d0 + cd * 0.42f, y0, y1, shell);
	lbox(w, BAG_LIT, L, a0 + cw * 0.1f, a1 - cw * 0.1f, d0 + cd * 0.42f, d0 + cd * 0.78f, y0 + ch * 0.06f, y1 - ch * 0.08f, darken(shell, 0.08f));
	lbox(w, BAG_LIT, L, a0 + cw * 0.26f, a1 - cw * 0.26f, d0 + cd * 0.78f, d1, y0 + ch * 0.16f, y1 - ch * 0.24f, darken(shell, 0.16f));
	for (int i = 0; i < 5; i++) { /* vents on top */
		float vd = d0 + cd * (0.1f + 0.06f * (float)i);
		lbox(w, BAG_LIT, L, a0 + cw * 0.25f, a1 - cw * 0.25f, vd, vd + cd * 0.025f, y1, y1 + 0.002f, darken(shell, 0.45f));
	}
	lbox(w, BAG_LIT, L, a0 + cw * 0.06f, a1 - cw * 0.06f, d0 - 0.006f, d0, y0 + ch * 0.2f, y1 - ch * 0.06f, darken(shell, 0.25f));
	lbox(w, BAG_LIT, L, a0 + cw * 0.1f, a1 - cw * 0.1f, d0 - 0.009f, d0 - 0.006f, y0 + ch * 0.25f, y1 - ch * 0.1f, M.black);
	float sa0 = a0 + cw * 0.13f, sa1 = a1 - cw * 0.13f, sy0 = y0 + ch * 0.28f, sy1 = y1 - ch * 0.13f;
	SDL_Color bg = M.screen;
	M.screen = darken(mix(glow, M.black, 0.85f), 0.2f);
	screen(w, L, sa0, sa1, sy0, sy1, d0 - 0.011f, tex, glow, seed);
	M.screen = bg;
	SDL_Color halo = glow;
	halo.a = 40;
	lbox(w, BAG_GLOW, L, sa0 - cw * 0.02f, sa1 + cw * 0.02f, d0 - 0.02f, d0 - 0.014f, sy0 - ch * 0.02f, sy1 + ch * 0.02f, halo);
	/* under the glass: a name plate, two knobs, the power light */
	lbox(w, BAG_LIT, L, a0 + cw * 0.12f, a0 + cw * 0.35f, d0 - 0.008f, d0 - 0.005f, y0 + ch * 0.09f, y0 + ch * 0.13f, lighten(shell, 0.3f));
	for (int i = 0; i < 2; i++) {
		lball(w, BAG_BALL, L, a1 - cw * (0.3f - 0.1f * (float)i), d0 - 0.012f, y0 + ch * 0.11f, cw * 0.05f, 0.02f, cw * 0.05f, M.dark);
	}
	lbox(w, BAG_LAMP, L, a1 - cw * 0.12f, a1 - cw * 0.09f, d0 - 0.008f, d0 - 0.004f, y0 + ch * 0.1f, y0 + ch * 0.12f, LED_GREEN);
}

/* Code: a workstation, the file's first lines on the screens; a big file,
 * more and bigger screens. A terminal in /dev: an old one, glowing green. */
static void make_console(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d;
	SDL_Color text = M.accent;
	if (k->dev == WALK_DEV_TTY || H < 0.55f) {
		/* a terminal on a low table, or on the floor when there is no room */
		SDL_Color shell = k->dev ? (SDL_Color){206, 196, 170, 255} : lighten(M.dark, 0.1f);
		SDL_Color glow = k->dev ? (rnd(&s) < 0.5f ? (SDL_Color){90, 255, 120, 255} : (SDL_Color){255, 186, 70, 255}) : text;
		float ty = H >= 0.75f ? H * 0.4f : 0, tw = fminf(W - 0.04f, (H - ty) * 1.25f), mid = W / 2;
		float cd = fminf(D * 0.62f, (H - ty) * 1.1f), kd = fminf(D - cd - 0.04f, 0.22f);
		if (ty > 0) {
			desk(w, L, 0.01f, W - 0.01f, 0.01f, D - 0.01f, ty, &s);
		}
		crt(w, L, mid - tw / 2, mid + tw / 2, D - cd - 0.02f, D - 0.02f, ty, H, shell, glow, k->dev ? 0 : k->tex, s);
		if (kd > 0.06f) {
			float kw = fminf(tw * 0.9f, W - 0.06f), kd0 = D - cd - 0.04f - kd;
			keyboard(w, L, mid - kw / 2, mid + kw / 2, kd0, kd0 + kd, ty, darken(shell, 0.1f), lighten(shell, 0.15f));
		}
		return;
	}
	/* a desk; a chair in front when it is deep enough */
	float ty = clampf(H * 0.6f, 0.32f, 0.76f);
	bool seat = D > 1.05f && H > 0.8f;
	float dd = seat ? fminf(D * 0.58f, 0.85f) : D - 0.02f, d0 = D - 0.01f - dd;
	desk(w, L, 0.01f, W - 0.01f, d0, D - 0.01f, ty, &s);
	float sc = clampf(fminf(W, dd) / 0.8f, 0.4f, 1.0f);
	if (seat) {
		chair(w, L, W / 2 + pick(&s, -0.1f, 0.1f) * W, d0 * 0.5f, clampf(ty / 0.75f, 0.5f, 1.0f), hue(rnd(&s), 0.3f, 0.3f));
	}
	/* the monitors along the back: more for a wider desk; a second row over
	 * the first on a tall one */
	float rows = H - ty > 1.0f ? 2 : 1, room = (H - ty) / rows;
	int n = across(W * 0.86f, fmaxf(room * 1.5f, 0.35f), 4);
	float mw = fminf(W * 0.86f / (float)n, room * 1.7f), md = d0 + dd * 0.62f;
	float m0 = W / 2 - mw * (float)n / 2;
	for (int r = 0; r < rows; r++) {
		for (int i = 0; i < n; i++) {
			float a0 = m0 + mw * (float)i + 0.01f, a1 = a0 + mw - 0.02f;
			monitor(w, L, a0, a1, md, ty + room * (float)r, ty + room * (float)(r + 1) - 0.01f, k->tex, s + (unsigned)(r * 4 + i) * 131u, text);
		}
	}
	/* the keyboard and mouse; books, papers and a mug either side */
	float kw = fminf(0.44f * sc, W * 0.42f), kd0 = d0 + 0.07f * sc, mid = W / 2;
	keyboard(w, L, mid - kw / 2, mid + kw / 2, kd0, kd0 + 0.15f * sc, ty, M.dark, lighten(M.dark, 0.22f));
	mouse(w, L, mid + kw / 2 + 0.11f * sc, kd0 + 0.08f * sc, ty, sc, M.dark);
	clutter(w, L, 0.04f, mid - kw / 2 - 0.04f, d0 + 0.03f, md - 0.03f, ty, 0.6f, 0, &s);
	clutter(w, L, mid + kw / 2 + 0.25f * sc, W - 0.04f, d0 + 0.03f, md - 0.03f, ty, 0.6f, 0, &s);
}

/* ---- music ---- */

/* how loud the music is just now, 0 .. 1, for each thing that plays it */
static float level(unsigned seed, float rate) {
	float t = M.time * rate + (float)(seed % 97);
	float v = 0.5f + 0.3f * sinf(t * 7.1f) + 0.2f * sinf(t * 17.3f + 1.3f);
	return clampf(v * (0.6f + 0.4f * sinf(t * 0.9f)), 0, 1);
}

/* A speaker cone at a, y on the front, r round: its rim, the cone beating
 * with the music, the cap in its middle. */
static void cone(Walk *w, const Loc *L, float a, float y, float d, float r, unsigned seed) {
	float beat = 1 + 0.07f * level(seed, 1.0f);
	lball(w, BAG_BALL, L, a, d, y, 2.2f * r, 0.03f, 2.2f * r, darken(M.metal, 0.35f));
	lball(w, BAG_BALL, L, a, d - 0.006f - 0.012f * (beat - 1) * 10 * r, y, 1.9f * r * beat, 0.06f, 1.9f * r * beat, M.black);
	lball(w, BAG_BALL, L, a, d - 0.02f - 0.012f * (beat - 1) * 10 * r, y, 0.55f * r, 0.05f, 0.55f * r, darken(M.metal, 0.15f));
}

/* A level meter: n lights up from y0 to y1 at a, as many lit as it is loud,
 * green, then amber, then red at the top. */
static void meter(Walk *w, const Loc *L, float a0, float a1, float y0, float y1, float d, int n, float v) {
	float step = (y1 - y0) / (float)n;
	int lit = (int)(v * (float)n + 0.5f);
	for (int i = 0; i < n; i++) {
		SDL_Color c = i >= n - 1 ? LED_RED : i >= n * 2 / 3 ? LED_AMBER : LED_GREEN;
		float y = y0 + step * (float)i;
		lbox(w, i < lit ? BAG_LAMP : BAG_LIT, L, a0, a1, d - 0.004f, d, y + step * 0.15f, y + step * 0.85f, i < lit ? c : darken(c, 0.8f));
	}
}

/* A loudspeaker standing a0 .. a1 across, d0 .. d1 deep, h tall: its cabinet,
 * the cloth-dark baffle, a woofer or two, a tweeter, a port, little feet. */
static void speaker(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, unsigned seed) {
	float sw = a1 - a0, mid = (a0 + a1) / 2, ft = fminf(0.03f, h * 0.04f);
	SDL_Color cab = darken(M.wood, 0.35f);
	for (int i = 0; i < 4; i++) {
		float fa = i & 1 ? a1 - sw * 0.15f : a0 + sw * 0.05f, fd = i & 2 ? d1 - 0.05f : d0 + 0.02f;
		lbox(w, BAG_LIT, L, fa, fa + sw * 0.1f, fd, fd + 0.03f, 0, ft, M.black);
	}
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, ft, h, cab);
	lbox(w, BAG_LIT, L, a0 + sw * 0.04f, a1 - sw * 0.04f, d0 - 0.008f, d0, ft + h * 0.02f, h - h * 0.02f, M.black);
	float r = fminf(sw * 0.36f, (h - ft) * 0.18f);
	int woofers = (h - ft) > sw * 2.6f ? 2 : 1;
	for (int i = 0; i < woofers; i++) {
		cone(w, L, mid, ft + r * 1.4f + (float)i * r * 2.4f, d0 - 0.008f, r, seed + (unsigned)i);
	}
	float ty = ft + r * 2.4f * (float)woofers + r * 0.7f;
	if (ty + r * 0.5f > h) {
		ty = h - r * 0.55f;
	}
	lball(w, BAG_BALL, L, mid, d0 - 0.01f, ty, r * 0.75f, 0.03f, r * 0.75f, darken(M.metal, 0.3f));
	lball(w, BAG_BALL, L, mid, d0 - 0.018f, ty, r * 0.35f, 0.03f, r * 0.35f, M.steel);
	lball(w, BAG_BALL, L, mid, d0 - 0.009f, ft + h * 0.035f + r * 0.2f, r * 0.5f, 0.02f, r * 0.25f, darken(M.black, 0.6f)); /* the port */
}

/* A turntable a0 .. a1 across, d0 .. d1 deep on y: its plinth, the platter
 * turning (a mark on the label goes round), the record, the arm across it. */
static void turntable(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y, unsigned *s) {
	float tw = a1 - a0, td = d1 - d0, ph = fminf(0.09f, tw * 0.15f);
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, y + 0.01f, y + ph, M.wood);
	lbox(w, BAG_LIT, L, a0 + tw * 0.03f, a1 - tw * 0.03f, d0 + td * 0.03f, d1 - td * 0.03f, y, y + 0.01f, M.black);
	lbox(w, BAG_LIT, L, a0, a1, d0 - 0.003f, d0, y + ph * 0.3f, y + ph * 0.75f, M.steel);
	float r = fminf(tw * 0.36f, td * 0.44f), pa = a0 + tw * 0.42f, pd = (d0 + d1) / 2, top = y + ph;
	lball(w, BAG_BALL, L, pa, pd, top + 0.012f, 2 * r, 2 * r, 0.024f, M.steel);
	lball(w, BAG_BALL, L, pa, pd, top + 0.026f, 1.96f * r, 1.96f * r, 0.012f, darken(M.black, 0.4f));
	lball(w, BAG_BALL, L, pa, pd, top + 0.03f, 0.7f * r, 0.7f * r, 0.012f, mix(M.kc, hue(rnd(s), 0.6f, 0.85f), 0.5f));
	lball(w, BAG_BALL, L, pa, pd, top + 0.034f, 0.06f * r, 0.06f * r, 0.03f, M.steel);
	float ang = M.time * 3.49f; /* 33 turns a minute */
	lball(w, BAG_BALL_LAMP, L, pa + cosf(ang) * r * 0.22f, pd + sinf(ang) * r * 0.22f, top + 0.036f, r * 0.1f, r * 0.1f, 0.006f, M.white);
	lball(w, BAG_BALL, L, pa + cosf(ang + 2.1f) * r * 0.7f, pd + sinf(ang + 2.1f) * r * 0.7f, top + 0.033f, r * 0.25f, r * 0.04f, 0.003f,
	      lighten(M.black, 0.25f)); /* the light catching a groove */
	/* the arm, from its post at the back corner to the record's edge */
	float aa = a1 - tw * 0.1f, ad = d1 - td * 0.15f;
	lball(w, BAG_BALL, L, aa, ad, top + 0.02f, 0.05f, 0.05f, 0.04f, M.steel);
	lbox(w, BAG_LIT, L, aa - 0.006f, aa + 0.006f, pd - r * 0.1f, ad, top + 0.04f, top + 0.05f, M.steel);
	lbox(w, BAG_LIT, L, pa + r * 0.55f, aa + 0.006f, pd - r * 0.12f, pd - r * 0.08f, top + 0.04f, top + 0.05f, M.steel);
	lbox(w, BAG_LIT, L, pa + r * 0.5f, pa + r * 0.62f, pd - r * 0.16f, pd - r * 0.04f, top + 0.035f, top + 0.05f, M.black);
	/* its buttons, one lit: playing */
	lbox(w, BAG_LAMP, L, a0 + tw * 0.04f, a0 + tw * 0.1f, d0 + td * 0.05f, d0 + td * 0.11f, top, top + 0.006f, LED_RED);
	lball(w, BAG_BALL, L, a0 + tw * 0.07f, d0 + td * 0.2f, top + 0.006f, 0.04f, 0.04f, 0.015f, M.steel);
}

/* A boombox a0 .. a1 across, d0 .. d1 deep, h tall: two speakers beating,
 * the tape in its window between them (the file's picture on its label),
 * a meter, buttons along the top, a handle and an aerial. */
static void boombox(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, unsigned tex, unsigned *s) {
	float bw = a1 - a0, mid = (a0 + a1) / 2, bh = h * 0.72f;
	SDL_Color body = rnd(s) < 0.5f ? M.dark : hue(rnd(s), 0.5f, 0.55f);
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, 0.01f, bh, body);
	lbox(w, BAG_LIT, L, a0 + 0.01f, a1 - 0.01f, d0 + 0.01f, d1 - 0.01f, 0, 0.012f, M.black);
	lbox(w, BAG_LIT, L, a0 - 0.004f, a1 + 0.004f, d0 - 0.004f, d1 + 0.004f, bh * 0.94f, bh, lighten(body, 0.2f));
	float r = fminf(bw * 0.13f, bh * 0.3f);
	cone(w, L, a0 + bw * 0.18f, bh * 0.45f, d0 - 0.004f, r, s[0]);
	cone(w, L, a1 - bw * 0.18f, bh * 0.45f, d0 - 0.004f, r, s[0] + 1);
	float ca0 = mid - bw * 0.15f, ca1 = mid + bw * 0.15f, cy0 = bh * 0.2f, cy1 = bh * 0.62f;
	lbox(w, BAG_LIT, L, ca0, ca1, d0 - 0.006f, d0, cy0, cy1, M.black);
	lbox(w, BAG_LIT, L, ca0 + bw * 0.02f, ca1 - bw * 0.02f, d0 - 0.008f, d0 - 0.006f, cy0 + bh * 0.06f, cy1 - bh * 0.06f, M.paper);
	if (tex) {
		float side = fminf(ca1 - ca0, cy1 - cy0) * 0.5f;
		lpic(w, L, tex, false, mid - side / 2, mid + side / 2, (cy0 + cy1) / 2 - side / 2, (cy0 + cy1) / 2 + side / 2, d0 - 0.009f, M.white);
	}
	for (int i = 0; i < 2; i++) { /* the reels, turning */
		float ra = mid + (i ? 1.0f : -1.0f) * bw * 0.065f, ang = M.time * 4 + (float)i;
		lball(w, BAG_BALL, L, ra, d0 - 0.009f, cy0 + bh * 0.08f, bw * 0.05f, 0.005f, bw * 0.05f, M.black);
		lbox(w, BAG_LIT, L, ra + cosf(ang) * bw * 0.015f - 0.003f, ra + cosf(ang) * bw * 0.015f + 0.003f, d0 - 0.012f, d0 - 0.009f,
		     cy0 + bh * 0.08f + sinf(ang) * bw * 0.015f - 0.003f, cy0 + bh * 0.08f + sinf(ang) * bw * 0.015f + 0.003f, M.white);
	}
	meter(w, L, ca0, ca0 + bw * 0.02f, cy1 + bh * 0.04f, bh * 0.88f, d0, 6, level(s[0], 1.0f));
	meter(w, L, ca1 - bw * 0.02f, ca1, cy1 + bh * 0.04f, bh * 0.88f, d0, 6, level(s[0] + 3, 1.1f));
	lbox(w, BAG_LAMP, L, mid - bw * 0.1f, mid + bw * 0.1f, d0 - 0.004f, d0, cy1 + bh * 0.08f, bh * 0.86f, mix(M.accent, M.black, 0.4f));
	for (int i = 0; i < 6; i++) { /* the keys along the top */
		float ka = mid - bw * 0.15f + bw * 0.05f * (float)i;
		lbox(w, BAG_LIT, L, ka + 0.004f, ka + bw * 0.05f - 0.004f, d0 + 0.01f, d0 + 0.06f, bh, bh + 0.012f, i == 1 ? LED_RED : M.steel);
	}
	/* the handle and the aerial */
	float hh = h - bh;
	lbox(w, BAG_LIT, L, a0 + bw * 0.15f, a0 + bw * 0.18f, (d0 + d1) / 2 - 0.015f, (d0 + d1) / 2 + 0.015f, bh, bh + hh * 0.85f, M.steel);
	lbox(w, BAG_LIT, L, a1 - bw * 0.18f, a1 - bw * 0.15f, (d0 + d1) / 2 - 0.015f, (d0 + d1) / 2 + 0.015f, bh, bh + hh * 0.85f, M.steel);
	lbox(w, BAG_LIT, L, a0 + bw * 0.15f, a1 - bw * 0.15f, (d0 + d1) / 2 - 0.02f, (d0 + d1) / 2 + 0.02f, bh + hh * 0.75f, bh + hh, M.black);
	lbox(w, BAG_LIT, L, a1 - bw * 0.06f, a1 - bw * 0.05f, d1 - 0.03f, d1 - 0.02f, bh, h + hh * 0.6f, M.steel);
}

/* A crate of records: their edges in a row, sleeves of every colour. */
static void record_crate(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, unsigned *s) {
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, 0, h * 0.75f, M.wood);
	lbox(w, BAG_LIT, L, a0 + 0.015f, a1 - 0.015f, d0 + 0.015f, d1 - 0.015f, h * 0.75f - 0.01f, h * 0.75f + 0.001f, darken(M.wood, 0.5f));
	for (float d = d0 + 0.02f; d < d1 - 0.025f; d += pick(s, 0.008f, 0.016f) * (M.fine ? 1 : 4)) {
		lbox(w, BAG_LIT, L, a0 + 0.02f, a1 - 0.02f, d, d + 0.005f, h * 0.4f, h * pick(s, 0.88f, 0.98f), hue(rnd(s), 0.5f, pick(s, 0.4f, 0.85f)));
	}
	lbox(w, BAG_LIT, L, a0 + (a1 - a0) * 0.35f, a1 - (a1 - a0) * 0.35f, d0 - 0.004f, d0, h * 0.5f, h * 0.62f, darken(M.wood, 0.6f));
}

/* A hi-fi: a cabinet of stacked parts (an amplifier with its meters, a disc
 * player, a tape deck), a turntable on top, speakers either side. */
static void hifi(Walk *w, const Loc *L, float a0, float a1, float D, float h, unsigned tex, unsigned *s) {
	float cw = a1 - a0, dd = fminf(D - 0.04f, 0.5f), d0 = D - 0.02f - dd, ch = h * 0.68f;
	SDL_Color face = rnd(s) < 0.5f ? M.steel : M.dark;
	lbox(w, BAG_LIT, L, a0, a0 + 0.025f, d0, D - 0.02f, 0, ch, M.wood);
	lbox(w, BAG_LIT, L, a1 - 0.025f, a1, d0, D - 0.02f, 0, ch, M.wood);
	lbox(w, BAG_LIT, L, a0, a1, d0, D - 0.02f, ch - 0.03f, ch, M.oak);
	lbox(w, BAG_LIT, L, a0 + 0.025f, a1 - 0.025f, d0 + 0.02f, D - 0.02f, 0, 0.06f, darken(M.wood, 0.4f));
	lbox(w, BAG_LIT, L, a0 + 0.025f, a1 - 0.025f, D - 0.04f, D - 0.02f, 0.06f, ch - 0.03f, darken(M.wood, 0.5f));
	float ia0 = a0 + 0.035f, ia1 = a1 - 0.035f, iw = ia1 - ia0, y = 0.07f, room = ch - 0.1f - 0.07f;
	float uh[3] = {room * 0.38f, room * 0.3f, room * 0.32f};
	for (int i = 0; i < 3; i++) {
		float y0 = y + 0.005f, y1 = y + uh[i] - 0.01f, dh = y1 - y0;
		lbox(w, BAG_LIT, L, ia0, ia1, d0 + 0.02f, d0 + dd * 0.9f, y0, y1, darken(M.dark, 0.2f));
		lbox(w, BAG_LIT, L, ia0, ia1, d0 + 0.012f, d0 + 0.02f, y0, y1, face);
		if (i == 0) { /* the amplifier: a big knob, two meters, buttons */
			lball(w, BAG_BALL, L, ia1 - iw * 0.15f, d0 + 0.005f, y0 + dh * 0.5f, fminf(iw * 0.16f, dh * 0.7f), 0.03f, fminf(iw * 0.16f, dh * 0.7f), M.metal);
			lball(w, BAG_BALL, L, ia0 + iw * 0.12f, d0 + 0.008f, y0 + dh * 0.4f, iw * 0.06f, 0.02f, iw * 0.06f, M.metal);
			for (int m = 0; m < 2; m++) {
				float ma = ia0 + iw * (0.26f + 0.24f * (float)m), v = level(s[0] + (unsigned)m, 1.0f);
				lbox(w, BAG_LAMP, L, ma, ma + iw * 0.2f, d0 + 0.008f, d0 + 0.012f, y0 + dh * 0.3f, y1 - dh * 0.15f, (SDL_Color){255, 200, 120, 255});
				float na = ma + iw * 0.02f + iw * 0.16f * v;
				lbox(w, BAG_LAMP, L, na, na + 0.003f, d0 + 0.006f, d0 + 0.008f, y0 + dh * 0.32f, y1 - dh * 0.2f, M.black);
			}
		} else if (i == 1) { /* a disc player: its tray, a display */
			lbox(w, BAG_LIT, L, ia0 + iw * 0.06f, ia0 + iw * 0.45f, d0 + 0.006f, d0 + 0.012f, y0 + dh * 0.55f, y0 + dh * 0.75f, M.black);
			lbox(w, BAG_LAMP, L, ia0 + iw * 0.55f, ia0 + iw * 0.85f, d0 + 0.007f, d0 + 0.012f, y0 + dh * 0.35f, y0 + dh * 0.75f, darken(M.accent, 0.5f));
			lbox(w, BAG_LAMP, L, ia0 + iw * 0.58f, ia0 + iw * (0.6f + 0.2f * fmodf(M.time * 0.05f, 1.0f)), d0 + 0.006f, d0 + 0.007f,
			     y0 + dh * 0.5f, y0 + dh * 0.6f, M.accent);
		} else { /* a tape deck, two windows */
			for (int t = 0; t < 2; t++) {
				float ta = ia0 + iw * (0.08f + 0.44f * (float)t);
				lbox(w, BAG_LIT, L, ta, ta + iw * 0.36f, d0 + 0.006f, d0 + 0.012f, y0 + dh * 0.2f, y1 - dh * 0.15f, M.black);
				lbox(w, BAG_LIT, L, ta + iw * 0.05f, ta + iw * 0.31f, d0 + 0.004f, d0 + 0.006f, y0 + dh * 0.4f, y1 - dh * 0.3f, M.paper);
			}
		}
		y += uh[i];
	}
	turntable(w, L, a0 + cw * 0.06f, a1 - cw * 0.06f, d0 + 0.02f, D - 0.04f, ch, s);
	if (tex) { /* the record's sleeve, leaning on the cabinet's foot */
		float side = fminf(0.3f, fminf(cw * 0.4f, ch * 0.5f));
		lbox(w, BAG_LIT, L, a0 + cw * 0.1f, a0 + cw * 0.1f + side, d0 - 0.02f, d0 - 0.012f, 0.004f, side, M.dark);
		lpic(w, L, tex, false, a0 + cw * 0.1f + 0.01f, a0 + cw * 0.1f + side - 0.01f, 0.014f, side - 0.01f, -0.021f + d0, M.white);
	}
}

/* A jukebox a0 .. a1 across, d0 .. d1 deep, h tall: its arched top, the
 * glass with the record behind it, light tubes going round the colours, a
 * panel of buttons, the grille with the speaker beating behind it. */
static void jukebox(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, unsigned tex, unsigned *s) {
	float jw = a1 - a0, mid = (a0 + a1) / 2, bh = h - jw * 0.32f;
	SDL_Color body = mix(M.wood, M.kc, 0.25f);
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, 0.04f, bh, body);
	lbox(w, BAG_LIT, L, a0 + 0.02f, a1 - 0.02f, d0 + 0.02f, d1 - 0.02f, 0, 0.04f, M.black);
	static const float arch[4] = {1.0f, 0.9f, 0.72f, 0.42f};
	for (int i = 0; i < 4; i++) { /* the arched top, in steps */
		float y = bh + (h - bh) * (float)i / 4, aw = jw * arch[i] / 2;
		lbox(w, BAG_LIT, L, mid - aw, mid + aw, d0 + 0.01f * (float)i, d1 - 0.02f * (float)i, y, y + (h - bh) / 4, i % 2 ? body : lighten(body, 0.08f));
	}
	for (int i = 0; i < 2; i++) { /* the tubes up its front edges */
		float ta = i ? a1 - jw * 0.07f : a0 + jw * 0.01f;
		for (int seg = 0; seg < 6; seg++) {
			float y0 = 0.08f + (bh - 0.1f) * (float)seg / 6, y1 = y0 + (bh - 0.1f) / 6 - 0.004f;
			lbox(w, BAG_LAMP, L, ta, ta + jw * 0.06f, d0 - 0.025f, d0 + 0.02f, y0, y1, hue(M.time * 0.25f + (float)seg * 0.08f + (float)i * 0.5f, 0.75f, 1));
		}
	}
	lball(w, BAG_BALL_LAMP, L, mid, d0 - 0.004f, bh, jw * 0.86f, 0.02f, (h - bh) * 1.7f, hue(M.time * 0.25f + 0.3f, 0.6f, 1));
	lball(w, BAG_BALL, L, mid, d0 - 0.012f, bh, jw * 0.7f, 0.02f, (h - bh) * 1.3f, darken(M.black, 0.3f));
	/* the glass and the record inside it */
	float gy0 = bh * 0.56f, gy1 = bh * 0.92f;
	lbox(w, BAG_LIT, L, a0 + jw * 0.12f, a1 - jw * 0.12f, d0 - 0.01f, d0, gy0, gy1, M.steel);
	lbox(w, BAG_LAMP, L, a0 + jw * 0.15f, a1 - jw * 0.15f, d0 - 0.013f, d0 - 0.01f, gy0 + 0.02f, gy1 - 0.02f, mix(M.accent, M.black, 0.75f));
	float rr = fminf(jw * 0.3f, (gy1 - gy0) * 0.4f);
	lball(w, BAG_BALL, L, mid, d0 - 0.014f, (gy0 + gy1) / 2, rr * 2, 0.004f, rr * 2, M.black);
	if (tex) {
		lpic(w, L, tex, true, mid - rr * 0.55f, mid + rr * 0.55f, (gy0 + gy1) / 2 - rr * 0.55f, (gy0 + gy1) / 2 + rr * 0.55f, d0 - 0.017f, M.white);
	}
	/* the buttons: one lit runs along them */
	int lit = (int)(M.time * 1.5f) % 10;
	for (int i = 0; i < 10; i++) {
		float ba = a0 + jw * 0.18f + jw * 0.064f * (float)(i % 10);
		lbox(w, i == lit ? BAG_LAMP : BAG_LIT, L, ba, ba + jw * 0.05f, d0 - 0.014f, d0, bh * 0.47f, bh * 0.52f, i == lit ? LED_AMBER : M.paper);
	}
	/* the grille, slats over the beating speaker */
	float sy0 = bh * 0.1f, sy1 = bh * 0.42f;
	cone(w, L, mid, (sy0 + sy1) / 2, d0 - 0.002f, fminf(jw * 0.22f, (sy1 - sy0) * 0.4f), s[0]);
	for (int i = 0; i < 7; i++) {
		float y = sy0 + (sy1 - sy0) * (float)i / 7;
		lbox(w, BAG_LIT, L, a0 + jw * 0.1f, a1 - jw * 0.1f, d0 - 0.04f, d0 - 0.03f, y, y + (sy1 - sy0) * 0.05f, M.steel);
	}
	lbox(w, BAG_LIT, L, a0 + jw * 0.1f, a0 + jw * 0.12f, d0 - 0.04f, d0, sy0, sy1, M.steel);
	lbox(w, BAG_LIT, L, a1 - jw * 0.12f, a1 - jw * 0.1f, d0 - 0.04f, d0, sy0, sy1, M.steel);
}

/* Audio: something that plays it. Small, a boombox or a turntable on a low
 * stand; middling, a hi-fi between its speakers; big, a jukebox. */
static void make_audio(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d;
	unsigned tex = k->tex;
	if (H < 0.9f) {
		float bw = fminf(W - 0.04f, H * 2.2f), bd = fminf(D - 0.04f, H * 0.7f);
		if (rnd(&s) < 0.5f) {
			boombox(w, L, (W - bw) / 2, (W + bw) / 2, D - 0.02f - bd, D - 0.02f, H, tex, &s);
		} else { /* a turntable on a low cabinet */
			float ty = H * 0.55f;
			desk(w, L, 0.02f, W - 0.02f, 0.02f, D - 0.02f, ty, &s);
			turntable(w, L, 0.06f, W - 0.06f, 0.06f, D - 0.06f, ty, &s);
		}
		if ((W - bw) / 2 > 0.3f) { /* records either side */
			record_crate(w, L, 0.03f, (W - bw) / 2 - 0.05f, 0.04f, fminf(D - 0.04f, 0.4f), fminf(H, 0.34f), &s);
			record_crate(w, L, (W + bw) / 2 + 0.05f, W - 0.03f, 0.04f, fminf(D - 0.04f, 0.4f), fminf(H, 0.34f), &s);
		}
		return;
	}
	if (H < 1.75f) {
		float sw = clampf(W * 0.2f, 0.2f, H * 0.38f), sd = fminf(D - 0.04f, sw * 1.3f);
		bool pair = W > sw * 2 + 0.4f;
		float c0 = pair ? sw + 0.06f : 0.02f, c1 = pair ? W - sw - 0.06f : W - 0.02f;
		if (pair) {
			speaker(w, L, 0.02f, 0.02f + sw, D - 0.02f - sd, D - 0.02f, H, s);
			speaker(w, L, W - 0.02f - sw, W - 0.02f, D - 0.02f - sd, D - 0.02f, H, s + 5);
		}
		hifi(w, L, c0, c1, D, H, tex, &s);
		return;
	}
	/* a jukebox; tall speakers either side where there is room */
	float jw = fminf(W - 0.04f, fminf(H * 0.55f, 1.1f)), jd = fminf(D - 0.04f, jw * 0.7f);
	jukebox(w, L, (W - jw) / 2, (W + jw) / 2, D - 0.02f - jd, D - 0.02f, H, tex, &s);
	float room = (W - jw) / 2 - 0.08f;
	if (room > 0.22f) {
		float sw = fminf(room, H * 0.3f), sd = fminf(D - 0.04f, sw * 1.2f);
		speaker(w, L, (W - jw) / 2 - 0.05f - sw, (W - jw) / 2 - 0.05f, D - 0.02f - sd, D - 0.02f, H * 0.85f, s + 1);
		speaker(w, L, (W + jw) / 2 + 0.05f, (W + jw) / 2 + 0.05f + sw, D - 0.02f - sd, D - 0.02f, H * 0.85f, s + 2);
	}
}

/* ---- pictures and films ---- */

/* the picture's corners in a0 .. a1 by y0 .. y1, its own shape kept */
static void fit_pic(float aspect, float *a0, float *a1, float *y0, float *y1) {
	if (aspect <= 0) {
		return;
	}
	float w = *a1 - *a0, h = *y1 - *y0;
	if (w / h > aspect) {
		float nw = h * aspect, c = (*a0 + *a1) / 2;
		*a0 = c - nw / 2;
		*a1 = c + nw / 2;
	} else {
		float nh = w / aspect, c = (*y0 + *y1) / 2;
		*y0 = c - nh / 2;
		*y1 = c + nh / 2;
	}
}

/* A picture: on an easel, its tray and legs, a stool and a table of paints
 * beside it where there is room; or framed in gilt on a stand. */
static void make_picture(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d;
	bool easel = rnd(&s) < 0.6f;
	float ew = fminf(W - 0.04f, fmaxf(H * 0.8f, 0.3f)), ea0 = easel && W > ew + 0.6f ? 0.04f : (W - ew) / 2, ea1 = ea0 + ew;
	float a0 = ea0 + ew * 0.06f, a1 = ea1 - ew * 0.06f, y0 = H * (easel ? 0.34f : 0.3f), y1 = H * 0.97f;
	fit_pic(k->tex ? k->aspect : 0.8f, &a0, &a1, &y0, &y1);
	float fd = fminf(D * 0.45f, 0.3f), fr = clampf((a1 - a0) * 0.06f, 0.02f, 0.07f);
	SDL_Color frame = easel ? M.wood : (SDL_Color){196, 160, 76, 255};
	if (easel) { /* legs, the tray the canvas stands on, the back leg */
		SDL_Color leg = M.oak;
		float t = 0.025f, mid = (ea0 + ea1) / 2;
		lbox(w, BAG_LIT, L, a0 + (a1 - a0) * 0.12f, a0 + (a1 - a0) * 0.12f + t, fd + 0.02f, fd + 0.02f + t, 0, H, leg);
		lbox(w, BAG_LIT, L, a1 - (a1 - a0) * 0.12f - t, a1 - (a1 - a0) * 0.12f, fd + 0.02f, fd + 0.02f + t, 0, H, leg);
		lbox(w, BAG_LIT, L, mid - t / 2, mid + t / 2, fminf(D - 0.03f, fd + H * 0.35f), fminf(D - 0.03f, fd + H * 0.35f) + t, 0, H * 0.85f, leg);
		lbox(w, BAG_LIT, L, mid - t / 2, mid + t / 2, fd + 0.02f, fminf(D - 0.03f, fd + H * 0.35f), H * 0.82f, H * 0.82f + t, leg);
		lbox(w, BAG_LIT, L, a0 - 0.04f, a1 + 0.04f, fd - 0.03f, fd + 0.05f, y0 - 0.04f, y0, leg);
		lbox(w, BAG_LIT, L, a0 - 0.04f, a1 + 0.04f, fd - 0.035f, fd - 0.03f, y0 - 0.04f, y0 + 0.02f, leg);
		lbox(w, BAG_LIT, L, mid - 0.03f, mid + 0.03f, fd + 0.02f, fd + 0.05f, y1 - 0.02f, y1 + 0.04f, leg);
		lbox(w, BAG_LIT, L, a0 + (a1 - a0) * 0.12f, a1 - (a1 - a0) * 0.12f, fd + 0.02f, fd + 0.02f + t, H * 0.18f, H * 0.18f + t, leg);
	} else { /* a stand: a plinth with a post */
		float mid = (a0 + a1) / 2, pw = fmaxf((a1 - a0) * 0.5f, 0.2f);
		lbox(w, BAG_LIT, L, mid - pw / 2, mid + pw / 2, 0.04f, fminf(D - 0.04f, 0.5f), 0, 0.06f, M.dark);
		lbox(w, BAG_LIT, L, mid - pw / 2 + 0.01f, mid + pw / 2 - 0.01f, 0.05f, fminf(D - 0.05f, 0.49f), 0.06f, 0.075f, M.metal);
		lbox(w, BAG_LIT, L, mid - 0.025f, mid + 0.025f, fd + 0.03f, fd + 0.08f, 0.07f, y1 - (y1 - y0) * 0.3f, M.metal);
	}
	/* the frame: its moulding, a lip, the mount, the picture */
	lbox(w, BAG_LIT, L, a0 - fr, a1 + fr, fd, fd + 0.03f, y0 - fr, y1 + fr * (easel ? 0.4f : 1), frame);
	lbox(w, BAG_LIT, L, a0 - fr * 0.55f, a1 + fr * 0.55f, fd - 0.012f, fd, y0 - fr * 0.55f, y1 + fr * 0.55f, lighten(frame, 0.2f));
	if (!easel) {
		lbox(w, BAG_LIT, L, a0 - fr * 0.3f, a1 + fr * 0.3f, fd - 0.016f, fd - 0.012f, y0 - fr * 0.3f, y1 + fr * 0.3f, M.paper);
	}
	if (k->tex) {
		lpic(w, L, k->tex, false, a0, a1, y0, y1, fd - 0.018f, M.white);
	} else {
		lbox(w, BAG_LIT, L, a0, a1, fd - 0.018f, fd - 0.014f, y0, y1, M.kc);
	}
	if (easel && W > ew + 0.6f) { /* a table of paints and a stool */
		float ta0 = ea1 + 0.1f, ta1 = fminf(W - 0.04f, ta0 + 0.6f), ty = fminf(H * 0.4f, 0.7f);
		desk(w, L, ta0, ta1, 0.06f, fminf(D - 0.04f, 0.5f), ty, &s);
		float pc = (ta0 + ta1) / 2, sc = clampf((ta1 - ta0) / 0.6f, 0.5f, 1);
		lball(w, BAG_BALL, L, pc, 0.2f, ty + 0.006f, 0.3f * sc, 0.2f * sc, 0.012f, M.oak); /* the palette, its paints */
		for (int i = 0; i < 6; i++) {
			float ang = (float)i * 0.9f;
			lball(w, BAG_BALL, L, pc + cosf(ang) * 0.09f * sc, 0.2f + sinf(ang) * 0.06f * sc, ty + 0.013f, 0.03f * sc, 0.03f * sc, 0.01f,
			      hue((float)i / 6 + rnd(&s) * 0.1f, 0.8f, 0.9f));
		}
		for (int i = 0; i < 3; i++) { /* tubes and a jar of brushes */
			float ta = ta0 + 0.06f + 0.06f * (float)i;
			lbox(w, BAG_LIT, L, ta, ta + 0.025f, 0.32f, 0.42f, ty, ty + 0.025f, hue(rnd(&s), 0.7f, 0.8f));
		}
		pens(w, L, ta1 - 0.1f, 0.38f, ty, sc, &s);
	}
}

/* A film: small, an old set with its aerial; middling, a flat screen on a
 * cabinet; big, a cinema screen on its stand and a projector throwing the
 * film at it. The screen flickers with the picture's light. */
static void make_video(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d, ph = (float)(s % 100);
	SDL_Color light = mix(darken(M.kc, 0.3f), M.white, 0.15f + 0.1f * sinf(M.time * 3.1f + ph) + 0.05f * sinf(M.time * 11 + ph));
	if (H < 0.9f) { /* an old set */
		float tw = fminf(W - 0.04f, H * 1.4f), td = fminf(D - 0.04f, H * 1.0f), a0 = (W - tw) / 2, a1 = a0 + tw;
		SDL_Color wood = M.wood;
		lbox(w, BAG_LIT, L, a0, a1, D - 0.02f - td, D - 0.02f, 0.04f, H * 0.85f, wood);
		for (int i = 0; i < 4; i++) {
			float la = i & 1 ? a1 - 0.05f : a0 + 0.02f, ld = i & 2 ? D - 0.06f : D - td;
			lbox(w, BAG_LIT, L, la, la + 0.03f, ld, ld + 0.03f, 0, 0.04f, M.dark);
		}
		float d0 = D - 0.02f - td;
		lbox(w, BAG_LIT, L, a0 + tw * 0.05f, a0 + tw * 0.7f, d0 - 0.008f, d0, H * 0.12f, H * 0.8f, M.dark);
		lbox(w, BAG_LAMP, L, a0 + tw * 0.08f, a0 + tw * 0.67f, d0 - 0.014f, d0 - 0.008f, H * 0.16f, H * 0.76f, light);
		if (k->tex) {
			float side = fminf(tw * 0.35f, H * 0.35f), cu = a0 + tw * 0.375f, cy = H * 0.46f;
			lpic(w, L, k->tex, true, cu - side / 2, cu + side / 2, cy - side / 2, cy + side / 2, d0 - 0.016f, M.white);
		}
		for (int i = 0; i < 2; i++) {
			lball(w, BAG_BALL, L, a0 + tw * 0.85f, d0 - 0.01f, H * (0.6f - 0.22f * (float)i), tw * 0.1f, 0.025f, tw * 0.1f, M.steel);
		}
		for (int i = 0; i < 6; i++) {
			lbox(w, BAG_LIT, L, a0 + tw * 0.78f, a0 + tw * 0.92f, d0 - 0.004f, d0, H * (0.15f + 0.03f * (float)i), H * (0.165f + 0.03f * (float)i), M.black);
		}
		float mid = (a0 + a1) / 2, md = d0 + td * 0.6f;
		lball(w, BAG_BALL, L, mid, md, H * 0.85f, 0.06f, 0.06f, 0.03f, M.dark);
		for (int i = -1; i <= 1; i += 2) { /* the aerial's two rods, splayed in steps */
			for (int j = 0; j < 4; j++) {
				float a = mid + (float)i * 0.03f * (float)(j + 1), y = H * 0.85f + 0.06f * (float)j;
				lbox(w, BAG_LIT, L, a - 0.005f, a + 0.005f, md - 0.005f, md + 0.005f, y, y + 0.065f, M.steel);
			}
		}
		return;
	}
	if (H < 1.7f || W < 1.2f) { /* a cabinet with a screen on it, a box with its light, a sound bar */
		float ch = clampf(H * 0.3f, 0.2f, 0.5f), cd = fminf(D - 0.04f, 0.45f), d0 = D - 0.02f - cd;
		lbox(w, BAG_LIT, L, 0.02f, W - 0.02f, d0, D - 0.02f, 0.06f, ch, M.wood);
		lbox(w, BAG_LIT, L, 0.01f, W - 0.01f, d0 - 0.01f, D - 0.015f, ch - 0.025f, ch, M.oak);
		lbox(w, BAG_LIT, L, 0.05f, W - 0.05f, d0 + 0.02f, D - 0.04f, 0, 0.06f, M.black);
		int doors = across(W, 0.45f, 6);
		float dw = (W - 0.06f) / (float)doors;
		for (int i = 0; i < doors; i++) {
			float da = 0.03f + dw * (float)i;
			lbox(w, BAG_LIT, L, da + 0.006f, da + dw - 0.006f, d0 - 0.008f, d0, 0.075f, ch - 0.035f, M.oak);
			lbox(w, BAG_LIT, L, da + dw * 0.12f, da + dw * 0.88f, d0 - 0.012f, d0 - 0.008f, 0.075f + (ch - 0.11f) * 0.15f, ch - 0.035f - (ch - 0.11f) * 0.15f,
			     lighten(M.oak, 0.06f));
			float ha = i % 2 ? da + dw * 0.1f : da + dw * 0.86f;
			lbox(w, BAG_LIT, L, ha, ha + 0.012f, d0 - 0.024f, d0 - 0.008f, ch * 0.45f, ch * 0.65f, M.steel);
		}
		/* the screen */
		float tw = fminf(W * 0.9f, (H - ch) * 1.9f), mid = W / 2, td = d0 + cd * 0.55f;
		float th = fminf(tw * 0.5625f, (H - ch) * 0.86f), ty1 = H, ty0 = H - th, bz = fmaxf(tw * 0.012f, 0.01f);
		lbox(w, BAG_LIT, L, mid - tw / 2, mid + tw / 2, td, td + 0.035f, ty0, ty1, M.black);
		lbox(w, BAG_LIT, L, mid - 0.04f, mid + 0.04f, td + 0.035f, td + 0.06f, ch, ty0 + th * 0.4f, M.dark);
		lbox(w, BAG_LIT, L, mid - tw * 0.15f, mid + tw * 0.15f, td - 0.05f, td + 0.12f, ch, ch + 0.012f, M.dark);
		lbox(w, BAG_LAMP, L, mid - tw / 2 + bz, mid + tw / 2 - bz, td - 0.004f, td, ty0 + bz, ty1 - bz, light);
		for (int i = 0; i < 3; i++) { /* the film: patches of light that drift */
			float u = 0.5f + 0.35f * sinf(M.time * (0.3f + 0.17f * (float)i) + ph + (float)i * 2);
			float v = 0.5f + 0.3f * sinf(M.time * (0.23f + 0.11f * (float)i) + ph * 0.7f + (float)i);
			float pw = tw * 0.22f, pv = th * 0.25f, pa = mid - tw / 2 + bz + (tw - 2 * bz - pw) * u, py = ty0 + bz + (th - 2 * bz - pv) * v;
			lbox(w, BAG_LAMP, L, pa, pa + pw, td - 0.006f, td - 0.004f, py, py + pv, hue((float)i * 0.3f + M.time * 0.02f, 0.4f, 0.55f));
		}
		if (k->tex) {
			float side = fminf(tw, th) * 0.3f;
			lpic(w, L, k->tex, true, mid - side / 2, mid + side / 2, (ty0 + ty1) / 2 - side / 2, (ty0 + ty1) / 2 + side / 2, td - 0.008f, M.white);
		}
		lbox(w, BAG_LIT, L, mid - tw * 0.32f, mid + tw * 0.32f, d0 + 0.02f, d0 + 0.1f, ch, ch + 0.06f, M.dark);
		lbox(w, BAG_LIT, L, mid - tw * 0.31f, mid + tw * 0.31f, d0 + 0.016f, d0 + 0.02f, ch + 0.006f, ch + 0.054f, M.black);
		float ba = 0.08f;
		if (ba + 0.25f < mid - tw * 0.32f) {
			lbox(w, BAG_LIT, L, ba, ba + 0.25f, d0 + 0.05f, d0 + 0.25f, ch, ch + 0.05f, M.black);
			lbox(w, BAG_LAMP, L, ba + 0.2f, ba + 0.215f, d0 + 0.046f, d0 + 0.05f, ch + 0.02f, ch + 0.03f, LED_BLUE);
		}
		return;
	}
	/* a cinema screen at the back, a projector on a table at the front, its beam */
	float sw = W - 0.1f, sh = fminf(H * 0.7f, sw * 0.56f), sy1 = H - 0.05f, sy0 = sy1 - sh, sd = D - 0.12f;
	for (int i = 0; i < 2; i++) {
		float la = i ? W - 0.08f : 0.03f;
		lbox(w, BAG_LIT, L, la, la + 0.05f, sd, sd + 0.05f, 0, H, M.dark);
		lbox(w, BAG_LIT, L, la - 0.08f, la + 0.13f, sd - 0.2f, sd + 0.25f, 0, 0.04f, M.dark);
	}
	lbox(w, BAG_LIT, L, 0.03f, W - 0.03f, sd - 0.01f, sd + 0.06f, sy1, H, M.dark);
	lbox(w, BAG_LIT, L, 0.08f, W - 0.08f, sd, sd + 0.02f, sy0 - 0.03f, sy1, M.black);
	lbox(w, BAG_LAMP, L, 0.1f, W - 0.1f, sd - 0.004f, sd, sy0, sy1 - 0.02f, light);
	for (int i = 0; i < 4; i++) {
		float u = 0.5f + 0.4f * sinf(M.time * (0.3f + 0.13f * (float)i) + ph + (float)i * 2);
		float v = 0.5f + 0.3f * sinf(M.time * (0.21f + 0.1f * (float)i) + ph * 0.7f + (float)i);
		float pw = sw * 0.2f, pv = sh * 0.25f, pa = 0.1f + (sw - 0.1f - pw) * u, py = sy0 + (sh - 0.02f - pv) * v;
		lbox(w, BAG_LAMP, L, pa, pa + pw, sd - 0.006f, sd - 0.004f, py, py + pv, hue((float)i * 0.27f + M.time * 0.02f, 0.4f, 0.6f));
	}
	if (k->tex) {
		float side = sh * 0.35f;
		lpic(w, L, k->tex, true, W / 2 - side / 2, W / 2 + side / 2, (sy0 + sy1) / 2 - side / 2, (sy0 + sy1) / 2 + side / 2, sd - 0.008f, M.white);
	}
	float ty = 0.7f, pa = W / 2, pd0 = 0.1f;
	desk(w, L, pa - 0.3f, pa + 0.3f, pd0, pd0 + 0.45f, ty, &s);
	lbox(w, BAG_LIT, L, pa - 0.16f, pa + 0.16f, pd0 + 0.08f, pd0 + 0.38f, ty + 0.01f, ty + 0.13f, M.steel);
	lbox(w, BAG_LIT, L, pa - 0.14f, pa + 0.14f, pd0 + 0.08f, pd0 + 0.38f, ty, ty + 0.01f, M.black);
	lball(w, BAG_BALL, L, pa + 0.07f, pd0 + 0.385f, ty + 0.07f, 0.09f, 0.04f, 0.09f, M.black);
	lball(w, BAG_BALL_LAMP, L, pa + 0.07f, pd0 + 0.4f, ty + 0.07f, 0.06f, 0.02f, 0.06f, lighten(light, 0.5f));
	SDL_Color beam = lighten(light, 0.4f);
	beam.a = 35;
	float by = (sy0 + sy1) / 2;
	lbox(w, BAG_GLOW, L, pa - 0.04f, pa + 0.18f, pd0 + 0.41f, sd - 0.01f, fminf(ty + 0.05f, by), fmaxf(ty + 0.09f, by * 0.6f + ty * 0.4f), beam);
}

/* ---- boxes, drives and the rest ---- */

/* A wooden crate a0 .. a1, d0 .. d1, y0 .. y1: planks round it with dark
 * gaps between, posts at the corners, its lid, the file's icon stamped on. */
static void crate(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y0, float y1, unsigned tex, unsigned *s) {
	SDL_Color wood = mix(M.oak, (SDL_Color){190, 150, 100, 255}, rnd(s) * 0.4f), gap = darken(wood, 0.6f);
	float h = y1 - y0, e = 0.012f, post = fminf(0.06f, fminf(a1 - a0, d1 - d0) * 0.1f);
	lbox(w, BAG_LIT, L, a0 + e, a1 - e, d0 + e, d1 - e, y0, y1 - 0.01f, gap);
	int n = M.fine ? (int)(h / 0.13f) : 2;
	n = n < 2 ? 2 : n > 12 ? 12 : n;
	float ph = h / (float)n;
	for (int i = 0; i < n; i++) {
		float p0 = y0 + ph * (float)i + 0.006f, p1 = y0 + ph * (float)(i + 1) - 0.006f;
		SDL_Color c = rnd(s) < 0.3f ? darken(wood, 0.08f) : wood;
		lbox(w, BAG_LIT, L, a0, a1, d0, d0 + e, p0, p1, c);
		lbox(w, BAG_LIT, L, a0, a1, d1 - e, d1, p0, p1, c);
		lbox(w, BAG_LIT, L, a0, a0 + e, d0, d1, p0, p1, c);
		lbox(w, BAG_LIT, L, a1 - e, a1, d0, d1, p0, p1, c);
	}
	for (int i = 0; i < 4; i++) {
		float pa = i & 1 ? a1 - post : a0, pd = i & 2 ? d1 - post : d0;
		lbox(w, BAG_LIT, L, pa - 0.006f, pa + post + 0.006f, pd - 0.006f, pd + post + 0.006f, y0, y1, darken(wood, 0.18f));
	}
	int lid = (int)((a1 - a0) / 0.14f);
	lid = lid < 2 ? 2 : lid > 12 ? 12 : lid;
	float lw = (a1 - a0) / (float)lid;
	for (int i = 0; i < lid; i++) {
		lbox(w, BAG_LIT, L, a0 + lw * (float)i + 0.004f, a0 + lw * (float)(i + 1) - 0.004f, d0, d1, y1 - 0.02f, y1, i % 3 ? wood : darken(wood, 0.06f));
	}
	if (tex) { /* stamped on in ink */
		float side = fminf((a1 - a0) * 0.4f, h * 0.5f), mid = (a0 + a1) / 2, cy = y0 + h * 0.5f;
		lpic(w, L, tex, false, mid - side / 2, mid + side / 2, cy - side / 2, cy + side / 2, d0 - 0.003f, (SDL_Color){60, 40, 30, 255});
	}
}

/* A pallet: boards over blocks. */
static void pallet(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h) {
	SDL_Color c = mix(M.oak, (SDL_Color){200, 170, 120, 255}, 0.4f);
	for (int i = 0; i < 3; i++) {
		float a = a0 + (a1 - a0 - 0.1f) * (float)i / 2;
		for (int j = 0; j < 3; j++) {
			float d = d0 + (d1 - d0 - 0.1f) * (float)j / 2;
			lbox(w, BAG_LIT, L, a, a + 0.1f, d, d + 0.1f, 0.02f, h - 0.022f, darken(c, 0.1f));
		}
		lbox(w, BAG_LIT, L, a, a + 0.1f, d0, d1, 0, 0.022f, c);
	}
	int n = (int)((a1 - a0) / 0.16f);
	n = n < 3 ? 3 : n;
	for (int i = 0; i < n; i++) {
		float a = a0 + (a1 - a0) * (float)i / (float)n;
		lbox(w, BAG_LIT, L, a + 0.01f, a + (a1 - a0) / (float)n - 0.01f, d0, d1, h - 0.022f, h, c);
	}
}

/* A filing cabinet a0 .. a1, d0 .. d1, h tall: drawers, each with its frame,
 * its handle and a card in its holder; a lock at the top. */
static void cabinet(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, SDL_Color c, unsigned *s) {
	float cw = a1 - a0, mid = (a0 + a1) / 2;
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, 0.02f, h, c);
	lbox(w, BAG_LIT, L, a0 + 0.02f, a1 - 0.02f, d0 + 0.02f, d1 - 0.02f, 0, 0.02f, M.black);
	lbox(w, BAG_LIT, L, a0 - 0.005f, a1 + 0.005f, d0 - 0.005f, d1 + 0.005f, h - 0.015f, h, lighten(c, 0.1f));
	int n = (int)((h - 0.06f) / 0.3f + 0.5f);
	n = n < 1 ? 1 : n > 6 ? 6 : n;
	float dh = (h - 0.06f) / (float)n;
	for (int i = 0; i < n; i++) {
		float y0 = 0.03f + dh * (float)i + 0.006f, y1 = y0 + dh - 0.012f;
		lbox(w, BAG_LIT, L, a0 + 0.012f, a1 - 0.012f, d0 - 0.012f, d0, y0, y1, lighten(c, 0.05f));
		lbox(w, BAG_LIT, L, a0 + 0.03f, a1 - 0.03f, d0 - 0.015f, d0 - 0.012f, y0 + 0.015f, y1 - 0.015f, lighten(c, 0.12f));
		float hy = y1 - dh * 0.3f;
		lbox(w, BAG_LIT, L, mid - cw * 0.2f, mid + cw * 0.2f, d0 - 0.035f, d0 - 0.015f, hy, hy + 0.015f, M.steel);
		lbox(w, BAG_LIT, L, mid - cw * 0.2f, mid - cw * 0.18f, d0 - 0.035f, d0 - 0.015f, hy, hy + 0.03f, M.steel);
		lbox(w, BAG_LIT, L, mid + cw * 0.18f, mid + cw * 0.2f, d0 - 0.035f, d0 - 0.015f, hy, hy + 0.03f, M.steel);
		lbox(w, BAG_LIT, L, mid - cw * 0.12f, mid + cw * 0.12f, d0 - 0.018f, d0 - 0.015f, hy + 0.045f, hy + 0.085f, M.steel);
		lbox(w, BAG_LIT, L, mid - cw * 0.1f, mid + cw * 0.1f, d0 - 0.019f, d0 - 0.017f, hy + 0.05f, hy + 0.08f, M.paper);
	}
	lball(w, BAG_BALL, L, mid + cw * 0.32f, d0 - 0.015f, h - 0.05f, 0.025f, 0.012f, 0.025f, M.steel);
	if (rnd(s) < 0.4f && cw > 0.35f) { /* papers on top */
		papers(w, L, a0 + 0.04f, d0 + 0.05f, h, fminf(1, cw / 0.5f), s);
	}
}

/* An archive: a wooden crate; bigger, filing cabinets or crates stacked on a
 * pallet. */
static void make_archive(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d, r = rnd(&s);
	if (H < 0.75f || (r < 0.35f && H < 1.4f)) {
		crate(w, L, 0.02f, W - 0.02f, 0.02f, D - 0.02f, 0, H, k->tex, &s);
		return;
	}
	if (r < 0.7f) {
		static const SDL_Color paint[4] = {{120, 128, 120, 255}, {170, 172, 168, 255}, {90, 104, 120, 255}, {196, 186, 160, 255}};
		SDL_Color c = mix(paint[(int)(rnd(&s) * 4) % 4], M.kc, 0.08f);
		int n = across(W, 0.5f, 10);
		float cw = W / (float)n, cd = fminf(D - 0.04f, 0.7f);
		for (int i = 0; i < n; i++) {
			cabinet(w, L, cw * (float)i + 0.01f, cw * (float)(i + 1) - 0.01f, 0.02f, 0.02f + cd, H, c, &s);
		}
		if (k->tex) {
			float side = fminf(cw * 0.3f, 0.16f);
			lpic(w, L, k->tex, false, cw / 2 - side / 2, cw / 2 + side / 2, H - 0.02f - side * 1.2f, H - 0.02f - side * 0.2f, -0.022f, M.white);
		}
		return;
	}
	/* crates stacked on a pallet */
	float ph = 0.14f;
	pallet(w, L, 0.02f, W - 0.02f, 0.02f, D - 0.02f, ph);
	int na = across(W, 0.9f, 4), nd = across(D, 0.9f, 4), ny = across(H - ph, 0.7f, 4);
	float ca = (W - 0.04f) / (float)na, cdd = (D - 0.04f) / (float)nd, cy = (H - ph) / (float)ny;
	for (int i = 0; i < na; i++) {
		for (int j = 0; j < nd; j++) {
			for (int l = 0; l < ny; l++) {
				if (i > 0 && i < na - 1 && j > 0 && j < nd - 1 && l < ny - 1) {
					continue; /* hidden inside */
				}
				float a0 = 0.02f + ca * (float)i + 0.01f, d0 = 0.02f + cdd * (float)j + 0.01f, y0 = ph + cy * (float)l;
				crate(w, L, a0, a0 + ca - 0.02f, d0, d0 + cdd - 0.02f, y0, y0 + cy - 0.005f, j == 0 ? k->tex : 0, &s);
			}
		}
	}
}

/* A drive with its lid off: the platter turning, the arm over it (it
 * twitches when the drive is busy), screws at the corners, its light. */
static void open_drive(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float h, bool busy) {
	float dw = a1 - a0, dd = d1 - d0, top = h;
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, 0, top - 0.015f, M.steel);
	lbox(w, BAG_LIT, L, a0 + 0.015f, a1 - 0.015f, d0 + 0.015f, d1 - 0.015f, top - 0.015f, top - 0.005f, darken(M.metal, 0.2f));
	float r = fminf(dw, dd) * 0.4f, pa = a0 + dw * 0.45f, pd = d0 + dd * 0.55f;
	lball(w, BAG_BALL, L, pa, pd, top - 0.002f, 2 * r, 2 * r, 0.012f, lighten(M.steel, 0.4f));
	lball(w, BAG_BALL, L, pa, pd, top + 0.004f, r * 0.3f, r * 0.3f, 0.012f, M.metal);
	float ang = M.time * 12;
	lball(w, BAG_BALL, L, pa + cosf(ang) * r * 0.6f, pd + sinf(ang) * r * 0.6f, top + 0.0045f, r * 0.5f, r * 0.12f, 0.002f, M.white);
	float aa = a1 - dw * 0.1f, ad = d0 + dd * 0.15f, reach = r * (0.75f + (busy ? 0.15f * sinf(M.time * 40) : 0));
	lball(w, BAG_BALL, L, aa, ad, top + 0.006f, 0.06f, 0.06f, 0.02f, M.dark);
	lbox(w, BAG_LIT, L, aa - reach, aa, ad - 0.01f, ad + 0.01f, top + 0.006f, top + 0.012f, M.dark);
	lbox(w, BAG_LIT, L, aa - reach - 0.015f, aa - reach + 0.015f, ad - 0.012f, ad + 0.012f, top + 0.004f, top + 0.012f, M.black);
	for (int i = 0; i < 4; i++) {
		float sa = i & 1 ? a1 - 0.03f : a0 + 0.03f, sd = i & 2 ? d1 - 0.03f : d0 + 0.03f;
		lball(w, BAG_BALL, L, sa, sd, top - 0.004f, 0.018f, 0.018f, 0.008f, M.dark);
	}
	lbox(w, busy ? BAG_LAMP : BAG_LIT, L, a0 + dw * 0.8f, a0 + dw * 0.9f, d0 - 0.004f, d0, h * 0.3f, h * 0.5f, busy ? M.accent : darken(M.accent, 0.6f));
}

/* A disk image or a disk: small, a drive with its lid off; bigger, an
 * enclosure of drive bays with trays, lights and a little display. */
static void make_disk(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d;
	bool busy = k->dev ? k->activity > 0.05f && fmodf(M.time * 14, 1.0f) > 0.3f : fmodf(M.time * 0.7f + rnd(&s), 1.0f) > 0.8f;
	if (H < 0.75f) {
		open_drive(w, L, 0.02f, W - 0.02f, 0.02f, D - 0.02f, H, busy);
		return;
	}
	SDL_Color body = rnd(&s) < 0.5f ? M.dark : M.steel, trim = rnd(&s) < 0.5f ? M.accent : M.kc;
	float a0 = 0.02f, a1 = W - 0.02f, d0 = 0.02f, d1 = D - 0.02f, ew = a1 - a0;
	for (int i = 0; i < 4; i++) {
		float fa = i & 1 ? a1 - 0.08f : a0 + 0.03f, fd = i & 2 ? d1 - 0.08f : d0 + 0.03f;
		lbox(w, BAG_LIT, L, fa, fa + 0.05f, fd, fd + 0.05f, 0, 0.025f, M.black);
	}
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, 0.025f, H, body);
	lbox(w, BAG_LIT, L, a0 - 0.006f, a1 + 0.006f, d0 - 0.006f, d1 + 0.006f, H - 0.02f, H, lighten(body, 0.15f));
	lbox(w, BAG_LIT, L, a0 - 0.006f, a1 + 0.006f, d0 - 0.006f, d1 + 0.006f, 0.025f, 0.045f, lighten(body, 0.15f));
	lbox(w, BAG_LAMP, L, a0, a1, d0 - 0.008f, d0 - 0.004f, H - 0.035f, H - 0.028f, trim);
	/* the bays: in columns on a wide one, each a tray with its handle and light */
	int cols = across(ew, 0.45f, 6), rows = (int)((H - 0.16f) / 0.12f);
	rows = rows < 1 ? 1 : rows > 16 ? 16 : rows;
	float cw = (ew - 0.04f) / (float)cols, bh = (H - 0.16f) / (float)rows;
	for (int c = 0; c < cols; c++) {
		for (int r = 0; r < rows; r++) {
			float ba0 = a0 + 0.02f + cw * (float)c + 0.006f, ba1 = ba0 + cw - 0.012f, y0 = 0.06f + bh * (float)r + 0.005f, y1 = y0 + bh - 0.01f;
			lbox(w, BAG_LIT, L, ba0, ba1, d0 - 0.01f, d0, y0, y1, darken(M.metal, 0.4f));
			lbox(w, BAG_LIT, L, ba0 + 0.01f, ba1 - (ba1 - ba0) * 0.25f, d0 - 0.016f, d0 - 0.01f, y0 + bh * 0.3f, y1 - bh * 0.3f, M.metal);
			for (int v = 0; v < (M.fine ? 4 : 0); v++) { /* holes in the tray */
				float va = ba0 + (ba1 - ba0) * (0.1f + 0.1f * (float)v);
				lbox(w, BAG_LIT, L, va, va + (ba1 - ba0) * 0.05f, d0 - 0.011f, d0 - 0.009f, y0 + bh * 0.12f, y0 + bh * 0.26f, M.black);
			}
			bool lit = r == rows - 1 && c == 0 ? busy : blink(&s, 2.5f + rnd(&s) * 6, busy ? 0.6f : 0.1f);
			float la = ba1 - (ba1 - ba0) * 0.15f;
			lbox(w, lit ? BAG_LAMP : BAG_LIT, L, la, la + (ba1 - ba0) * 0.06f, d0 - 0.014f, d0 - 0.01f, y0 + bh * 0.4f, y1 - bh * 0.4f,
			     lit ? M.accent : darken(M.accent, 0.7f));
			lbox(w, BAG_LAMP, L, la, la + (ba1 - ba0) * 0.06f, d0 - 0.014f, d0 - 0.01f, y1 - bh * 0.25f, y1 - bh * 0.15f, LED_GREEN);
		}
	}
	/* the display at the top: its lines */
	float sa0 = a0 + ew * 0.06f, sa1 = a0 + ew * fminf(0.5f, 0.06f + 0.4f / ew * 0.5f);
	lbox(w, BAG_LAMP, L, sa0, sa1, d0 - 0.006f, d0, H - 0.1f, H - 0.045f, mix(M.screen, M.accent, 0.2f));
	lbox(w, BAG_LAMP, L, sa0 + 0.01f, sa0 + (sa1 - sa0) * (0.3f + 0.6f * (k->dev ? k->activity : 0.4f)), d0 - 0.008f, d0 - 0.006f, H - 0.08f,
	     H - 0.07f, M.accent);
	lball(w, BAG_BALL_LAMP, L, a1 - 0.05f, d0 - 0.008f, H - 0.07f, 0.03f, 0.01f, 0.03f, busy ? LED_AMBER : LED_GREEN);
	if (k->dev && k->activity > 0.05f) {
		SDL_Color g = M.accent;
		g.a = 50;
		lbox(w, BAG_GLOW, L, a0 - 0.01f, a1 + 0.01f, d0 - 0.03f, d0 - 0.02f, 0.03f, H, g);
	}
}

/* A cardboard box, taped, its flaps meeting along the top, a label with the
 * file's icon on its front. */
static void carton(Walk *w, const Loc *L, float a0, float a1, float d0, float d1, float y0, float y1, unsigned tex, unsigned *s) {
	SDL_Color card = mix((SDL_Color){180, 140, 92, 255}, M.kc, 0.08f), tape = {200, 170, 120, 255};
	card = darken(card, rnd(s) * 0.15f);
	float mid = (d0 + d1) / 2, cw = a1 - a0, h = y1 - y0;
	lbox(w, BAG_LIT, L, a0, a1, d0, d1, y0, y1, card);
	lbox(w, BAG_LIT, L, a0 - 0.002f, a1 + 0.002f, mid - 0.002f, mid + 0.002f, y1 - 0.001f, y1 + 0.001f, darken(card, 0.5f));
	lbox(w, BAG_LIT, L, a0 - 0.003f, a1 + 0.003f, mid - 0.03f, mid + 0.03f, y1, y1 + 0.002f, tape);
	lbox(w, BAG_LIT, L, a0 - 0.003f, a0 + 0.002f, mid - 0.03f, mid + 0.03f, y1 - h * 0.25f, y1 + 0.002f, tape);
	lbox(w, BAG_LIT, L, a1 - 0.002f, a1 + 0.003f, mid - 0.03f, mid + 0.03f, y1 - h * 0.25f, y1 + 0.002f, tape);
	float la = a0 + cw * 0.15f, lw = fminf(cw * 0.4f, h * 0.5f), ly = y0 + h * 0.3f;
	lbox(w, BAG_LIT, L, la, la + lw, d0 - 0.003f, d0, ly, ly + lw * 0.7f, M.white);
	if (tex) {
		float side = lw * 0.6f;
		lpic(w, L, tex, false, la + lw * 0.05f, la + lw * 0.05f + side, ly + lw * 0.05f, ly + lw * 0.05f + side, -0.004f + d0, M.white);
	}
	ink_lines(w, BAG_LIT, L, la + lw * 0.7f, la + lw * 0.95f, d0 - 0.004f, d0 - 0.004f, ly + lw * 0.1f, 1, M.black, s);
	/* a printed arrow, this way up */
	lbox(w, BAG_LIT, L, a1 - cw * 0.2f, a1 - cw * 0.17f, d0 - 0.002f, d0, y1 - h * 0.35f, y1 - h * 0.12f, darken(card, 0.55f));
}

/* Anything else: a cardboard box; bigger, boxes stacked. */
static void make_other(Walk *w, Block *k, const Loc *L, float H, unsigned s) {
	float W = L->w, D = L->d;
	int na = across(W, 0.8f, 3), ny = across(H, 0.65f, 4);
	float ca = (W - 0.04f) / (float)na, cy = H / (float)ny;
	for (int l = 0; l < ny; l++) {
		for (int i = 0; i < na; i++) {
			float in = l ? pick(&s, 0.01f, 0.05f) : 0.005f, a0 = 0.02f + ca * (float)i + in;
			carton(w, L, a0, a0 + ca - 2 * in, 0.02f + in, D - 0.02f - in, cy * (float)l, cy * (float)(l + 1) - 0.002f, l == 0 || i == 0 ? k->tex : 0, &s);
		}
	}
}

/* a file that runs: a program by its ending, or one marked to run with none */
static bool runs(Block *k) {
	if (k->kind == 6) {
		return true;
	}
	if (k->dev || k->kind != KIND_COUNT - 1 || !k->node) {
		return false;
	}
	if (!k->exe) {
		char path[1400];
		struct stat st;
		node_path(k->node, path, sizeof path);
		k->exe = !stat(path, &st) && S_ISREG(st.st_mode) && (st.st_mode & 0111) ? 1 : -1;
	}
	return k->exe > 0;
}

/* Each file a thing of its kind, made of parts, filling its block: the
 * front, where its screen or its controls are, the way the room's
 * arrangement turned it (k->front). */
static void furnish(Walk *w, Block *k, const Theme *t) {
	const Box *b = &k->b;
	float H = b->hi[1], W = b->hi[0] - b->lo[0], D = b->hi[2] - b->lo[2], small = W < D ? W : D;
	float cx = (b->lo[0] + b->hi[0]) / 2, cz = (b->lo[2] + b->hi[2]) / 2;
	int fr = k->front;
	float fw = face_of(b, fr, 0).w, deep = fr == 0 || fr == 2 ? D : W;
	unsigned s = k->seed;
	materials(k, t, w->time);
	float nx = clampf(w->px, b->lo[0], b->hi[0]) - w->px, nz = clampf(w->pz, b->lo[2], b->hi[2]) - w->pz;
	M.fine = nx * nx + nz * nz < 14 * 14;
	M.ex = w->px;
	M.ez = w->pz;
	SDL_Color kc = M.kc, black = M.black, panel = rgb(t->panel), metal = M.metal, accent = M.accent, white = M.white;
	int style = k->dev ? 100 + k->dev : runs(k) ? 6 : k->kind;
	shadow(w, b);

	void (*make)(Walk *, Block *, const Loc *, float, unsigned) = NULL;
	switch (style) {
	case 0: make = make_picture; break;
	case 1: make = make_video; break;
	case 2:
	case 100 + WALK_DEV_SOUND: make = make_audio; break;
	case 3: make = make_archive; break;
	case 5:
	case 100 + WALK_DEV_TTY: make = make_console; break;
	case 6: make = make_program; break;
	case 7:
	case 100 + WALK_DEV_DISK: make = make_disk; break;
	case 8: make = make_other; break;
	default: break;
	}
	if (make || style == 4) {
		Loc L = loc_of(b, fr, deep);
		if (style == 4) {
			make_documents(w, k, &L, H, s);
		} else {
			make(w, k, &L, H, s);
		}
		return;
	}

	/* the rest of /dev: a body with a look */
	SDL_Color body = kc;
	switch (style) {
	case 100 + WALK_DEV_GPU: { /* graphics: a card, two fans, a strip of colour */
		body = black;
		float r = fminf(W, D) * 0.28f;
		for (int i = 0; i < 2; i++) {
			float fx = W >= D ? b->lo[0] + W * (i ? 0.72f : 0.28f) : cx, fz = W >= D ? cz : b->lo[2] + D * (i ? 0.72f : 0.28f);
			bag_put(&w->bag[BAG_BALL], fx, H + 0.01f, fz, 2 * r, 0.03f, 2 * r, darken(metal, 0.4f));
			bag_put(&w->bag[BAG_BALL], fx, H + 0.025f, fz, r * 0.5f, 0.03f, r * 0.5f, metal);
		}
		bag_box(w, BAG_LAMP, face_box(b, fr, 0.04f, 0.96f, H * 0.72f, H * 0.82f, 0, 0.012f), hue(w->time * 0.15f, 0.8f, 1));
		break;
	}

	case 100 + WALK_DEV_INPUT: { /* input: keys on top */
		body = darken(panel, 0.3f);
		int cols = (int)(W / 0.07f), rows = (int)(D / 0.07f);
		cols = cols > 16 ? 16 : cols;
		rows = rows > 6 ? 6 : rows;
		for (int i = 0; i < cols; i++) {
			for (int j = 0; j < rows; j++) {
				float x = b->lo[0] + W * ((float)i + 0.5f) / (float)cols, z = b->lo[2] + D * ((float)j + 0.5f) / (float)rows;
				bag_put(&w->bag[BAG_LIT], x, H + 0.012f, z, W / (float)cols * 0.78f, 0.025f, D / (float)rows * 0.78f, lighten(panel, 0.35f));
			}
		}
		break;
	}

	case 100 + WALK_DEV_VOID: { /* null, zero, random: an orb over a plinth */
		body = metal;
		SDL_Color c = !strcmp(k->name, "null") ? (SDL_Color){150, 90, 255, 255}
		            : !strcmp(k->name, "zero") || !strcmp(k->name, "full") ? white
		                                                                   : hue(w->time * 0.3f + (float)(s % 7) / 7, 0.7f, 1);
		float r = small * 0.3f + 0.12f, y = H + 0.5f + 0.08f * sinf(w->time * 1.7f + (float)(s % 9));
		bag_put(&w->bag[BAG_BALL_GLOW], cx, y, cz, 2 * r, 2 * r, 2 * r, c);
		bag_put(&w->bag[BAG_BALL_LAMP], cx, y, cz, r * 0.35f, r * 0.35f, r * 0.35f, c);
		bag_put(&w->bag[BAG_BALL_GLOW], cx, H + 0.02f, cz, small * 0.8f, 0.03f, small * 0.8f, c);
		break;
	}

	case 100 + WALK_DEV_CHAR: /* any other device: a box with a port */
		body = darken(metal, 0.3f);
		bag_box(w, BAG_LAMP, face_box(b, fr, 0.4f, 0.6f, H * 0.45f, H * 0.6f, 0, 0.012f), accent);
		break;

	default:
		break;
	}
	(void)fw;
	bag_box(w, BAG_BODY, *b, body);
}

/* the rugs the lounges' music stands round: a border, the field, a panel in the middle */
static void lay_rugs(Walk *w) {
	for (int i = 0; i < w->nrug; i++) {
		Box r = w->rug[i];
		unsigned s = w->rug_seed[i];
		SDL_Color c = hue(rnd(&s), 0.5f, 0.42f), edge = darken(c, 0.4f), mid = lighten(hue(rnd(&s), 0.35f, 0.5f), 0.1f);
		float e = fminf(0.14f, (r.hi[0] - r.lo[0]) * 0.06f), mx = (r.hi[0] - r.lo[0]) * 0.28f, mz = (r.hi[2] - r.lo[2]) * 0.28f;
		bag_box(w, BAG_LIT, r, edge);
		bag_box(w, BAG_LIT, box(r.lo[0] + e, r.hi[1], r.lo[2] + e, r.hi[0] - e, r.hi[1] + 0.003f, r.hi[2] - e), c);
		bag_box(w, BAG_LIT, box(r.lo[0] + mx, r.hi[1], r.lo[2] + mz, r.hi[0] - mx, r.hi[1] + 0.005f, r.hi[2] - mz), mid);
	}
}

/* Some of b (and what stands out of it: a chair's back, a glow over it)
 * in the view mvp shows; furniture out of sight is not made at all. */
static bool in_view(const float *m, const Box *b) {
	int out[5] = {0, 0, 0, 0, 0};
	for (int i = 0; i < 8; i++) {
		float x = i & 1 ? b->hi[0] + 0.3f : b->lo[0] - 0.3f, y = i & 2 ? b->hi[1] + 1.0f : 0, z = i & 4 ? b->hi[2] + 0.3f : b->lo[2] - 0.3f;
		float cx = m[0] * x + m[4] * y + m[8] * z + m[12], cy = m[1] * x + m[5] * y + m[9] * z + m[13];
		float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
		out[0] += cx < -cw;
		out[1] += cx > cw;
		out[2] += cy < -cw;
		out[3] += cy > cw;
		out[4] += cw < 0.05f;
	}
	return out[0] < 8 && out[1] < 8 && out[2] < 8 && out[3] < 8 && out[4] < 8;
}

/* ---- the floor you are on, in boxes, made once a build ---- */

static void ibox(Walk *w, int bag, Box b, SDL_Color c) {
	bag_put(&w->ibag[bag], (b.lo[0] + b.hi[0]) / 2, (b.lo[1] + b.hi[1]) / 2, (b.lo[2] + b.hi[2]) / 2, b.hi[0] - b.lo[0],
	        b.hi[1] - b.lo[1], b.hi[2] - b.lo[2], c);
}

static void quad_to(Quad3 **v, int *n, int *cap, Vec3 a, Vec3 b, Vec3 c, Vec3 d, SDL_Color col) {
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*v = SDL_realloc(*v, (size_t)*cap * sizeof **v);
	}
	Quad3 *q = &(*v)[(*n)++];
	Vec3 p[4] = {a, b, c, d};
	for (int i = 0; i < 4; i++) {
		q->p[i][0] = p[i].x;
		q->p[i][1] = p[i].y;
		q->p[i][2] = p[i].z;
	}
	memcpy(q->rgba, &col, 4);
}

/* the faint tint of a window's glass, a streak of light across it */
static SDL_Color glass_tint(const Theme *t) {
	SDL_Color tint = mix(rgb(t->accent), (SDL_Color){200, 230, 255, 255}, 0.5f);
	return (SDL_Color){(Uint8)(tint.r * 0.08f), (Uint8)(tint.g * 0.08f), (Uint8)(tint.b * 0.08f), 255};
}

/* The floor's pieces in the theme's colours. */
static void interior_make(Walk *w, const Theme *t) {
	for (int i = 0; i < IB_COUNT; i++) {
		w->ibag[i].n = 0;
	}
	w->niglass = 0;
	w->floor_dirty = false;
	w->floor_theme = *t;
	SDL_Color wall = mix(rgb(t->panel), rgb(t->accent_dim), 0.55f), floor = mix(rgb(t->bg_alt), rgb(t->panel_hover), 0.5f);
	SDL_Color col[] = {
		[IR_WALL] = wall,
		[IR_OUTWALL] = darken(wall, 0.1f),
		[IR_FLOOR] = floor,
		[IR_CEIL] = darken(mix(rgb(t->panel), wall, 0.4f), 0.25f),
		[IR_STEP] = lighten(floor, 0.12f),
		[IR_RAIL] = darken(rgb(t->panel), 0.4f),
		[IR_FRAME] = darken(rgb(t->panel), 0.35f),
		[IR_DOOR] = lighten(rgb(t->accent), 0.2f),
		[IR_FRONT] = LAMP_LIGHT,
		[IR_LAMP] = lighten(rgb(t->accent_dim), 0.55f),
		[IR_BOARD] = darken(rgb(t->panel), 0.55f),
		[IR_PLAQUE] = mix(rgb(t->bg), rgb(t->accent), 0.3f),
		[IR_DARK] = darken(rgb(t->panel), 0.5f),
	};
	for (int i = 0; i < w->npiece; i++) {
		const Piece *p = &w->piece[i];
		int bag = p->role == IR_WALL || p->role == IR_OUTWALL || p->role == IR_FLOOR || p->role == IR_CEIL ? IB_WALL
		        : p->role == IR_DOOR || p->role == IR_FRONT || p->role == IR_LAMP || p->role == IR_PLAQUE ? IB_LAMP
		                                                                                                 : IB_LIT;
		ibox(w, bag, p->b, col[p->role]);
	}
	/* each pane: glass, faint, drawn last as light added */
	SDL_Color tint = glass_tint(t);
	for (int i = w->nwall; i < w->nwin; i++) {
		const Box *b = &w->solid[i];
		Vec3 a, c, d, e;
		if (b->hi[0] - b->lo[0] < b->hi[2] - b->lo[2]) {
			float x = (b->lo[0] + b->hi[0]) / 2;
			a = v3(x, b->lo[1], b->lo[2]), c = v3(x, b->lo[1], b->hi[2]), d = v3(x, b->hi[1], b->hi[2]), e = v3(x, b->hi[1], b->lo[2]);
		} else {
			float z = (b->lo[2] + b->hi[2]) / 2;
			a = v3(b->lo[0], b->lo[1], z), c = v3(b->hi[0], b->lo[1], z), d = v3(b->hi[0], b->hi[1], z), e = v3(b->lo[0], b->hi[1], z);
		}
		quad_to(&w->iglass, &w->niglass, &w->iglass_cap, a, c, d, e, tint);
	}
}

/* ---- the city's boxes, made once, and again when you have gone far ---- */

static void cbox(Walk *w, int bag, Box b, SDL_Color c) {
	bag_put(&w->cbag[bag], (b.lo[0] + b.hi[0]) / 2, (b.lo[1] + b.hi[1]) / 2, (b.lo[2] + b.hi[2]) / 2, b.hi[0] - b.lo[0],
	        b.hi[1] - b.lo[1], b.hi[2] - b.lo[2], c);
}

/* far off, things fade toward the horizon's colour */
static SDL_Color haze(SDL_Color c, SDL_Color horizon, float d) { return mix(c, horizon, clampf((d - 40) / 360, 0, 0.6f)); }

/* along side f of q, from o0 to o1 out from it (below 0, in), a0 to a1 along, y0 to y1 up */
static Box face_out(WRect q, int f, float o0, float o1, float a0, float a1, float y0, float y1) {
	return side_box(q, f, -o1, -o0, a0, a1, y0, y1);
}

static void csign_add(Walk *w, Vec3 p, int look, const char *text) {
	if (w->ncsign == w->csign_cap) {
		w->csign_cap = w->csign_cap ? w->csign_cap * 2 : 128;
		w->csign = SDL_realloc(w->csign, (size_t)w->csign_cap * sizeof *w->csign);
	}
	Sign *s = &w->csign[w->ncsign++];
	s->p = p;
	s->room = -1;
	s->look = look;
	snprintf(s->text, sizeof s->text, "%s", text);
}

/* a road's name: the root's, with a number for those along z and a letter for those along x */
static void road_name(const Walk *w, const WRoad *r, char *out, size_t n) {
	if (r->along_x) {
		snprintf(out, n, "%.60s %s %c", w->world.name, r->avenue ? "Avenue" : "Street", 'A' + r->n % 26);
	} else {
		snprintf(out, n, "%.60s %s %d", w->world.name, r->avenue ? "Avenue" : "Street", r->n);
	}
}

/* each style's walls, its frames, how its windows look unlit */
static void style_look(int style, int kind, const Theme *t, SDL_Color *body, SDL_Color *trim, SDL_Color *dark) {
	SDL_Color base = kind >= 0 ? kind_colour(kind, t) : mix(rgb(t->panel_hover), rgb(t->accent_dim), 0.35f), own;
	switch (style) {
	case STYLE_GLASS: own = (SDL_Color){44, 78, 96, 255}; break;
	case STYLE_WAREHOUSE: own = (SDL_Color){128, 62, 46, 255}; break;
	case STYLE_LIBRARY: own = (SDL_Color){176, 164, 136, 255}; break;
	case STYLE_RECORDS: own = darken(base, 0.45f); break;
	case STYLE_DATA: own = (SDL_Color){78, 84, 92, 255}; break;
	case STYLE_GALLERY: own = (SDL_Color){206, 204, 198, 255}; break;
	default: own = mix(darken(base, 0.5f), rgb(t->panel), 0.4f); break;
	}
	*body = mix(own, rgb(t->panel), 0.2f);
	*trim = darken(*body, 0.35f);
	*dark = style == STYLE_GLASS ? (SDL_Color){30, 58, 76, 255} : darken(mix(base, rgb(t->bg), 0.7f), 0.4f);
}

/* the light in a room's windows: warm or cool, some brighter than others */
static SDL_Color room_light(unsigned s) {
	SDL_Color c = mix((SDL_Color){255, 200, 130, 255}, (SDL_Color){190, 222, 255, 255}, rnd(&s) * rnd(&s));
	return darken(c, rnd(&s) * 0.35f);
}

/* ---- the crew: SYN-CORP's agents, each in their office ----
 *
 * When the city is your home (or in it), each crew member's home stands
 * at its edge as their office, and they are in it: walking between their
 * files and stopping at one, or at their desk while SYN-CORP has them in
 * a turn (a corp-turn-NAME unit running). Their last logged action
 * (~/.local/state/syn-corp/syn-corp.log) shows over them a minute. All
 * read from what is on the machine: nothing is asked of them. */

#define CREW_MAX 12
typedef struct {
	char id[32], name[64];
	Node *home;          /* their home, scanned */
	SDL_Color hood;
	bool busy;           /* in a turn now */
	char last[96];       /* their last logged action */
	Uint32 last_at;
	/* what they last said on the radio, and when each word of it is spoken */
	char said[600], said_id[64], cue_line[64];
	Uint32 said_at, talk_at;
	int talk_stream, cue[900], ncue;
	/* in their office */
	bool placed;
	float x, z, yaw, tx, tz, wait, stride, mv;
	int target;
	/* switched off on the app (the radio's rooms say so): at home, resting */
	bool off;
	/* what they have running (a turn's step, a duty), for their worker at the desk */
	char work[96], presence[96], duty[64];
	/* following you: where they are in the floor you are on */
	bool fplaced;
	float fx, fz, fy, fyaw, fmv, frun, fstride;
} Crew;
static struct {
	Crew c[CREW_MAX];
	int n;
	int extra_of[16];    /* World.extra[i] is crew.c[extra_of[i]] */
	SDL_atomic_t state;  /* 0 not asked, 1 scanning, 2 ready */
	SDL_mutex *mu;
	long log_at;
} crew;

#ifndef _WIN32
static int radio_thread(void *arg);

static void crew_roster(void) {
	static const struct {
		const char *id, *name;
		SDL_Color hood;
	} known[] = {
		{"zofia", "Lieutenant Zofia", {62, 30, 34, 255}},  {"blackmesa", "Black Mesa", {70, 52, 22, 255}},
		{"overwatch", "Overwatch", {26, 44, 70, 255}},     {"data", "Data", {34, 58, 44, 255}},
		{"trinity", "Trinity", {20, 20, 22, 255}},         {"glados", "GLaDOS", {78, 78, 84, 255}},
		{"jackal", "Jackal", {58, 40, 62, 255}},
	};
	crew.n = 0;
	for (size_t i = 0; i < sizeof known / sizeof *known && crew.n < CREW_MAX; i++) {
		Crew *c = &crew.c[crew.n++];
		memset(c, 0, sizeof *c);
		snprintf(c->id, sizeof c->id, "%s", known[i].id);
		snprintf(c->name, sizeof c->name, "%s", known[i].name);
		c->hood = known[i].hood;
		/* the role file's name, where SYN-CORP's repository has one */
		char path[600], line[300];
		const char *home = getenv("HOME");
		snprintf(path, sizeof path, "%s/GithubProjects/SYN-CORP/roles/%s.toml", home ? home : "", c->id);
		FILE *f = fopen(path, "r");
		while (f && fgets(line, sizeof line, f)) {
			char nm[64];
			if (sscanf(line, "name = \"%63[^\"]\"", nm) == 1) {
				snprintf(c->name, sizeof c->name, "%s", nm);
				break;
			}
		}
		if (f) {
			fclose(f);
		}
	}
}

/* what is running and what was logged, every few seconds, for as long as the program runs */
static void crew_watch(void) {
	char line[1024];
	bool busy[CREW_MAX] = {0};
	FILE *p = popen("systemctl list-units 'corp-turn-*' --no-legend --plain 2>/dev/null", "r");
	while (p && fgets(line, sizeof line, p)) {
		const char *u = strstr(line, "corp-turn-");
		for (int i = 0; u && i < crew.n; i++) {
			size_t n = strlen(crew.c[i].id);
			busy[i] |= !strncmp(u + 10, crew.c[i].id, n) && u[10 + n] == '-';
		}
	}
	if (p) {
		pclose(p);
	}
	/* duties running now, and whose (the log says each duty's agent: "duty NAME as=AGENT") */
	static char duty_of[32][2][48];
	static int nduty;
	char running[CREW_MAX][64] = {{0}};
	p = popen("systemctl list-units 'corp-duty-*' --no-legend --plain --state=running 2>/dev/null", "r");
	while (p && fgets(line, sizeof line, p)) {
		char unit[96] = "";
		const char *u = strstr(line, "corp-duty-");
		if (!u || sscanf(u + 10, "%95[^ .]", unit) != 1) {
			continue;
		}
		for (int d = 0; d < nduty; d++) {
			if (!strcmp(duty_of[d][0], unit)) {
				for (int i = 0; i < crew.n; i++) {
					if (!strcmp(crew.c[i].id, duty_of[d][1])) {
						snprintf(running[i], sizeof running[i], "%s", unit);
					}
				}
			}
		}
	}
	if (p) {
		pclose(p);
	}
	char path[600];
	const char *home = getenv("HOME");
	snprintf(path, sizeof path, "%s/.local/state/syn-corp/syn-corp.log", home ? home : "");
	FILE *f = fopen(path, "r");
	char last[CREW_MAX][96] = {{0}};
	if (f) {
		fseek(f, 0, SEEK_END);
		long end = ftell(f);
		if (crew.log_at <= 0 || crew.log_at > end) {
			crew.log_at = end > 8192 ? end - 8192 : 0; /* the first look: the last few lines */
		}
		fseek(f, crew.log_at, SEEK_SET);
		while (fgets(line, sizeof line, f)) {
			char who[32] = "", act[48] = "", repo[48] = "", dn[48] = "", das[48] = "";
			const char *dl = strstr(line, " duty ");
			if (dl && sscanf(dl + 6, "%47s as=%47s", dn, das) == 2 && strcmp(das, "-")) {
				int d = 0;
				while (d < nduty && strcmp(duty_of[d][0], dn)) {
					d++;
				}
				if (d == nduty && nduty < 32) {
					nduty++;
				}
				if (d < 32) {
					snprintf(duty_of[d][0], sizeof duty_of[d][0], "%s", dn);
					snprintf(duty_of[d][1], sizeof duty_of[d][1], "%s", das);
				}
			}
			const char *a = strstr(line, " agent="), *b = strstr(line, " action="), *r = strstr(line, " repo=");
			if (a) {
				sscanf(a + 7, "%31[^ \n]", who);
			}
			if (b) {
				sscanf(b + 8, "%47[^ \n]", act);
			}
			if (r) {
				sscanf(r + 6, "%47[^ \n]", repo);
			}
			for (int i = 0; who[0] && i < crew.n; i++) {
				if (strcmp(who, crew.c[i].id)) {
					continue;
				}
				if (strstr(line, " turn agent=")) {
					snprintf(last[i], sizeof last[i], "%s", strstr(line, "POSTED") ? "answered" : strstr(line, "FAILED") ? "a turn failed" : "a turn ended");
				} else if (act[0]) {
					snprintf(last[i], sizeof last[i], "%s%s%s%s", act, repo[0] ? " " : "", repo, strstr(line, "REFUSED") ? " (refused)" : "");
				}
			}
		}
		crew.log_at = ftell(f);
		fclose(f);
	}
	SDL_LockMutex(crew.mu);
	for (int i = 0; i < crew.n; i++) {
		crew.c[i].busy = busy[i];
		snprintf(crew.c[i].duty, sizeof crew.c[i].duty, "%s", running[i]);
		if (last[i][0]) {
			snprintf(crew.c[i].last, sizeof crew.c[i].last, "%s", last[i]);
			crew.c[i].last_at = SDL_GetTicks();
		}
	}
	SDL_UnlockMutex(crew.mu);
}

static int crew_thread(void *arg) {
	(void)arg;
	for (int i = 0; i < crew.n; i++) {
		char path[300], err[256];
		snprintf(path, sizeof path, "/home/%s", crew.c[i].id);
		struct stat st;
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			ScanProgress prog = {0}; /* nothing shares it: no lock */
			crew.c[i].home = scan_tree(path, &prog, err, sizeof err);
		}
	}
	SDL_AtomicSet(&crew.state, 2);
	for (;;) {
		crew_watch();
		SDL_Delay(3000);
	}
	return 0;
}
#endif

static bool crew_ready(void) {
	if (SDL_AtomicGet(&crew.state) != 2) {
		return false;
	}
	for (int i = 0; i < crew.n; i++) {
		if (crew.c[i].home) {
			return true;
		}
	}
	return false;
}

/* whether the city at root is yours, to have the crew at its edge */
static bool crew_here(const Node *root) {
	const char *home = getenv("HOME");
	char path[1024];
	node_path(root, path, sizeof path);
	size_t n = home ? strlen(home) : 0;
	return n > 1 && !strncmp(path, home, n) && (path[n] == '\0' || path[n] == '/');
}

/* the crew's homes in the world about to be made, if it is yours and they are scanned */
static void crew_into(World *wd, const Node *root) {
	wd->nextra = 0;
#ifndef _WIN32
	if (!crew_here(root)) {
		return;
	}
	if (SDL_AtomicGet(&crew.state) == 0) {
		crew.mu = SDL_CreateMutex();
		crew_roster();
		SDL_AtomicSet(&crew.state, 1);
		SDL_Thread *th = SDL_CreateThread(crew_thread, "crew", NULL);
		if (th) {
			SDL_DetachThread(th);
		}
		th = SDL_CreateThread(radio_thread, "radio", NULL);
		if (th) {
			SDL_DetachThread(th);
		}
	}
	if (SDL_AtomicGet(&crew.state) != 2) {
		return;
	}
	for (int i = 0; i < crew.n && wd->nextra < 16; i++) {
		if (crew.c[i].home) {
			crew.extra_of[wd->nextra] = i;
			wd->extra[wd->nextra++] = crew.c[i].home;
		}
	}
#endif
}


/* ---- the radio: what the crew say, heard as text ----
 *
 * The same door as the phone app (cast protocol v1, a WebSocket on
 * 127.0.0.1:47984), joined to the crew's rooms with the owner's token
 * (~/.config/syn-corp/owner-token, never printed). It only listens: no
 * line is ever sent from here, so no turn is ever run. A spoken reply
 * comes with when each word of it is said ('cue': word, start, end in ms
 * from its voice stream's start), so the word being said is boxed as on
 * the phone; a line with no voice is read at a speaking pace. */

#ifndef _WIN32
static struct {
	char said[600], from[64];
	Uint32 at;
} you_said; /* your own lines, from the phone */

static bool ws_all(int fd, void *buf, size_t n) {
	unsigned char *p = buf;
	while (n) {
		ssize_t r = recv(fd, p, n, 0);
		if (r <= 0) {
			return false;
		}
		p += r;
		n -= (size_t)r;
	}
	return true;
}

static bool ws_send(int fd, int op, const char *data, size_t n) {
	unsigned char head[14], mask[4];
	size_t h = 0;
	head[h++] = (unsigned char)(0x80 | op);
	if (n < 126) {
		head[h++] = (unsigned char)(0x80 | n);
	} else {
		head[h++] = 0x80 | 126;
		head[h++] = (unsigned char)(n >> 8);
		head[h++] = (unsigned char)n;
	}
	for (int i = 0; i < 4; i++) {
		mask[i] = (unsigned char)(rand() & 0xff);
		head[h++] = mask[i];
	}
	unsigned char *buf = SDL_malloc(h + n);
	memcpy(buf, head, h);
	for (size_t i = 0; i < n; i++) {
		buf[h + i] = (unsigned char)data[i] ^ mask[i & 3];
	}
	bool ok = send(fd, buf, h + n, MSG_NOSIGNAL) == (ssize_t)(h + n);
	SDL_free(buf);
	return ok;
}

/* where "key": starts its value in a JSON object, or NULL */
static const char *jat(const char *js, const char *key) {
	char pat[48];
	snprintf(pat, sizeof pat, "\"%s\"", key);
	for (const char *p = strstr(js, pat); p; p = strstr(p + 1, pat)) {
		const char *v = p + strlen(pat);
		while (*v == ' ') {
			v++;
		}
		if (*v == ':') {
			v++;
			while (*v == ' ') {
				v++;
			}
			return v;
		}
	}
	return NULL;
}

/* a string value, its escapes undone (\uXXXX as UTF-8, a line end as a space) */
static bool jstr(const char *js, const char *key, char *out, size_t n) {
	const char *v = jat(js, key);
	size_t o = 0;
	if (!v || *v != '"' || !n) {
		return false;
	}
	for (v++; *v && *v != '"' && o + 5 < n; v++) {
		unsigned c = (unsigned char)*v;
		if (c == '\\' && v[1]) {
			v++;
			switch (*v) {
			case 'n': case 'r': case 't': c = ' '; break;
			case 'u': {
				unsigned u = 0;
				for (int i = 1; i <= 4 && v[i]; i++) {
					char h = v[i];
					u = u * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
				}
				v += 4;
				if (u >= 0xd800 && u < 0xdc00 && v[1] == '\\' && v[2] == 'u') { /* a pair: one character */
					unsigned l = 0;
					for (int i = 3; i <= 6 && v[i]; i++) {
						char h = v[i];
						l = l * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
					}
					u = 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00);
					v += 6;
				}
				if (u < 0x80) {
					out[o++] = (char)u;
				} else if (u < 0x800) {
					out[o++] = (char)(0xc0 | u >> 6);
					out[o++] = (char)(0x80 | (u & 0x3f));
				} else if (u < 0x10000) {
					out[o++] = (char)(0xe0 | u >> 12);
					out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
					out[o++] = (char)(0x80 | (u & 0x3f));
				} else {
					out[o++] = (char)(0xf0 | u >> 18);
					out[o++] = (char)(0x80 | ((u >> 12) & 0x3f));
					out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
					out[o++] = (char)(0x80 | (u & 0x3f));
				}
				continue;
			}
			default: c = (unsigned char)*v; break;
			}
		}
		out[o++] = (char)c;
	}
	out[o] = '\0';
	return true;
}

static long jlong(const char *js, const char *key, long def) {
	const char *v = jat(js, key);
	return v && (*v == '-' || (*v >= '0' && *v <= '9')) ? strtol(v, NULL, 10) : def;
}

static Crew *crew_named(const char *id) {
	for (int i = 0; id[0] && i < crew.n; i++) {
		if (!strcmp(crew.c[i].id, id)) {
			return &crew.c[i];
		}
	}
	return NULL;
}

/* one message from the radio */
static void radio_heard(const char *js) {
	char type[16] = "", from[64] = "", as[32] = "", text[600] = "", id[64] = "", line[64] = "";
	jstr(js, "type", type, sizeof type);
	if (jat(js, "rooms") && (!strcmp(type, "rooms") || !strcmp(type, "ok"))) {
		SDL_LockMutex(crew.mu);
		for (int i = 0; i < crew.n; i++) { /* each crew room's "off": switched off on the app */
			char pat[64];
			snprintf(pat, sizeof pat, "\"%s\"", crew.c[i].id);
			for (const char *q = strstr(js, pat); q; q = strstr(q + 1, pat)) {
				const char *k = q - 1;
				while (k > js && (*k == ' ' || *k == ':')) {
					k--;
				}
				if (k - js < 9 || strncmp(k - 8, "\"channel\"", 9)) {
					continue; /* not its channel field */
				}
				const char *end = strchr(q, '}');
				char obj[600];
				const char *start = q;
				while (start > js && *start != '{') {
					start--;
				}
				snprintf(obj, sizeof obj, "%.*s", (int)((end ? end : q + strlen(q)) - start), start);
				const char *ov = jat(obj, "off");
				crew.c[i].off = ov && !strncmp(ov, "true", 4);
				break;
			}
		}
		SDL_UnlockMutex(crew.mu);
	}
	const char *rp = jat(js, "replay");
	if (rp && !strncmp(rp, "true", 4) && !SDL_getenv("SYN_RADIO_REPLAY")) {
		return; /* the record, sent on joining: only what is said from now (SYN_RADIO_REPLAY=1 shows it, for trying) */
	}
	if (SDL_getenv("SYN_WORLD_DUMP")) {
		char ty[16] = "", fr[40] = "";
		jstr(js, "type", ty, sizeof ty);
		jstr(js, "from", fr, sizeof fr);
		fprintf(stderr, "radio: %s from %s\n", ty, fr);
	}
	jstr(js, "from", from, sizeof from);
	jstr(js, "as", as, sizeof as);
	SDL_LockMutex(crew.mu);
	Crew *c = crew_named(as[0] ? as : from);
	if (!strcmp(type, "say") && jstr(js, "text", text, sizeof text) && text[0] && strncmp(text, "::", 2) &&
	    strcmp(from, "system") && strcmp(as, "system")) {
		if (!jstr(js, "message", id, sizeof id)) {
			jstr(js, "id", id, sizeof id);
		}
		if (c) {
			snprintf(c->said, sizeof c->said, "%s", text);
			snprintf(c->said_id, sizeof c->said_id, "%s", id);
			c->said_at = SDL_GetTicks();
			if (strcmp(c->cue_line, id)) {
				c->ncue = 0;
			}
		} else {
			snprintf(you_said.said, sizeof you_said.said, "%s", text);
			snprintf(you_said.from, sizeof you_said.from, "%s", from);
			you_said.at = SDL_GetTicks();
		}
	} else if (!strcmp(type, "presence")) {
		char mem[48] = "", st[24] = "", tx[200] = "";
		jstr(js, "member", mem, sizeof mem);
		jstr(js, "status", st, sizeof st);
		jstr(js, "text", tx, sizeof tx);
		Crew *pc = crew_named(mem);
		if (pc) { /* busy: the newest step of its turn ("wrk1 n= s= WORDS") */
			const char *wd = tx;
			if (!strncmp(wd, "wrk1 ", 5)) {
				for (int sp = 0; *wd && sp < 3; wd++) {
					sp += *wd == ' ';
				}
			}
			snprintf(pc->presence, sizeof pc->presence, "%s", !strcmp(st, "busy") ? (wd[0] ? wd : "working") : "");
		}
	} else if (!strcmp(type, "talk") && c) {
		c->talk_stream = (int)jlong(js, "stream", -1);
		c->talk_at = SDL_GetTicks(); /* heard live: its first packet is about now */
	} else if (!strcmp(type, "cue")) {
		int stream = (int)jlong(js, "stream", -2);
		jstr(js, "line", line, sizeof line);
		for (int i = 0; i < crew.n; i++) {
			Crew *k = &crew.c[i];
			if (k->talk_stream != stream || !line[0]) {
				continue;
			}
			if (strcmp(k->cue_line, line)) {
				snprintf(k->cue_line, sizeof k->cue_line, "%s", line);
				k->ncue = 0;
			}
			const char *t = jat(js, "t");
			if (t && *t == '[') { /* [word, start, end, ...] */
				char *e;
				for (t++; k->ncue < 900;) {
					long v = strtol(t, &e, 10);
					if (e == t) {
						break;
					}
					k->cue[k->ncue++] = (int)v;
					t = e;
					while (*t == ',' || *t == ' ') {
						t++;
					}
				}
				k->ncue -= k->ncue % 3;
			}
		}
	}
	SDL_UnlockMutex(crew.mu);
}

/* one connection to the radio, till it drops */
static void radio_once(const char *token) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_port = htons(47984);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
		if (fd >= 0) {
			close(fd);
		}
		return;
	}
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char key[25];
	for (int i = 0; i < 22; i++) {
		key[i] = b64[rand() % 64];
	}
	key[22] = key[23] = '=';
	key[24] = '\0';
	char req[400];
	int rn = snprintf(req, sizeof req,
	                  "GET /cast/v1 HTTP/1.1\r\nHost: 127.0.0.1:47984\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
	                  "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
	                  key);
	char resp[1024];
	size_t got = 0;
	bool ok = send(fd, req, (size_t)rn, MSG_NOSIGNAL) == rn;
	while (ok && got < sizeof resp - 1 && !strstr(resp, "\r\n\r\n")) { /* the answer, a byte at a time: no frame read with it */
		ok = recv(fd, resp + got, 1, 0) == 1;
		got += ok;
		resp[got] = '\0';
	}
	if (!ok || !strstr(resp, " 101")) {
		close(fd);
		return;
	}
	char hello[1200], chans[600] = "";
	size_t cl = 0;
	for (int i = 0; i < crew.n && cl + 40 < sizeof chans; i++) {
		cl += (size_t)snprintf(chans + cl, sizeof chans - cl, "%s\"%s\"", i ? "," : "", crew.c[i].id);
	}
	int hn = snprintf(hello, sizeof hello,
	                  "{\"type\":\"hello\",\"v\":1,\"token\":\"%s\",\"device\":\"syn-shell\",\"client\":\"syn-shell/1\",\"channels\":[%s],\"seq\":1}",
	                  token, chans);
	ok = hn > 0 && hn < (int)sizeof hello && ws_send(fd, 1, hello, (size_t)hn);
	memset(hello, 0, sizeof hello); /* the token, not left about */
	char *msg = NULL;
	size_t mlen = 0;
	while (ok) {
		unsigned char h[2];
		if (!ws_all(fd, h, 2)) {
			break;
		}
		int op = h[0] & 0x0f;
		bool fin = h[0] & 0x80;
		uint64_t n = h[1] & 0x7f;
		if (n == 126) {
			unsigned char e[2];
			ok = ws_all(fd, e, 2);
			n = (uint64_t)e[0] << 8 | e[1];
		} else if (n == 127) {
			unsigned char e[8];
			ok = ws_all(fd, e, 8);
			n = 0;
			for (int i = 0; i < 8; i++) {
				n = n << 8 | e[i];
			}
		}
		if (!ok || n > (16u << 20)) {
			break;
		}
		char *pl = SDL_malloc((size_t)n + 1);
		ok = ws_all(fd, pl, (size_t)n);
		pl[n] = '\0';
		if (ok && op == 9) {
			ws_send(fd, 10, pl, (size_t)n); /* a ping: its pong */
		} else if (ok && op == 8) {
			ok = false;
		} else if (ok && (op == 1 || (op == 0 && msg))) { /* text, or the rest of it */
			msg = SDL_realloc(msg, mlen + (size_t)n + 1);
			memcpy(msg + mlen, pl, (size_t)n);
			mlen += (size_t)n;
			msg[mlen] = '\0';
			if (fin) {
				radio_heard(msg);
				SDL_free(msg);
				msg = NULL;
				mlen = 0;
			}
		} /* binary: the voice itself, not played here */
		SDL_free(pl);
	}
	SDL_free(msg);
	close(fd);
}

static int radio_thread(void *arg) {
	(void)arg;
	char path[600], token[256] = "";
	const char *home = getenv("HOME");
	snprintf(path, sizeof path, "%s/.config/syn-corp/owner-token", home ? home : "");
	srand((unsigned)SDL_GetTicks() ^ (unsigned)getpid());
	const char *fake = SDL_getenv("SYN_RADIO_FAKE"); /* "id|text": a line as if said, for trying (nothing is sent) */
	if (fake && fake[0] == '{') { /* a whole radio message, as if heard */
		SDL_Delay(6000);
		radio_heard(fake);
	} else if (fake && strchr(fake, '|')) {
		char js[900], who[40];
		snprintf(who, sizeof who, "%.*s", (int)(strchr(fake, '|') - fake), fake);
		snprintf(js, sizeof js, "{\"type\":\"say\",\"channel\":\"%s\",\"from\":\"%s\",\"message\":\"fake1\",\"text\":\"%s\"}", who, who,
		         strchr(fake, '|') + 1);
		SDL_Delay(6000);
		radio_heard(js);
	}
	for (;;) {
		FILE *f = fopen(path, "r");
		if (f) {
			if (fgets(token, sizeof token, f)) {
				token[strcspn(token, "\r\n \"\\")] = '\0';
			}
			fclose(f);
		}
		if (token[0]) {
			radio_once(token);
		}
		memset(token, 0, sizeof token);
		SDL_Delay(5000); /* the radio down or restarting: again in a while */
	}
	return 0;
}
#endif

/* The words of a line, the one being said boxed: those said in the text's
 * colour, the rest dimmer. Wrapped to maxw, its top middle at (cx, y)
 * (up from y when up). word: the one being said, -1 none, n all said. */
static void words_draw(float cx, float y, float maxw, const char *who, const char *text, int word, bool up, const Theme *t,
                       SDL_Color name_c) {
	char buf[640];
	snprintf(buf, sizeof buf, "%s", text);
	const char *wd[160];
	int nw = 0;
	for (char *p = strtok(buf, " \t\n\r"); p && nw < 160; p = strtok(NULL, " \t\n\r")) {
		wd[nw++] = p;
	}
	float th = (float)d2_text_height(), sp = (float)d2_text_width(" ");
	float lx[160], ly[160], wide = who[0] ? (float)d2_text_width(who) + sp * 2 : 0, x = wide, row = 0, most = wide;
	for (int i = 0; i < nw; i++) {
		float ww = (float)d2_text_width(wd[i]);
		if (x + ww > maxw && x > 0) {
			x = 0;
			row++;
		}
		lx[i] = x;
		ly[i] = row;
		x += ww + sp;
		most = fmaxf(most, x - sp);
	}
	float bw = most + 16, bh = (row + 1) * (th + 3) + 10, bx = cx - bw / 2, by = up ? y - bh : y;
	SDL_Color bg = rgb(t->bg), fg = rgb(t->text), dim = mix(fg, bg, 0.55f), acc = rgb(t->accent);
	bg.a = 225;
	d2_rect(bx, by, bw, bh, bg);
	d2_frame(bx, by, bw, bh, 1, acc);
	d2_text(bx + 8, by + 5, who, name_c);
	for (int i = 0; i < nw; i++) {
		float wx = bx + 8 + lx[i], wy = by + 5 + ly[i] * (th + 3);
		if (i == word) {
			d2_rect(wx - 2, wy - 1, (float)d2_text_width(wd[i]) + 4, th + 2, acc); /* the word being said */
			d2_text(wx, wy, wd[i], rgb(t->bg));
		} else {
			d2_text(wx, wy, wd[i], i < word ? fg : dim);
		}
	}
}

/* which word of what c said is being said now: -1 not yet, n all said, -2 long over */
static int said_word(const Crew *c, Uint32 now) {
	int n = 0;
	bool in = false;
	for (const char *p = c->said; *p; p++) {
		bool sp = *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r';
		n += !sp && !in;
		in = !sp;
	}
	if (c->ncue >= 3 && c->talk_at && !strcmp(c->cue_line, c->said_id)) {
		int ms = (int)(now - c->talk_at), w = -1;
		for (int i = 0; i + 2 < c->ncue; i += 3) {
			if (c->cue[i + 1] <= ms) {
				w = c->cue[i];
			}
		}
		int end = c->cue[c->ncue - 1];
		return ms > end + 8000 ? -2 : ms > end + 300 ? n : w;
	}
	int ms = (int)(now - c->said_at), w = ms / 330; /* no voice: about three words a second */
	return ms > n * 330 + 8000 ? -2 : w >= n ? n : w;
}

/* the crew member whose office building b is, or NULL */
static Crew *crew_of(const World *wd, int b) {
	if (b < 0 || b >= wd->nb || !wd->b[b].crew) {
		return NULL;
	}
	return &crew.c[crew.extra_of[wd->b[b].crew - 1]];
}

/* SYN_CREW_TEST=NAME: into that one's office once it stands, for trying it without walking there */
static void crew_test(Walk *w) {
	static bool crew_tried;
	const char *ct = SDL_getenv("SYN_CREW_TEST");
	for (int b = 0; ct && !crew_tried && b < w->world.nb; b++) {
		const Crew *c = crew_of(&w->world, b);
		if (c && !strcmp(c->id, ct)) {
			crew_tried = true;
			build_floor(w, b, 0);
			set_room(w, w->world.b[b].room0);
			at_door(w, b, 1.8f);
		}
	}
}

enum { FIG_FOLLOW, FIG_REST, FIG_WORK };

/* whether a body r round at (x, z), feet at y, stands in something it can't step onto */
static bool fig_blocked(const Walk *w, float x, float y, float z, float r) {
	if (w->bld >= 0) { /* indoors: never out through the walls into the street */
		WRect q = inset(w->world.b[w->bld].r, WALL_OUT + r);
		if (!in_rect(q, x, z)) {
			return true;
		}
	}
	for (int i = 0, n = solid_n(w); i < n; i++) {
		const Box *b = solid_i(w, i);
		if (b->hi[1] > y + STEP_UP && b->lo[1] < y + BODY_H && x + r > b->lo[0] && x - r < b->hi[0] && z + r > b->lo[2] && z - r < b->hi[2]) {
			return true;
		}
	}
	return false;
}

/* What the crew are doing this frame. Those switched on follow you as a
 * crew: in a wedge behind you, walking with you and running to catch up,
 * spreading to their places; still when you stop, looking round about them
 * and back at you now and then. Through doors and up stairs they catch
 * you up; while you drive they wait. Those switched off are at home,
 * resting. One with work running has a worker at their desk. */
static void crew_move(Walk *w, float dt) {
	int frame = w->bld * 64 + w->floor;
	bool driving = car_in(w);
	if (frame != w->crew_frame || driving) {
		for (int i = 0; i < crew.n; i++) {
			crew.c[i].fplaced = false; /* through a door, up the stairs, into the car: they catch up behind you */
		}
		w->crew_frame = frame;
	}
	if (driving) {
		return;
	}
	/* a wedge opening behind you, the lane straight back left clear for the camera */
	static const float slot[][2] = {{-1.9f, 0.5f}, {1.9f, 0.5f}, {-3.3f, 1.7f}, {3.3f, 1.7f}, {-4.7f, 2.9f}, {4.7f, 2.9f}, {-6.1f, 4.1f},
	                                {6.1f, 4.1f}, {-7.5f, 5.3f}, {7.5f, 5.3f}, {-8.9f, 6.5f}, {8.9f, 6.5f}};
	float c = cosf(w->body_yaw), s = sinf(w->body_yaw), you = sqrtf(w->hvx * w->hvx + w->hvz * w->hvz);
	int j = 0;
	SDL_LockMutex(crew.mu);
	for (int i = 0; i < crew.n; i++) {
		Crew *k = &crew.c[i];
		if (k->off || !k->home) {
			k->fplaced = false;
			continue;
		}
		float lx = slot[j % 12][0], lz = slot[j % 12][1];
		j++;
		float sx = w->px + c * lx + s * lz, sz = w->pz - s * lx + c * lz;
		if (!k->fplaced) { /* in their place behind you, or just behind you if that is in a wall */
			k->fx = sx;
			k->fz = sz;
			for (float back = 0.9f + 0.8f * (float)(j - 1); fig_blocked(w, k->fx, w->py, k->fz, 0.3f) && back >= 0; back -= 0.4f) {
				k->fx = w->px + s * back; /* in a line behind you instead, as far back as there is room */
				k->fz = w->pz + c * back;
			}
			k->fy = w->py;
			k->fyaw = w->body_yaw;
			k->fplaced = true;
		}
		float dx = sx - k->fx, dz = sz - k->fz, d = sqrtf(dx * dx + dz * dz);
		if (d > 30) { /* lost: back in their place */
			k->fplaced = false;
			continue;
		}
		float want = d > 6 ? RUN_V : d > 0.5f ? fmaxf(WALK_V * clampf(d / 2.5f, 0.35f, 1), fminf(you, RUN_V)) : 0;
		float step = fminf(d, want * dt);
		if (step > 0) {
			float mx = dx / d * step, mz = dz / d * step;
			if (!fig_blocked(w, k->fx + mx, k->fy, k->fz, 0.3f)) {
				k->fx += mx;
			}
			if (!fig_blocked(w, k->fx, k->fy, k->fz + mz, 0.3f)) {
				k->fz += mz;
			}
			float face = atan2f(-dx, -dz), df = face - k->fyaw;
			while (df > (float)M_PI) {
				df -= 2 * (float)M_PI;
			}
			while (df < -(float)M_PI) {
				df += 2 * (float)M_PI;
			}
			k->fyaw += df * clampf(dt * 10, 0, 1);
		} else {
			/* still: looking about, each in their own time, back at you now and then */
			float ph = (float)i * 1.7f, t = w->time * 0.45f + ph;
			float look = w->body_yaw + sinf(t) * 0.9f + sinf(t * 2.3f) * 0.25f;
			if (fmodf(w->time + ph * 3, 9) < 1.6f) {
				look = atan2f(-(w->px - k->fx), -(w->pz - k->fz)); /* at you */
			}
			float df = look - k->fyaw;
			while (df > (float)M_PI) {
				df -= 2 * (float)M_PI;
			}
			while (df < -(float)M_PI) {
				df += 2 * (float)M_PI;
			}
			k->fyaw += df * clampf(dt * 2.5f, 0, 1);
		}
		float sp = step / fmaxf(dt, 1e-4f);
		k->fmv = clampf(sp / WALK_V, 0, 1);
		k->frun = clampf((sp - WALK_V) / (RUN_V - WALK_V), 0, 1);
		k->fstride += sp * dt * 2.2f;
		float under = floor_under(w, k->fx, k->fy + STEP_UP, k->fz);
		k->fy += (under - k->fy) * clampf(dt * 12, 0, 1);
	}
	SDL_UnlockMutex(crew.mu);
}

/* the crew's figures into their bags: following, resting at home, at work */
static void crew_figs(Walk *w) {
	w->nfig = 0;
	for (int f = 0; f < FIG_MAX; f++) {
		w->fbox[f].n = w->fball[f].n = w->flamp[f].n = 0;
	}
	const Crew *office = w->bld >= 0 && w->floor == 0 ? crew_of(&w->world, w->bld) : NULL;
	SDL_LockMutex(crew.mu);
	for (int i = 0; i < crew.n && w->nfig < FIG_MAX - 1; i++) {
		Crew *k = &crew.c[i];
		SDL_Color glow = lighten(k->hood, 0.7f);
		if (k->fplaced && !car_in(w)) {
			int f = w->nfig++;
			w->fig[f].crew = i;
			w->fig[f].kind = FIG_FOLLOW;
			w->fig[f].x = k->fx;
			w->fig[f].y = k->fy;
			w->fig[f].z = k->fz;
			w->fig[f].yaw = k->fyaw;
			figure(&w->fbox[f], &w->fball[f], &w->flamp[f], k->fx, k->fy, k->fz, k->fmv, k->frun, k->fstride, w->time, k->hood, glow, false, 0);
		}
		bool working = k->busy || k->presence[0] || k->duty[0];
		if (office != k || w->nblock == 0) {
			continue;
		}
		/* at home: resting in a chair in the middle of the room; a worker at the desk (the first file) */
		const WRoom *r = &w->world.room[w->world.b[w->bld].room0];
		for (int kind = FIG_REST; kind <= FIG_WORK && w->nfig < FIG_MAX; kind++) {
			if ((kind == FIG_REST && !k->off) || (kind == FIG_WORK && !working)) {
				continue;
			}
			float x, z, yaw;
			if (kind == FIG_REST) {
				x = (r->r.x0 + r->r.x1) / 2 + 1.2f;
				z = (r->r.z0 + r->r.z1) / 2;
				yaw = 0.6f;
			} else {
				const Block *b = &w->block[0];
				float cx = (b->b.lo[0] + b->b.hi[0]) / 2, cz = (b->b.lo[2] + b->b.hi[2]) / 2;
				switch (b->front) { /* before its front, facing it */
				case 0: x = cx; z = b->b.lo[2] - 0.62f; yaw = (float)M_PI; break;
				case 1: x = b->b.hi[0] + 0.62f; z = cz; yaw = (float)M_PI / 2; break;
				case 2: x = cx; z = b->b.hi[2] + 0.62f; yaw = 0; break;
				default: x = b->b.lo[0] - 0.62f; z = cz; yaw = -(float)M_PI / 2; break;
				}
			}
			int f = w->nfig++;
			w->fig[f].crew = i;
			w->fig[f].kind = kind;
			w->fig[f].x = x;
			w->fig[f].y = 0;
			w->fig[f].z = z;
			w->fig[f].yaw = yaw;
			figure(&w->fbox[f], &w->fball[f], &w->flamp[f], x, 0, z, 0, 0, 0, w->time, k->hood, kind == FIG_WORK ? lighten(k->hood, 0.9f) : darken(glow, 0.4f),
			       kind == FIG_WORK, kind == FIG_WORK ? 2 : 1);
		}
	}
	SDL_UnlockMutex(crew.mu);
}

/* Building b from outside: its walls and roof, on each floor the windows
 * its rooms have inside (lit) and where it has none (dark); its door, a
 * canopy and its name over it; what its style adds. Near: window by
 * window; far: a band a floor. */
static void building_out(Walk *w, int bi, const Theme *t, SDL_Color horizon, bool near) {
	const World *wd = &w->world;
	const WBuilding *b = &wd->b[bi];
	WRect q = b->r;
	float H = (float)b->floors * FLOOR_H, cx = (q.x0 + q.x1) / 2, cz = (q.z0 + q.z1) / 2, d = sqrtf(cx * cx + cz * cz);
	SDL_Color body, trim, dark, kc = b->kind >= 0 ? kind_colour(b->kind, t) : rgb(t->accent);
	style_look(b->style, b->kind, t, &body, &trim, &dark);
	body = haze(body, horizon, d);
	trim = haze(trim, horizon, d);
	dark = haze(dark, horizon, d);
	cbox(w, CB_BODY, rbox(q, 0, H), body);
	cbox(w, CB_LIT, rbox(inset(q, -0.15f), H, H + 0.35f), trim); /* the parapet */
	float out = 0.04f;
	for (int fl = 0; fl < b->floors; fl++) {
		float y = (float)fl * FLOOR_H, ww, y0, y1;
		world_window_size(wd, bi, fl, &ww, &y0, &y1);
		y1 = fminf(y1, WALL_H - 0.15f);
		if (fl) {
			cbox(w, CB_LIT, rbox(inset(q, -0.05f), y - 0.12f, y + 0.02f), trim); /* a line between floors */
		}
		for (int f = 0; f < 4; f++) {
			float A0 = f == 0 || f == 2 ? q.x0 : q.z0, A1 = f == 0 || f == 2 ? q.x1 : q.z1;
			/* where rooms meet this side on this floor, lit; between, dark */
			float seg[64][2];
			unsigned lit[64];
			int ns = 0;
			for (int r = b->room0; r < b->room0 + b->nroom && ns < 64; r++) {
				const WRoom *R = &wd->room[r];
				if (R->floor == fl && outer_side(wd, R, f)) {
					seg[ns][0] = f == 0 || f == 2 ? R->r.x0 : R->r.z0;
					seg[ns][1] = f == 0 || f == 2 ? R->r.x1 : R->r.z1;
					lit[ns++] = R->seed;
				}
			}
			if (!near) {
				SDL_Color c = ns ? haze(room_light(b->seed + (unsigned)(fl * 4 + f)), horizon, d * 0.6f) : dark;
				cbox(w, ns ? CB_LAMP : CB_LIT, face_out(q, f, 0, out, A0 + 0.5f, A1 - 0.5f, y + y0, y + y1), c);
				continue;
			}
			float mid[64];
			for (int s = 0; s < ns; s++) {
				int n = world_windows(wd, bi, fl, f, seg[s][0], seg[s][1], mid, 64);
				SDL_Color c = haze(room_light(lit[s]), horizon, d * 0.6f);
				for (int i = 0; i < n; i++) {
					cbox(w, CB_LAMP, face_out(q, f, 0, out, mid[i] - ww / 2, mid[i] + ww / 2, y + y0, y + y1), c);
				}
			}
			/* the gaps no room meets: sorted, what is between them */
			for (int i = 1; i < ns; i++) {
				for (int j = i; j > 0 && seg[j][0] < seg[j - 1][0]; j--) {
					float s0 = seg[j][0], s1 = seg[j][1];
					seg[j][0] = seg[j - 1][0], seg[j][1] = seg[j - 1][1];
					seg[j - 1][0] = s0, seg[j - 1][1] = s1;
				}
			}
			float from = A0;
			for (int s = 0; s <= ns; s++) {
				float to = s < ns ? seg[s][0] : A1;
				if (to - from > 1.5f) {
					int n = world_windows(wd, bi, fl, f, from, to, mid, 64);
					for (int i = 0; i < n; i++) {
						cbox(w, CB_LIT, face_out(q, f, 0, out, mid[i] - ww / 2, mid[i] + ww / 2, y + y0, y + y1), dark);
					}
				}
				from = s < ns ? fmaxf(from, seg[s][1]) : from;
			}
		}
	}
	/* the door: lit, a frame, a canopy, the name over it in its kind's colour */
	int f = b->front;
	float dd = b->door, dw = FRONT_DOOR_W / 2;
	cbox(w, CB_LAMP, face_out(q, f, 0, 0.06f, dd - dw, dd + dw, 0, FRONT_DOOR_H), haze(LAMP_LIGHT, horizon, d * 0.5f));
	cbox(w, CB_LIT, face_out(q, f, 0, 0.12f, dd - dw - 0.15f, dd - dw, 0, FRONT_DOOR_H), trim);
	cbox(w, CB_LIT, face_out(q, f, 0, 0.12f, dd + dw, dd + dw + 0.15f, 0, FRONT_DOOR_H), trim);
	cbox(w, CB_LIT, face_out(q, f, 0, 1.1f, dd - dw - 0.4f, dd + dw + 0.4f, FRONT_DOOR_H + 0.05f, FRONT_DOOR_H + 0.2f), trim);
	cbox(w, CB_LAMP, face_out(q, f, 0, 0.1f, dd - dw - 0.2f, dd + dw + 0.2f, FRONT_DOOR_H + 0.35f, FRONT_DOOR_H + 0.75f),
	     haze(lighten(kc, 0.15f), horizon, d * 0.5f));
	float A0 = f == 0 || f == 2 ? q.x0 : q.z0, A1 = f == 0 || f == 2 ? q.x1 : q.z1;
	switch (b->style) {
	case STYLE_LIBRARY: /* columns either side of the door, a pediment over */
		for (int i = -1; i <= 1; i += 2) {
			float c = dd + (float)i * (dw + 0.7f);
			cbox(w, CB_LIT, face_out(q, f, 0, 0.9f, c - 0.25f, c + 0.25f, 0, fminf(H, 4.2f)), lighten(body, 0.15f));
		}
		cbox(w, CB_LIT, face_out(q, f, 0, 1.0f, dd - dw - 1.2f, dd + dw + 1.2f, fminf(H, 4.2f), fminf(H, 4.2f) + 0.45f), lighten(body, 0.1f));
		break;
	case STYLE_WAREHOUSE: /* a roller door beside the way in */
		if (dd + dw + 4.2f < A1 - 0.5f) {
			for (float y = 0.2f; y < 3.0f; y += 0.3f) {
				cbox(w, CB_LIT, face_out(q, f, 0, 0.08f, dd + dw + 1.0f, dd + dw + 4.0f, y, y + 0.22f), darken(body, 0.45f));
			}
		}
		break;
	case STYLE_RECORDS: /* a shop front: a lit fascia across, an awning */
		cbox(w, CB_LAMP, face_out(q, f, 0, 0.12f, A0 + 0.4f, A1 - 0.4f, 2.95f, 3.4f), haze(kc, horizon, d * 0.5f));
		cbox(w, CB_LIT, face_out(q, f, 0, 1.4f, A0 + 0.4f, A1 - 0.4f, 2.75f, 2.85f), darken(kc, 0.3f));
		break;
	case STYLE_DATA: /* a green light along each floor, plant on the roof */
		for (int fl = 0; fl < b->floors && near; fl++) {
			for (int s = 0; s < 4; s++) {
				float a0 = s == 0 || s == 2 ? q.x0 : q.z0, a1 = s == 0 || s == 2 ? q.x1 : q.z1;
				cbox(w, CB_LAMP, face_out(q, s, 0, 0.05f, a0 + 0.3f, a1 - 0.3f, (float)fl * FLOOR_H + 3.05f, (float)fl * FLOOR_H + 3.12f),
				     (SDL_Color){70, 235, 110, 255});
			}
		}
		for (int i = 0; i < 3; i++) {
			float x = q.x0 + wr_w(q) * (0.25f + 0.25f * (float)i);
			cbox(w, CB_LIT, box(x - 1.2f, H, cz - 1.5f, x + 1.2f, H + 1.6f, cz + 1.5f), darken(body, 0.2f));
		}
		break;
	case STYLE_GLASS: /* fins up its faces, a band of light at its top */
		for (int s = 0; s < 4 && near; s++) {
			float a0 = s == 0 || s == 2 ? q.x0 : q.z0, a1 = s == 0 || s == 2 ? q.x1 : q.z1;
			for (float a = a0 + 3.4f; a < a1 - 1; a += 3.4f) {
				cbox(w, CB_LIT, face_out(q, s, 0, 0.18f, a - 0.06f, a + 0.06f, 3.4f, H), lighten(body, 0.2f));
			}
		}
		cbox(w, CB_LAMP, rbox(inset(q, -0.02f), H - 0.5f, H - 0.3f), haze(lighten(kc, 0.3f), horizon, d * 0.6f));
		break;
	case STYLE_GALLERY: /* a banner down its front */
		cbox(w, CB_LAMP, face_out(q, f, 0, 0.12f, A0 + 1.0f, A0 + 2.2f, fmaxf(1.0f, H - 7), H - 0.5f), haze(kc, horizon, d * 0.5f));
		break;
	default: break;
	}
	if (b->floors >= 8) {
		/* a mast, its light on top */
		float mh = 3 + H * 0.12f;
		cbox(w, CB_LIT, box(cx - 0.12f, H, cz - 0.12f, cx + 0.12f, H + mh, cz + 0.12f), trim);
		cbox(w, CB_LAMP, box(cx - 0.25f, H + mh, cz - 0.25f, cx + 0.25f, H + mh + 0.5f, cz + 0.25f), (SDL_Color){255, 70, 60, 255});
	}
	char text[96];
	const Crew *who = crew_of(&w->world, bi);
	if (who) {
		snprintf(text, sizeof text, "OFFICE: %.60s", who->name);
	} else {
		snprintf(text, sizeof text, "%.60s", b->name);
	}
	Vec3 p = side_at(q, f, -0.4f, dd, FRONT_DOOR_H + 1.1f);
	csign_add(w, p, 0, text);
}

/* something in the street to walk round: a post, a trunk */
static void prop_add(Walk *w, Box b) {
	if (w->ncprop == w->cprop_cap) {
		w->cprop_cap = w->cprop_cap ? w->cprop_cap * 2 : 256;
		w->cprop = SDL_realloc(w->cprop, (size_t)w->cprop_cap * sizeof *w->cprop);
	}
	w->cprop[w->ncprop++] = b;
}

/* a street lamp at (x, z), its arm reaching toward (dx, dz) */
static void lamp_post(Walk *w, float x, float z, float dx, float dz, SDL_Color pole, SDL_Color light, bool near) {
	if (near) {
		cbox(w, CB_LIT, box(x - 0.07f, 0, z - 0.07f, x + 0.07f, 5.0f, z + 0.07f), pole);
		prop_add(w, box(x - 0.1f, 0, z - 0.1f, x + 0.1f, 5.0f, z + 0.1f));
		cbox(w, CB_LIT, box(fminf(x, x + dx), 4.9f, fminf(z, z + dz), fmaxf(x, x + dx) + 0.06f, 5.0f, fmaxf(z, z + dz) + 0.06f), pole);
	}
	cbox(w, CB_LAMP, box(x + dx - 0.25f, 4.82f, z + dz - 0.25f, x + dx + 0.25f, 4.92f, z + dz + 0.25f), light);
}

static void tree(Walk *w, float x, float z, unsigned s, SDL_Color horizon, float d) {
	float r = 0.9f + rnd(&s) * 0.5f, th = 1.8f + rnd(&s);
	cbox(w, CB_LIT, box(x - 0.14f, 0, z - 0.14f, x + 0.14f, th + r * 0.5f, z + 0.14f), haze((SDL_Color){70, 52, 38, 255}, horizon, d));
	prop_add(w, box(x - 0.18f, 0, z - 0.18f, x + 0.18f, th + r * 1.6f, z + 0.18f));
	bag_put(&w->cbag[CB_BALL], x, th + r, z, 2 * r, 2.2f * r, 2 * r, haze((SDL_Color){40, 98, 60, 255}, horizon, d));
}

/* ---- beyond the wall: the rest of the disk ----
 *
 * The city is the tree you scanned; round it, out to a plain as wide as
 * its disk would be at the city's scale (the city's share of the plain is
 * the tree's share of the disk), the disk's other top-level folders stand
 * as towers by their size: measured once in the background, kept a day
 * in ~/.cache/SYN-OS/syn-shell/beyond. What could not be read is one
 * tower; the free space, open grid. */

#define BEYOND_MAX 48
typedef struct {
	char name[64];
	uint64_t size;
} Beyond;
static struct {
	char root[1024];          /* the tree measured round (left out of it) */
	uint64_t total, used, own; /* the disk, its used bytes, and the tree's */
	Beyond part[BEYOND_MAX];
	int n;
	SDL_atomic_t state;        /* 0 not asked, 1 measuring, 2 ready */
} beyond;

#ifndef _WIN32
static uint64_t beyond_sum;
static const char *beyond_skip;
static int beyond_add(const char *path, const struct stat *st, int flag, struct FTW *f) {
	(void)f;
	if (beyond_skip && !strncmp(path, beyond_skip, strlen(beyond_skip)) &&
	    (path[strlen(beyond_skip)] == '\0' || path[strlen(beyond_skip)] == '/')) {
		return FTW_SKIP_SUBTREE; /* the city itself */
	}
	if (flag == FTW_F || flag == FTW_D || flag == FTW_SL) {
		beyond_sum += (uint64_t)st->st_blocks * 512u;
	}
	return FTW_CONTINUE;
}

static void beyond_cache_path(char *out, size_t n) {
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.cache/SYN-OS/syn-shell/beyond", home ? home : "/tmp");
}

static int by_beyond(const void *a, const void *b) {
	uint64_t x = ((const Beyond *)a)->size, y = ((const Beyond *)b)->size;
	return x < y ? 1 : x > y ? -1 : 0;
}

static int beyond_thread(void *arg) {
	(void)arg;
	struct stat rs;
	char cache[1100];
	beyond_cache_path(cache, sizeof cache);
	if (stat(beyond.root, &rs) != 0) {
		SDL_AtomicSet(&beyond.state, 2);
		return 0;
	}
	/* a day's old measure of the same tree does */
	struct stat cs;
	FILE *f = stat(cache, &cs) == 0 && time(NULL) - cs.st_mtime < 86400 ? fopen(cache, "r") : NULL;
	if (f) {
		char line[1200], root[1024] = "";
		if (fgets(line, sizeof line, f) && sscanf(line, "root %1023[^\n]", root) == 1 && !strcmp(root, beyond.root)) {
			beyond.n = 0;
			unsigned long long sz;
			char name[64];
			while (beyond.n < BEYOND_MAX && fgets(line, sizeof line, f) && sscanf(line, "%llu %63[^\n]", &sz, name) == 2) {
				snprintf(beyond.part[beyond.n].name, sizeof beyond.part[0].name, "%s", name);
				beyond.part[beyond.n++].size = sz;
			}
			fclose(f);
			SDL_AtomicSet(&beyond.state, 2);
			return 0;
		}
		fclose(f);
	}
	/* each top-level folder on the same disk, the city left out */
	static const char *virt[] = {"proc", "sys", "dev", "run", "tmp", "lost+found"};
	DIR *d = opendir("/");
	int n = 0;
	uint64_t seen = 0;
	struct dirent *e;
	while (d && (e = readdir(d)) && n < BEYOND_MAX - 1) {
		if (e->d_name[0] == '.') {
			continue;
		}
		bool skip = false;
		for (size_t i = 0; i < sizeof virt / sizeof *virt; i++) {
			skip |= !strcmp(e->d_name, virt[i]);
		}
		char path[300];
		snprintf(path, sizeof path, "/%s", e->d_name);
		struct stat st;
		if (skip || lstat(path, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_dev != rs.st_dev) {
			continue; /* virtual, a link, a file, or another disk */
		}
		beyond_sum = 0;
		beyond_skip = beyond.root;
		nftw(path, beyond_add, 32, FTW_PHYS | FTW_MOUNT | FTW_ACTIONRETVAL);
		if (beyond_sum > 0) {
			snprintf(beyond.part[n].name, sizeof beyond.part[0].name, "%s", path);
			beyond.part[n++].size = beyond_sum;
			seen += beyond_sum;
		}
	}
	if (d) {
		closedir(d);
	}
	/* what is used but could not be read (other users' homes, root's): one tower */
	uint64_t rest = beyond.used > beyond.own + seen ? beyond.used - beyond.own - seen : 0;
	if (rest > beyond.used / 100) {
		snprintf(beyond.part[n].name, sizeof beyond.part[0].name, "no access");
		beyond.part[n++].size = rest;
	}
	qsort(beyond.part, (size_t)n, sizeof *beyond.part, by_beyond);
	beyond.n = n;
	if (SDL_getenv("SYN_WORLD_DUMP")) {
		fprintf(stderr, "beyond: measured %d folders\n", n);
	}
	char dir[1100];
	snprintf(dir, sizeof dir, "%s", cache);
	for (char *s = strchr(dir + 1, '/'); s; s = strchr(s + 1, '/')) {
		*s = '\0';
		mkdir(dir, 0755);
		*s = '/';
	}
	if ((f = fopen(cache, "w"))) {
		fprintf(f, "root %s\n", beyond.root);
		for (int i = 0; i < n; i++) {
			fprintf(f, "%llu %s\n", (unsigned long long)beyond.part[i].size, beyond.part[i].name);
		}
		fclose(f);
	}
	SDL_AtomicSet(&beyond.state, 2);
	return 0;
}
#endif

/* the disk under the tree at root: its size now, its folders measured in the background */
static void beyond_ask(const Node *root) {
#ifndef _WIN32
	char path[1024];
	node_path(root, path, sizeof path);
	if (!strcmp(path, "/")) {
		return; /* the whole disk is the city */
	}
	struct statvfs v;
	if (statvfs(path, &v) != 0) {
		return;
	}
	beyond.total = (uint64_t)v.f_blocks * v.f_frsize;
	beyond.used = beyond.total - (uint64_t)v.f_bfree * v.f_frsize;
	beyond.own = root->size;
	if (SDL_AtomicGet(&beyond.state) == 1 || (SDL_AtomicGet(&beyond.state) == 2 && !strcmp(beyond.root, path))) {
		return;
	}
	snprintf(beyond.root, sizeof beyond.root, "%s", path);
	SDL_AtomicSet(&beyond.state, 1);
	if (SDL_getenv("SYN_WORLD_DUMP")) {
		fprintf(stderr, "beyond: measuring the disk round %s (%llu used of %llu)\n", path, (unsigned long long)beyond.used, (unsigned long long)beyond.total);
	}
	SDL_Thread *th = SDL_CreateThread(beyond_thread, "beyond", NULL);
	if (th) {
		SDL_DetachThread(th);
	} else {
		SDL_AtomicSet(&beyond.state, 0);
	}
#else
	(void)root;
#endif
}

static void fsign_add(Walk *w, Vec3 p, const char *text) {
	if (w->nfsign == w->fsign_cap) {
		w->fsign_cap = w->fsign_cap ? w->fsign_cap * 2 : 32;
		w->fsign = SDL_realloc(w->fsign, (size_t)w->fsign_cap * sizeof *w->fsign);
	}
	Sign *s = &w->fsign[w->nfsign++];
	s->p = p;
	s->room = -1;
	s->look = 0;
	snprintf(s->text, sizeof s->text, "%s", text);
}

/* how far the plain goes: the disk at the city's scale */
static float beyond_reach(const Walk *w) {
	float E = w->world.edge;
	if (!beyond.total || !beyond.own) {
		return E + 400;
	}
	return clampf(E * sqrtf((float)((double)beyond.total / (double)beyond.own)), E + 200, 2600);
}

/* the plain, its grid, the disk's edge and the towers of the rest of it */
static void beyond_make(Walk *w, const Theme *t, SDL_Color back) {
	float E = w->world.edge + 0.5f, R = beyond_reach(w);
	SDL_Color acc = rgb(t->accent), plain = mix(back, acc, 0.05f), grid = mix(back, acc, 0.32f), deep = mix((SDL_Color){6, 7, 9, 255}, acc, 0.08f);
	plain.a = grid.a = deep.a = 255;
	cbox(w, CB_GROUND, box(-R, -KERB - 0.5f, -R, R, -KERB - 0.1f, R), plain);
	for (float a = -R; a <= R + 0.01f; a += 20) { /* the grid, every 20 m; under the city's ground there */
		cbox(w, CB_LAMP, box(a - 0.07f, -KERB - 0.1f, -R, a + 0.07f, -KERB - 0.085f, R), grid);
		cbox(w, CB_LAMP, box(-R, -KERB - 0.1f, a - 0.07f, R, -KERB - 0.085f, a + 0.07f), grid);
	}
	SDL_Color edge = lighten(acc, 0.2f);
	for (int s = 0; s < 4; s++) { /* the disk's edge: a line of light, a low glowing wall */
		WRect all = {-R, -R, R, R};
		cbox(w, CB_LAMP, side_box(all, s, 0, 0.6f, -R, R, -KERB - 0.1f, -KERB + 0.4f), edge);
	}
	w->nfsign = 0;
	char size[32], free_s[32], text[96];
	format_size(beyond.total, size, sizeof size);
	format_size(beyond.total - beyond.used, free_s, sizeof free_s);
	snprintf(text, sizeof text, "THE DISK: %s, %s free", size, free_s);
	fsign_add(w, v3(0, 6, -R + 2), text);
	w->beyond_had = SDL_AtomicGet(&beyond.state) == 2;
	if (!w->beyond_had) {
		fsign_add(w, v3(0, 12, -(E + R) / 2), "the rest of the disk: measuring");
		return;
	}
	/* the towers round the city, biggest first, in a ring between the wall and the edge */
	float ang = 0.35f;
	for (int i = 0; i < beyond.n; i++) {
		const Beyond *b = &beyond.part[i];
		float share = (float)((double)b->size / (double)(beyond.total ? beyond.total : 1));
		float side = clampf(sqrtf(share) * R * 0.75f, 6, R * 0.3f), h = clampf(side * 1.3f, 12, 220);
		float r = E + 40 + side * 0.75f + (R - E - 40 - side * 1.5f) * 0.3f;
		float step = (side + 30) / r;
		float cx = sinf(ang + step / 2) * r, cz = -cosf(ang + step / 2) * r;
		ang += step;
		Box core = box(cx - side / 2, -KERB, cz - side / 2, cx + side / 2, h, cz + side / 2);
		cbox(w, CB_BODY, core, deep);
		SDL_Color line = mix(mix(acc, (SDL_Color){255, 255, 255, 255}, 0.1f + 0.2f * (float)(i % 3) / 2), back, 0.35f);
		line.a = 255;
		float e = clampf(side * 0.006f, 0.2f, 0.9f);
		for (int c = 0; c < 4; c++) { /* edges of light up each corner */
			float x = c & 1 ? core.hi[0] : core.lo[0], z = c & 2 ? core.hi[2] : core.lo[2];
			cbox(w, CB_LAMP, box(x - e, -KERB, z - e, x + e, h + e, z + e), line);
		}
		for (float y = 8; y < h; y += 9 + side * 0.08f) { /* bands round it */
			SDL_Color band = darken(line, 0.45f);
			cbox(w, CB_LAMP, box(core.lo[0] - 0.05f, y, core.lo[2] - 0.05f, core.hi[0] + 0.05f, y + e * 0.6f, core.hi[2] + 0.05f), band);
		}
		cbox(w, CB_LAMP, box(core.lo[0] - e, h, core.lo[2] - e, core.hi[0] + e, h + e, core.hi[2] + e), line);
		format_size(b->size, size, sizeof size);
		size_t nl = strlen(b->name);
		bool rest = !strncmp(beyond.root, b->name, nl) && beyond.root[nl] == '/'; /* the city is in it */
		snprintf(text, sizeof text, "%s%s  %s", b->name, rest ? " (the rest)" : "", size);
		fsign_add(w, v3(cx, h + 6, cz), text);
	}
}

/* The city's boxes, from the world (never the tree): the ground, roads
 * with their markings and crossings, the blocks' pavements with lamps
 * and trees along them, the square, the wall round it all; then each
 * building, its boxes together. Near (lx, lz) in more detail. */
static void city_make(Walk *w, const Theme *t, SDL_Color back, float lx, float lz) {
	const World *wd = &w->world;
	for (int i = 0; i < CB_COUNT; i++) {
		w->cbag[i].n = 0;
	}
	w->nglass = w->ncsign = w->ncprop = 0;
	w->near_ok = false;
	w->city_dirty = false;
	w->city_theme = *t;
	w->city_back = back;
	w->lod_x = lx;
	w->lod_z = lz;
	SDL_Color horizon = mix(back, rgb(t->bg_alt), 0.75f);
	horizon.a = 255;
	SDL_Color pave = mix(rgb(t->bg_alt), rgb(t->panel_hover), 0.55f), kerb = lighten(pave, 0.18f);
	SDL_Color road = darken(mix(rgb(t->bg_alt), rgb(t->panel), 0.4f), 0.3f), paint = mix(road, (SDL_Color){235, 235, 225, 255}, 0.6f);
	SDL_Color pole = darken(rgb(t->panel), 0.3f), wallc = mix(rgb(t->panel), rgb(t->accent_dim), 0.55f);
	float E = wd->edge;
#define NEAR(x, z, r) (((x) - lx) * ((x) - lx) + ((z) - lz) * ((z) - lz) < (r) * (r))

	/* the ground: road out to the wall, a plain beyond going into the haze */
	cbox(w, CB_GROUND, box(-E, -KERB - 0.3f, -E, E, -KERB, E), road);
	beyond_make(w, t, back);

	/* the roads: a dashed line down a street, a double one down an avenue,
	 * none across a crossing, zebra stripes either side of each */
	for (int i = 0; i < wd->nroad; i++) {
		const WRoad *r = &wd->road[i];
		bool ax = r->along_x;
		float a0 = ax ? r->r.x0 : r->r.z0, a1 = ax ? r->r.x1 : r->r.z1, m = ax ? (r->r.z0 + r->r.z1) / 2 : (r->r.x0 + r->r.x1) / 2;
		float wid = ax ? wr_d(r->r) : wr_w(r->r);
		/* where the roads across it are */
		float cut[96][2];
		int nc = 0;
		for (int j = 0; j < wd->nroad && nc < 96; j++) {
			const WRoad *o = &wd->road[j];
			if (o->along_x != ax) {
				float c0 = ax ? o->r.x0 : o->r.z0, c1 = ax ? o->r.x1 : o->r.z1;
				float o0 = ax ? o->r.z0 : o->r.x0, o1 = ax ? o->r.z1 : o->r.x1;
				if (m > o0 && m < o1) {
					cut[nc][0] = c0;
					cut[nc++][1] = c1;
				}
			}
		}
		for (float a = a0 + 1; a + 2 < a1; a += r->avenue ? 3.0f : 4.5f) {
			bool skip = false;
			for (int c = 0; c < nc && !skip; c++) {
				skip = a + 2 > cut[c][0] - 4 && a < cut[c][1] + 4;
			}
			float px = ax ? a : m, pz = ax ? m : a;
			if (skip || !NEAR(px, pz, 260)) {
				continue;
			}
			float len = r->avenue ? 3.0f : 2.0f;
			for (int k = r->avenue ? -1 : 0; k <= (r->avenue ? 1 : 0); k += 2) {
				float o = (float)k * 0.18f;
				cbox(w, CB_LIT, ax ? box(a, -KERB, m + o - 0.08f, a + len, -KERB + 0.02f, m + o + 0.08f)
				                   : box(m + o - 0.08f, -KERB, a, m + o + 0.08f, -KERB + 0.02f, a + len),
				     haze(paint, horizon, sqrtf(px * px + pz * pz)));
				if (!r->avenue) {
					break;
				}
			}
		}
		for (int c = 0; c < nc; c++) {
			for (int e = 0; e < 2; e++) {
				float s0 = e ? cut[c][1] + 0.6f : cut[c][0] - 3.6f, s1 = s0 + 3.0f;
				float px = ax ? (s0 + s1) / 2 : m, pz = ax ? m : (s0 + s1) / 2;
				if (s0 < a0 || s1 > a1 || !NEAR(px, pz, 120)) {
					continue;
				}
				for (float o = -wid / 2 + 0.6f; o + 0.5f < wid / 2 - 0.4f; o += 1.0f) {
					cbox(w, CB_LIT, ax ? box(s0, -KERB, m + o, s1, -KERB + 0.02f, m + o + 0.5f) : box(m + o, -KERB, s0, m + o + 0.5f, -KERB + 0.02f, s1), paint);
				}
			}
		}
	}

	/* the street names: a post at a corner of each crossing */
	for (int i = 0; i < wd->nroad; i++) {
		const WRoad *a = &wd->road[i];
		if (a->along_x) {
			continue;
		}
		for (int j = 0; j < wd->nroad; j++) {
			const WRoad *b = &wd->road[j];
			if (!b->along_x || a->r.x0 < b->r.x0 || a->r.x1 > b->r.x1) {
				continue;
			}
			float x = a->r.x1 + 0.8f, z = b->r.z0 - 0.8f;
			if (!NEAR(x, z, 150) || fabsf(x) > E - 2 || fabsf(z) > E - 2) {
				continue;
			}
			cbox(w, CB_LIT, box(x - 0.05f, 0, z - 0.05f, x + 0.05f, 2.9f, z + 0.05f), pole);
			cbox(w, CB_LAMP, box(x - 0.6f, 2.55f, z - 0.03f, x + 0.6f, 2.85f, z + 0.03f), mix(rgb(t->accent), (SDL_Color){40, 120, 70, 255}, 0.5f));
			char na[80], nb[80], text[200];
			road_name(w, a, na, sizeof na);
			road_name(w, b, nb, sizeof nb);
			snprintf(text, sizeof text, "%s / %s", na, nb);
			csign_add(w, v3(x, 3.1f, z), 3, text);
		}
	}

	/* each block's pavement, a kerb round it, lamps along it with trees between */
	SDL_Color light = LAMP_LIGHT;
	for (int i = -1; i < wd->nb; i++) {
		WRect c = i < 0 ? inset(wd->square, -PAVE) : wd->b[i].cell;
		float cx = (c.x0 + c.x1) / 2, cz = (c.z0 + c.z1) / 2, d = sqrtf(cx * cx + cz * cz);
		bool near = NEAR(cx, cz, LOD_NEAR + 30);
		cbox(w, CB_LIT, rbox(c, -KERB - 0.2f, 0), haze(pave, horizon, d));
		for (int s = 0; s < 4; s++) {
			float a0 = s == 0 || s == 2 ? c.x0 : c.z0, a1 = s == 0 || s == 2 ? c.x1 : c.z1;
			cbox(w, CB_LIT, side_box(c, s, 0, 0.22f, a0, a1, -KERB - 0.05f, 0.012f), haze(kerb, horizon, d));
			float span = a1 - a0;
			int n = (int)(span / 16);
			/* none in front of a door */
			bool front = i >= 0 && s == wd->b[i].front;
			float door = front ? wd->b[i].door : 1e9f;
			for (int k = 0; k <= n; k++) {
				float a = a0 + 2 + (span - 4) * (n ? (float)k / (float)n : 0.5f);
				Vec3 p = side_at(c, s, 0.6f, a, 0), o = side_at(c, s, -0.6f, a, 0);
				if (fabsf(a - door) > 3) {
					lamp_post(w, p.x, p.z, o.x - p.x, o.z - p.z, haze(pole, horizon, d), haze(light, horizon, d * 0.5f), near);
				}
				float ta = a + (span - 4) / (float)(n ? n : 1) / 2;
				if (near && k < n && !front && fabsf(ta - door) > 4) {
					Vec3 tp = side_at(c, s, 0.9f, ta, 0);
					if (i < 0 || !in_rect(inset(wd->b[i].r, -2.0f), tp.x, tp.z)) {
						tree(w, tp.x, tp.z, (unsigned)(i * 131 + s * 17 + k), horizon, d);
					}
				}
			}
		}
		/* behind a building with room to spare: grass and trees */
		if (i >= 0 && near) {
			const WBuilding *b = &wd->b[i];
			WRect g = inset(c, PAVE);
			switch (b->front) {
			case 0: g.z0 = b->r.z1 + 1.5f; break;
			case 1: g.x1 = b->r.x0 - 1.5f; break;
			case 2: g.z1 = b->r.z0 - 1.5f; break;
			default: g.x0 = b->r.x1 + 1.5f; break;
			}
			if (wr_w(g) > 4 && wr_d(g) > 4) {
				cbox(w, CB_LIT, rbox(g, 0, 0.12f), haze(mix(pave, (SDL_Color){44, 92, 52, 255}, 0.55f), horizon, d));
				unsigned s = b->seed;
				int n = 1 + (int)(wr_w(g) * wr_d(g) / 60);
				for (int k = 0; k < n && k < 8; k++) {
					tree(w, g.x0 + 1.2f + rnd(&s) * (wr_w(g) - 2.4f), g.z0 + 1.2f + rnd(&s) * (wr_d(g) - 2.4f), s, horizon, d);
				}
			}
		}
	}

	/* the square: paving in a pattern, the fountain in its middle, its name on a stone */
	WRect sq = wd->square;
	float sx = (sq.x0 + sq.x1) / 2, sz = (sq.z0 + sq.z1) / 2;
	cbox(w, CB_LIT, box(sq.x0, 0, sz - 1.2f, sq.x1, 0.015f, sz + 1.2f), lighten(pave, 0.1f));
	cbox(w, CB_LIT, box(sx - 1.2f, 0, sq.z0, sx + 1.2f, 0.015f, sq.z1), lighten(pave, 0.1f));
	cbox(w, CB_LIT, box(sx - 3.2f, 0, sz - 3.2f, sx + 3.2f, 0.55f, sz + 3.2f), lighten(pave, 0.25f));
	cbox(w, CB_LAMP, box(sx - 2.8f, 0.4f, sz - 2.8f, sx + 2.8f, 0.5f, sz + 2.8f), mix(rgb(t->accent), (SDL_Color){90, 170, 230, 255}, 0.6f));
	cbox(w, CB_LIT, box(sx - 0.3f, 0, sz - 0.3f, sx + 0.3f, 1.6f, sz + 0.3f), lighten(pave, 0.2f));
	bag_put(&w->cbag[CB_BALL], sx, 1.9f, sz, 0.9f, 1.1f, 0.9f, mix(rgb(t->accent), (SDL_Color){150, 210, 255, 255}, 0.6f));
	for (int k = 0; k < 4; k++) {
		float tx = k & 1 ? sq.x1 - 2.5f : sq.x0 + 2.5f, tz = k & 2 ? sq.z1 - 2.5f : sq.z0 + 2.5f;
		tree(w, tx, tz, (unsigned)k * 7919u, horizon, 0);
	}
	char text[160];
	snprintf(text, sizeof text, "%.80s: its own files", wd->name);
	csign_add(w, v3(sx, 2.8f, sz + 3.4f), 0, text);

	/* the wall round the city, a light along its top */
	SDL_Color fence = haze(darken(wallc, 0.2f), horizon, E);
	for (int s = 0; s < 4; s++) {
		WRect all = {-E - 0.5f, -E - 0.5f, E + 0.5f, E + 0.5f};
		cbox(w, CB_BODY, side_box(all, s, 0, 0.5f, -E - 0.5f, E + 0.5f, -KERB, CITY_FENCE), fence);
		cbox(w, CB_LAMP, side_box(all, s, 0.2f, 0.55f, -E - 0.5f, E + 0.5f, CITY_FENCE - 0.2f, CITY_FENCE - 0.1f),
		     haze(lighten(rgb(t->accent_dim), 0.2f), horizon, E * 0.5f));
	}

	/* the buildings, each one's boxes together */
	if (w->cb_from_cap < (wd->nb + 1) * CB_COUNT) {
		w->cb_from_cap = (wd->nb + 1) * CB_COUNT;
		w->cb_from = SDL_realloc(w->cb_from, (size_t)w->cb_from_cap * sizeof *w->cb_from);
	}
	for (int i = 0; i <= wd->nb; i++) {
		for (int k = 0; k < CB_COUNT; k++) {
			w->cb_from[i * CB_COUNT + k] = w->cbag[k].n;
		}
		if (i < wd->nb) {
			WRect q = wd->b[i].r;
			float cx = (q.x0 + q.x1) / 2, cz = (q.z0 + q.z1) / 2, reach = LOD_NEAR + fmaxf(wr_w(q), wr_d(q)) / 2;
			building_out(w, i, t, horizon, NEAR(cx, cz, reach));
		}
	}
#undef NEAR
}

/* draws a city bag, all of it, or (inside a building) all but that building's */
static void city_draw(const Walk *w, int bag, int kind, float edge) {
	const Bag *g = &w->cbag[bag];
	int from = g->n, to = g->n;
	if (w->bld >= 0 && w->cb_from) {
		from = w->cb_from[w->bld * CB_COUNT + bag];
		to = w->cb_from[(w->bld + 1) * CB_COUNT + bag];
	}
	for (int part = 0; part < 2; part++) {
		const Solid *v = part ? g->v + to : g->v;
		int n = part ? g->n - to : from;
		if (n <= 0) {
			continue;
		}
		if (kind == 1) {
			d3_spheres(v, n);
		} else if (kind == 2) {
			d3_boxes(v, n);
		} else {
			d3_boxes_edged(v, n, edge);
		}
	}
}

/* whether the line from o to p passes through b */
static bool cuts(const Box *b, Vec3 o, Vec3 p) {
	float a[3] = {o.x, o.y, o.z}, v[3] = {p.x - o.x, p.y - o.y, p.z - o.z}, enter = 0.001f, leave = 0.999f;
	for (int k = 0; k < 3 && enter <= leave; k++) {
		if (fabsf(v[k]) < 1e-6f) {
			if (a[k] < b->lo[k] || a[k] > b->hi[k]) {
				return false;
			}
			continue;
		}
		float t0 = (b->lo[k] - a[k]) / v[k], t1 = (b->hi[k] - a[k]) / v[k];
		enter = fmaxf(enter, fminf(t0, t1));
		leave = fminf(leave, fmaxf(t0, t1));
	}
	return enter <= leave;
}

/* Names in the world, nearest first, none over another: signs from the
 * list, those within reach, in view and (with walls given) not behind one. */
static void signs_draw(Walk *w, const Sign *v, int n, int room, const float *mvp, Vec3 eye, float reach, const Box *wall, int nwall,
                       float x, float y, float vw, float vh, const Theme *t, SDL_Color bg) {
	if (w->ctag_cap < n) {
		w->ctag_cap = n + 64;
		w->ctag = SDL_realloc(w->ctag, (size_t)w->ctag_cap * sizeof *w->ctag);
	}
	int m = 0;
	for (int i = 0; i < n; i++) {
		Vec3 to = v3_sub(v[i].p, eye);
		float d = sqrtf(v3_dot(to, to));
		if (d < reach && (room < -1 || v[i].room == room || (v[i].look == 2 && d < 12))) {
			w->ctag[m++] = (Near){d, i};
		}
	}
	qsort(w->ctag, (size_t)m, sizeof *w->ctag, nearer);
	float th = (float)d2_text_height();
	for (int k = 0, shown = 0; k < m && shown < 24; k++) {
		const Sign *s = &v[w->ctag[k].i];
		float sx, sy;
		if (!m4_project(mvp, s->p, vw, vh, &sx, &sy) || sx < 0 || sx > vw || sy < 0 || sy > vh) {
			continue;
		}
		bool hid = false;
		for (int j = 0; j < nwall && !hid; j++) {
			hid = wall[j].hi[1] > 1 && cuts(&wall[j], eye, s->p);
		}
		if (hid) {
			continue;
		}
		SDL_Color edge = s->look == 1 ? rgb(t->accent) : s->look == 2 ? LAMP_LIGHT : s->look == 3 ? (SDL_Color){90, 190, 120, 255} : rgb(t->border);
		if (tag(x + sx, y + sy - th / 2, s->text, rgb(t->text), bg, edge)) {
			shown++;
		}
	}
}

/* Where you are out in the city, in words, for the panel top left. */
static void street_where(const Walk *w, char *out, size_t n) {
	const World *wd = &w->world;
	char where[200] = "";
	if (in_rect(inset(wd->square, -PAVE), w->px, w->pz)) {
		snprintf(where, sizeof where, "in the market square: %.60s's own files", wd->name);
	} else {
		for (int i = 0; i < wd->nroad; i++) {
			if (in_rect(wd->road[i].r, w->px, w->pz)) {
				char name[96];
				road_name(w, &wd->road[i], name, sizeof name);
				snprintf(where, sizeof where, "on %s", name);
				break;
			}
		}
	}
	float best = 6;
	const WBuilding *by = NULL;
	for (int i = 0; i < wd->nb; i++) {
		WRect q = wd->b[i].r;
		float dx = w->px < q.x0 ? q.x0 - w->px : w->px > q.x1 ? w->px - q.x1 : 0;
		float dz = w->pz < q.z0 ? q.z0 - w->pz : w->pz > q.z1 ? w->pz - q.z1 : 0;
		if (dx + dz < best) {
			best = dx + dz;
			by = &wd->b[i];
		}
	}
	if (by) {
		char size[32];
		format_size(by->size, size, sizeof size);
		snprintf(out, n, "%s%sby %.60s, %s, %d floor%s: in at its door", where, where[0] ? "  |  " : "", by->name, size, by->floors,
		         by->floors == 1 ? "" : "s");
	} else {
		snprintf(out, n, "%s", where[0] ? where : "out in the city");
	}
}

/* You: a hooded figure of light, floating over the ring that throws it,
 * the face a dark hollow with two eyes in it. Made facing -z about your
 * feet, turned as you face when drawn. */
/* A man in a hoodie, the hood up over a balaclava, a snood at the neck, a
 * tactical belt with its pouches, a drop-leg holster, cargo trousers with
 * knee pads, boots; the eyes in the mask's slit glow. Built facing -z,
 * standing at (x, y, z), into the bags given (drawn turned the way he
 * faces). mv: 0 still .. 1 walking; stride: where in his step. */
static void figure(Bag *bx, Bag *ball, Bag *lamp, float x, float y, float z, float mv, float run, float stride, float time, SDL_Color hood, SDL_Color glow, bool flicker, int pose) {
	const SDL_Color hem = darken(hood, 0.25f), pants = {50, 54, 46, 255}, pad = {28, 30, 27, 255},
	                boot = {20, 20, 22, 255}, sole = {12, 12, 13, 255}, web = {74, 66, 50, 255}, pouch = {64, 58, 44, 255},
	                metal = {150, 152, 158, 255}, mask = {13, 13, 15, 255}, snood = {46, 48, 54, 255}, glove = {18, 18, 20, 255},
	                cord = {196, 198, 204, 255};
	float s = sinf(stride), lift = fmaxf(0, cosf(stride));
	/* sprinting (run 0 .. 1): leaning into it, lower, knees high, arms pumping, bent at the elbow */
	float lean = run * 0.22f;
	y += mv * (0.03f + 0.05f * run) * fabsf(s) + (1 - mv) * 0.006f * sinf(time * 1.6f) - run * 0.05f; /* a step's rise; breathing still */
	if (flicker && fmodf(time, 6.1f) < 0.07f) {
		x += 0.03f * sinf(time * 90); /* the daemon flickers, now and then */
	}
	float sit = pose ? 0.42f : 0; /* seated: all from the hips up that much lower */
	if (pose) {
		mv = run = 0;
	}
#define FB(cx, cy, cz, sx, sy, sz, c) bag_put(bx, x + (cx), y + (cy) - ((cy) > 0.85f ? sit : 0), z + (cz) - lean * fmaxf(0, (cy) - 0.85f), sx, sy, sz, c)
#define FS(cx, cy, cz, sx, sy, sz, c) bag_put(ball, x + (cx), y + (cy) - ((cy) > 0.85f ? sit : 0), z + (cz) - lean * fmaxf(0, (cy) - 0.85f), sx, sy, sz, c)
	/* seated: thighs forward on the chair, shins down, boots on the floor; the chair under */
	for (int i = 0; pose && i < 2; i++) {
		float side = i ? 1.0f : -1.0f, lx = 0.11f * side;
		FB(lx, 0.47f, -0.2f, 0.17f, 0.17f, 0.42f, pants);
		FB(lx, 0.24f, -0.4f, 0.14f, 0.4f, 0.16f, pants);
		FB(lx, 0.075f, -0.44f, 0.13f, 0.1f, 0.29f, boot);
		FB(lx, 0.015f, -0.445f, 0.14f, 0.03f, 0.31f, sole);
	}
	if (pose) {
		SDL_Color chair = {34, 34, 38, 255};
		FB(0, 0.36f, -0.05f, 0.48f, 0.06f, 0.48f, chair);
		FB(0, 0.18f, -0.05f, 0.06f, 0.36f, 0.06f, chair);
		FB(0, 0.02f, -0.05f, 0.5f, 0.04f, 0.5f, chair);
		FB(0, 0.66f, 0.2f, 0.44f, 0.5f, 0.05f, chair); /* its back, under the sit line so not lowered */
	}
	/* legs: the feet forward and back along z (front is -z), the one behind lifting */
	for (int i = 0; !pose && i < 2; i++) {
		float side = i ? 1.0f : -1.0f, lx = 0.11f * side, swing = mv * s * (i ? -1.0f : 1.0f);
		float fz = -swing * (0.22f + 0.2f * run), up = mv * (i ? lift : fmaxf(0, -cosf(stride))) * 0.06f * (swing > 0 ? 0 : 1);
		up += mv * run * 0.24f * fmaxf(0, -cosf(stride + (i ? (float)M_PI : 0))); /* the knee driven up */
		FB(lx, 0.015f + up, fz - 0.035f, 0.14f, 0.03f, 0.31f, sole);
		FB(lx, 0.075f + up, fz - 0.03f, 0.13f, 0.1f, 0.29f, boot);
		FB(lx, 0.17f + up, fz, 0.125f, 0.1f, 0.15f, boot); /* the boot's ankle */
		FB(lx, 0.38f + up * 0.6f, fz * 0.6f, 0.14f, 0.36f, 0.16f, pants);
		FB(lx, 0.5f + up * 0.5f, fz * 0.55f - 0.085f, 0.12f, 0.12f, 0.03f, pad); /* knee pads */
		FB(lx, 0.74f + up * 0.25f, fz * (0.3f + 0.25f * run), 0.17f, 0.4f, 0.19f, pants);
		FB(lx + side * 0.092f, 0.66f, fz * 0.3f, 0.026f, 0.15f, 0.13f, darken(pants, 0.15f)); /* cargo pockets */
	}
	float rz = -mv * s * -0.22f * 0.3f; /* the right thigh's swing, for the holster */
	if (!pose) {
		FB(0.205f, 0.7f, rz, 0.05f, 0.19f, 0.1f, darken(web, 0.3f)); /* drop-leg holster and its straps */
		FB(0.11f, 0.64f, rz, 0.19f, 0.03f, 0.2f, web);
		FB(0.11f, 0.78f, rz, 0.19f, 0.03f, 0.2f, web);
		FB(0.205f, 0.82f, rz, 0.03f, 0.1f, 0.03f, web);
	}
	/* hips, the hoodie over them, the belt over that */
	FB(0, pose ? 0.5f : 0.92f, pose ? -0.02f : 0, 0.38f, 0.14f, 0.22f, pants);
	FB(0, 1.13f, 0, 0.44f, 0.28f, 0.25f, hood);
	FB(0, 1.36f, 0, 0.48f, 0.24f, 0.27f, hood);
	FB(0, 1.03f, 0, 0.45f, 0.06f, 0.26f, hem);
	FB(0, 1.13f, -0.13f, 0.3f, 0.15f, 0.02f, hem); /* the front pocket */
	FB(0, 0.99f, 0, 0.42f, 0.065f, 0.27f, web);
	FB(0, 0.99f, -0.14f, 0.075f, 0.055f, 0.016f, metal); /* the buckle, a light in it */
	bag_put(lamp, x, y - sit + 0.99f, z - 0.149f - lean * fmaxf(0, 0.99f - 0.85f), 0.014f, 0.014f, 0.006f, glow);
	for (int i = 0; i < 2; i++) { /* magazine pouches in front, pouches at the sides and back */
		float side = i ? 1.0f : -1.0f;
		FB(side * 0.13f, 0.96f, -0.145f, 0.07f, 0.1f, 0.035f, pouch);
		FB(side * 0.225f, 0.95f, 0.02f, 0.045f, 0.11f, 0.1f, pouch);
		FB(side * 0.09f, 0.95f, 0.15f, 0.1f, 0.1f, 0.045f, pouch);
	}
	FB(0.05f, 1.37f, -0.14f, 0.012f, 0.17f, 0.012f, cord); /* drawstrings */
	FB(-0.05f, 1.37f, -0.14f, 0.012f, 0.17f, 0.012f, cord);
	/* arms, swinging against the legs; gloved hands */
	for (int i = 0; i < 2; i++) {
		float side = i ? 1.0f : -1.0f, az = mv * s * (i ? 1.0f : -1.0f) * (0.17f + 0.2f * run), bend = run * mv;
		FS(side * 0.245f, 1.44f, 0, 0.17f, 0.16f, 0.21f, hood);
		FB(side * 0.275f, 1.27f, az * 0.5f, 0.12f, 0.3f, 0.13f, hood);
		if (pose) { /* seated: forearms forward, on the keys (typing, a hand at a time) or in the lap */
			float tap = pose == 2 ? 0.015f * sinf(time * 17 + (float)i * 2.1f) : 0, fy = pose == 2 ? 1.05f : 0.98f;
			FB(side * 0.24f, fy + tap, -0.17f, 0.11f, 0.11f, 0.3f, hood);
			FB(side * 0.23f, fy + tap, -0.335f, 0.115f, 0.125f, 0.04f, hem);
			FS(side * 0.22f, fy + tap, -0.39f, 0.1f, 0.09f, 0.11f, glove);
			continue;
		}
		FB(side * 0.285f, 1.0f + bend * 0.16f, az - bend * 0.1f, 0.11f, 0.27f, 0.12f, hood);
		FB(side * 0.285f, 0.865f + bend * 0.26f, az - bend * 0.17f, 0.115f, 0.04f, 0.125f, hem);
		FS(side * 0.285f, 0.8f + bend * 0.3f, az - 0.01f - bend * 0.2f, 0.09f, 0.11f, 0.1f, glove);
	}
	/* the snood, the head in its balaclava, the eye slit and the eyes */
	FS(0, 1.52f, 0, 0.19f, 0.12f, 0.2f, snood);
	FS(0, 1.65f, 0, 0.2f, 0.24f, 0.22f, mask);
	FB(0, 1.67f, -0.1f, 0.13f, 0.034f, 0.03f, sole);
	bag_put(lamp, x - 0.032f, y - sit + 1.67f, z - 0.116f - lean * fmaxf(0, 1.67f - 0.85f), 0.032f, 0.015f, 0.01f, glow);
	bag_put(lamp, x + 0.032f, y - sit + 1.67f, z - 0.116f - lean * fmaxf(0, 1.67f - 0.85f), 0.032f, 0.015f, 0.01f, glow);
	/* the hood: round behind, its sides framing the face, a peak shading it */
	FS(0, 1.7f, 0.05f, 0.28f, 0.31f, 0.29f, hood);
	FB(-0.12f, 1.66f, -0.06f, 0.04f, 0.27f, 0.12f, hood);
	FB(0.12f, 1.66f, -0.06f, 0.04f, 0.27f, 0.12f, hood);
	FB(0, 1.8f, -0.075f, 0.25f, 0.05f, 0.13f, hood);
	FB(0, 1.5f, 0.12f, 0.3f, 0.14f, 0.08f, hood); /* down over the shoulders behind */
	/* piping that glows faintly in the theme's colour: round the hood's opening, down the sleeves */
	SDL_Color pipe = darken(glow, 0.35f);
	bag_put(lamp, x - 0.141f, y - sit + 1.66f, z - 0.115f - lean * fmaxf(0, 1.66f - 0.85f), 0.006f, 0.26f, 0.008f, pipe);
	bag_put(lamp, x + 0.141f, y - sit + 1.66f, z - 0.115f - lean * fmaxf(0, 1.66f - 0.85f), 0.006f, 0.26f, 0.008f, pipe);
	bag_put(lamp, x, y - sit + 1.775f, z - 0.141f - lean * fmaxf(0, 1.775f - 0.85f), 0.25f, 0.006f, 0.008f, pipe);
	for (int i = 0; i < 2; i++) {
		float side = i ? 1.0f : -1.0f, az = mv * s * (i ? 1.0f : -1.0f) * (0.17f + 0.2f * run), bend = run * mv;
		bag_put(lamp, x + side * 0.337f, y - sit + 1.27f, z + az * 0.5f - lean * fmaxf(0, 1.27f - 0.85f), 0.006f, 0.28f, 0.02f, pipe);
		bag_put(lamp, x + side * 0.342f, y - sit + 1.0f + bend * 0.16f, z + az - bend * 0.1f - lean * fmaxf(0, 1.0f - 0.85f), 0.006f, 0.25f, 0.02f, pipe);
	}
#undef FB
#undef FS
}

/* you, and the ring you were spawned from, on what is under you */
static void hologram(Walk *w, const Theme *t, float under) {
	figure(&w->bag[BAG_FIG_BOX], &w->bag[BAG_FIG_DARK], &w->bag[BAG_FIG_LAMP], w->px, w->py, w->pz, w->ground ? w->moving : 0,
	       w->ground ? clampf((sqrtf(w->hvx * w->hvx + w->hvz * w->hvz) - WALK_V) / (RUN_V - WALK_V), 0, 1) : 0, w->stride,
	       w->time, (SDL_Color){40, 42, 48, 255}, lighten(rgb(t->accent), 0.6f), true, 0);
	SDL_Color ring = rgb(t->accent);
	ring.a = 170;
	bag_put(&w->bag[BAG_BALL_GLOW], w->px, under + 0.02f, w->pz, 0.8f, 0.02f, 0.8f, ring);
}

/* Pictures for the blocks from the host, a couple a frame: those on the
 * walls first, then the nearest. The GL context is current. */
static void pictures(Walk *w, Vec3 eye) {
	for (int i = 0; i < w->ndead; i++) {
		d3_texture_free(w->dead[i]);
	}
	w->ndead = 0;
	if (!walk_picture) {
		return;
	}
	for (int budget = 2; budget > 0; budget--) {
		Block *pick = NULL;
		for (int i = 0; i < w->ndeco && !pick; i++) {
			if (w->deco[i].block >= 0 && !w->block[w->deco[i].block].tried) {
				pick = &w->block[w->deco[i].block];
			}
		}
		float best = 30;
		for (int i = 0; i < w->nblock && !pick; i++) {
			Block *k = &w->block[i];
			if (k->tried || k->dev) {
				continue;
			}
			float dx = (k->b.lo[0] + k->b.hi[0]) / 2 - eye.x, dz = (k->b.lo[2] + k->b.hi[2]) / 2 - eye.z;
			float d = sqrtf(dx * dx + dz * dz);
			if (d < best) {
				best = d;
				pick = k;
			}
		}
		if (!pick) {
			return;
		}
		pick->tried = true;
		char path[1400];
		node_path(pick->node, path, sizeof path);
		unsigned char *px = NULL;
		int iw = 0, ih = 0;
		if (walk_picture(path, pick->kind, pick->dev, &px, &iw, &ih) && px && iw > 0 && ih > 0) {
			pick->tex = d3_texture(px, iw, ih);
			pick->aspect = (float)iw / (float)ih;
		}
		free(px);
	}
}

static const Block *block_near(const Walk *w, bool *on_it) {
	const Block *best = NULL;
	float bestd = 1.2f;
	*on_it = false;
	for (int i = 0; i < w->nblock; i++) {
		const Box *b = &w->block[i].b;
		float dx = w->px < b->lo[0] ? b->lo[0] - w->px : w->px > b->hi[0] ? w->px - b->hi[0] : 0;
		float dz = w->pz < b->lo[2] ? b->lo[2] - w->pz : w->pz > b->hi[2] ? w->pz - b->hi[2] : 0;
		float d = sqrtf(dx * dx + dz * dz);
		bool on = d < BODY_R && fabsf(w->py - b->hi[1]) < 0.06f;
		if (on) {
			*on_it = true;
			return &w->block[i];
		}
		if (d < bestd) {
			bestd = d;
			best = &w->block[i];
		}
	}
	return best;
}

/* ---- using things: E on the file you face ---- */

#define USE_REACH 3.0f /* how far off a thing can be used, in metres */

struct WalkUse {
	int target;              /* the block you face and are close to, as last drawn; -1 none */
	const Node *target_node; /* ... to know it is still that one after a rebuild */
	float glow;              /* its outline, fading in: 0 .. 1 */
	bool asked;              /* E on it, not yet handed to the host */
	char path[1400];
	int kind, dev;
	char playing[1400]; /* what the host plays, "" for nothing */
	bool no_player;     /* the host has nothing to play it with */
	bool host;          /* a host to use things (SYN-SHELL): the standalone window has none */
};

static struct WalkUse *use_of(Walk *w) {
	if (!w->use) {
		w->use = SDL_calloc(1, sizeof *w->use);
		if (w->use) {
			w->use->target = -1;
		}
	}
	return w->use;
}

static const Block *use_target(const Walk *w) {
	const struct WalkUse *u = w->use;
	if (!u || u->target < 0 || u->target >= w->nblock || w->block[u->target].node != u->target_node) {
		return NULL;
	}
	return &w->block[u->target];
}

/* whether this block is the file the host is playing */
static bool use_playing(const Walk *w, const Block *k) {
	const struct WalkUse *u = w->use;
	if (!u || !u->playing[0] || k->dev || !k->node) {
		return false;
	}
	char path[1400];
	node_path(k->node, path, sizeof path);
	return !strcmp(path, u->playing);
}

/* where the line from o along d (one metre long) first enters b; below 0 for never */
static float ray_box(Vec3 o, Vec3 d, const Box *b) {
	float p[3] = {o.x, o.y, o.z}, v[3] = {d.x, d.y, d.z}, enter = 0, leave = 1e9f;
	for (int k = 0; k < 3; k++) {
		if (fabsf(v[k]) < 1e-6f) {
			if (p[k] < b->lo[k] || p[k] > b->hi[k]) {
				return -1;
			}
			continue;
		}
		float t0 = (b->lo[k] - p[k]) / v[k], t1 = (b->hi[k] - p[k]) / v[k];
		if (t0 > t1) {
			float s = t0;
			t0 = t1;
			t1 = s;
		}
		enter = t0 > enter ? t0 : enter;
		leave = t1 < leave ? t1 : leave;
		if (enter > leave) {
			return -1;
		}
	}
	return enter;
}

/* The block you face and are close to: along the middle of the view (from
 * your eyes, or from your head when seen from behind), else the one at your
 * feet when you look down at it or stand on it; -1 for none. */
static int use_find(const Walk *w, Vec3 eye, Vec3 at) {
	Vec3 o = w->first ? eye : at, d = v3_sub(at, eye);
	float len = sqrtf(v3_dot(d, d)), reach = w->first ? USE_REACH : USE_REACH + 0.5f;
	if (len < 1e-4f) {
		return -1;
	}
	d = v3_scale(d, 1 / len);
	for (int i = 0; i < w->nwall; i++) {
		float t = ray_box(o, d, &w->solid[i]);
		if (t >= 0 && t < reach) {
			reach = t; /* nothing through a wall */
		}
	}
	int best = -1;
	for (int i = 0; i < w->nblock; i++) {
		float t = ray_box(o, d, &w->block[i].b);
		if (t >= 0 && t < reach) {
			reach = t;
			best = i;
		}
	}
	if (best >= 0) {
		return best;
	}
	bool on;
	const Block *n = block_near(w, &on);
	if (!n) {
		return -1;
	}
	if (on || (w->first && w->pitch > 0.5f)) {
		return (int)(n - w->block);
	}
	if (!w->first) {
		/* from behind: the one beside you if you face it at all */
		float cx = (n->b.lo[0] + n->b.hi[0]) / 2 - w->px, cz = (n->b.lo[2] + n->b.hi[2]) / 2 - w->pz;
		float cl = sqrtf(cx * cx + cz * cz), fx = -sinf(w->yaw), fz = -cosf(w->yaw);
		if (cl < 1e-3f || (fx * cx + fz * cz) / cl > 0.7f) {
			return (int)(n - w->block);
		}
	}
	return -1;
}

/* The plaque on a store room's board you face, as near as a file would be; -1 none. */
static int plaque_find(const Walk *w, Vec3 eye, Vec3 at) {
	Vec3 o = w->first ? eye : at, d = v3_sub(at, eye);
	float len = sqrtf(v3_dot(d, d)), reach = w->first ? USE_REACH : USE_REACH + 1.0f;
	if (len < 1e-4f || !w->nplaque) {
		return -1;
	}
	d = v3_scale(d, 1 / len);
	int best = -1;
	for (int i = 0; i < w->nplaque; i++) {
		Box b = w->plaque[i].b;
		for (int k = 0; k < 3; k++) {
			b.lo[k] -= 0.04f;
			b.hi[k] += 0.04f;
		}
		float t = ray_box(o, d, &b);
		if (t >= 0 && t < reach) {
			reach = t;
			best = i;
		}
	}
	return best;
}

/* The lift: the store room you are in is the folder to now, its files
 * standing in it, its board showing what is in it. */
static void lift(Walk *w, Node *to) {
	int at = w->at;
	w->pick = to;
	build_floor(w, w->bld, w->floor);
	set_room(w, at);
	w->fade = 0.25f;
}

/* E: the target, for the host to use when it next asks; a plaque, the lift */
static void use_key(Walk *w) {
	if (w->plaque_at >= 0 && w->plaque_at < w->nplaque) {
		lift(w, w->plaque[w->plaque_at].to);
		return;
	}
	struct WalkUse *u = use_of(w);
	const Block *k = u ? use_target(w) : NULL;
	if (!k) {
		return;
	}
	node_path(k->node, u->path, sizeof u->path);
	u->kind = k->kind;
	u->dev = k->dev;
	u->asked = true;
}

bool walk_used(Walk *w, char *path, int len, int *kind, int *dev) {
	struct WalkUse *u = w->use;
	if (!u || !u->asked) {
		return false;
	}
	u->asked = false;
	snprintf(path, (size_t)len, "%s", u->path);
	*kind = u->kind;
	*dev = u->dev;
	return true;
}

void walk_set_playing(Walk *w, const char *path) {
	struct WalkUse *u = use_of(w);
	if (u) {
		snprintf(u->playing, sizeof u->playing, "%s", path ? path : "");
	}
}

void walk_set_player(Walk *w, bool there) {
	struct WalkUse *u = use_of(w);
	if (u) {
		u->no_player = !there;
		u->host = true;
	}
}

void walk_capture(Walk *w) {
	if (w->on && !w->captured) {
		w->captured = w->look = true;
		SDL_SetRelativeMouseMode(SDL_TRUE);
	}
}

bool walk_go_to(Walk *w, const Node *n) {
	for (int i = 0; i < w->nblock; i++) {
		const Box *b = &w->block[i].b;
		if (w->block[i].node != n) {
			continue;
		}
		/* half the gap between two blocks off each face, looking at it */
		float cx = (b->lo[0] + b->hi[0]) / 2, cz = (b->lo[2] + b->hi[2]) / 2, off = 0.4f;
		float side[4][3] = {{cx, b->lo[2] - off, (float)M_PI}, {b->hi[0] + off, cz, (float)M_PI / 2},
		                    {cx, b->hi[2] + off, 0}, {b->lo[0] - off, cz, -(float)M_PI / 2}}; /* by face_of's sides */
		for (int t = 0; t < 4; t++) {
			int s = (w->block[i].front + t) % 4; /* its front first */
			float x = side[s][0], z = side[s][1];
			bool clear = true;
			for (int j = 0; j < w->nsolid && clear; j++) {
				clear = !overlaps_you(w, &w->solid[j], x, 0, z);
			}
			if (!clear) {
				continue;
			}
			w->px = x;
			w->pz = z;
			w->py = floor_under(w, x, 0, z);
			w->yaw = w->body_yaw = side[s][2];
			w->vy = w->hvx = w->hvz = 0;
			w->follow_ok = false;
			w->cam_len = 0;
			return true;
		}
		return false;
	}
	return false;
}

/* What E does to it, in a word. */
static const char *use_verb(const Walk *w, const Block *k) {
	if (k->dev) {
		return "inspect";
	}
	const char *dot = strrchr(k->name, '.');
	switch (k->kind) {
	case 0: return "view";
	case 1: return "watch";
	case 2: return w->use && w->use->no_player ? "needs ffmpeg to play" : use_playing(w, k) ? "stop" : "play";
	case 3: return "browse";
	case 4: return dot && (!SDL_strcasecmp(dot, ".txt") || !SDL_strcasecmp(dot, ".md") || !SDL_strcasecmp(dot, ".csv")) ? "read" : "open";
	case 5: return "read";
	case 6: return "inspect";
	default: return "open";
	}
}

bool walk_target(const Walk *w, char *out, int len) {
	if (w->plaque_at >= 0 && w->plaque_at < w->nplaque) {
		snprintf(out, (size_t)len, "lift to %s", w->plaque[w->plaque_at].to->name);
		return true;
	}
	const Block *k = use_target(w);
	if (k) {
		snprintf(out, (size_t)len, "%s %s", use_verb(w, k), k->name);
	}
	return k != NULL;
}

/* The target's outline, a glowing frame along its edges; the file playing,
 * a ring at its foot that beats. Into the bags, each frame. */
static void use_mark(Walk *w, const Theme *t, Vec3 eye, Vec3 at) {
	w->plaque_at = plaque_find(w, eye, at);
	struct WalkUse *u = use_of(w);
	if (!u || !u->host) {
		return;
	}
	int i = w->plaque_at >= 0 ? -1 : use_find(w, eye, at);
	const Node *n = i >= 0 ? w->block[i].node : NULL;
	if (n != u->target_node) {
		u->glow = 0;
	}
	u->target = i;
	u->target_node = n;
	for (int j = 0; j < w->nblock && u->playing[0]; j++) {
		const Box *b = &w->block[j].b;
		if (!use_playing(w, &w->block[j])) {
			continue;
		}
		float beat = 0.5f + 0.5f * sinf(w->time * 7.5f), grow = 0.25f + 0.12f * beat;
		SDL_Color c = lighten(kind_colour(w->block[j].kind, t), 0.3f);
		c.a = (Uint8)(120 + 100 * beat);
		bag_box(w, BAG_GLOW, box(b->lo[0] - grow, 0.01f, b->lo[2] - grow, b->hi[0] + grow, 0.05f, b->hi[2] + grow), c);
	}
	if (i < 0) {
		return;
	}
	u->glow = clampf(u->glow + w->cam_dt * 6, 0, 1);
	const Box *b = &w->block[i].b;
	float e = 0.04f, s = 0.045f; /* off the box, and how thick */
	float lo[3] = {b->lo[0] - e, b->lo[1] + 0.02f, b->lo[2] - e}, hi[3] = {b->hi[0] + e, b->hi[1] + e, b->hi[2] + e};
	SDL_Color c = lighten(rgb(t->accent), 0.4f);
	c.a = (Uint8)(255 * u->glow * (0.8f + 0.2f * sinf(w->time * 5)));
	/* the twelve edges: four along each axis */
	for (int k = 0; k < 3; k++) {
		int a1 = (k + 1) % 3, a2 = (k + 2) % 3;
		for (int j = 0; j < 4; j++) {
			float p[3], size3[3] = {s, s, s};
			p[k] = (lo[k] + hi[k]) / 2;
			p[a1] = j & 1 ? hi[a1] : lo[a1];
			p[a2] = j & 2 ? hi[a2] : lo[a2];
			size3[k] = hi[k] - lo[k] + s;
			bag_put(&w->bag[BAG_GLOW], p[0], p[1], p[2], size3[0], size3[1], size3[2], c);
		}
	}
}

/* What E would do, under the middle of the view; what is playing, bottom right. */
static void use_hud(Walk *w, const Theme *t, float x, float y, float vw, float vh) {
	const struct WalkUse *u = w->use;
	if (!u) {
		return;
	}
	float th = (float)d2_text_height();
	SDL_Color panel = rgb(t->bg_alt), fg = rgb(t->text), accent = rgb(t->accent);
	panel.a = 225;
	char buf[400];
	if (u->playing[0]) {
		const char *name = strrchr(u->playing, '/');
		snprintf(buf, sizeof buf, "PLAYING  %.200s", name ? name + 1 : u->playing);
		float bw = (float)d2_text_width(buf) + 24;
		bw = bw > vw - 20 ? vw - 20 : bw;
		float bx = x + vw - bw - 10, by = y + vh - th * 2 - 30;
		d2_rect(bx, by, bw, th + 12, panel);
		d2_frame(bx, by, bw, th + 12, 1, accent);
		d2_text_fit(bx + 12, by + 6, bw - 24, buf, accent);
	}
	if (w->plaque_at >= 0 && w->plaque_at < w->nplaque) {
		/* the lift, under the middle of the view */
		snprintf(buf, sizeof buf, "lift to  %.120s", w->plaque[w->plaque_at].to->name);
		float kw = (float)d2_text_width("E") + 16, h = th + 14, bw = 8 + kw + 12 + (float)d2_text_width(buf) + 14;
		float bx = x + (vw - bw) / 2, by = y + vh - th * 3 - 20 - h - 14;
		d2_rect(bx, by, bw, h, panel);
		d2_frame(bx, by, bw, h, 2, accent);
		d2_rect(bx + 8, by + 4, kw, h - 8, accent);
		d2_text(bx + 16, by + 7, "E", rgb(t->bg));
		d2_text_fit(bx + 8 + kw + 12, by + 7, bw - kw - 34, buf, fg);
		return;
	}
	const Block *k = use_target(w);
	if (!k) {
		return;
	}
	snprintf(buf, sizeof buf, "%s  %.120s", use_verb(w, k), k->name);
	float kw = (float)d2_text_width("E") + 16, tw = (float)d2_text_width(buf), h = th + 14;
	float bw = 8 + kw + 12 + tw + 14;
	bw = bw > vw - 20 ? vw - 20 : bw;
	float bx = x + (vw - bw) / 2, by = y + vh - th * 3 - 20 - h - 14; /* just over the BY panel */
	Uint8 alpha = (Uint8)(255 * u->glow);
	panel.a = (Uint8)(225 * u->glow);
	SDL_Color edge = kind_colour(k->kind, t), cap = accent, ink = rgb(t->bg);
	edge.a = cap.a = ink.a = fg.a = alpha;
	d2_rect(bx, by, bw, h, panel);
	d2_frame(bx, by, bw, h, 2, edge);
	d2_rect(bx + 8, by + 4, kw, h - 8, cap); /* the key */
	d2_text(bx + 8 + 8, by + 7, "E", ink);
	d2_text_fit(bx + 8 + kw + 12, by + 7, bw - kw - 34, buf, fg);
}

/* "<head><path>" into out, the path's front given up for ".." till it is no
 * wider than maxw, so the folder you are in still shows */
static void fit_path(char *out, size_t n, const char *head, const char *path, float maxw) {
	size_t len = strlen(path), lo = len > 400 ? len - 400 : 0, hi = len;
	snprintf(out, n, "%s%.400s", head, path);
	if (!lo && (float)d2_text_width(out) <= maxw) {
		return;
	}
	/* the fewest characters to drop, found by halving */
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		while (mid < hi && (path[mid] & 0xC0) == 0x80) {
			mid++; /* not halfway into a character */
		}
		snprintf(out, n, "%s..%.400s", head, path + mid);
		if ((float)d2_text_width(out) <= maxw) {
			hi = mid;
		} else {
			lo = mid + 1;
		}
	}
	while (path[lo] && (path[lo] & 0xC0) == 0x80) {
		lo++;
	}
	snprintf(out, n, "%s..%.400s", head, path + lo);
}

/* ---- the radar: what is round you, from above, turned so ahead is up ----
 *
 * Top right of the view. Out in the city, some 140 m round: the
 * buildings (the crew's offices in their colours), the wall, the car;
 * inside, 30 m round: the rooms on your floor, the files in them, the
 * crew member there. You in the middle, pointing up; north on its rim. */

typedef struct {
	float cx, cy, r, scale, px, pz, fx, fz, rx, rz;
} Radar;

static void radar_at(const Radar *R, float wx, float wz, float *sx, float *sy) {
	float dx = wx - R->px, dz = wz - R->pz;
	*sx = R->cx + (dx * R->rx + dz * R->rz) * R->scale;
	*sy = R->cy - (dx * R->fx + dz * R->fz) * R->scale;
}

/* a line on the radar, cut to its round */
static void radar_line(const Radar *R, float wx0, float wz0, float wx1, float wz1, float thick, SDL_Color c) {
	float ax, ay, bx, by;
	radar_at(R, wx0, wz0, &ax, &ay);
	radar_at(R, wx1, wz1, &bx, &by);
	float dx = bx - ax, dy = by - ay, fx = ax - R->cx, fy = ay - R->cy;
	float a = dx * dx + dy * dy, b = 2 * (fx * dx + fy * dy), cc = fx * fx + fy * fy - R->r * R->r;
	float t0 = 0, t1 = 1;
	if (a < 1e-6f) {
		return;
	}
	float disc = b * b - 4 * a * cc;
	if (disc <= 0) {
		return; /* misses the round altogether */
	}
	float sq = sqrtf(disc), u0 = (-b - sq) / (2 * a), u1 = (-b + sq) / (2 * a);
	t0 = fmaxf(t0, u0);
	t1 = fminf(t1, u1);
	if (t0 >= t1) {
		return;
	}
	d2_line(ax + dx * t0, ay + dy * t0, ax + dx * t1, ay + dy * t1, thick, c);
}

static void radar_rect(const Radar *R, float x0, float z0, float x1, float z1, float thick, SDL_Color c) {
	radar_line(R, x0, z0, x1, z0, thick, c);
	radar_line(R, x1, z0, x1, z1, thick, c);
	radar_line(R, x1, z1, x0, z1, thick, c);
	radar_line(R, x0, z1, x0, z0, thick, c);
}

static void radar_draw(Walk *w, const Theme *t, float x, float y, float vw) {
	bool driving = car_in(w);
	Radar R;
	R.r = 88;
	R.cx = x + vw - R.r - 14;
	R.cy = y + 62 + R.r;
	float range = w->bld >= 0 ? 30 : driving ? 220 : 140;
	R.scale = R.r / range;
	float yaw = driving && w->car_chase ? w->car_yaw : w->yaw;
	R.px = w->px;
	R.pz = w->pz;
	R.fx = -sinf(yaw);
	R.fz = -cosf(yaw);
	R.rx = cosf(yaw);
	R.rz = -sinf(yaw);
	SDL_Color bg = rgb(t->bg), acc = rgb(t->accent), fg = rgb(t->text), dim = mix(fg, bg, 0.55f);
	bg.a = 210;
	d2_disc(R.cx, R.cy, R.r, bg, bg);
	d2_ring(R.cx, R.cy, R.r * 0.5f, 1, mix(acc, bg, 0.7f));
	const World *wd = &w->world;
	if (w->bld < 0) {
		for (int i = 0; i < wd->nb; i++) {
			const WBuilding *b = &wd->b[i];
			float mx = (b->r.x0 + b->r.x1) / 2 - R.px, mz = (b->r.z0 + b->r.z1) / 2 - R.pz;
			if (sqrtf(mx * mx + mz * mz) > range + 60) {
				continue;
			}
			const Crew *c = crew_of(wd, i);
			radar_rect(&R, b->r.x0, b->r.z0, b->r.x1, b->r.z1, c ? 2.0f : 1.0f, c ? lighten(c->hood, 0.55f) : dim);
		}
		float E = wd->edge;
		radar_rect(&R, -E, -E, E, E, 2, mix(acc, bg, 0.3f));
		radar_rect(&R, wd->square.x0, wd->square.z0, wd->square.x1, wd->square.z1, 1, mix(acc, bg, 0.6f));
		if (w->car_ok && !driving) { /* the car, its nose a line */
			float c0x, c0z, c1x, c1z, c2x, c2z, c3x, c3z, nx, nz;
			car_local(w, -CAR_HALF_W, -CAR_HALF_L, &c0x, &c0z);
			car_local(w, CAR_HALF_W, -CAR_HALF_L, &c1x, &c1z);
			car_local(w, CAR_HALF_W, CAR_HALF_L, &c2x, &c2z);
			car_local(w, -CAR_HALF_W, CAR_HALF_L, &c3x, &c3z);
			car_local(w, 0, -CAR_HALF_L - 1.5f, &nx, &nz);
			radar_line(&R, c0x, c0z, c1x, c1z, 2, acc);
			radar_line(&R, c1x, c1z, c2x, c2z, 2, acc);
			radar_line(&R, c2x, c2z, c3x, c3z, 2, acc);
			radar_line(&R, c3x, c3z, c0x, c0z, 2, acc);
			radar_line(&R, (c0x + c1x) / 2, (c0z + c1z) / 2, nx, nz, 1, acc);
		}
	} else {
		for (int k = 0; k < w->nfroom; k++) {
			WRect q = wd->room[w->froom[k]].r;
			radar_rect(&R, q.x0, q.z0, q.x1, q.z1, w->froom[k] == w->at ? 2.0f : 1.0f, w->froom[k] == w->at ? fg : dim);
		}
		for (int i = 0; i < w->nblock; i++) {
			const Box *b = &w->block[i].b;
			float sx, sy;
			radar_at(&R, (b->lo[0] + b->hi[0]) / 2, (b->lo[2] + b->hi[2]) / 2, &sx, &sy);
			if ((sx - R.cx) * (sx - R.cx) + (sy - R.cy) * (sy - R.cy) < R.r * R.r) {
				d2_rect(sx - 1.5f, sy - 1.5f, 3, 3, kind_colour(w->block[i].kind, t));
			}
		}
	}
	for (int f = 0; f < w->nfig; f++) { /* the crew, in their colours */
		const Crew *k = &crew.c[w->fig[f].crew];
		float sx, sy;
		radar_at(&R, w->fig[f].x, w->fig[f].z, &sx, &sy);
		if ((sx - R.cx) * (sx - R.cx) + (sy - R.cy) * (sy - R.cy) < R.r * R.r) {
			d2_disc(sx, sy, 3.5f, lighten(k->hood, 0.6f), lighten(k->hood, 0.3f));
		}
	}
	/* you, pointing ahead; driving, the car */
	if (driving) {
		d2_frame(R.cx - 4, R.cy - 9, 8, 18, 2, acc);
	} else {
		float a = w->body_yaw - yaw; /* which way you face, against which way you look */
		float s = 7, tx = R.cx - sinf(a) * s * 1.4f, ty = R.cy - cosf(a) * s * 1.4f;
		float lx = R.cx + cosf(a) * s * 0.8f - sinf(a) * -s, ly = R.cy - sinf(a) * s * 0.8f - cosf(a) * -s;
		float rx = R.cx - cosf(a) * s * 0.8f - sinf(a) * -s, ry = R.cy + sinf(a) * s * 0.8f - cosf(a) * -s;
		d2_line(tx, ty, lx, ly, 2, acc);
		d2_line(lx, ly, rx, ry, 2, acc);
		d2_line(rx, ry, tx, ty, 2, acc);
	}
	d2_ring(R.cx, R.cy, R.r, 2, acc);
	float nx = R.cx + sinf(yaw) * (R.r - 9), ny = R.cy - cosf(yaw) * (R.r - 9); /* north: -z */
	d2_text(nx - (float)d2_text_width("N") / 2, ny - (float)d2_text_height() / 2, "N", fg);
	char lab[32];
	snprintf(lab, sizeof lab, "%.0f m", range);
	d2_text(R.cx - (float)d2_text_width(lab) / 2, R.cy + R.r + 3, lab, dim);
}

void walk_draw(Walk *w, float x, float y, float vw, float vh, const Theme *t, SDL_Color back, float mx, float my) {
	w->vx = x;
	w->vy0 = y;
	w->vw = vw;
	w->vh = vh;
	char buf[600], size[32];
	float th = (float)d2_text_height();

	/* the sky */
	SDL_Color top = back, bottom = rgb(t->bg_alt);
	bottom.a = back.a;
	d2_rect_shade(x, y, vw, vh, top, bottom);

	/* the floor you are on is in its own frame: the city is seen through
	 * the camera raised by the floor's height */
	Vec3 eye, at;
	camera(w, &eye, &at);
	Vec3 eyew = v3(eye.x, eye.y + w->base, eye.z), atw = v3(at.x, at.y + w->base, at.z);
	float proj[16], look[16], mvpw[16], mvp[16], lift[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, w->base, 0, 1};
	m4_perspective(proj, FOV_WALK, vw / vh, 0.05f, fmaxf(500, beyond_reach(w) * 1.6f));
	m4_look_at(look, eyew, atw, v3(0, 1, 0));
	m4_mul(mvpw, proj, look);
	m4_mul(mvp, mvpw, lift);

	float mvx = eyew.x - w->lod_x, mvz = eyew.z - w->lod_z;
	if (w->city_dirty || (!w->beyond_had && SDL_AtomicGet(&beyond.state) == 2) || mvx * mvx + mvz * mvz > LOD_MOVE * LOD_MOVE || memcmp(&w->city_theme, t, sizeof *t) ||
	    memcmp(&w->city_back, &back, sizeof back)) {
		city_make(w, t, back, eyew.x, eyew.z);
	}
	if (w->floor_dirty || memcmp(&w->floor_theme, t, sizeof *t)) {
		interior_make(w, t);
	}
	pictures(w, eye);
	for (int i = 0; i < BAG_COUNT; i++) {
		w->bag[i].n = 0;
	}
	w->npic = 0;
	lay_rugs(w);
	/* the files: in full near you and in sight, a plain box of their kind's colour further off */
	for (int j = 0; j < w->nblock; j++) {
		Block *k = &w->block[j];
		float nx = clampf(eye.x, k->b.lo[0], k->b.hi[0]) - eye.x, nz = clampf(eye.z, k->b.lo[2], k->b.hi[2]) - eye.z;
		if (nx * nx + nz * nz < 24 * 24) {
			if (!in_view(mvp, &k->b)) {
				continue;
			}
			furnish(w, k, t);
		} else {
			bag_box(w, BAG_BODY, k->b, mix(kind_colour(k->kind, t), rgb(t->panel), 0.5f));
		}
	}
	float under = floor_under(w, w->px, w->py, w->pz);
	if (!w->first && w->cam_len > 0.8f) {
		hologram(w, t, under); /* not with the camera pulled right in to your head */
	}
	car_fill(w, t);
	crew_figs(w);
	Crew *cm = NULL; /* the office's own figure is gone: the crew follow you, rest or work */
	use_mark(w, t, eye, at);

	float eyef[3] = {eyew.x, eyew.y, eyew.z}, eyel[3] = {eye.x, eye.y, eye.z};
	d3_glow(true);
	d3_begin(x, y, vw, vh, mvpw);
	d3_rim(0.35f);
	/* what is lit: the city (all but the building you are in), then the floor you are on and its things */
	d3_eye(eyef, w->time);
	city_draw(w, CB_GROUND, 0, 0);
	city_draw(w, CB_BODY, 0, 0.1f);
	city_draw(w, CB_LIT, 0, 0.02f);
	city_draw(w, CB_BALL, 1, 0);
	d3_mvp(mvp);
	d3_eye(eyel, w->time);
	d3_boxes_edged(w->ibag[IB_WALL].v, w->ibag[IB_WALL].n, 0.04f);
	d3_boxes_edged(w->ibag[IB_LIT].v, w->ibag[IB_LIT].n, 0.015f);
	d3_boxes_edged(w->bag[BAG_WALL].v, w->bag[BAG_WALL].n, 0.05f);
	d3_boxes_edged(w->bag[BAG_BODY].v, w->bag[BAG_BODY].n, 0.06f);
	d3_boxes_edged(w->bag[BAG_LIT].v, w->bag[BAG_LIT].n, 0.015f);
	d3_spheres(w->bag[BAG_BALL].v, w->bag[BAG_BALL].n);
	for (int i = 0; i < w->npic; i++) {
		if (!w->pic[i].lamp) {
			d3_quads(w->pic[i].tex, &w->pic[i].q, 1);
		}
	}
	d3_spheres_turned(w->bag[BAG_FIG_DARK].v, w->bag[BAG_FIG_DARK].n, w->body_yaw, w->px, w->pz);
	d3_boxes_turned(w->bag[BAG_FIG_BOX].v, w->bag[BAG_FIG_BOX].n, 0, w->body_yaw, w->px, w->pz);
	car_draw(w, 0);
	for (int f = 0; f < w->nfig; f++) {
		d3_spheres_turned(w->fball[f].v, w->fball[f].n, w->fig[f].yaw, w->fig[f].x, w->fig[f].z);
		d3_boxes_turned(w->fbox[f].v, w->fbox[f].n, 0, w->fig[f].yaw, w->fig[f].x, w->fig[f].z);
	}
	/* what shines by itself: windows, lamps, screens */
	d3_look(LOOK_LAMP);
	d3_mvp(mvpw);
	city_draw(w, CB_LAMP, 2, 0);
	d3_mvp(mvp);
	d3_boxes(w->ibag[IB_LAMP].v, w->ibag[IB_LAMP].n);
	d3_boxes(w->bag[BAG_LAMP].v, w->bag[BAG_LAMP].n);
	d3_spheres(w->bag[BAG_BALL_LAMP].v, w->bag[BAG_BALL_LAMP].n);
	for (int i = 0; i < w->npic; i++) {
		if (w->pic[i].lamp) {
			d3_quads(w->pic[i].tex, &w->pic[i].q, 1);
		}
	}
	d3_spheres_turned(w->bag[BAG_FIG_LAMP].v, w->bag[BAG_FIG_LAMP].n, w->body_yaw, w->px, w->pz);
	for (int f = 0; f < w->nfig; f++) {
		d3_spheres_turned(w->flamp[f].v, w->flamp[f].n, w->fig[f].yaw, w->fig[f].x, w->fig[f].z);
	}
	car_draw(w, 1);
	/* and the holograms, last: they hide nothing. You first laid down as a
	 * shell, so only your outside glows and not every part through the others */
	d3_look(LOOK_LIT);
	d3_depth_only(true);
	d3_spheres_turned(w->bag[BAG_FIG_GLOW].v, w->bag[BAG_FIG_GLOW].n, w->body_yaw, w->px, w->pz);
	d3_depth_only(false);
	d3_look(LOOK_GLOW);
	d3_quads(0, w->iglass, w->niglass); /* the windows' glass: a faint tint over the city */
	car_draw(w, 2);
	d3_boxes(w->bag[BAG_GLOW].v, w->bag[BAG_GLOW].n);
	d3_spheres(w->bag[BAG_BALL_GLOW].v, w->bag[BAG_BALL_GLOW].n);
	d3_spheres_turned(w->bag[BAG_FIG_GLOW].v, w->bag[BAG_FIG_GLOW].n, w->body_yaw, w->px, w->pz);
	d3_end();
	d3_rim(0);
	d3_glow(false);

	/* names in the world */
	d2_clip(x, y, vw, vh);
	ntags = 0;
	SDL_Color label_bg = rgb(t->bg_alt), fg = rgb(t->text);
	label_bg.a = 215;
	float sx, sy;
	if (w->bld >= 0) {
		/* this room's: its name, its doorways, its stairs; the way out near it */
		signs_draw(w, w->sign, w->nsign, w->at, mvp, eye, 30, NULL, 0, x, y, vw, vh, t, label_bg);
	}
	/* the files near you, nearest first */
	Near order[MAX_FILES];
	int no = 0;
	for (int i = 0; i < w->nblock && no < MAX_FILES; i++) {
		const Box *b = &w->block[i].b;
		Vec3 to = v3_sub(v3((b->lo[0] + b->hi[0]) / 2, b->hi[1], (b->lo[2] + b->hi[2]) / 2), eye);
		float d = sqrtf(v3_dot(to, to));
		if (d < 12) {
			order[no++] = (Near){d, i};
		}
	}
	qsort(order, (size_t)no, sizeof *order, nearer);
	for (int k = 0; k < no; k++) {
		const Box *b = &w->block[order[k].i].b;
		Vec3 p = v3((b->lo[0] + b->hi[0]) / 2, b->hi[1] + 0.25f, (b->lo[2] + b->hi[2]) / 2);
		if (v3_dot(v3_sub(p, eye), look_dir(w)) <= 0 || !m4_project(mvp, p, vw, vh, &sx, &sy)) {
			continue;
		}
		snprintf(buf, sizeof buf, "%.24s", w->block[order[k].i].name);
		tag(x + sx, y + sy - th, buf, fg, label_bg, rgb(t->border));
	}
	/* a store room's board: its plaques' names */
	for (int i = 0; i < w->nplaque; i++) {
		const Box *b = &w->plaque[i].b;
		Vec3 p = v3((b->lo[0] + b->hi[0]) / 2, (b->lo[1] + b->hi[1]) / 2, (b->lo[2] + b->hi[2]) / 2), to = v3_sub(p, eye);
		if (v3_dot(to, to) < 10 * 10 && m4_project(mvp, p, vw, vh, &sx, &sy)) {
			bool hot = i == w->plaque_at;
			tag(x + sx, y + sy - th / 2, w->plaque[i].text, hot ? rgb(t->bg) : fg, hot ? rgb(t->accent) : label_bg, rgb(t->accent));
		}
	}
	Uint32 now_t = SDL_GetTicks();
	bool shown_said[CREW_MAX] = {false};
	(void)cm;
	for (int f = 0; f < w->nfig; f++) {
		/* each figure's name and what it is at; what that one says on the radio over the nearest of them */
		Crew *k = &crew.c[w->fig[f].crew];
		float sx, sy, top = w->fig[f].y + (w->fig[f].kind == FIG_FOLLOW ? 2.15f : 1.75f);
		Vec3 head = v3(w->fig[f].x, top, w->fig[f].z), to = v3_sub(head, eye);
		if (v3_dot(to, look_dir(w)) <= 0 || v3_dot(to, to) > 40 * 40 || !m4_project(mvp, head, vw, vh, &sx, &sy)) {
			continue;
		}
		SDL_LockMutex(crew.mu);
		bool fresh = k->last[0] && now_t - k->last_at < 60000;
		char doing[96], said[600];
		snprintf(doing, sizeof doing, "%s", k->presence[0] ? k->presence : k->duty[0] ? k->duty : k->busy ? "a turn" : k->last);
		snprintf(said, sizeof said, "%s", k->said);
		int word = k->said[0] ? said_word(k, now_t) : -2;
		SDL_UnlockMutex(crew.mu);
		if (w->fig[f].kind == FIG_REST) {
			snprintf(buf, sizeof buf, "%s  |  switched off, resting", k->name);
		} else if (w->fig[f].kind == FIG_WORK) {
			snprintf(buf, sizeof buf, "%s's worker  |  %s", k->name, doing);
		} else {
			snprintf(buf, sizeof buf, "%s%s%s", k->name, (k->busy || k->presence[0]) ? "  |  in a turn" : fresh ? "  |  " : "",
			         (k->busy || k->presence[0]) ? "" : fresh ? doing : "");
		}
		tag(x + sx, y + sy - th, buf, rgb(t->bg), w->fig[f].kind == FIG_WORK ? rgb(t->accent) : lighten(k->hood, 0.5f), rgb(t->accent));
		if (word != -2 && !shown_said[w->fig[f].crew] && w->fig[f].kind != FIG_WORK) {
			shown_said[w->fig[f].crew] = true;
			words_draw(x + sx, y + sy - th - 6, fminf(vw * 0.45f, 520), "", said, word, true, t, rgb(t->accent));
		}
	}
	radar_draw(w, t, x, y, vw);
	/* the car: E at its door; from the seat, how fast */
	if (car_by_door(w)) {
		float dx, dz, sx, sy;
		car_local(w, CAR_HALF_W + 0.1f, 0, &dx, &dz);
		if (m4_project(mvpw, v3(dx, 1.5f, dz), vw, vh, &sx, &sy)) {
			tag(x + sx, y + sy, "E  get in", rgb(t->bg), rgb(t->accent), rgb(t->accent));
		}
	}
	if (car_in(w) && w->car_state == CAR_DRIVING) {
		snprintf(buf, sizeof buf, "%3.0f km/h   %s   |   W S  A D   Shift more   Space handbrake   V view   E out%s", fabsf(w->car_v) * 3.6f,
		         w->car_v < -0.2f ? "R" : "D", fabsf(w->car_v) >= 2 ? " (slow down)" : "");
		tag(x + vw / 2, y + vh - th * 1.9f, buf, rgb(t->text), mix(rgb(t->bg), rgb(t->panel), 0.5f), rgb(t->accent));
	}
	/* the radio along the foot of the view: what the crew say, wherever you are, and your own lines */
	{
		float ry = y + vh - th * 3.2f;
		SDL_LockMutex(crew.mu);
		for (int i = 0; i < crew.n; i++) {
			Crew *c = &crew.c[i];
			if (!c->said[0] || shown_said[i]) {
				continue; /* said over their head already */
			}
			int word = said_word(c, now_t);
			if (word == -2) {
				continue;
			}
			char who[80];
			snprintf(who, sizeof who, "%s:", c->name);
			words_draw(x + vw / 2, ry, vw * 0.7f, who, c->said, word, true, t, lighten(c->hood, 0.6f));
			ry -= th * 2.4f;
		}
		if (you_said.said[0] && now_t - you_said.at < 10000) {
			words_draw(x + vw / 2, ry, vw * 0.7f, "You:", you_said.said, 9999, true, t, rgb(t->text));
		}
		SDL_UnlockMutex(crew.mu);
	}
	if (w->bld < 0) {
		/* out in the street: the buildings' names over their doors, the streets' at the corners */
		signs_draw(w, w->csign, w->ncsign, -2, mvpw, eyew, 60, w->cnear, w->ncnear, x, y, vw, vh, t, label_bg);
		signs_draw(w, w->fsign, w->nfsign, -2, mvpw, eyew, 1e5f, NULL, 0, x, y, vw, vh, t, label_bg);
	}

	/* where you are, top left */
	SDL_Color panel = rgb(t->bg_alt);
	panel.a = 225;
	format_size(w->room->size, size, sizeof size);
	char line2[300], line3[200] = "";
	const char *head = w->bld < 0 ? "OUTSIDE IN  " : "WALKING IN  ";
	snprintf(buf, sizeof buf, "%s%.200s", head, w->path);
	if (w->bld < 0) {
		street_where(w, line2, sizeof line2);
	} else {
		const WBuilding *b = &w->world.b[w->bld];
		snprintf(line2, sizeof line2, "%s  |  %llu files  |  floor %d of %d, %.40s  |  %d room%s on this floor", size,
		         (unsigned long long)w->room->files, w->floor + 1, b->floors, b->name, w->nfroom, w->nfroom == 1 ? "" : "s");
	}
	if (w->more_files) {
		snprintf(line3, sizeof line3, "not shown here: %d smaller files", w->more_files);
	}
	if (w->world.more) {
		size_t used = strlen(line3);
		snprintf(line3 + used, sizeof line3 - used, "%s%d small folders with no building", used ? "  |  " : "not shown: ", w->world.more);
	}
	float pw = (float)d2_text_width(buf);
	pw = (float)d2_text_width(line2) > pw ? (float)d2_text_width(line2) : pw;
	pw = (float)d2_text_width(line3) > pw ? (float)d2_text_width(line3) : pw;
	/* it stops short of the buttons; where that leaves too little, it goes
	 * under them and takes the whole width */
	SDL_Rect vbr, mbr;
	buttons(w, &vbr, &mbr);
	float px = x + 10, py = y + 10, room = (float)vbr.x - 10 - px - 20;
	if (room < (float)d2_text_width(head) + 160) {
		py = (float)(vbr.y + vbr.h) + 8;
		room = vw - 20 - 20;
	}
	if (room < 0) {
		room = 0;
	}
	if (pw > room) {
		pw = room;
		fit_path(buf, sizeof buf, head, w->path, pw);
	}
	float ph = th * (line3[0] ? 3 : 2) + 14;
	d2_rect(px, py, pw + 20, ph, panel);
	d2_frame(px, py, pw + 20, ph, 1, rgb(t->accent));
	d2_text_fit(px + 10, py + 6, pw, buf, rgb(t->accent));
	d2_text_fit(px + 10, py + 6 + th, pw, line2, fg);
	if (line3[0]) {
		d2_text_fit(px + 10, py + 6 + th * 2, pw, line3, rgb(t->border));
	}

	/* the file you are by or on, bottom middle */
	bool on_it;
	const Block *b = block_near(w, &on_it);
	if (b) {
		format_size(b->size, size, sizeof size);
		float share = w->room->size ? (float)b->size / (float)w->room->size * 100 : 0;
		if (b->dev == WALK_DEV_DISK) {
			snprintf(buf, sizeof buf, "%s %s  |  %s  |  disk%s", on_it ? "STANDING ON" : "BY", b->name, size,
			         b->activity > 0.05f ? "  |  busy" : "");
		} else if (b->dev) {
			snprintf(buf, sizeof buf, "%s %s  |  %s", on_it ? "STANDING ON" : "BY", b->name, dev_name(b->dev));
		} else {
			snprintf(buf, sizeof buf, "%s %s  |  %s  |  %.1f%% of %s  |  %s", on_it ? "STANDING ON" : "BY", b->name, size, share,
			         w->bld < 0 ? "the square" : "this room", kind_name(b->kind));
		}
		/* never wider than the view; the text cut short to fit */
		float bw = (float)d2_text_width(buf) + 30, by = y + vh - th * 3 - 20;
		if (bw > vw - 20) {
			bw = vw - 20;
		}
		float bx = x + (vw - bw) / 2;
		d2_rect(bx, by, bw, th + 14, panel);
		d2_frame(bx, by, bw, th + 14, 2, kind_colour(b->kind, t));
		d2_text_fit(bx + 15, by + 7, bw - 30, buf, fg);
	}

	/* the keys, bottom left */
	const char *keys = w->captured
	                       ? "mouse look  |  WASD walk  |  arrows turn  |  Shift run  |  Space jump (hold: higher)  |  E use  |  doors: walk through  |  stairs: up into a folder  |  V first / third person  |  Esc frees the mouse"
	                       : "click to look with the mouse  |  WASD walk  |  left / right turn  |  Shift run  |  Space jump  |  E use  |  doors: walk through  |  stairs: up into a folder  |  V first / third person  |  Esc back to the map";
	d2_rect(x, y + vh - th - 12, vw, th + 12, panel);
	d2_text_fit(x + 12, y + vh - th - 6, vw - 24, keys, rgb(t->border));
	d2_unclip();

	if (w->captured && w->first) {
		/* the middle of the view */
		float cx = x + vw / 2, cy = y + vh / 2;
		SDL_Color c = fg;
		c.a = 200;
		d2_rect(cx - 8, cy - 1, 6, 2, c);
		d2_rect(cx + 2, cy - 1, 6, 2, c);
		d2_rect(cx - 1, cy - 8, 2, 6, c);
		d2_rect(cx - 1, cy + 2, 2, 6, c);
	}

	/* the two buttons, top right */
	const char *lab[2] = {w->first ? "3RD PERSON (V)" : "1ST PERSON (V)", "BACK TO MAP (Esc)"};
	SDL_Rect r[2] = {vbr, mbr};
	for (int i = 0; i < 2; i++) {
		bool hover = !w->captured && in(r[i], mx, my);
		d2_rect((float)r[i].x, (float)r[i].y, (float)r[i].w, (float)r[i].h, hover ? rgb(t->panel_hover) : rgb(t->panel));
		d2_frame((float)r[i].x, (float)r[i].y, (float)r[i].w, (float)r[i].h, 1, hover ? rgb(t->accent) : rgb(t->border));
		d2_text((float)r[i].x + 12, (float)r[i].y + 7, lab[i], hover ? rgb(t->accent) : fg);
	}

	use_hud(w, t, x, y, vw, vh);

	/* fading in, after a lift */
	if (w->fade > 0) {
		SDL_Color f = rgb(t->bg);
		f.a = (Uint8)clampf(w->fade / 0.35f * 255, 0, 255);
		d2_rect(x, y, vw, vh, f);
	}
}
