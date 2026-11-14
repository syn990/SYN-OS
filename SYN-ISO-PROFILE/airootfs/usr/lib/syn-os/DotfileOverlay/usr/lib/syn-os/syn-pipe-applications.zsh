#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                 S Y N - P I P E - A P P L I C A T I O N S
#
#   The menu's Applications list: xdg_menu's openbox3 pipe menu (every
#   installed desktop entry, by category). xdg_menu resolves each icon to a
#   file in hicolor or 16px AdwaitaLegacy (it hardcodes both); where Papirus
#   has an icon of that name, the path becomes the bare name so labwc draws
#   it from the icon theme in rc.xml <icon> at the menu's own size. Icons
#   Papirus lacks (SYN-SHELL's own, say) keep their path.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-APPLICATIONS (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
xdg_menu --format openbox3-pipe --root-menu /etc/xdg/menus/arch-applications.menu 2>/dev/null \
  | perl -pe '
      BEGIN { %p = map { s{.*/}{}; s{\.svg$}{}; ($_ => 1) } glob "/usr/share/icons/Papirus/48x48/*/*.svg" }
      s{icon="[^"]*/([^/"]+)\.(?:png|svg|svgz|xpm)"}{ $p{$1} ? qq(icon="$1") : $& }ge'
