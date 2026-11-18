/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "draw.h"

#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "gl.h"

#define PI 3.14159265f

static int g_w, g_h;

static GLuint program(const char *vs_src, const char *fs_src) {
	GLuint prog = glCreateProgram();
	const char *src[2] = {vs_src, fs_src};
	GLenum type[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader(type[i]);
		glShaderSource(s, 1, &src[i], NULL);
		glCompileShader(s);
		GLint ok = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			char log[1024];
			glGetShaderInfoLog(s, sizeof log, NULL, log);
			fprintf(stderr, "syn-dirstat: shader: %s\n", log);
			return 0;
		}
		glAttachShader(prog, s);
	}
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(prog, sizeof log, NULL, log);
		fprintf(stderr, "syn-dirstat: shader link: %s\n", log);
		return 0;
	}
	return prog;
}

/* ======================================================================
 *   Flat: one batch of triangles, sent when the texture or the clip
 *   changes and when the frame ends
 * ====================================================================== */

typedef struct {
	float x, y, u, v;
	unsigned char r, g, b, a;
} Vert;

static const char *FLAT_VS =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aRgba;\n"
	"uniform vec2 uScreen;\n"
	"out vec2 vUV;\n"
	"out vec4 vRgba;\n"
	"void main() {\n"
	"    vUV = aUV;\n"
	"    vRgba = aRgba;\n"
	"    vec2 p = aPos / uScreen * 2.0 - 1.0;\n"
	"    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
	"}\n";

static const char *FLAT_FS =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"in vec4 vRgba;\n"
	"uniform sampler2D uTex;\n"
	"out vec4 FragColor;\n"
	"void main() { FragColor = texture(uTex, vUV) * vRgba; }\n";

static GLuint flat_prog, flat_vao, flat_vbo, white_tex;
static GLint flat_screen;
static Vert *batch;
static int batch_n, batch_cap;
static GLuint batch_tex;

static void flat_state(void) {
	glViewport(0, 0, g_w, g_h);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void flush(void) {
	if (!batch_n) {
		return;
	}
	glUseProgram(flat_prog);
	glUniform2f(flat_screen, (float)g_w, (float)g_h);
	glBindVertexArray(flat_vao);
	glBindBuffer(GL_ARRAY_BUFFER, flat_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)batch_n * sizeof *batch), batch, GL_STREAM_DRAW);
	glBindTexture(GL_TEXTURE_2D, batch_tex);
	glDrawArrays(GL_TRIANGLES, 0, batch_n);
	batch_n = 0;
}

static Vert *reserve(GLuint tex, int n) {
	if (tex != batch_tex) {
		flush();
		batch_tex = tex;
	}
	if (batch_n + n > batch_cap) {
		batch_cap = (batch_n + n) * 2;
		batch = realloc(batch, (size_t)batch_cap * sizeof *batch);
		if (!batch) {
			abort();
		}
	}
	batch_n += n;
	return &batch[batch_n - n];
}

static Vert vert(float x, float y, float u, float v, SDL_Color c) {
	return (Vert){x, y, u, v, c.r, c.g, c.b, c.a};
}

/* a, b, c, d go round the shape */
static void quad(GLuint tex, Vert a, Vert b, Vert c, Vert d) {
	Vert *v = reserve(tex, 6);
	v[0] = a;
	v[1] = b;
	v[2] = c;
	v[3] = a;
	v[4] = c;
	v[5] = d;
}

void d2_rect_shade(float x, float y, float w, float h, SDL_Color top, SDL_Color bottom) {
	if (w <= 0 || h <= 0) {
		return;
	}
	quad(white_tex, vert(x, y, 0, 0, top), vert(x + w, y, 0, 0, top), vert(x + w, y + h, 0, 0, bottom),
	     vert(x, y + h, 0, 0, bottom));
}

void d2_rect(float x, float y, float w, float h, SDL_Color c) { d2_rect_shade(x, y, w, h, c, c); }

void d2_frame(float x, float y, float w, float h, float t, SDL_Color c) {
	d2_rect(x, y, w, t, c);
	d2_rect(x, y + h - t, w, t, c);
	d2_rect(x, y + t, t, h - 2 * t, c);
	d2_rect(x + w - t, y + t, t, h - 2 * t, c);
}

void d2_line(float x0, float y0, float x1, float y1, float thick, SDL_Color c) {
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f) {
		return;
	}
	float nx = -dy / len * thick / 2, ny = dx / len * thick / 2;
	quad(white_tex, vert(x0 + nx, y0 + ny, 0, 0, c), vert(x1 + nx, y1 + ny, 0, 0, c),
	     vert(x1 - nx, y1 - ny, 0, 0, c), vert(x0 - nx, y0 - ny, 0, 0, c));
}

static int disc_steps(float r) {
	int n = (int)(r * 0.9f) + 10;
	return n > 96 ? 96 : n;
}

void d2_disc(float cx, float cy, float r, SDL_Color centre, SDL_Color rim) {
	int n = disc_steps(r);
	Vert *v = reserve(white_tex, n * 3);
	for (int i = 0; i < n; i++) {
		float a0 = 2 * PI * (float)i / (float)n, a1 = 2 * PI * (float)(i + 1) / (float)n;
		/* the light sits up and to the left of the middle, as on a ball */
		v[i * 3 + 0] = vert(cx - r * 0.3f, cy - r * 0.3f, 0, 0, centre);
		v[i * 3 + 1] = vert(cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, rim);
		v[i * 3 + 2] = vert(cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, rim);
	}
}

void d2_ring(float cx, float cy, float r, float thick, SDL_Color c) {
	int n = disc_steps(r);
	float in = r - thick / 2, out = r + thick / 2;
	for (int i = 0; i < n; i++) {
		float a0 = 2 * PI * (float)i / (float)n, a1 = 2 * PI * (float)(i + 1) / (float)n;
		quad(white_tex, vert(cx + cosf(a0) * in, cy + sinf(a0) * in, 0, 0, c),
		     vert(cx + cosf(a0) * out, cy + sinf(a0) * out, 0, 0, c),
		     vert(cx + cosf(a1) * out, cy + sinf(a1) * out, 0, 0, c),
		     vert(cx + cosf(a1) * in, cy + sinf(a1) * in, 0, 0, c));
	}
}

void d2_clip(float x, float y, float w, float h) {
	flush();
	glEnable(GL_SCISSOR_TEST);
	/* OpenGL counts rows from the bottom */
	glScissor((int)x, g_h - (int)(y + h), (int)w, (int)h);
}

void d2_unclip(void) {
	flush();
	glDisable(GL_SCISSOR_TEST);
}

/* ---- text: each string drawn once into a texture and kept ---- */

#define TEXTS 2048 /* a power of two */

typedef struct {
	char *s;
	GLuint tex;
	int w, h;
} Text;

static TTF_Font *font;
static Text texts[TEXTS];
static int text_count;

static void texts_forget(void) {
	flush();
	for (int i = 0; i < TEXTS; i++) {
		if (texts[i].s) {
			free(texts[i].s);
			glDeleteTextures(1, &texts[i].tex);
			texts[i] = (Text){0};
		}
	}
	text_count = 0;
}

static const Text *text_get(const char *s) {
	unsigned h = 2166136261u;
	for (const char *p = s; *p; p++) {
		h = (h ^ (unsigned char)*p) * 16777619u;
	}
	unsigned i = h & (TEXTS - 1);
	while (texts[i].s && strcmp(texts[i].s, s) != 0) {
		i = (i + 1) & (TEXTS - 1);
	}
	if (texts[i].s) {
		return &texts[i];
	}
	if (text_count > TEXTS / 2) {
		/* a long scroll through names fills it: start again */
		texts_forget();
		return text_get(s);
	}
	SDL_Surface *drawn = TTF_RenderUTF8_Blended(font, s, (SDL_Color){255, 255, 255, 255});
	if (!drawn) {
		return NULL; /* a string with nothing to draw */
	}
	SDL_Surface *px = SDL_ConvertSurfaceFormat(drawn, SDL_PIXELFORMAT_ABGR8888, 0);
	SDL_FreeSurface(drawn);
	Text *t = &texts[i];
	t->s = strdup(s);
	t->w = px->w;
	t->h = px->h;
	glGenTextures(1, &t->tex);
	glBindTexture(GL_TEXTURE_2D, t->tex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, px->pitch / 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, px->w, px->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px->pixels);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, batch_tex);
	SDL_FreeSurface(px);
	text_count++;
	return t;
}

int d2_text_height(void) { return TTF_FontHeight(font); }

int d2_text_width(const char *s) {
	int w = 0;
	TTF_SizeUTF8(font, s, &w, NULL);
	return w;
}

void d2_text(float x, float y, const char *s, SDL_Color c) {
	const Text *t = *s ? text_get(s) : NULL;
	if (!t) {
		return;
	}
	x = floorf(x);
	y = floorf(y);
	float w = (float)t->w, h = (float)t->h;
	quad(t->tex, vert(x, y, 0, 0, c), vert(x + w, y, 1, 0, c), vert(x + w, y + h, 1, 1, c),
	     vert(x, y + h, 0, 1, c));
}

void d2_text_fit(float x, float y, float maxw, const char *s, SDL_Color c) {
	int count = 0, extent = 0;
	TTF_MeasureUTF8(font, s, (int)maxw, &extent, &count);
	/* count is in characters; walk that many UTF-8 characters to find the bytes */
	const char *end = s;
	for (int i = 0; i < count && *end; i++) {
		end++;
		while ((*end & 0xC0) == 0x80) {
			end++;
		}
	}
	if (!*end) {
		d2_text(x, y, s, c);
		return;
	}
	/* room for the two dots: give up two more characters */
	for (int i = 0; i < 2 && end > s; i++) {
		end--;
		while (end > s && (*end & 0xC0) == 0x80) {
			end--;
		}
	}
	if (end == s) {
		return;
	}
	char cut[512];
	snprintf(cut, sizeof cut, "%.*s..", (int)(end - s), s);
	d2_text(x, y, cut, c);
}

/* ======================================================================
 *   Solid: one cube and one sphere, each drawn many times over with a
 *   place, a size and a colour per copy
 * ====================================================================== */

static const char *SOLID_VS =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec3 aNormal;\n"
	"layout(location = 2) in vec3 iPos;\n"
	"layout(location = 3) in vec3 iSize;\n"
	"layout(location = 4) in vec4 iRgba;\n"
	"uniform mat4 uMvp;\n"
	"out vec3 vNormal;\n"
	"out vec3 vLocal;\n"
	"out vec3 vSize;\n"
	"out vec4 vRgba;\n"
	"void main() {\n"
	"    vNormal = aNormal;\n"
	"    vLocal = aPos;\n"
	"    vSize = iSize;\n"
	"    vRgba = iRgba;\n"
	"    gl_Position = uMvp * vec4(aPos * iSize + iPos, 1.0);\n"
	"}\n";

/* Lit from above and one side. A box's edges are darkened, by a width
 * that is the same on every box whatever its size, so boxes of one
 * colour standing together still read as separate. */
static const char *SOLID_FS =
	"#version 330 core\n"
	"in vec3 vNormal;\n"
	"in vec3 vLocal;\n"
	"in vec3 vSize;\n"
	"in vec4 vRgba;\n"
	"uniform float uEdge;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec3 n = normalize(vNormal);\n"
	"    float light = 0.42 + 0.58 * max(dot(n, normalize(vec3(0.35, 0.85, 0.4))), 0.0);\n"
	"    vec3 d = (0.5 - abs(vLocal)) * vSize;\n"
	"    vec3 on = step(0.5, abs(n));\n"
	"    d += on * 1e6;\n"
	"    float edge = min(d.x, min(d.y, d.z));\n"
	"    light *= uEdge > 0.0 ? mix(0.55, 1.0, smoothstep(0.0, uEdge, edge)) : 1.0;\n"
	"    FragColor = vec4(vRgba.rgb * light, vRgba.a);\n"
	"}\n";

static const char *LINE_VS =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec4 aRgba;\n"
	"uniform mat4 uMvp;\n"
	"out vec4 vRgba;\n"
	"void main() { vRgba = aRgba; gl_Position = uMvp * vec4(aPos, 1.0); }\n";

static const char *LINE_FS =
	"#version 330 core\n"
	"in vec4 vRgba;\n"
	"out vec4 FragColor;\n"
	"void main() { FragColor = vRgba; }\n";

typedef struct {
	GLuint vao, inst;
	int verts;
} Mesh;

static GLuint solid_prog, line_prog, line_vao, line_vbo;
static GLint solid_mvp, solid_edge, line_mvp;
static Mesh cube, sphere;
static float edge_width;

/* pn: position then normal, six floats a vertex */
static Mesh mesh_make(const float *pn, int verts) {
	Mesh m = {.verts = verts};
	GLuint vbo;
	glGenVertexArrays(1, &m.vao);
	glBindVertexArray(m.vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)verts * 6 * sizeof *pn), pn, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof *pn, (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof *pn, (void *)(3 * sizeof *pn));
	glGenBuffers(1, &m.inst);
	glBindBuffer(GL_ARRAY_BUFFER, m.inst);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Solid), (void *)offsetof(Solid, pos));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Solid), (void *)offsetof(Solid, size));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Solid), (void *)offsetof(Solid, rgba));
	for (GLuint i = 2; i <= 4; i++) {
		glVertexAttribDivisor(i, 1);
	}
	glBindVertexArray(0);
	return m;
}

static Mesh cube_make(void) {
	/* each face: its normal, and the two directions across it */
	static const float FACE[6][9] = {
		{1, 0, 0, 0, 1, 0, 0, 0, 1},  {-1, 0, 0, 0, 0, 1, 0, 1, 0}, {0, 1, 0, 0, 0, 1, 1, 0, 0},
		{0, -1, 0, 1, 0, 0, 0, 0, 1}, {0, 0, 1, 1, 0, 0, 0, 1, 0},  {0, 0, -1, 0, 1, 0, 1, 0, 0},
	};
	static const float CORNER[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
	float pn[36 * 6];
	for (int f = 0; f < 6; f++) {
		const float *n = FACE[f], *a = FACE[f] + 3, *b = FACE[f] + 6;
		for (int c = 0; c < 6; c++) {
			float *o = &pn[(f * 6 + c) * 6];
			for (int k = 0; k < 3; k++) {
				o[k] = (n[k] + a[k] * CORNER[c][0] + b[k] * CORNER[c][1]) * 0.5f;
				o[3 + k] = n[k];
			}
		}
	}
	return mesh_make(pn, 36);
}

/* a point on the ball one unit across, which is also twice its normal */
static void ball_point(float *o, int i, int j, int around, int down) {
	float lat = PI * (float)j / (float)down, lon = 2 * PI * (float)i / (float)around;
	float p[3] = {sinf(lat) * cosf(lon), cosf(lat), sinf(lat) * sinf(lon)};
	for (int k = 0; k < 3; k++) {
		o[k] = p[k] * 0.5f;
		o[3 + k] = p[k];
	}
}

static Mesh sphere_make(void) {
	enum { AROUND = 24, DOWN = 14 };
	static float pn[AROUND * DOWN * 6 * 6];
	int n = 0;
	for (int j = 0; j < DOWN; j++) {
		for (int i = 0; i < AROUND; i++) {
			static const int STEP[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
			for (int c = 0; c < 6; c++) {
				ball_point(&pn[n++ * 6], i + STEP[c][0], j + STEP[c][1], AROUND, DOWN);
			}
		}
	}
	return mesh_make(pn, n);
}

void d3_begin(float x, float y, float w, float h, const float mvp[16]) {
	flush();
	glViewport((int)x, g_h - (int)(y + h), (int)w, (int)h);
	glEnable(GL_SCISSOR_TEST);
	glScissor((int)x, g_h - (int)(y + h), (int)w, (int)h);
	glEnable(GL_DEPTH_TEST);
	glClear(GL_DEPTH_BUFFER_BIT);
	glDisable(GL_BLEND);
	glUseProgram(line_prog);
	glUniformMatrix4fv(line_mvp, 1, GL_FALSE, mvp);
	glUseProgram(solid_prog);
	glUniformMatrix4fv(solid_mvp, 1, GL_FALSE, mvp);
}

static void solids(const Mesh *m, const Solid *v, int n, float edge) {
	if (!n) {
		return;
	}
	glUseProgram(solid_prog);
	glUniform1f(solid_edge, edge);
	glBindVertexArray(m->vao);
	glBindBuffer(GL_ARRAY_BUFFER, m->inst);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * sizeof *v), v, GL_STREAM_DRAW);
	glDrawArraysInstanced(GL_TRIANGLES, 0, m->verts, n);
}

void d3_boxes(const Solid *v, int n) { solids(&cube, v, n, edge_width); }
void d3_spheres(const Solid *v, int n) { solids(&sphere, v, n, 0); }

void d3_lines(const LinePoint *v, int n) {
	if (!n) {
		return;
	}
	glUseProgram(line_prog);
	glBindVertexArray(line_vao);
	glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * sizeof *v), v, GL_STREAM_DRAW);
	glDrawArrays(GL_LINES, 0, n);
}

void d3_end(void) {
	glDisable(GL_SCISSOR_TEST);
	flat_state();
}

/* ====================================================================== */

bool draw_init(int font_px) {
	if (!gl_load()) {
		return false;
	}
	flat_prog = program(FLAT_VS, FLAT_FS);
	solid_prog = program(SOLID_VS, SOLID_FS);
	line_prog = program(LINE_VS, LINE_FS);
	if (!flat_prog || !solid_prog || !line_prog) {
		return false;
	}
	flat_screen = glGetUniformLocation(flat_prog, "uScreen");
	solid_mvp = glGetUniformLocation(solid_prog, "uMvp");
	solid_edge = glGetUniformLocation(solid_prog, "uEdge");
	line_mvp = glGetUniformLocation(line_prog, "uMvp");
	edge_width = 0.004f;

	glGenVertexArrays(1, &flat_vao);
	glBindVertexArray(flat_vao);
	glGenBuffers(1, &flat_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, flat_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)offsetof(Vert, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)offsetof(Vert, u));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vert), (void *)offsetof(Vert, r));

	glGenVertexArrays(1, &line_vao);
	glBindVertexArray(line_vao);
	glGenBuffers(1, &line_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LinePoint), (void *)offsetof(LinePoint, pos));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(LinePoint),
	                      (void *)offsetof(LinePoint, rgba));

	unsigned char white[4] = {255, 255, 255, 255};
	glGenTextures(1, &white_tex);
	glBindTexture(GL_TEXTURE_2D, white_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	batch_tex = white_tex;

	cube = cube_make();
	sphere = sphere_make();

	if (TTF_Init() != 0) {
		fprintf(stderr, "syn-dirstat: text: %s\n", TTF_GetError());
		return false;
	}
	font = TTF_OpenFontRW(SDL_RWFromConstMem(ASSET_FONT, ASSET_FONT_LEN), 1, font_px);
	if (!font) {
		fprintf(stderr, "syn-dirstat: font: %s\n", TTF_GetError());
		return false;
	}
	return true;
}

void draw_begin(int w, int h, SDL_Color clear) {
	g_w = w;
	g_h = h;
	glDisable(GL_SCISSOR_TEST);
	glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	flat_state();
}

void draw_end(void) { flush(); }

bool draw_shot(const char *path) {
	unsigned char *px = malloc((size_t)g_w * g_h * 3);
	FILE *f = fopen(path, "wb");
	if (!px || !f) {
		return false;
	}
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, g_w, g_h, GL_RGB, GL_UNSIGNED_BYTE, px);
	fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
	for (int y = g_h - 1; y >= 0; y--) {
		fwrite(px + (size_t)y * g_w * 3, 1, (size_t)g_w * 3, f);
	}
	fclose(f);
	free(px);
	return true;
}
