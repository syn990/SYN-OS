/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
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
			fprintf(stderr, "syn-shell: shader: %s\n", log);
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
		fprintf(stderr, "syn-shell: shader link: %s\n", log);
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
	/* the colour as usual, the alpha added up: what ends in the window is
	 * premultiplied, as a compositor takes a see-through window */
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

unsigned d2_icon_make(const unsigned char *alpha, int w, int h) {
	unsigned char *px = malloc((size_t)w * (size_t)h * 4);
	if (!px) {
		abort();
	}
	for (int i = 0; i < w * h; i++) {
		px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
		px[i * 4 + 3] = alpha[i];
	}
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, batch_tex);
	free(px);
	return tex;
}

void d2_icon(unsigned icon, float x, float y, float size, SDL_Color c) {
	x = floorf(x);
	y = floorf(y);
	quad(icon, vert(x, y, 0, 0, c), vert(x + size, y, 1, 0, c), vert(x + size, y + size, 1, 1, c),
	     vert(x, y + size, 0, 1, c));
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
	"uniform vec4 uTurn;\n" /* cos, sin of a turn about the upright through (z, w); 1 0 for none */
	"out vec3 vNormal;\n"
	"out vec3 vLocal;\n"
	"out vec3 vSize;\n"
	"out vec4 vRgba;\n"
	"out vec3 vWorld;\n"
	"void main() {\n"
	"    vec3 p = aPos * iSize + iPos;\n"
	"    vec2 r = p.xz - uTurn.zw;\n"
	"    p.xz = uTurn.zw + vec2(r.x * uTurn.x + r.y * uTurn.y, -r.x * uTurn.y + r.y * uTurn.x);\n"
	"    vNormal = vec3(aNormal.x * uTurn.x + aNormal.z * uTurn.y, aNormal.y, -aNormal.x * uTurn.y + aNormal.z * uTurn.x);\n"
	"    vLocal = aPos;\n"
	"    vSize = iSize;\n"
	"    vRgba = iRgba;\n"
	"    vWorld = p;\n"
	"    gl_Position = uMvp * vec4(p, 1.0);\n"
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
	"in vec3 vWorld;\n"
	"uniform float uEdge;\n"
	"uniform int uLook;\n"   /* 0 lit, 1 hologram, 2 lamp (its own light) */
	"uniform vec3 uEye;\n"
	"uniform float uTime;\n"
	"uniform float uRim;\n" /* light round the edges turned from you, as SYN-TREK's; 0 none */
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec3 n = normalize(vNormal);\n"
	"    if (uLook == 2) { FragColor = vRgba; return; }\n"
	"    if (uLook == 1) {\n"
	/* a hologram: bright where it turns away from you, faint face on,
	 * crossed by scanlines that climb, and a slow band of light */
	/* the rim by which way it turns round, hardly by up and down, so parts
	 * stacked one on another read as one smooth shape */
	"        vec3 v = normalize(uEye - vWorld);\n"
	"        vec3 m = normalize(vec3(n.x, n.y * 0.3, n.z));\n"
	"        float rim = pow(1.0 - abs(dot(m, v)), 1.5);\n"
	"        float scan = 0.78 + 0.22 * sin(gl_FragCoord.y * 1.6 - uTime * 9.0);\n"
	"        float band = 1.0 + 0.9 * smoothstep(0.93, 1.0, sin(vWorld.y * 2.4 - uTime * 2.1));\n"
	"        float a = clamp((0.10 + 0.9 * rim) * scan * band, 0.0, 1.0) * vRgba.a;\n"
	"        FragColor = vec4(vRgba.rgb * (0.65 + 1.2 * rim) * band, a);\n"
	"        return;\n"
	"    }\n"
	"    float light = 0.42 + 0.58 * max(dot(n, normalize(vec3(0.35, 0.85, 0.4))), 0.0);\n"
	"    vec3 d = (0.5 - abs(vLocal)) * vSize;\n"
	"    vec3 on = step(0.5, abs(n));\n"
	"    d += on * 1e6;\n"
	"    float edge = min(d.x, min(d.y, d.z));\n"
	"    light *= uEdge > 0.0 ? mix(0.55, 1.0, smoothstep(0.0, uEdge, edge)) : 1.0;\n"
	"    vec3 c = vRgba.rgb * light;\n"
	"    if (uRim > 0.0) {\n"
	"        float r = pow(1.0 - max(dot(n, normalize(uEye - vWorld)), 0.0), 3.0) * (1.0 - abs(n.y));\n" /* sides only: not the ground */
	"        c += r * uRim * mix(vRgba.rgb, vec3(0.62, 0.7, 0.85), 0.6);\n"
	"    }\n"
	"    FragColor = vec4(c, vRgba.a);\n"
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

/* a picture on a face: its texture times its colour, shaded a little by
 * which way it faces when lit; the clear parts of an icon cut out */
static const char *QUAD_VS =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aRgba;\n"
	"uniform mat4 uMvp;\n"
	"out vec2 vUV;\n"
	"out vec4 vRgba;\n"
	"out vec3 vWorld;\n"
	"void main() { vUV = aUV; vRgba = aRgba; vWorld = aPos; gl_Position = uMvp * vec4(aPos, 1.0); }\n";

static const char *QUAD_FS =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"in vec4 vRgba;\n"
	"in vec3 vWorld;\n"
	"uniform sampler2D uTex;\n"
	"uniform int uLit;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 c = texture(uTex, vUV) * vRgba;\n"
	"    if (c.a < 0.08) discard;\n"
	"    if (uLit == 1) {\n"
	"        vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));\n"
	"        c.rgb *= 0.55 + 0.45 * abs(dot(n, normalize(vec3(0.35, 0.85, 0.4))));\n"
	"    }\n"
	"    FragColor = vec4(c.rgb, 1.0);\n"
	"}\n";

typedef struct {
	float pos[3], uv[2];
	unsigned char rgba[4];
} QuadVert;

static GLuint quad_prog, quad_vao, quad_vbo;
static GLint quad_mvp, quad_lit;
static QuadVert *quad_buf;
static int quad_cap;

typedef struct {
	GLuint vao, inst;
	int verts;
} Mesh;

static GLuint solid_prog, line_prog, line_vao, line_vbo;
static GLint solid_mvp, solid_edge, solid_turn, solid_look, solid_eye, solid_time, solid_rim, line_mvp;
static int cur_look;
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

static void solids(const Mesh *m, const Solid *v, int n, float edge);

/* ---- glow: the bright parts of the view bleed light round them ----
 *
 * As SYN-TREK spends its light: the view drawn into a buffer of its own,
 * its bright parts (windows, lamps, screens, lines of light) taken out at
 * half size and blurred, wider each pass, then the view and the glow
 * added over it put in the window. Asked for a view at a time
 * (d3_glow before d3_begin); the rest of the window is drawn as before. */

static const char *POST_VS =
	"#version 330 core\n"
	"out vec2 vUv;\n"
	"void main() {\n"
	"    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
	"    vUv = p;\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";
static const char *POST_FS =
	"#version 330 core\n"
	"in vec2 vUv;\n"
	"uniform sampler2D uTex;\n"
	"uniform int uMode;\n" /* 0 the bright parts, 1 a blur along uStep, 2 as it is, 3 the glow, so strong */
	"uniform vec2 uStep;\n"
	"uniform float uStrength;\n"
	"out vec4 o;\n"
	"void main() {\n"
	"    if (uMode == 0) {\n"
	"        vec3 c = texture(uTex, vUv).rgb;\n"
	"        float m = max(c.r, max(c.g, c.b));\n"
	"        float k = clamp((m - 0.6) / 0.4, 0.0, 1.0);\n"
	"        o = vec4(c * k * k, 1.0);\n"
	"    } else if (uMode == 1) {\n"
	"        vec3 s = texture(uTex, vUv).rgb * 0.227;\n"
	"        s += (texture(uTex, vUv + uStep * 1.385).rgb + texture(uTex, vUv - uStep * 1.385).rgb) * 0.316;\n"
	"        s += (texture(uTex, vUv + uStep * 3.231).rgb + texture(uTex, vUv - uStep * 3.231).rgb) * 0.070;\n"
	"        o = vec4(s, 1.0);\n"
	"    } else if (uMode == 2) {\n"
	"        o = texture(uTex, vUv);\n"
	"    } else {\n"
	"        o = vec4(texture(uTex, vUv).rgb * uStrength, 0.0);\n"
	"    }\n"
	"}\n";

static GLuint post_prog, post_vao, glow_fbo[3], glow_tex[3], glow_depth;
static GLint post_mode, post_step, post_strength;
static int glow_w, glow_h;
static bool glow_want, glow_on;
static GLint glow_prev;
static int glow_x, glow_y;

void d3_glow(bool on) { glow_want = on; }

void d3_rim(float k) {
	glUseProgram(solid_prog);
	glUniform1f(solid_rim, k);
}

static void glow_tex_make(GLuint *tex, int w, int h) {
	if (!*tex) {
		glGenTextures(1, tex);
	}
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* the buffers for a view w by h; false if the driver will not */
static bool glow_buffers(int w, int h) {
	if (w == glow_w && h == glow_h && glow_fbo[0]) {
		return true;
	}
	if (!glow_fbo[0]) {
		glGenFramebuffers(3, glow_fbo);
		glGenRenderbuffers(1, &glow_depth);
	}
	int hw = w / 2 > 1 ? w / 2 : 1, hh = h / 2 > 1 ? h / 2 : 1;
	glow_tex_make(&glow_tex[0], w, h);
	glow_tex_make(&glow_tex[1], hw, hh);
	glow_tex_make(&glow_tex[2], hw, hh);
	glBindRenderbuffer(GL_RENDERBUFFER, glow_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	bool ok = true;
	for (int i = 0; i < 3; i++) {
		glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_tex[i], 0);
		if (i == 0) {
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, glow_depth);
		}
		ok &= glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)glow_prev);
	glBindTexture(GL_TEXTURE_2D, 0);
	glow_w = ok ? w : 0;
	glow_h = ok ? h : 0;
	return ok;
}

static void post_pass(GLuint fbo, GLuint tex, int vx, int vy, int vw, int vh, int mode, float sx, float sy) {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(vx, vy, vw, vh);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(post_mode, mode);
	glUniform2f(post_step, sx, sy);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* the view so far, glowing, into the window where it belongs */
static void glow_finish(void) {
	int w = glow_w, h = glow_h, hw = w / 2 > 1 ? w / 2 : 1, hh = h / 2 > 1 ? h / 2 : 1;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glUseProgram(post_prog);
	glBindVertexArray(post_vao);
	glUniform1f(post_strength, 1.6f);
	post_pass(glow_fbo[1], glow_tex[0], 0, 0, hw, hh, 0, 0, 0);
	for (int i = 0; i < 3; i++) {
		float s = 1.0f + 1.5f * (float)i;
		post_pass(glow_fbo[2], glow_tex[1], 0, 0, hw, hh, 1, s / (float)hw, 0);
		post_pass(glow_fbo[1], glow_tex[2], 0, 0, hw, hh, 1, 0, s / (float)hh);
	}
	post_pass((GLuint)glow_prev, glow_tex[0], glow_x, glow_y, w, h, 2, 0, 0);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE); /* the glow added, the alpha kept */
	post_pass((GLuint)glow_prev, glow_tex[1], glow_x, glow_y, w, h, 3, 0, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glow_on = false;
}

void d3_begin(float x, float y, float w, float h, const float mvp[16]) {
	flush();
	glViewport((int)x, g_h - (int)(y + h), (int)w, (int)h);
	glEnable(GL_SCISSOR_TEST);
	glScissor((int)x, g_h - (int)(y + h), (int)w, (int)h);
	glow_on = false;
	if (glow_want && post_prog && w >= 8 && h >= 8) {
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &glow_prev);
		if (glow_buffers((int)w, (int)h)) {
			/* the sky already drawn there, into the view's own buffer */
			glow_x = (int)x;
			glow_y = g_h - (int)(y + h);
			glDisable(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)glow_prev);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, glow_fbo[0]);
			glBlitFramebuffer(glow_x, glow_y, glow_x + (int)w, glow_y + (int)h, 0, 0, (int)w, (int)h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo[0]);
			glViewport(0, 0, (int)w, (int)h);
			glEnable(GL_SCISSOR_TEST);
			glScissor(0, 0, (int)w, (int)h);
			glow_on = true;
		}
	}
	glEnable(GL_DEPTH_TEST);
	glClear(GL_DEPTH_BUFFER_BIT);
	glDisable(GL_BLEND);
	glUseProgram(line_prog);
	glUniformMatrix4fv(line_mvp, 1, GL_FALSE, mvp);
	glUseProgram(solid_prog);
	glUniformMatrix4fv(solid_mvp, 1, GL_FALSE, mvp);
	glUniform4f(solid_turn, 1, 0, 0, 0);
	glUniform1i(solid_look, 0);
	cur_look = LOOK_LIT;
	glUseProgram(quad_prog);
	glUniformMatrix4fv(quad_mvp, 1, GL_FALSE, mvp);
}

void d3_mvp(const float mvp[16]) {
	glUseProgram(line_prog);
	glUniformMatrix4fv(line_mvp, 1, GL_FALSE, mvp);
	glUseProgram(quad_prog);
	glUniformMatrix4fv(quad_mvp, 1, GL_FALSE, mvp);
	glUseProgram(solid_prog);
	glUniformMatrix4fv(solid_mvp, 1, GL_FALSE, mvp);
}

void d3_eye(const float eye[3], float time) {
	glUseProgram(solid_prog);
	glUniform3f(solid_eye, eye[0], eye[1], eye[2]);
	glUniform1f(solid_time, time);
}

void d3_look(int look) {
	cur_look = look;
	glUseProgram(solid_prog);
	glUniform1i(solid_look, look);
	if (look == LOOK_GLOW) {
		/* light added to what is behind, never hiding it; the alpha kept;
		 * on a shell laid down by d3_depth_only, only its outside */
		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		glDepthMask(GL_FALSE);
		glDepthFunc(GL_LEQUAL);
	} else {
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
	}
}

void d3_depth_only(bool on) {
	glColorMask(!on, !on, !on, !on);
}

void d3_spheres_turned(const Solid *v, int n, float yaw, float px, float pz) {
	glUseProgram(solid_prog);
	glUniform4f(solid_turn, cosf(yaw), sinf(yaw), px, pz);
	solids(&sphere, v, n, 0);
	glUniform4f(solid_turn, 1, 0, 0, 0);
}

/* ---- pictures in the world: textured quads ---- */

unsigned d3_texture(const unsigned char *rgba, int w, int h) {
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

void d3_texture_free(unsigned tex) {
	GLuint t = tex;
	if (t) {
		glDeleteTextures(1, &t);
	}
}

void d3_quads(unsigned tex, const Quad3 *q, int n) {
	if (!n) {
		return;
	}
	static const float UV[6][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 1}, {1, 0}, {0, 0}};
	static const int CORNER[6] = {0, 1, 2, 0, 2, 3};
	if (quad_cap < n * 6) {
		quad_cap = n * 6 * 2;
		quad_buf = realloc(quad_buf, (size_t)quad_cap * sizeof *quad_buf);
		if (!quad_buf) {
			abort();
		}
	}
	for (int i = 0; i < n; i++) {
		for (int c = 0; c < 6; c++) {
			QuadVert *v = &quad_buf[i * 6 + c];
			memcpy(v->pos, q[i].p[CORNER[c]], sizeof v->pos);
			v->uv[0] = UV[c][0];
			v->uv[1] = UV[c][1];
			memcpy(v->rgba, q[i].rgba, 4);
		}
	}
	glUseProgram(quad_prog);
	glUniform1i(quad_lit, cur_look == LOOK_LIT);
	glBindVertexArray(quad_vao);
	glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * 6 * sizeof *quad_buf), quad_buf, GL_STREAM_DRAW);
	glBindTexture(GL_TEXTURE_2D, tex ? tex : white_tex);
	glDrawArrays(GL_TRIANGLES, 0, n * 6);
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

void d3_boxes_edged(const Solid *v, int n, float edge) { solids(&cube, v, n, edge); }

void d3_boxes_turned(const Solid *v, int n, float edge, float yaw, float px, float pz) {
	glUseProgram(solid_prog);
	glUniform4f(solid_turn, cosf(yaw), sinf(yaw), px, pz);
	solids(&cube, v, n, edge);
	glUniform4f(solid_turn, 1, 0, 0, 0);
}

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
	if (glow_on) {
		glow_finish();
	}
	d3_look(LOOK_LIT);
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
	quad_prog = program(QUAD_VS, QUAD_FS);
	if (!flat_prog || !solid_prog || !line_prog || !quad_prog) {
		return false;
	}
	post_prog = program(POST_VS, POST_FS); /* none: no glow, all else as it was */
	if (post_prog) {
		glGenVertexArrays(1, &post_vao);
		post_mode = glGetUniformLocation(post_prog, "uMode");
		post_step = glGetUniformLocation(post_prog, "uStep");
		post_strength = glGetUniformLocation(post_prog, "uStrength");
	}
	quad_mvp = glGetUniformLocation(quad_prog, "uMvp");
	quad_lit = glGetUniformLocation(quad_prog, "uLit");
	glGenVertexArrays(1, &quad_vao);
	glBindVertexArray(quad_vao);
	glGenBuffers(1, &quad_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVert), (void *)offsetof(QuadVert, pos));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVert), (void *)offsetof(QuadVert, uv));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QuadVert), (void *)offsetof(QuadVert, rgba));
	flat_screen = glGetUniformLocation(flat_prog, "uScreen");
	solid_mvp = glGetUniformLocation(solid_prog, "uMvp");
	solid_edge = glGetUniformLocation(solid_prog, "uEdge");
	solid_turn = glGetUniformLocation(solid_prog, "uTurn");
	solid_look = glGetUniformLocation(solid_prog, "uLook");
	solid_eye = glGetUniformLocation(solid_prog, "uEye");
	solid_rim = glGetUniformLocation(solid_prog, "uRim");
	solid_time = glGetUniformLocation(solid_prog, "uTime");
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
		fprintf(stderr, "syn-shell: text: %s\n", TTF_GetError());
		return false;
	}
	font = TTF_OpenFontRW(SDL_RWFromConstMem(ASSET_FONT, ASSET_FONT_LEN), 1, font_px);
	if (!font) {
		fprintf(stderr, "syn-shell: font: %s\n", TTF_GetError());
		return false;
	}
	return true;
}

void draw_begin(int w, int h, SDL_Color clear) {
	g_w = w;
	g_h = h;
	glDisable(GL_SCISSOR_TEST);
	/* premultiplied, so a see-through clear is the colour times its alpha */
	float ca = clear.a / 255.0f;
	glClearColor(clear.r / 255.0f * ca, clear.g / 255.0f * ca, clear.b / 255.0f * ca, ca);
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
