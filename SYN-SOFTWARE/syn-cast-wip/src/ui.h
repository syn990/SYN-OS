/* ------------------------------------------------------------------------
 *   The window: a native Wayland xdg-shell toplevel drawn with pixman and
 *   fcft (Terminus, like foot), coloured from the live SYN-OS theme. It
 *   reads app state and calls app_* actions; nothing else.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_UI_H
#define SYN_CAST_UI_H

#include <stdbool.h>

int ui_init(void);
void ui_shutdown(void);
bool ui_running(void);

/* poll plumbing */
int ui_display_fd(void);
int ui_repeat_fd(void);
int ui_clip_fd(void);           /* a paste being read, or -1 */
void ui_before_poll(void);
void ui_after_poll(short display_revents);
void ui_on_repeat(void);
void ui_on_clip(void);
int ui_timeout_ms(void);
void ui_frame(void);

#endif
