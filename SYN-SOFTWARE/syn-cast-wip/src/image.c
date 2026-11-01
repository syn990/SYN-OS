/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "image.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

/* Box-filter downscale of RGB pixels. */
static uint8_t *downscale(const uint8_t *src, int sw, int sh, int dw, int dh) {
	uint8_t *dst = malloc((size_t)dw * dh * 3);
	for (int y = 0; y < dh; y++) {
		int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < dw; x++) {
			int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
			if (x1 <= x0) x1 = x0 + 1;
			unsigned acc[3] = {0, 0, 0}, n = 0;
			for (int yy = y0; yy < y1; yy++) {
				const uint8_t *row = src + ((size_t)yy * sw + x0) * 3;
				for (int xx = x0; xx < x1; xx++, row += 3) {
					acc[0] += row[0];
					acc[1] += row[1];
					acc[2] += row[2];
					n++;
				}
			}
			uint8_t *d = dst + ((size_t)y * dw + x) * 3;
			d[0] = (uint8_t)(acc[0] / n);
			d[1] = (uint8_t)(acc[1] / n);
			d[2] = (uint8_t)(acc[2] / n);
		}
	}
	return dst;
}

static void fit(int w, int h, int max, int *ow, int *oh) {
	if (w <= max && h <= max) {
		*ow = w;
		*oh = h;
	} else if (w >= h) {
		*ow = max;
		*oh = h * max / w > 0 ? h * max / w : 1;
	} else {
		*oh = max;
		*ow = w * max / h > 0 ? w * max / h : 1;
	}
}

static uint8_t *load_rgb(const char *path, int *w, int *h, char *err, size_t errn) {
	size_t n;
	uint8_t *file = read_file(path, &n, 64u << 20);
	if (!file) {
		snprintf(err, errn, "cannot read %s", path);
		return NULL;
	}
	uint8_t *rgb = NULL;
	tjhandle tj = tjInitDecompress();
	int sub, cs;
	if (tjDecompressHeader3(tj, file, (unsigned long)n, w, h, &sub, &cs) == 0) {
		rgb = malloc((size_t)*w * *h * 3);
		if (tjDecompress2(tj, file, (unsigned long)n, rgb, *w, 0, *h, TJPF_RGB, TJFLAG_ACCURATEDCT) != 0) {
			free(rgb);
			rgb = NULL;
		}
	}
	tjDestroy(tj);
	free(file);
	if (!rgb) {
		int pf = TJPF_RGB;
		rgb = tjLoadImage(path, w, 1, h, &pf, 0);  /* BMP or PPM */
		if (!rgb) {
			snprintf(err, errn, "%s: not a JPEG, BMP or PPM image", path);
		}
	}
	return rgb;
}

static uint8_t *compress(const uint8_t *rgb, int w, int h, int quality, size_t *n) {
	tjhandle tj = tjInitCompress();
	unsigned char *out = NULL;
	unsigned long sz = 0;
	uint8_t *res = NULL;
	if (tjCompress2(tj, rgb, w, 0, h, TJPF_RGB, &out, &sz, TJSAMP_420, quality, 0) == 0) {
		res = malloc(sz);
		memcpy(res, out, sz);
		*n = sz;
	}
	tjFree(out);
	tjDestroy(tj);
	return res;
}

int image_prepare(const char *path, uint8_t **full, size_t *full_n, uint8_t **thumb,
	size_t *thumb_n, int *w, int *h, char *err, size_t errn) {
	int sw, sh;
	uint8_t *rgb = load_rgb(path, &sw, &sh, err, errn);
	if (!rgb) {
		return -1;
	}
	int fw, fh, tw, th;
	fit(sw, sh, IMAGE_FULL_MAX, &fw, &fh);
	fit(sw, sh, IMAGE_THUMB_MAX, &tw, &th);
	uint8_t *f = (fw == sw && fh == sh) ? rgb : downscale(rgb, sw, sh, fw, fh);
	uint8_t *t = downscale(rgb, sw, sh, tw, th);
	*full = compress(f, fw, fh, 85, full_n);
	*thumb = compress(t, tw, th, 75, thumb_n);
	if (f != rgb) {
		free(f);
	}
	free(t);
	free(rgb);
	if (!*full || !*thumb) {
		free(*full);
		free(*thumb);
		snprintf(err, errn, "JPEG encoding failed");
		return -1;
	}
	*w = fw;
	*h = fh;
	return 0;
}

uint32_t *image_load_argb(const char *path, int max_w, int max_h, int *w, int *h) {
	char err[160];
	int sw, sh;
	uint8_t *rgb = load_rgb(path, &sw, &sh, err, sizeof err);
	if (!rgb) {
		return NULL;
	}
	int dw = sw, dh = sh;
	if (dw > max_w) {
		dh = dh * max_w / dw;
		dw = max_w;
	}
	if (dh > max_h) {
		dw = dw * max_h / dh;
		dh = max_h;
	}
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	uint8_t *s = (dw == sw && dh == sh) ? rgb : downscale(rgb, sw, sh, dw, dh);
	uint32_t *px = malloc((size_t)dw * dh * 4);
	for (int k = 0; k < dw * dh; k++) {
		px[k] = 0xFF000000u | ((uint32_t)s[k * 3] << 16) | ((uint32_t)s[k * 3 + 1] << 8) | s[k * 3 + 2];
	}
	if (s != rgb) {
		free(s);
	}
	free(rgb);
	*w = dw;
	*h = dh;
	return px;
}
