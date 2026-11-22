#!/usr/bin/env python3
# ------------------------------------------------------------------------
#   Writes src/space/icons_data.c from assets/space-icons: each toolbar icon's alpha,
#   at 24 and 48 pixels, so the program stays one file and tints them with
#   the theme. Run again after an icon changes:
#       python3 cmake/icons2c.py assets/space-icons src/space/icons_data.c
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-SHELL (Disk space)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
import sys
from PIL import Image

NAMES = ["disks", "up", "rescan", "blocks", "graph", "flat", "solid", "theme", "zoomin", "zoomout", "reset"]
src, out = sys.argv[1], sys.argv[2]
lines = ["/* Written by cmake/icons2c.py from assets/space-icons; do not edit. */",
         '#include "icons.h"', ""]
for size, suffix in ((24, ""), (48, "@2x")):
    lines.append(f"const unsigned char ICON_{size}[ICON_COUNT][{size * size}] = {{")
    for name in NAMES:
        im = Image.open(f"{src}/{name}{suffix}.png").convert("RGBA")
        assert im.size == (size, size), (name, im.size)
        alpha = list(im.getchannel("A").tobytes())
        body = ",".join(str(a) for a in alpha)
        lines.append(f"\t/* {name} */ {{{body}}},")
    lines.append("};")
    lines.append("")
open(out, "w").write("\n".join(lines))
