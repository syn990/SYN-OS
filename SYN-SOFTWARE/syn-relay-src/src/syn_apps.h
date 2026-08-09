/* ------------------------------------------------------------------------
 *   Enumerates installed applications via .desktop files under
 *   /usr/share/applications and ~/.local/share/applications. Minimal
 *   parser (Name=/Exec=/Icon=/NoDisplay=/Hidden= only, no Name[xx]=
 *   locale variants, no Actions=) — matches the manual fgets/key=value
 *   scanning style syn_theme.c already uses for .theme files, since no
 *   .desktop-spec library exists anywhere in this repo.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_APPS_H
#define SYN_APPS_H

#include <stdbool.h>

#define SYN_APPS_MAX 512

typedef struct {
	char id[512];    /* the .desktop file's absolute path — opaque handle,
	                   * passed back verbatim to syn_apps_launch() */
	char name[256];
	char exec[512];  /* raw Exec= value, %-field-codes not yet stripped */
	char icon[256];
} syn_app_entry;

/* Fills up to `max` entries into `out`, skipping NoDisplay=true/Hidden=true
 * entries and anything without both Name= and Exec=. Returns the count
 * actually filled. */
int syn_apps_list(syn_app_entry *out, int max);

/* Looks up `id` (a path previously returned by syn_apps_list) again,
 * re-parses its Exec= line, strips %f/%u/%F/%U-style field codes, and
 * fork+execvp's it detached so it outlives this process. Returns true if
 * the .desktop file was found and a launch was attempted. */
bool syn_apps_launch(const char *id);

/* Strips %f/%u/%F/%U/%i/%c/%k-style desktop-entry field codes in place. */
void syn_apps_strip_field_codes(char *exec);

#endif
