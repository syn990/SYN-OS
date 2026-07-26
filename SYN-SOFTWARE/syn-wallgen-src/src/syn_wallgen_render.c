/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_wallgen_render.h"
#include "syn_wallgen_rng.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SMALL_W 240
#define SMALL_H 135 /* 8x downscale working resolution, upsampled with bicubic */

typedef struct { double r, g, b, a; } rgba_f; /* overlay pixel, 0..255/0..1, double */

void syn_wg_image_free(syn_wg_image *im) {
	free(im->px);
	im->px = NULL;
}

static syn_wg_image image_new(void) {
	syn_wg_image im;
	im.px = calloc((size_t)SYN_WG_W * SYN_WG_H, sizeof(syn_wg_rgb));
	return im;
}

static void image_fill(syn_wg_image *im, syn_wg_rgb c) {
	for (int i = 0; i < SYN_WG_W * SYN_WG_H; i++) {
		im->px[i] = c;
	}
}

/* ---- HSV relight (matches Python's colorsys.rgb_to_hsv / hsv_to_rgb) --- */
static void rgb_to_hsv(double r, double g, double b, double *h, double *s, double *v) {
	double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
	*v = mx;
	double d = mx - mn;
	*s = (mx == 0) ? 0 : d / mx;
	if (d == 0) {
		*h = 0;
		return;
	}
	if (mx == r) {
		*h = fmod((g - b) / d, 6.0);
	} else if (mx == g) {
		*h = (b - r) / d + 2.0;
	} else {
		*h = (r - g) / d + 4.0;
	}
	*h /= 6.0;
	if (*h < 0) {
		*h += 1.0;
	}
}

static void hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b) {
	if (s == 0.0) {
		*r = *g = *b = v;
		return;
	}
	double hh = h * 6.0;
	int i = (int)floor(hh);
	double f = hh - i;
	double p = v * (1 - s);
	double q = v * (1 - s * f);
	double t = v * (1 - s * (1 - f));
	switch (((i % 6) + 6) % 6) {
	case 0: *r = v; *g = t; *b = p; break;
	case 1: *r = q; *g = v; *b = p; break;
	case 2: *r = p; *g = v; *b = t; break;
	case 3: *r = p; *g = q; *b = v; break;
	case 4: *r = t; *g = p; *b = v; break;
	default: *r = v; *g = p; *b = q; break;
	}
}

/* Re-lights rgb to a target HSV value (0-1), keeping its hue/sat. */
static syn_wg_rgb scale_lightness(syn_wg_rgb rgb, double target_v) {
	double r = rgb.r / 255.0, g = rgb.g / 255.0, b = rgb.b / 255.0;
	double h, s, v;
	rgb_to_hsv(r, g, b, &h, &s, &v);
	double r2, g2, b2;
	hsv_to_rgb(h, s, target_v, &r2, &g2, &b2);
	syn_wg_rgb out = { round(r2 * 255), round(g2 * 255), round(b2 * 255) };
	return out;
}

/* ---- Bicubic upsample (Catmull-Rom, a=-0.5 — matches PIL's BICUBIC) ---- */
static double cubic_kernel(double x, double a) {
	x = fabs(x);
	if (x < 1.0) {
		return (a + 2.0) * x * x * x - (a + 3.0) * x * x + 1.0;
	}
	if (x < 2.0) {
		return a * x * x * x - 5.0 * a * x * x + 8.0 * a * x - 4.0 * a;
	}
	return 0.0;
}

static double sample_bicubic(const double *small, int sw, int sh, double sx, double sy) {
	int ix = (int)floor(sx), iy = (int)floor(sy);
	double fx = sx - ix, fy = sy - iy;
	double result = 0.0, wsum = 0.0;
	for (int m = -1; m <= 2; m++) {
		int yy = iy + m;
		if (yy < 0) {
			yy = 0;
		} else if (yy >= sh) {
			yy = sh - 1;
		}
		double wy = cubic_kernel(m - fy, -0.5);
		for (int n = -1; n <= 2; n++) {
			int xx = ix + n;
			if (xx < 0) {
				xx = 0;
			} else if (xx >= sw) {
				xx = sw - 1;
			}
			double wx = cubic_kernel(n - fx, -0.5);
			double w = wx * wy;
			result += small[yy * sw + xx] * w;
			wsum += w;
		}
	}
	return wsum != 0.0 ? result / wsum : result;
}

typedef double (*mask_fn)(double x, double y, void *ctx);

/* Evaluates fn at SMALL_W x SMALL_H, upsamples to a SYN_WG_W*SYN_WG_H mask
 * (values 0..255, stored as double for compositing precision). out_full
 * must hold SYN_WG_W*SYN_WG_H doubles. */
static void mask_from_fn(double *out_full, mask_fn fn, void *ctx) {
	double *small = malloc(sizeof(double) * SMALL_W * SMALL_H);
	for (int y = 0; y < SMALL_H; y++) {
		for (int x = 0; x < SMALL_W; x++) {
			small[y * SMALL_W + x] = fn((double)x, (double)y, ctx);
		}
	}

	double sx_scale = (double)SMALL_W / SYN_WG_W, sy_scale = (double)SMALL_H / SYN_WG_H;
	for (int y = 0; y < SYN_WG_H; y++) {
		double sy = (y + 0.5) * sy_scale - 0.5;
		for (int x = 0; x < SYN_WG_W; x++) {
			double sx = (x + 0.5) * sx_scale - 0.5;
			double v = sample_bicubic(small, SMALL_W, SMALL_H, sx, sy);
			if (v < 0) {
				v = 0;
			} else if (v > 255) {
				v = 255;
			}
			out_full[y * SYN_WG_W + x] = v;
		}
	}
	free(small);
}

/* out = a_color where mask==255, b_color where mask==0, blended between */
static void composite_rgb(syn_wg_image *out, syn_wg_rgb a_color, syn_wg_rgb b_color, const double *mask) {
	for (int i = 0; i < SYN_WG_W * SYN_WG_H; i++) {
		double t = mask[i] / 255.0;
		out->px[i].r = a_color.r * t + b_color.r * (1 - t);
		out->px[i].g = a_color.g * t + b_color.g * (1 - t);
		out->px[i].b = a_color.b * t + b_color.b * (1 - t);
	}
}

static void composite_image(syn_wg_image *out, const syn_wg_image *a, const syn_wg_image *b, const double *mask) {
	for (int i = 0; i < SYN_WG_W * SYN_WG_H; i++) {
		double t = mask[i] / 255.0;
		out->px[i].r = a->px[i].r * t + b->px[i].r * (1 - t);
		out->px[i].g = a->px[i].g * t + b->px[i].g * (1 - t);
		out->px[i].b = a->px[i].b * t + b->px[i].b * (1 - t);
	}
}

/* ---- base_field / add_radial_light / vignette -------------------------- */
typedef struct { double cx, cy, diag; } radial_ctx;

static double base_field_fn(double x, double y, void *ctx0) {
	radial_ctx *ctx = ctx0;
	double d = hypot(x - ctx->cx, y - ctx->cy) / ctx->diag;
	double v = d * 1.15;
	if (v < 0) {
		v = 0;
	} else if (v > 1) {
		v = 1;
	}
	return floor(255.0 * v);
}

/* Radial blend from a dim base (corners) to a lifted mid-tone, off-center
 * toward the upper-left (a "light source" position), so the image reads
 * as a scene rather than a flat swatch. */
static syn_wg_image base_field(syn_wg_rgb dark_base, syn_wg_rgb mid_tone, double cx_frac, double cy_frac) {
	radial_ctx ctx = { SMALL_W * cx_frac, SMALL_H * cy_frac, hypot(SMALL_W, SMALL_H) };
	double *mask = malloc(sizeof(double) * SYN_WG_W * SYN_WG_H);
	mask_from_fn(mask, base_field_fn, &ctx);
	syn_wg_image im = image_new();
	composite_rgb(&im, dark_base, mid_tone, mask);
	free(mask);
	return im;
}

typedef struct { double cx, cy, r, strength; } glow_ctx;

static double radial_light_fn(double x, double y, void *ctx0) {
	glow_ctx *ctx = ctx0;
	double d = hypot(x - ctx->cx, y - ctx->cy) / ctx->r;
	double v = 1.0 - d;
	if (v < 0) {
		v = 0;
	}
	return floor(255.0 * pow(v, 1.6) * ctx->strength);
}

static syn_wg_image add_radial_light(const syn_wg_image *im, syn_wg_rgb color, double cx_frac, double cy_frac,
                                      double radius_frac, double strength) {
	glow_ctx ctx = { SMALL_W * cx_frac, SMALL_H * cy_frac, SMALL_W * radius_frac, strength };
	double *mask = malloc(sizeof(double) * SYN_WG_W * SYN_WG_H);
	mask_from_fn(mask, radial_light_fn, &ctx);
	syn_wg_image color_im = image_new();
	image_fill(&color_im, color);
	syn_wg_image out = image_new();
	composite_image(&out, &color_im, im, mask);
	syn_wg_image_free(&color_im);
	free(mask);
	return out;
}

typedef struct { double cx, cy, diag, strength; } vignette_ctx;

static double vignette_fn(double x, double y, void *ctx0) {
	vignette_ctx *ctx = ctx0;
	double d = hypot(x - ctx->cx, y - ctx->cy) / ctx->diag;
	if (d < 0) {
		d = 0;
	} else if (d > 1) {
		d = 1;
	}
	return floor(255.0 * d * d * ctx->strength);
}

static syn_wg_image vignette(const syn_wg_image *im, double strength) {
	vignette_ctx ctx = { SMALL_W * 0.5, SMALL_H * 0.5, 0, strength };
	ctx.diag = hypot(ctx.cx, ctx.cy);
	double *mask = malloc(sizeof(double) * SYN_WG_W * SYN_WG_H);
	mask_from_fn(mask, vignette_fn, &ctx);
	syn_wg_image black = image_new();
	image_fill(&black, (syn_wg_rgb){0, 0, 0});
	syn_wg_image out = image_new();
	composite_image(&out, &black, im, mask);
	syn_wg_image_free(&black);
	free(mask);
	return out;
}

/* ---- Overlay drawing: RGBA float accumulation buffer, composited once - */
typedef struct { rgba_f *px; } overlay;

static overlay overlay_new(void) {
	overlay ov;
	ov.px = calloc((size_t)SYN_WG_W * SYN_WG_H, sizeof(rgba_f));
	return ov;
}
static void overlay_free(overlay *ov) {
	free(ov->px);
	ov->px = NULL;
}

/* "over" compositing within the overlay itself, matching repeated
 * ImageDraw calls onto the same RGBA layer in the reference renderer. */
static void ov_set(overlay *ov, int x, int y, syn_wg_rgb c, double a) {
	if (x < 0 || x >= SYN_WG_W || y < 0 || y >= SYN_WG_H) {
		return;
	}
	rgba_f *p = &ov->px[y * SYN_WG_W + x];
	double src_a = a / 255.0;
	double out_a = src_a + p->a * (1 - src_a);
	if (out_a <= 0) {
		p->r = p->g = p->b = p->a = 0;
		return;
	}
	p->r = (c.r * src_a + p->r * p->a * (1 - src_a)) / out_a;
	p->g = (c.g * src_a + p->g * p->a * (1 - src_a)) / out_a;
	p->b = (c.b * src_a + p->b * p->a * (1 - src_a)) / out_a;
	p->a = out_a;
}

static void ov_vline(overlay *ov, int x, syn_wg_rgb c, double a) {
	for (int y = 0; y < SYN_WG_H; y++) {
		ov_set(ov, x, y, c, a);
	}
}
static void ov_hline(overlay *ov, int y, syn_wg_rgb c, double a) {
	for (int x = 0; x < SYN_WG_W; x++) {
		ov_set(ov, x, y, c, a);
	}
}
static void ov_rect(overlay *ov, int x0, int y0, int x1, int y1, syn_wg_rgb c, double a) {
	if (x0 < 0) {
		x0 = 0;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (x1 >= SYN_WG_W) {
		x1 = SYN_WG_W - 1;
	}
	if (y1 >= SYN_WG_H) {
		y1 = SYN_WG_H - 1;
	}
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			ov_set(ov, x, y, c, a);
		}
	}
}
static void ov_ellipse_outline(overlay *ov, double cx, double cy, double r, int width, syn_wg_rgb c, double a) {
	int steps = (int)(2 * M_PI * (r + width)) + 32;
	for (int i = 0; i < steps; i++) {
		double t = 2 * M_PI * i / steps;
		double ct = cos(t), st = sin(t);
		for (int w = -width / 2; w <= width / 2; w++) {
			double rr = r + w;
			int x = (int)round(cx + rr * ct);
			int y = (int)round(cy + rr * st);
			ov_set(ov, x, y, c, a);
		}
	}
}

/* Scanline fill for the bevel-sheen quad — matches ImageDraw.polygon(fill=). */
static void ov_polygon_fill(overlay *ov, const double *xs, const double *ys, int n, syn_wg_rgb c, double a) {
	double ymin = ys[0], ymax = ys[0];
	for (int i = 1; i < n; i++) {
		if (ys[i] < ymin) {
			ymin = ys[i];
		}
		if (ys[i] > ymax) {
			ymax = ys[i];
		}
	}
	int y0 = (int)fmax(0, floor(ymin)), y1 = (int)fmin(SYN_WG_H - 1, ceil(ymax));
	for (int y = y0; y <= y1; y++) {
		double yc = y + 0.5;
		double xints[16];
		int nx = 0;
		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			double yi = ys[i], yj = ys[j];
			if ((yi <= yc && yj > yc) || (yj <= yc && yi > yc)) {
				double xi = xs[i] + (yc - yi) / (yj - yi) * (xs[j] - xs[i]);
				if (nx < 16) {
					xints[nx++] = xi;
				}
			}
		}
		for (int i = 1; i < nx; i++) {
			double key = xints[i];
			int k = i - 1;
			while (k >= 0 && xints[k] > key) {
				xints[k + 1] = xints[k];
				k--;
			}
			xints[k + 1] = key;
		}
		for (int i = 0; i + 1 < nx; i += 2) {
			int x0 = (int)ceil(xints[i]), x1 = (int)floor(xints[i + 1]);
			if (x0 < 0) {
				x0 = 0;
			}
			if (x1 >= SYN_WG_W) {
				x1 = SYN_WG_W - 1;
			}
			for (int x = x0; x <= x1; x++) {
				ov_set(ov, x, y, c, a);
			}
		}
	}
}

static void alpha_composite(syn_wg_image *im, const overlay *ov) {
	for (int i = 0; i < SYN_WG_W * SYN_WG_H; i++) {
		double a = ov->px[i].a;
		if (a <= 0.0) {
			continue;
		}
		im->px[i].r = ov->px[i].r * a + im->px[i].r * (1 - a);
		im->px[i].g = ov->px[i].g * a + im->px[i].g * (1 - a);
		im->px[i].b = ov->px[i].b * a + im->px[i].b * (1 - a);
	}
}

/* Separable 3-pass box blur approximating a Gaussian at the given radius —
 * the same technique most Gaussian-blur implementations use internally,
 * visually equivalent at this radius without needing to match any one
 * library's exact coefficients. */
static void box_blur_pass(rgba_f *buf, int radius) {
	rgba_f *tmp = malloc(sizeof(rgba_f) * SYN_WG_W * SYN_WG_H);
	for (int y = 0; y < SYN_WG_H; y++) {
		double sr = 0, sg = 0, sb = 0, sa = 0;
		int count = 0;
		for (int x = -radius; x <= radius; x++) {
			int xx = x < 0 ? 0 : (x >= SYN_WG_W ? SYN_WG_W - 1 : x);
			rgba_f p = buf[y * SYN_WG_W + xx];
			sr += p.r; sg += p.g; sb += p.b; sa += p.a; count++;
		}
		for (int x = 0; x < SYN_WG_W; x++) {
			tmp[y * SYN_WG_W + x].r = sr / count;
			tmp[y * SYN_WG_W + x].g = sg / count;
			tmp[y * SYN_WG_W + x].b = sb / count;
			tmp[y * SYN_WG_W + x].a = sa / count;
			int addx = x + radius + 1;
			if (addx >= SYN_WG_W) {
				addx = SYN_WG_W - 1;
			}
			int subx = x - radius;
			if (subx < 0) {
				subx = 0;
			}
			rgba_f pa = buf[y * SYN_WG_W + addx], ps = buf[y * SYN_WG_W + subx];
			sr += pa.r - ps.r; sg += pa.g - ps.g; sb += pa.b - ps.b; sa += pa.a - ps.a;
		}
	}
	memcpy(buf, tmp, sizeof(rgba_f) * SYN_WG_W * SYN_WG_H);

	for (int x = 0; x < SYN_WG_W; x++) {
		double sr = 0, sg = 0, sb = 0, sa = 0;
		int count = 0;
		for (int y = -radius; y <= radius; y++) {
			int yy = y < 0 ? 0 : (y >= SYN_WG_H ? SYN_WG_H - 1 : y);
			rgba_f p = buf[yy * SYN_WG_W + x];
			sr += p.r; sg += p.g; sb += p.b; sa += p.a; count++;
		}
		for (int y = 0; y < SYN_WG_H; y++) {
			tmp[y * SYN_WG_W + x].r = sr / count;
			tmp[y * SYN_WG_W + x].g = sg / count;
			tmp[y * SYN_WG_W + x].b = sb / count;
			tmp[y * SYN_WG_W + x].a = sa / count;
			int addy = y + radius + 1;
			if (addy >= SYN_WG_H) {
				addy = SYN_WG_H - 1;
			}
			int suby = y - radius;
			if (suby < 0) {
				suby = 0;
			}
			rgba_f pa = buf[addy * SYN_WG_W + x], ps = buf[suby * SYN_WG_W + x];
			sr += pa.r - ps.r; sg += pa.g - ps.g; sb += pa.b - ps.b; sa += pa.a - ps.a;
		}
	}
	memcpy(buf, tmp, sizeof(rgba_f) * SYN_WG_W * SYN_WG_H);
	free(tmp);
}

static void gaussian_blur_overlay(overlay *ov, double radius) {
	int box_r = (int)round(radius * 1.5); /* boxRadius ~ radius*sqrt(3), 3 passes */
	if (box_r < 1) {
		box_r = 1;
	}
	for (int pass = 0; pass < 3; pass++) {
		box_blur_pass(ov->px, box_r);
	}
}

/* ---- Pattern overlays --------------------------------------------------- */
static syn_wg_image hairline_grid(const syn_wg_image *im, syn_wg_rgb color, int spacing, int alpha) {
	overlay ov = overlay_new();
	for (int x = 0; x < SYN_WG_W; x += spacing) {
		ov_vline(&ov, x, color, alpha);
	}
	for (int y = 0; y < SYN_WG_H; y += spacing) {
		ov_hline(&ov, y, color, alpha);
	}
	syn_wg_image out = image_new();
	memcpy(out.px, im->px, sizeof(syn_wg_rgb) * SYN_WG_W * SYN_WG_H);
	alpha_composite(&out, &ov);
	overlay_free(&ov);
	return out;
}

static syn_wg_image slab_bands(const syn_wg_image *im, syn_wg_rgb color, int count, int alpha, int band_h) {
	overlay ov = overlay_new();
	for (int i = 0; i < count; i++) {
		int y = (int)((double)SYN_WG_H * (i + 0.5) / count) - band_h / 2;
		double edge_alpha = fmin(255.0, alpha * 3.0);
		ov_rect(&ov, 0, y, SYN_WG_W - 1, y + 6, color, edge_alpha);
		ov_rect(&ov, 0, y, SYN_WG_W - 1, y + band_h, color, alpha);
		ov_rect(&ov, 0, y + band_h - 6, SYN_WG_W - 1, y + band_h, color, edge_alpha);
	}
	syn_wg_image out = image_new();
	memcpy(out.px, im->px, sizeof(syn_wg_rgb) * SYN_WG_W * SYN_WG_H);
	alpha_composite(&out, &ov);
	overlay_free(&ov);
	return out;
}

static syn_wg_image halo_rings(const syn_wg_image *im, syn_wg_rgb color, double cx_frac, double cy_frac,
                                int ring_count, int alpha) {
	overlay ov = overlay_new();
	double cx = SYN_WG_W * cx_frac, cy = SYN_WG_H * cy_frac;
	double max_r = hypot(SYN_WG_W, SYN_WG_H) * 0.42;
	for (int i = 1; i <= ring_count; i++) {
		double r = max_r * i / ring_count;
		double a = alpha * (1.0 - (double)(i - 1) / ring_count);
		ov_ellipse_outline(&ov, cx, cy, r, 2, color, a);
	}
	syn_wg_image out = image_new();
	memcpy(out.px, im->px, sizeof(syn_wg_rgb) * SYN_WG_W * SYN_WG_H);
	alpha_composite(&out, &ov);
	overlay_free(&ov);
	return out;
}

static syn_wg_image bevel_sheen(const syn_wg_image *im, double cx_frac, int band_w, int alpha) {
	overlay ov = overlay_new();
	double cx = SYN_WG_W * cx_frac;
	double xs[4] = { cx - band_w, cx + band_w, cx - band_w * 0.2, cx - band_w * 1.8 };
	double ys[4] = { 0, 0, SYN_WG_H, SYN_WG_H };
	ov_polygon_fill(&ov, xs, ys, 4, (syn_wg_rgb){255, 255, 255}, alpha);
	gaussian_blur_overlay(&ov, 110.0);
	syn_wg_image out = image_new();
	memcpy(out.px, im->px, sizeof(syn_wg_rgb) * SYN_WG_W * SYN_WG_H);
	alpha_composite(&out, &ov);
	overlay_free(&ov);
	return out;
}

syn_wg_image syn_wg_build_wallpaper(const char *mode, const char *family, syn_wg_rgb bg_alt, syn_wg_rgb accent,
                                     syn_wg_rgb accent_dim, syn_wg_rgb border) {
	syn_wg_rgb dark_base, mid_tone, light_color;
	double glow_strength, vig_strength;
	int is_dark = strcmp(mode, "dark") == 0;

	if (is_dark) {
		dark_base = (bg_alt.r + bg_alt.g + bg_alt.b > 10) ? scale_lightness(bg_alt, 0.05) : (syn_wg_rgb){5, 5, 6};
		mid_tone = scale_lightness(accent_dim, 0.14);
		light_color = scale_lightness(accent, 0.55);
		glow_strength = 0.85;
		vig_strength = 0.55;
	} else {
		dark_base = (bg_alt.r + bg_alt.g + bg_alt.b < 750) ? scale_lightness(bg_alt, 0.90) : (syn_wg_rgb){235, 235, 232};
		mid_tone = scale_lightness(accent_dim, 0.82);
		light_color = scale_lightness(accent, 0.35);
		glow_strength = 0.35;
		vig_strength = 0.22;
	}

	syn_wg_rgb slab_color = scale_lightness(accent, is_dark ? 0.45 : 0.55);

	/* Per-run jitter — same theme, same overall look, different exact
	 * composition each run. Ranges are deliberately tight: enough that two
	 * runs' wallpapers for a theme are visibly distinct, not so much that
	 * a theme stops looking like itself. */
	double base_cx = 0.32 + syn_wg_rng_uniform(-0.08, 0.08);
	double base_cy = 0.28 + syn_wg_rng_uniform(-0.08, 0.08);
	double light_cx = 0.30 + syn_wg_rng_uniform(-0.08, 0.08);
	double light_cy = 0.26 + syn_wg_rng_uniform(-0.08, 0.08);
	glow_strength *= syn_wg_rng_uniform(0.85, 1.15);
	vig_strength *= syn_wg_rng_uniform(0.85, 1.15);

	syn_wg_image im = base_field(dark_base, mid_tone, base_cx, base_cy);

	if (strcmp(family, "SYN-OS-FLATLINE") == 0) {
		int spacing = syn_wg_rng_randint(80, 128);
		syn_wg_image next = hairline_grid(&im, border, spacing, 24);
		syn_wg_image_free(&im);
		im = next;
	} else if (strcmp(family, "SYN-OS-SLAB") == 0) {
		int count = syn_wg_rng_randint(3, 5);
		syn_wg_image next = slab_bands(&im, slab_color, count, 38, 120);
		syn_wg_image_free(&im);
		im = next;
	} else if (strcmp(family, "SYN-OS-HALO") == 0) {
		int ring_count = syn_wg_rng_randint(4, 7);
		syn_wg_image next = halo_rings(&im, accent, base_cx, base_cy, ring_count, 75);
		syn_wg_image_free(&im);
		im = next;
	} else if (strcmp(family, "SYN-OS-BEVEL") == 0) {
		double bevel_cx = 0.6 + syn_wg_rng_uniform(-0.1, 0.1);
		int band_w = syn_wg_rng_randint(260, 380);
		syn_wg_image next = bevel_sheen(&im, bevel_cx, band_w, 45);
		syn_wg_image_free(&im);
		im = next;
	}

	syn_wg_image lit = add_radial_light(&im, light_color, light_cx, light_cy, 0.55, glow_strength);
	syn_wg_image_free(&im);
	syn_wg_image vig = vignette(&lit, vig_strength);
	syn_wg_image_free(&lit);
	return vig;
}
