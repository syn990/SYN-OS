/* ------------------------------------------------------------------------
 *   A centered, bordered, scrolling log panel shown for the whole build
 *   (SYN-SOFTWARE compile loop + mkarchiso) — replaces the old
 *   approach of tearing down ncurses entirely (endwin()) and streaming
 *   raw printf output to the plain terminal for the build's duration.
 *   That approach had no resize handling of its own (a terminal resize
 *   mid-build just corrupted the plain scrolling output) and dropped
 *   the dashboard from view. Keeping ncurses alive and rendering into
 *   this panel instead means resize is handled by ncurses' own
 *   KEY_RESIZE path like every other screen in this tool, not a
 *   bespoke SIGWINCH workaround.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_BUILDLOG_H
#define SYN_TUI_BUILDLOG_H

typedef struct syn_tui_buildlog syn_tui_buildlog;

/* Draws the initial empty panel and returns a handle. `title` is shown
 * in the panel's border, e.g. "Build Output". */
syn_tui_buildlog *syn_tui_buildlog_open(const char *title);

/* Appends one line, scrolls if the panel is already full, and redraws.
 * Safe to call at any rate — this is the hot path for both the
 * SYN-SOFTWARE build loop's lines and mkarchiso's own resolved output
 * lines. Clears any pending provisional line (see
 * syn_tui_buildlog_update_provisional() below) — a real committed line
 * always supersedes an in-progress redraw. */
void syn_tui_buildlog_append(syn_tui_buildlog *log, const char *line);

/* Sets (replacing any previous value) a single provisional line shown
 * after the committed scrollback, without ever entering it — for a
 * live \r-redrawn progress bar (mksquashfs, pacman downloads) to
 * animate in place instead of either flooding the scrollback with a
 * new permanent line per redraw, or the panel going silent for the
 * whole step. Call syn_tui_buildlog_append() once the real line is
 * known (on the eventual \n) to commit it for real and clear this. */
void syn_tui_buildlog_update_provisional(syn_tui_buildlog *log, const char *line);

/* Recomputes panel geometry from the current terminal size and
 * redraws every retained line — call this after a KEY_RESIZE, and
 * syn_tui_buildlog_append() calls it internally too so every new line
 * always draws at the current size. */
void syn_tui_buildlog_redraw(syn_tui_buildlog *log);

/* Frees the panel's retained lines. Does not touch the screen contents
 * or call endwin() — caller draws whatever comes next (e.g.
 * syn_tui_message() for the build result) over it. */
void syn_tui_buildlog_close(syn_tui_buildlog *log);

#endif
