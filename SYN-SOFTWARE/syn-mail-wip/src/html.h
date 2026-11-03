/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_HTML_H
#define SYN_MAIL_HTML_H
/* HTML mail as it was made (G265). The HTML part is written to a private folder (700, files 600) with its CID
 * parts beside it and every cid: reference pointed at them, under a guard: a Content-Security-Policy that lets
 * no script run and, unless remote is asked for, no image, style or font load from the network; DNS prefetch and
 * meta refresh are switched off. A headless Chrome draws it to a PNG for the window, through a proxy to nowhere
 * as a second lock on the network; o opens the same page in the operator's Chrome. */
#include "mime.h"

#include <stdint.h>

/* the guarded page; remote_refs gets how many remote images, styles or fonts it names (held back unless remote) */
void html_prepare(const MimeHtml *h, int remote, Buf *out, int *remote_refs);

/* a fresh private folder for one page, under $XDG_RUNTIME_DIR/syn-mail (else ~/.cache/syn-mail); kind is
 * "render" or "open"; 0 on success */
int html_dir(const char *kind, char *out, size_t n, char *err, size_t errn);
/* writes the guarded page and its parts into dir (from html_dir); the page's path goes to path */
int html_write(const MimeHtml *h, int remote, const char *dir, char *path, size_t pn, int *remote_refs, char *err,
	size_t errn);
/* removes a folder html_dir made, and everything in it */
void html_rmdir(const char *dir);
/* removes "open" folders older than an hour (the pages o handed to Chrome) */
void html_sweep(void);

/* the headless browser that draws pages: $SYN_MAIL_CHROME ("none" turns drawing off), else chromium,
 * google-chrome-stable or google-chrome on the PATH; NULL when there is none */
const char *html_renderer(void);
/* the browser o opens pages in: $SYN_MAIL_BROWSER, else google-chrome-stable, google-chrome or chromium */
const char *html_browser(void);

#define HTML_RENDER_MAX_H 10000 /* the tallest drawing, in pixels */
#define HTML_MIN_CSS_W 640       /* the narrowest layout width; a narrower drawing is the page scaled down */

/* draws the page at path width pixels wide (laid out at 640 and scaled when narrower) into png (the profile goes in a folder beside the page); the network is
 * shut off unless remote; 0 on success, killed after 30 s */
int html_render(const char *page, int width, int remote, const char *png, char *err, size_t errn);
/* opens the page in a new tab of the browser, detached; 0 when the browser started */
int html_open(const char *page, char *err, size_t errn);

/* a PNG as 0xAARRGGBB pixels, *h cut to drop the plain rows at the bottom (16 kept); *more is 1 when the page filled
 * the drawing to its last row, so it may go on past it; NULL on failure */
uint32_t *html_png_load(const char *path, int *w, int *h, int *more);
#endif
