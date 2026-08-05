/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_rofi_prompt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same rofi invocation shape as syn-picker-lib.zsh's syn_pick::rofi —
 * SYN_BG/SYN_PANEL/SYN_TEXT/SYN_ACCENT/SYN_ACCENT_DIM env vars (set by
 * syn-theme-lib.zsh's syn_theme_load, already exported into every
 * desktop session) style the popup to match the active theme; the same
 * fixed defaults that script uses cover the case where they're unset
 * (e.g. this binary invoked outside a themed session). */
static void build_theme_str(char *out, size_t out_size) {
	const char *bg = getenv("SYN_BG");
	const char *panel = getenv("SYN_PANEL");
	const char *text = getenv("SYN_TEXT");
	const char *accent = getenv("SYN_ACCENT");
	const char *accent_dim = getenv("SYN_ACCENT_DIM");
	if (!bg || !bg[0]) bg = "#000000";
	if (!panel || !panel[0]) panel = "#2c0101";
	if (!text || !text[0]) text = "#ffffff";
	if (!accent || !accent[0]) accent = "#800000";
	if (!accent_dim || !accent_dim[0]) accent_dim = "#260101";

	snprintf(out, out_size,
		"* { background: %se6; background-color: %s; foreground: %s; "
		"lightbg: %s; lightfg: %s; selected-normal-background: %s; "
		"selected-normal-foreground: %s; border-color: %s; } "
		"window { location: center; width: 480px; border: 3px; "
		"border-radius: 0px; padding: 16px; } "
		"entry { placeholder-color: %s; } "
		"inputbar { border: 0 0 2px 0; border-color: %s; padding: 4px 0; "
		"margin: 0 0 8px 0; }",
		bg, panel, text, accent_dim, text, accent, text, accent, text, accent);
}

bool syn_rofi_prompt(const char *prompt, char *out, size_t out_size) {
	if (out_size > 0) {
		out[0] = '\0';
	}

	char theme_str[1024];
	build_theme_str(theme_str, sizeof(theme_str));

	/* Build the argv-safe command line by hand rather than via a shell
	 * string — prompt text is our own fixed literal in every caller
	 * (never user-controlled), but avoiding a shell interpolation step
	 * entirely is simplest and safest regardless. popen() still goes
	 * through /bin/sh, so the theme string and prompt are single-quoted
	 * and any embedded single quote is escaped defensively even though
	 * none of today's callers can produce one. */
	char cmd[2048];
	int n = snprintf(cmd, sizeof(cmd),
		"rofi -dmenu -theme-str '%s' -p '%s' </dev/null 2>/dev/null",
		theme_str, prompt);
	if (n < 0 || (size_t)n >= sizeof(cmd)) {
		return false;
	}

	FILE *pipe = popen(cmd, "r");
	if (!pipe) {
		return false;
	}

	bool got_line = fgets(out, (int)out_size, pipe) != NULL;
	pclose(pipe);

	if (!got_line) {
		return false;
	}
	size_t len = strlen(out);
	while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
		out[--len] = '\0';
	}
	return len > 0;
}
