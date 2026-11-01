/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_UI_H
#define SYN_MAIL_UI_H
/* The SYN-Mail window (slice 2, G223): a native Wayland client drawn with pixman and fcft in the live SYN-OS theme,
 * the same way SYN-CAST draws. argv: "window" [--shot FILE.png] [--size WxH] [--open N]. --shot draws one frame
 * offscreen, once the first folder and message have loaded, and saves it as a PNG; no compositor is needed.
 * SYN_MAIL_DRAWLOG=FILE appends every string drawn, one per line, so tests can see what the window showed. */
int ui_main(int argc, char **argv);
#endif
