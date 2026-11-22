/* ------------------------------------------------------------------------
 *   The toolbar's icons, built in (cmake/icons2c.py writes icons_data.c
 *   from assets/space-icons): one alpha mask each, white where it is drawn, at
 *   24 and 48 pixels, tinted with the theme when drawn.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_ICONS_H
#define SYN_SHELL_SPACE_ICONS_H

enum { ICON_DISKS, ICON_UP, ICON_RESCAN, ICON_BLOCKS, ICON_GRAPH, ICON_FLAT, ICON_SOLID, ICON_THEME, ICON_ZOOMIN, ICON_ZOOMOUT, ICON_RESET, ICON_COUNT };

extern const unsigned char ICON_24[ICON_COUNT][24 * 24];
extern const unsigned char ICON_48[ICON_COUNT][48 * 48];

#endif
