/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <ctype.h>
#include <string.h>

#include "scene.h"

static const struct {
	const char *name;
	SDL_Color col;
	const char *ends; /* the endings, each between spaces */
} KIND[KIND_COUNT] = {
	{"Pictures", {0x4c, 0xb8, 0x6a, 255}, " jpg jpeg png gif bmp webp tif tiff svg ico heic raw cr2 nef psd xcf "},
	{"Video", {0x4a, 0x8f, 0xe0, 255}, " mp4 mkv avi mov webm wmv flv m4v mpg mpeg ts vob "},
	{"Audio", {0x9b, 0x6c, 0xe0, 255}, " mp3 flac wav ogg opus m4a aac wma aiff mid "},
	{"Archives", {0xe0, 0x9a, 0x3c, 255}, " zip tar gz xz zst bz2 7z rar tgz txz cab deb rpm pkg jar apk msi "},
	{"Documents", {0xd8, 0xcf, 0x5a, 255}, " pdf doc docx odt xls xlsx ods ppt pptx odp txt md rtf epub mobi csv "},
	{"Code", {0x3c, 0xc4, 0xc4, 0xff}, " c h cpp hpp cc rs go py js ts tsx jsx java kt cs rb php sh zsh lua html css json xml yml yaml toml sql "},
	{"Programs", {0xe0, 0x5a, 0x5a, 255}, " exe dll so a o lib bin sys ko out class pyc elf appimage "},
	{"Disk images", {0xd0, 0x62, 0xb4, 255}, " iso img vdi vmdk qcow2 vhd vhdx wim esd sqlite db mdb pak dat "},
	{"Other", {0x8a, 0x8a, 0x8a, 255}, ""},
};

int kind_of(const char *name) {
	/* the ending, past any version after it: libz.so.1.3 is told by its "so" */
	const char *end = name + strlen(name), *dot;
	for (;;) {
		dot = end;
		while (dot > name && dot[-1] != '.') {
			dot--;
		}
		if (dot == name || end == dot) {
			return KIND_COUNT - 1;
		}
		if (dot[strspn(dot, "0123456789")] != *end) {
			break; /* not all digits */
		}
		end = dot - 1;
	}
	if (end - dot > 12) {
		return KIND_COUNT - 1;
	}
	char ending[16] = " ";
	size_t n = 1;
	for (const char *p = dot; p < end; p++) {
		ending[n++] = (char)tolower((unsigned char)*p);
	}
	ending[n++] = ' ';
	ending[n] = '\0';
	for (int k = 0; k < KIND_COUNT - 1; k++) {
		if (strstr(KIND[k].ends, ending)) {
			return k;
		}
	}
	return KIND_COUNT - 1;
}

const char *kind_name(int kind) { return KIND[kind].name; }

SDL_Color kind_colour(int kind, const Theme *t) { return mix(KIND[kind].col, rgb(t->accent), 0.15f); }

void scene_clear(Scene *s) {
	s->n = 0;
	s->nedge = 0;
}

Item *scene_add(Scene *s) {
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 1024;
		s->v = SDL_realloc(s->v, (size_t)s->cap * sizeof *s->v);
	}
	Item *it = &s->v[s->n++];
	memset(it, 0, sizeof *it);
	return it;
}

void scene_join(Scene *s, int parent, int child) {
	if (s->nedge + 2 > s->edge_cap) {
		s->edge_cap = s->edge_cap ? s->edge_cap * 2 : 256;
		s->edge = SDL_realloc(s->edge, (size_t)s->edge_cap * sizeof *s->edge);
	}
	s->edge[s->nedge++] = parent;
	s->edge[s->nedge++] = child;
}
