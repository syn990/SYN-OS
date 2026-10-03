#include "SynIcons.h"
#include "Archive.h"

#include <QHash>
#include <QMimeDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QSet>

namespace SynIcons {

namespace {

Kind kindForMime(const QMimeType &m)
{
  const QString n = m.name();
  auto any = [&n](std::initializer_list<const char *> names) {
    for (const char *x : names)
      if (n == QLatin1String(x))
        return true;
    return false;
  };
  auto starts = [&n](const char *prefix) { return n.startsWith(QLatin1String(prefix)); };

  if (starts("image/"))
    return Kind::Image;
  if (starts("audio/"))
    return Kind::Audio;
  if (starts("video/"))
    return Kind::Video;
  if (starts("font/") || starts("application/x-font") || n == QLatin1String("application/vnd.ms-opentype"))
    return Kind::Font;
  if (any({"application/vnd.debian.binary-package", "application/x-deb", "application/x-rpm",
           "application/java-archive", "application/x-java-archive",
           "application/vnd.android.package-archive", "application/vnd.flatpak",
           "application/vnd.appimage", "application/x-xpinstall"}))
    return Kind::Package;
  if (any({"application/vnd.efi.iso", "application/x-cd-image", "application/x-iso9660-image",
           "application/x-raw-disk-image", "application/x-qemu-disk", "application/vnd.efi.img",
           "application/x-virtualbox-vdi", "application/x-virtualbox-vmdk",
           "application/x-apple-diskimage"}))
    return Kind::Disc;
  if (Archive::isArchiveMime(n) || Archive::isCompressedFileMime(n))
    return Kind::Archive;
  if (n == QLatin1String("application/pdf") || n == QLatin1String("application/epub+zip")
      || n == QLatin1String("application/msword") || n == QLatin1String("application/rtf")
      || n == QLatin1String("application/postscript") || starts("application/vnd.ms-")
      || starts("application/vnd.oasis.opendocument.")
      || starts("application/vnd.openxmlformats-officedocument."))
    return Kind::Document;
  if (starts("application/pgp") || n.contains(QLatin1String("x509")) || starts("application/pkix")
      || starts("application/x-pkcs") || n == QLatin1String("application/x-pem-file"))
    return Kind::Key;
  if (n.contains(QLatin1String("shellscript")) || any({"text/x-zsh", "application/x-zsh",
                                                       "application/x-csh", "text/x-fish"}))
    return Kind::Script;
  if (any({"application/x-executable", "application/x-sharedlib", "application/x-pie-executable",
           "application/x-object", "application/x-msdownload", "application/x-ms-dos-executable",
           "application/vnd.microsoft.portable-executable", "application/x-msi",
           "application/x-core"}))
    return Kind::Binary;
  if (any({"application/x-desktop", "text/x-systemd-unit", "application/x-wine-extension-ini"}))
    return Kind::Config;
  if (any({"application/json", "application/xml", "application/yaml", "application/toml",
           "text/csv", "text/tab-separated-values", "application/vnd.sqlite3",
           "application/x-sqlite3"})
      || m.inherits(QStringLiteral("application/xml")) || m.inherits(QStringLiteral("application/json")))
    return Kind::Data;
  if (n == QLatin1String("text/plain") || n == QLatin1String("text/markdown")
      || n == QLatin1String("text/x-log") || n == QLatin1String("text/x-readme"))
    return Kind::Text;
  if (starts("text/x-") || any({"application/javascript", "application/x-perl", "application/x-php",
                                "application/x-ruby", "application/sql", "application/x-cmakecache"}))
    return Kind::Code;
  if (m.inherits(QStringLiteral("text/plain")))
    return Kind::Text;
  return Kind::File;
}

} // namespace

Kind kindFor(const QString &fileName, bool dir, bool exec, bool special)
{
  if (dir)
    return Kind::Folder;
  if (special)
    return Kind::Special;

  const QString lower = fileName.toLower();
  static const QHash<QString, Kind> byName = {
    {"makefile", Kind::Code}, {"cmakelists.txt", Kind::Code}, {"dockerfile", Kind::Code},
    {"meson.build", Kind::Code}, {"pkgbuild", Kind::Script}, {"license", Kind::Text},
    {"copying", Kind::Text}, {"readme", Kind::Text}, {"authors", Kind::Text},
    {"id_rsa", Kind::Key}, {"id_ed25519", Kind::Key}, {"id_ecdsa", Kind::Key},
    {"authorized_keys", Kind::Key}, {"known_hosts", Kind::Key},
  };
  if (const auto it = byName.constFind(lower); it != byName.constEnd())
    return it.value();
  if (lower.contains(QLatin1String(".pkg.tar.")))
    return Kind::Package;

  const int dot = lower.lastIndexOf(QLatin1Char('.'));
  // .zshrc, .gitconfig: a dotfile with nothing after its one dot.
  if (dot == 0)
    return Kind::Config;
  if (dot < 0)
    return exec ? Kind::Binary : Kind::File;

  static const QHash<QString, Kind> bySuffix = {
    {"conf", Kind::Config}, {"cfg", Kind::Config}, {"ini", Kind::Config}, {"rc", Kind::Config},
    {"service", Kind::Config}, {"timer", Kind::Config}, {"socket", Kind::Config},
    {"mount", Kind::Config}, {"target", Kind::Config}, {"theme", Kind::Config},
    {"tmpl", Kind::Config}, {"rules", Kind::Config}, {"jsonc", Kind::Data},
    {"key", Kind::Key}, {"pem", Kind::Key}, {"crt", Kind::Key}, {"asc", Kind::Key},
    {"gpg", Kind::Key}, {"sig", Kind::Key}, {"pub", Kind::Key},
    {"zsh", Kind::Script}, {"sh", Kind::Script}, {"bash", Kind::Script}, {"fish", Kind::Script},
    {"img", Kind::Disc}, {"qcow2", Kind::Disc}, {"vdi", Kind::Disc}, {"vmdk", Kind::Disc},
  };
  const QString suffix = lower.mid(dot + 1);
  Kind kind;
  if (const auto it = bySuffix.constFind(suffix); it != bySuffix.constEnd()) {
    kind = it.value();
  } else {
    // By extension only (never reading the file): this runs for every row
    // painted, so the answer is cached per suffix.
    static QHash<QString, Kind> cache;
    auto c = cache.constFind(suffix);
    if (c == cache.constEnd())
      c = cache.insert(suffix, kindForMime(QMimeDatabase().mimeTypeForFile(
                                 fileName, QMimeDatabase::MatchExtension)));
    kind = c.value();
  }
  if (exec && kind == Kind::File)
    return Kind::Binary;
  if (exec && kind == Kind::Text)
    return Kind::Script;
  return kind;
}

void paint(QPainter *p, const QRectF &box, Kind kind, const QColor &line,
           const QColor &accent, bool symlink)
{
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);
  p->translate(box.topLeft());
  p->scale(box.width(), box.height());

  const qreal px = qMax(1.0, box.width() / 14.0); // stroke, in device pixels
  QPen linePen(line, px);
  linePen.setCosmetic(true);
  linePen.setJoinStyle(Qt::MiterJoin);
  QPen accentPen(accent, px);
  accentPen.setCosmetic(true);
  accentPen.setJoinStyle(Qt::MiterJoin);
  accentPen.setCapStyle(Qt::FlatCap);
  QColor accentFill = accent;
  accentFill.setAlphaF(accent.alphaF() * 0.35);

  auto lines = [&](const QPen &pen, std::initializer_list<QLineF> ls) {
    p->setPen(pen);
    for (const QLineF &l : ls)
      p->drawLine(l);
  };
  auto poly = [&](std::initializer_list<QPointF> pts, bool closed) {
    QPainterPath path;
    auto it = pts.begin();
    path.moveTo(*it);
    for (++it; it != pts.end(); ++it)
      path.lineTo(*it);
    if (closed)
      path.closeSubpath();
    return path;
  };

  // The sheet: every file kind but packages and discs.
  const QPainterPath sheet = poly({{0.18, 0.06}, {0.60, 0.06}, {0.84, 0.30}, {0.84, 0.94},
                                   {0.18, 0.94}}, true);

  switch (kind) {
  case Kind::Folder: {
    // The tab is cut at 45 degrees, the same cut as the sheet's corner.
    const QPainterPath folder = poly({{0.06, 0.20}, {0.38, 0.20}, {0.48, 0.32}, {0.94, 0.32},
                                      {0.94, 0.86}, {0.06, 0.86}}, true);
    p->fillPath(folder, accentFill);
    p->setPen(accentPen);
    p->drawPath(folder);
    lines(accentPen, {QLineF(0.06, 0.44, 0.94, 0.44)});
    break;
  }
  case Kind::Package: {
    const QPainterPath cube = poly({{0.50, 0.08}, {0.88, 0.28}, {0.88, 0.72}, {0.50, 0.92},
                                    {0.12, 0.72}, {0.12, 0.28}}, true);
    p->fillPath(poly({{0.50, 0.08}, {0.88, 0.28}, {0.50, 0.48}, {0.12, 0.28}}, true), accentFill);
    p->setPen(linePen);
    p->drawPath(cube);
    lines(accentPen, {{{0.12, 0.28}, {0.50, 0.48}}, {{0.50, 0.48}, {0.88, 0.28}},
                      {{0.50, 0.48}, {0.50, 0.92}}});
    break;
  }
  case Kind::Disc: {
    p->setPen(linePen);
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(QPointF(0.5, 0.5), 0.40, 0.40);
    p->setPen(accentPen);
    p->drawArc(QRectF(0.22, 0.22, 0.56, 0.56), 30 * 16, 70 * 16);
    p->setPen(Qt::NoPen);
    p->setBrush(accent);
    p->drawEllipse(QPointF(0.5, 0.5), 0.09, 0.09);
    break;
  }
  default: {
    if (kind == Kind::Special) {
      linePen.setStyle(Qt::DashLine);
      linePen.setDashPattern({2, 2});
    }
    p->setPen(linePen);
    p->setBrush(Qt::NoBrush);
    p->drawPath(sheet);

    // The mark, in the accent, inside the sheet's lower part.
    p->setBrush(Qt::NoBrush);
    switch (kind) {
    case Kind::Text:
      lines(accentPen, {{0.30, 0.44, 0.72, 0.44}, {0.30, 0.58, 0.72, 0.58},
                        {0.30, 0.72, 0.58, 0.72}});
      break;
    case Kind::Code:
      p->setPen(accentPen);
      p->drawPath(poly({{0.42, 0.46}, {0.30, 0.60}, {0.42, 0.74}}, false));
      p->drawPath(poly({{0.60, 0.46}, {0.72, 0.60}, {0.60, 0.74}}, false));
      break;
    case Kind::Script:
      p->setPen(accentPen);
      p->drawPath(poly({{0.30, 0.46}, {0.42, 0.58}, {0.30, 0.70}}, false));
      lines(accentPen, {{0.48, 0.74, 0.72, 0.74}});
      break;
    case Kind::Binary:
      p->setPen(accentPen);
      p->drawRect(QRectF(0.36, 0.46, 0.30, 0.30));
      lines(accentPen, {{0.28, 0.54, 0.36, 0.54}, {0.28, 0.68, 0.36, 0.68},
                        {0.66, 0.54, 0.74, 0.54}, {0.66, 0.68, 0.74, 0.68}});
      p->fillRect(QRectF(0.45, 0.55, 0.12, 0.12), accent);
      break;
    case Kind::Image:
      p->fillPath(poly({{0.28, 0.82}, {0.44, 0.56}, {0.55, 0.70}, {0.62, 0.62}, {0.76, 0.82}}, true),
                  accent);
      p->setPen(Qt::NoPen);
      p->setBrush(accent);
      p->drawEllipse(QPointF(0.66, 0.44), 0.06, 0.06);
      break;
    case Kind::Audio: {
      const qreal h[] = {0.14, 0.30, 0.20, 0.36, 0.12};
      for (int i = 0; i < 5; ++i)
        p->fillRect(QRectF(0.29 + i * 0.09, 0.64 - h[i] / 2, 0.05, h[i]), accent);
      break;
    }
    case Kind::Video:
      p->fillPath(poly({{0.38, 0.44}, {0.38, 0.80}, {0.70, 0.62}}, true), accent);
      break;
    case Kind::Archive:
      // A zip running down the sheet, its pull at the bottom.
      for (int i = 0; i < 6; ++i)
        p->fillRect(QRectF(i % 2 ? 0.51 : 0.45, 0.10 + i * 0.09, 0.06, 0.05), accent);
      p->fillRect(QRectF(0.43, 0.66, 0.16, 0.18), accent);
      break;
    case Kind::Document:
      p->fillRect(QRectF(0.28, 0.38, 0.46, 0.10), accent);
      lines(accentPen, {{0.30, 0.60, 0.72, 0.60}, {0.30, 0.74, 0.62, 0.74}});
      break;
    case Kind::Data:
      p->setPen(accentPen);
      p->drawRect(QRectF(0.30, 0.44, 0.42, 0.36));
      lines(accentPen, {{0.30, 0.56, 0.72, 0.56}, {0.30, 0.68, 0.72, 0.68}, {0.46, 0.44, 0.46, 0.80}});
      break;
    case Kind::Config:
      lines(accentPen, {{0.28, 0.52, 0.74, 0.52}, {0.28, 0.72, 0.74, 0.72}});
      p->fillRect(QRectF(0.36, 0.46, 0.10, 0.12), accent);
      p->fillRect(QRectF(0.58, 0.66, 0.10, 0.12), accent);
      break;
    case Kind::Key:
      p->setPen(accentPen);
      p->drawEllipse(QPointF(0.38, 0.60), 0.10, 0.10);
      lines(accentPen, {{0.48, 0.60, 0.76, 0.60}, {0.66, 0.60, 0.66, 0.72}, {0.74, 0.60, 0.74, 0.68}});
      break;
    case Kind::Font:
      p->setPen(accentPen);
      p->drawPath(poly({{0.30, 0.82}, {0.51, 0.40}, {0.72, 0.82}}, false));
      lines(accentPen, {{0.39, 0.68, 0.63, 0.68}});
      break;
    case Kind::Special:
      p->setPen(Qt::NoPen);
      p->setBrush(accent);
      p->drawEllipse(QPointF(0.51, 0.62), 0.08, 0.08);
      break;
    default:
      break;
    }
    break;
  }
  }

  if (symlink) {
    // A link: an arrow out of the bottom-left corner.
    QPen arrow = accentPen;
    arrow.setWidthF(px * 1.4);
    p->setPen(arrow);
    p->drawLine(QLineF(0.04, 0.98, 0.34, 0.68));
    p->drawPath(poly({{0.16, 0.68}, {0.34, 0.68}, {0.34, 0.86}}, false));
  }
  p->restore();
}

} // namespace SynIcons
