// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   SynIcons: SYN-OS's own file-type icons, drawn rather than loaded.
//   Flat and angular like the rest of the desktop: a sheet with its top
//   corner cut off at 45 degrees, a folder whose tab is cut the same way,
//   and a small mark inside in the theme's accent saying what kind of
//   file it is. Being paths, they're sharp at every zoom level and take
//   their colours from the palette, so a theme switch recolours them.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

namespace SynIcons {

enum class Kind {
  Folder,
  File,      // nothing more specific known
  Text,      // plain text, markdown, logs
  Code,      // source in any language
  Script,    // shell scripts
  Binary,    // ELF executables and libraries, .exe
  Image,
  Audio,
  Video,
  Archive,
  Package,   // .deb .rpm .pkg.tar.* .apk .jar
  Disc,      // .iso and disk images
  Document,  // pdf, office, epub
  Data,      // json xml yaml toml csv sqlite
  Config,    // conf ini rc .desktop .service, dotfiles
  Key,       // keys, certificates, signatures
  Font,
  Special,   // devices, fifos, sockets
};

// What to draw for a name. `exec` and `special` come from the file
// itself when known (on disk); inside an archive only the name counts.
Kind kindFor(const QString &fileName, bool dir, bool exec, bool special = false);

// Draws the icon filling `box` (a square). `line` is the outline colour,
// `accent` the mark's and the folder's.
void paint(QPainter *p, const QRectF &box, Kind kind, const QColor &line,
           const QColor &accent, bool symlink);

} // namespace SynIcons
