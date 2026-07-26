/* ------------------------------------------------------------------------
 *   Generates a wallpaper PNG per theme in --themes-dir (default
 *   ~/.config/syn-os/themes), writing to --out-dir (default ~/.wallpaper)
 *   as <SYN_THEME_NAME>-wallpaper.png. Called once by syn-stage1.zsh
 *   during install, against the new user's home — and still runnable by
 *   hand for dev after adding a theme, or from BUILD-ARCHISO.zsh to bake
 *   fresh wallpapers into the skel overlay before mkarchiso packages the
 *   ISO. See syn_wallgen_render.h for how the image itself is built.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_wallgen_theme.h"
#include "syn_wallgen_render.h"
#include "syn_wallgen_png.h"
#include "syn_wallgen_rng.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void default_path(char *out, size_t outsz, const char *suffix) {
	const char *home = getenv("HOME");
	if (!home) {
		home = "";
	}
	snprintf(out, outsz, "%s%s", home, suffix);
}

static int cmp_str(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

int main(int argc, char **argv) {
	char themes_dir[512], out_dir[512];
	default_path(themes_dir, sizeof(themes_dir), "/.config/syn-os/themes");
	default_path(out_dir, sizeof(out_dir), "/.wallpaper");
	char suffix[64] = "";
	int only_missing = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--themes-dir") == 0 && i + 1 < argc) {
			snprintf(themes_dir, sizeof(themes_dir), "%s", argv[++i]);
		} else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
			snprintf(out_dir, sizeof(out_dir), "%s", argv[++i]);
		} else if (strcmp(argv[i], "--suffix") == 0 && i + 1 < argc) {
			snprintf(suffix, sizeof(suffix), "%s", argv[++i]);
		} else if (strcmp(argv[i], "--only-missing") == 0) {
			only_missing = 1;
		}
	}

	syn_wg_rng_seed();

	char mkcmd[600];
	snprintf(mkcmd, sizeof(mkcmd), "mkdir -p '%s'", out_dir);
	if (system(mkcmd) != 0) {
		fprintf(stderr, "syn-wallgen: failed to create out-dir %s\n", out_dir);
		return 1;
	}

	DIR *d = opendir(themes_dir);
	if (!d) {
		fprintf(stderr, "syn-wallgen: cannot open themes dir: %s\n", themes_dir);
		return 1;
	}

	char **names = NULL;
	int nnames = 0, cap = 0;
	struct dirent *de;
	while ((de = readdir(d))) {
		size_t len = strlen(de->d_name);
		if (len > 6 && strcmp(de->d_name + len - 6, ".theme") == 0) {
			if (nnames >= cap) {
				cap = cap ? cap * 2 : 16;
				names = realloc(names, sizeof(char *) * cap);
			}
			names[nnames++] = strdup(de->d_name);
		}
	}
	closedir(d);

	qsort(names, nnames, sizeof(char *), cmp_str);

	int done = 0;
	for (int i = 0; i < nnames; i++) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", themes_dir, names[i]);
		syn_wg_theme_vals v;
		if (syn_wg_parse_theme(path, &v) != 0) {
			free(names[i]);
			continue;
		}

		size_t nlen = strlen(names[i]);
		char name_buf[128];
		size_t stem_len = nlen - 6 < sizeof(name_buf) - 1 ? nlen - 6 : sizeof(name_buf) - 1;
		memcpy(name_buf, names[i], stem_len);
		name_buf[stem_len] = '\0';
		const char *name = syn_wg_theme_get(&v, "SYN_THEME_NAME", name_buf);

		char out_path[1024];
		snprintf(out_path, sizeof(out_path), "%s/%s-wallpaper%s.png", out_dir, name, suffix);

		struct stat st;
		if (only_missing && stat(out_path, &st) == 0) {
			free(names[i]);
			continue;
		}

		const char *mode = syn_wg_theme_get(&v, "SYN_THEME_MODE", "dark");
		const char *family = syn_wg_theme_get(&v, "SYN_THEME_FAMILY", "SYN-OS-VANILLA");
		const char *bg_alt_s = syn_wg_theme_get(&v, "SYN_BG_ALT", NULL);
		const char *accent_s = syn_wg_theme_get(&v, "SYN_ACCENT", NULL);
		if (!bg_alt_s || !accent_s) {
			fprintf(stderr, "syn-wallgen: %s: missing required keys, skipping\n", name);
			free(names[i]);
			continue;
		}
		const char *accent_dim_s = syn_wg_theme_get(&v, "SYN_ACCENT_DIM", accent_s);
		const char *border_s = syn_wg_theme_get(&v, "SYN_BORDER", accent_s);

		syn_wg_rgb bg_alt = syn_wg_hexrgb(bg_alt_s);
		syn_wg_rgb accent = syn_wg_hexrgb(accent_s);
		syn_wg_rgb accent_dim = syn_wg_hexrgb(accent_dim_s);
		syn_wg_rgb border = syn_wg_hexrgb(border_s);

		syn_wg_image im = syn_wg_build_wallpaper(mode, family, bg_alt, accent, accent_dim, border);
		if (syn_wg_write_png(out_path, &im) != 0) {
			fprintf(stderr, "syn-wallgen: %s: failed to write %s\n", name, out_path);
		} else {
			done++;
			printf("%s: %s %s -> %s\n", name, family, mode, out_path);
		}
		syn_wg_image_free(&im);
		free(names[i]);
	}
	free(names);

	printf("\n%d wallpapers written to %s\n", done, out_dir);
	return 0;
}
