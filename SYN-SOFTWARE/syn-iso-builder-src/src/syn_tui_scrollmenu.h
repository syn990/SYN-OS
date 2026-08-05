/* ------------------------------------------------------------------------
 *   A scrollable, filterable, multi-column list widget — extends the
 *   scroll-clamp pattern already proven in syn-crypter-src's
 *   syn_tui_file_picker (same "if (selected < scroll_top) ... if
 *   (selected >= scroll_top + list_height) ..." clamp math) onto a
 *   caller-supplied row type with named columns, for browsing hundreds
 *   of git commits (short SHA / date / author / subject per row) —
 *   syn_tui_menu's flat single-column list has no scroll offset at all
 *   and silently truncates past one screen, so it can't be reused as-is
 *   for this.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_SCROLLMENU_H
#define SYN_TUI_SCROLLMENU_H

#define SYN_SCROLLMENU_MAX_COLS 4

typedef struct {
	/* Up to SYN_SCROLLMENU_MAX_COLS column strings for this row (e.g.
	 * short SHA, date, author, subject). Unused trailing columns must
	 * be "" not NULL. `filter_text` is what live search matches
	 * against (substring, case-sensitive) — usually the subject/author
	 * columns concatenated, not necessarily identical to what's shown. */
	char columns[SYN_SCROLLMENU_MAX_COLS][256];
	char filter_text[512];
} syn_scrollmenu_row;

/* Column widths in terminal cells, left-to-right, matching `columns`
 * above — the widget pads/truncates each column to its configured
 * width. A width of 0 means "give this column whatever's left after the
 * others" (only one column should use this). */
typedef struct {
	int widths[SYN_SCROLLMENU_MAX_COLS];
	int count; /* how many of `widths`/`columns` are actually used, 1-4 */
} syn_scrollmenu_layout;

/* Interactive scrollable/filterable list. `rows`/`count` describe the
 * full unfiltered set (caller owns this memory, widget only reads it).
 * Live-filters via a bottom input line (plain substring match against
 * each row's filter_text, case-sensitive) as the user types; arrow
 * keys/j-k move the selection; Enter selects, Esc/q cancels. Returns
 * the selected row's index into the ORIGINAL `rows` array (not the
 * filtered view), or -1 on cancel. */
int syn_tui_scrollmenu(const char *title, const syn_scrollmenu_row *rows, int count,
	const syn_scrollmenu_layout *layout);

#endif
