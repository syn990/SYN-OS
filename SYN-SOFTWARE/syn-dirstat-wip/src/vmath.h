/* ------------------------------------------------------------------------
 *   The little 3D math the views need: 3-vectors and 4x4 matrices,
 *   column-major as OpenGL takes them.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIRSTAT_VMATH_H
#define SYN_DIRSTAT_VMATH_H

#include <math.h>
#include <string.h>

typedef struct {
	float x, y, z;
} Vec3;

static inline Vec3 v3(float x, float y, float z) { return (Vec3){x, y, z}; }
static inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3_scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3_cross(Vec3 a, Vec3 b) {
	return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline Vec3 v3_norm(Vec3 a) { return v3_scale(a, 1.0f / sqrtf(v3_dot(a, a))); }

static inline void m4_mul(float out[16], const float a[16], const float b[16]) {
	float r[16];
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
			                 a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
		}
	}
	memcpy(out, r, sizeof r);
}

static inline void m4_perspective(float m[16], float fovy, float aspect, float z_near, float z_far) {
	float f = 1.0f / tanf(fovy / 2);
	memset(m, 0, 16 * sizeof *m);
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (z_far + z_near) / (z_near - z_far);
	m[11] = -1;
	m[14] = 2 * z_far * z_near / (z_near - z_far);
}

static inline void m4_look_at(float m[16], Vec3 eye, Vec3 at, Vec3 up) {
	Vec3 f = v3_norm(v3_sub(at, eye)), s = v3_norm(v3_cross(f, up)), u = v3_cross(s, f);
	float r[16] = {s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
	               -v3_dot(s, eye), -v3_dot(u, eye), v3_dot(f, eye), 1};
	memcpy(m, r, sizeof r);
}

/* A point through the matrix to a w x h rectangle's pixels (top-left
 * origin); false if it is behind the eye. */
static inline int m4_project(const float m[16], Vec3 p, float w, float h, float *sx, float *sy) {
	float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
	float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
	float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
	if (cw <= 0.0001f) {
		return 0;
	}
	*sx = (cx / cw * 0.5f + 0.5f) * w;
	*sy = (1 - (cy / cw * 0.5f + 0.5f)) * h;
	return 1;
}

#endif
