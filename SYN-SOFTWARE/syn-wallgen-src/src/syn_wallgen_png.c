/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_wallgen_png.h"

#include <math.h>
#include <stdlib.h>
#include <png.h>

int syn_wg_write_png(const char *path, const syn_wg_image *im) {
	FILE *fp = fopen(path, "wb");
	if (!fp) {
		return -1;
	}
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fclose(fp);
		return -1;
	}
	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_write_struct(&png, NULL);
		fclose(fp);
		return -1;
	}
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(fp);
		return -1;
	}

	png_init_io(png, fp);
	png_set_IHDR(png, info, SYN_WG_W, SYN_WG_H, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	             PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);

	unsigned char *row = malloc((size_t)SYN_WG_W * 3);
	for (int y = 0; y < SYN_WG_H; y++) {
		for (int x = 0; x < SYN_WG_W; x++) {
			syn_wg_rgb p = im->px[y * SYN_WG_W + x];
			row[x * 3 + 0] = (unsigned char)fmin(255, fmax(0, round(p.r)));
			row[x * 3 + 1] = (unsigned char)fmin(255, fmax(0, round(p.g)));
			row[x * 3 + 2] = (unsigned char)fmin(255, fmax(0, round(p.b)));
		}
		png_write_row(png, row);
	}
	free(row);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(fp);
	return 0;
}
