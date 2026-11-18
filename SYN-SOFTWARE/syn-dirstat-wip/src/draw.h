/* ------------------------------------------------------------------------
 *   Everything drawn, on OpenGL 3.3: the flat things (rectangles, lines,
 *   discs, text) in window pixels from the top left, and the solid ones
 *   (boxes, spheres, lines) through a camera into a rectangle of the
 *   window.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIRSTAT_DRAW_H
#define SYN_DIRSTAT_DRAW_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Call with the OpenGL context current. font_px is the text's height. */
bool draw_init(int font_px);

/* A frame w x h pixels, cleared to a colour; end sends what was drawn. */
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
void d3_end(void);

/* The finished frame as a binary PPM; call after draw_end. */
bool draw_shot(const char *path);

#endif
