/* ------------------------------------------------------------------------
 *   Everything drawn, on OpenGL 3.3: the flat things (rectangles, lines,
 *   discs, text) in window pixels from the top left, and the solid ones
 *   (boxes, spheres, lines) through a camera into a rectangle of the
 *   window.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_DRAW_H
#define SYN_SHELL_SPACE_DRAW_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Call with the OpenGL context current. font_px is the text's height. */
bool draw_init(int font_px);

/* A frame w x h pixels, cleared to a colour (its alpha below 255 for a
 * see-through window); end sends what was drawn. */
void draw_begin(int w, int h, SDL_Color clear);
void draw_end(void);

/* ---- flat ---- */
void d2_rect(float x, float y, float w, float h, SDL_Color c);
/* shaded from one colour at the top to another at the bottom */
void d2_rect_shade(float x, float y, float w, float h, SDL_Color top, SDL_Color bottom);
void d2_frame(float x, float y, float w, float h, float thick, SDL_Color c);
void d2_line(float x0, float y0, float x1, float y1, float thick, SDL_Color c);
/* a disc shaded from its centre's colour to its rim's */
void d2_disc(float cx, float cy, float r, SDL_Color centre, SDL_Color rim);
void d2_ring(float cx, float cy, float r, float thick, SDL_Color c);
int d2_text_height(void);
int d2_text_width(const char *s);
void d2_text(float x, float y, const char *s, SDL_Color c);
/* the text cut short with ".." where it is wider than maxw */
void d2_text_fit(float x, float y, float maxw, const char *s, SDL_Color c);
/* An icon: a w x h alpha mask made into a texture once (the handle back),
 * then drawn size x size, tinted c, as often as wanted. */
unsigned d2_icon_make(const unsigned char *alpha, int w, int h);
void d2_icon(unsigned icon, float x, float y, float size, SDL_Color c);
/* what is drawn until d2_unclip is cut to the rectangle */
void d2_clip(float x, float y, float w, float h);
void d2_unclip(void);

/* ---- solid ---- */
typedef struct {
	float pos[3];  /* the centre */
	float size[3]; /* the whole width, height and depth */
	unsigned char rgba[4];
} Solid;

typedef struct {
	float pos[3];
	unsigned char rgba[4];
} LinePoint;

/* Start drawing solids into the rectangle (window pixels, top left),
 * seen through mvp; the flat drawing so far is sent first, and comes
 * back after d3_end. */
void d3_begin(float x, float y, float w, float h, const float mvp[16]);
void d3_boxes(const Solid *v, int n);
void d3_spheres(const Solid *v, int n);
void d3_lines(const LinePoint *v, int n); /* two points a line */
/* boxes with their edges darkened this wide (in the boxes' own units) */
void d3_boxes_edged(const Solid *v, int n, float edge);
/* the same, the lot turned by yaw radians about the upright through (px, pz) */
void d3_boxes_turned(const Solid *v, int n, float edge, float yaw, float px, float pz);
void d3_end(void);
/* the next view (d3_begin .. d3_end) with its bright parts glowing, as SYN-TREK draws */
void d3_glow(bool on);
/* light round lit things' edges turned from the eye (d3_eye), so strong; 0 none */
void d3_rim(float k);
/* Seen through another matrix from here on, till d3_end (the depth kept):
 * things made in a frame of their own, a floor of a building at its level. */
void d3_mvp(const float mvp[16]);

/* How the solids and pictures drawn next look, until d3_end: LOOK_LIT as
 * above; LOOK_GLOW a hologram, see-through and brightest at its rim,
 * crossed by climbing scanlines (draw it last: it hides nothing); LOOK_LAMP
 * its own colour at full strength, a screen or a light. */
enum { LOOK_LIT, LOOK_GLOW, LOOK_LAMP };
void d3_look(int look);
/* Solids drawn while on go into the depth only, nothing seen: a hologram
 * made of overlapping parts laid down so first glows as one shell. */
void d3_depth_only(bool on);
/* where the eye is and the time in seconds, for the hologram */
void d3_eye(const float eye[3], float time);
void d3_spheres_turned(const Solid *v, int n, float yaw, float px, float pz);

/* A picture on a face: four corners, bottom left, bottom right, top
 * right, top left as seen from the front; the texture's top row at the
 * top. Texture 0 is plain white, for a flat colour. */
typedef struct {
	float p[4][3];
	unsigned char rgba[4];
} Quad3;
unsigned d3_texture(const unsigned char *rgba, int w, int h); /* RGBA, top row first */
void d3_texture_free(unsigned tex);
void d3_quads(unsigned tex, const Quad3 *q, int n);

/* The finished frame as a binary PPM; call after draw_end. */
bool draw_shot(const char *path);

#endif
