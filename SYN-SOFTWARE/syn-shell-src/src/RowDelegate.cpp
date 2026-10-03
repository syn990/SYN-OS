#include "RowDelegate.h"
#include "ArchiveModel.h"
#include "SynIcons.h"
#include "GitStatus.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QPainter>
#include <QScrollBar>

namespace {

constexpr int kGutter = 10; // mark bar + breathing room before the name

bool g_icons = true;
int g_fields = RowDelegate::Size;
const GitStatus *g_git = nullptr;
const QHash<QString, qint64> *g_folderSizes = nullptr;
double g_backgroundAlpha = 1.0;

QString perms(bool dir, bool link, uint mode)
{
  QString s(link ? QLatin1Char('l') : dir ? QLatin1Char('d') : QLatin1Char('-'));
  if ((mode & 0777) == 0 && !link)
    return s + QStringLiteral("?????????"); // a folder an archive only implies
  const char rwx[] = "rwxrwxrwx";
  for (int i = 0; i < 9; ++i)
    s += (mode & (0400 >> i)) ? QLatin1Char(rwx[i]) : QLatin1Char('-');
  return s;
}

uint modeOf(const QFileInfo &info)
{
  const QFile::Permissions p = info.permissions();
  uint m = 0;
  if (p & QFile::ReadOwner) m |= 0400;
  if (p & QFile::WriteOwner) m |= 0200;
  if (p & QFile::ExeOwner) m |= 0100;
  if (p & QFile::ReadGroup) m |= 040;
  if (p & QFile::WriteGroup) m |= 020;
  if (p & QFile::ExeGroup) m |= 010;
  if (p & QFile::ReadOther) m |= 04;
  if (p & QFile::WriteOther) m |= 02;
  if (p & QFile::ExeOther) m |= 01;
  return m;
}

QString stamp(const QDateTime &t)
{
  return t.isValid() ? t.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("-");
}

// Where the name starts: past the gutter, and past the icon when shown.
int textStart(const QRect &row)
{
  return g_icons ? kGutter + row.height() + 2 : kGutter;
}

} // namespace

void RowDelegate::setShowIcons(bool show)
{
  g_icons = show;
}

void RowDelegate::setFields(int fields)
{
  g_fields = fields;
}

int RowDelegate::fields()
{
  return g_fields;
}

void RowDelegate::setGit(const GitStatus *git)
{
  g_git = git;
}

void RowDelegate::setFolderSizes(const QHash<QString, qint64> *sizes)
{
  g_folderSizes = sizes;
}

void RowDelegate::setBackgroundAlpha(double alpha)
{
  g_backgroundAlpha = alpha;
}

double RowDelegate::backgroundAlpha()
{
  return g_backgroundAlpha;
}

int RowDelegate::nameStart(int rowHeight)
{
  return textStart(QRect(0, 0, 1, rowHeight));
}

QList<RowDelegate::Column> RowDelegate::columns(const QFontMetrics &fm, int rowWidth, int start)
{
  // Widths from the widest thing each can hold, so every row lines up.
  auto chars = [&fm](int n) { return fm.horizontalAdvance(QString(n, QLatin1Char('M'))); };
  const QList<Column> all = {
    {Permissions, QObject::tr("PERMS"), 0, chars(10)},
    {Owner, QObject::tr("OWNER"), 0, chars(9)},
    {Size, QObject::tr("SIZE"), 0, chars(5)},
    {Modified, QObject::tr("MODIFIED"), 0, chars(16)},
    {Created, QObject::tr("CREATED"), 0, chars(16)},
  };
  const int gap = chars(2);
  const int right = rowWidth - 8;
  int shown = g_fields;
  // Too narrow: these go first, in this order, so the name keeps room.
  for (Field f : {Owner, Created, Permissions, Modified, Size}) {
    int total = 0;
    for (const Column &c : all)
      if (shown & c.field)
        total += c.width + gap;
    if (start + chars(10) + total <= right)
      break;
    shown &= ~f;
  }
  QList<Column> out;
  for (const Column &c : all)
    if (shown & c.field)
      out << c;
  int x = right;
  for (int i = int(out.size()) - 1; i >= 0; --i) {
    x -= out[i].width;
    out[i].left = x;
    x -= gap;
  }
  return out;
}

bool RowDelegate::showIcons()
{
  return g_icons;
}

QString humanSize(qint64 bytes)
{
  static const char units[] = "BKMGTP";
  double v = bytes;
  int i = 0;
  while (v >= 1024.0 && i < 5) {
    v /= 1024.0;
    ++i;
  }
  if (i == 0)
    return QString::number(bytes) + QLatin1Char('B');
  return QString::number(v, 'f', v < 10.0 ? 1 : 0) + QLatin1Char(units[i]);
}

void RowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
  // A disk row carries a QFileInfo; an archive row carries EntryRole data.
  QString name, linkTarget, owner, absPath;
  bool dir, link, exec, encrypted = false, special = false;
  qint64 size;
  uint mode = 0;
  QDateTime modified, created;
  const QVariant fileInfo = index.data(QFileSystemModel::FileInfoRole);
  if (fileInfo.isValid()) {
    const QFileInfo info = fileInfo.value<QFileInfo>();
    absPath = info.absoluteFilePath();
    if (m_detailed && g_fields) {
      mode = modeOf(info);
      if (g_fields & Owner)
        owner = info.owner();
      modified = info.lastModified();
      if (g_fields & Created)
        created = info.birthTime(); // statx; invalid where the filesystem keeps none
    }
    name = info.fileName().isEmpty() ? info.filePath() : info.fileName();
    dir = info.isDir();
    link = info.isSymLink();
    linkTarget = link ? info.symLinkTarget() : QString();
    exec = info.isExecutable();
    size = info.size();
    special = !link && info.exists() && !info.isFile() && !dir;
  } else {
    name = index.data().toString();
    dir = index.data(EntryRole::IsDir).toBool();
    linkTarget = index.data(EntryRole::Link).toString();
    link = !linkTarget.isEmpty();
    exec = !dir && (index.data(EntryRole::Mode).toUInt() & 0111);
    size = index.data(EntryRole::Size).toLongLong();
    encrypted = index.data(EntryRole::Encrypted).toBool();
    mode = index.data(EntryRole::Mode).toUInt();
    modified = index.data(EntryRole::MTime).toDateTime();
  }

  const QPalette &pal = option.palette;
  const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
  const bool cursor = view && view->currentIndex() == index;
  const bool marked = option.state & QStyle::State_Selected;
  const QRect r = option.rect;

  painter->save();

  if (cursor)
    painter->fillRect(r, pal.highlight());
  else if (option.state & QStyle::State_MouseOver)
    painter->fillRect(r, pal.button());

  if (marked)
    painter->fillRect(QRect(r.left(), r.top(), 3, r.height()),
                      cursor ? QColor(Qt::white) : pal.highlight().color());

  QColor fg = cursor ? pal.highlightedText().color() : pal.text().color();
  const QColor accent = cursor ? pal.highlightedText().color() : pal.highlight().color();
  QColor dim = fg;
  dim.setAlphaF(0.5);
  GitStatus::Mark git;
  if (m_detailed && g_git && !absPath.isEmpty())
    git = g_git->markFor(absPath);
  if ((name.startsWith(QLatin1Char('.')) || git.ignored) && !cursor)
    fg.setAlphaF(git.ignored ? 0.4 : 0.55); // ignored by git: quieter still

  QString suffix;
  if (link)
    suffix = QStringLiteral("@");
  else if (dir)
    suffix = QStringLiteral("/");
  else if (exec)
    suffix = QStringLiteral("*");

  QString right;
  if (link)
    right = QStringLiteral("-> ") + linkTarget;
  else if (!dir)
    right = size >= 0 ? humanSize(size) : QStringLiteral("?");
  if (encrypted)
    right = QStringLiteral("enc  ") + right;

  QFont font = option.font;
  font.setBold(dir);
  painter->setFont(font);
  const QFontMetrics fm(font);

  if (g_icons) {
    const int side = r.height() - 4;
    QColor line = fg;
    line.setAlphaF(fg.alphaF() * 0.8);
    SynIcons::paint(painter, QRectF(r.left() + kGutter - 2, r.top() + 2, side, side),
                    SynIcons::kindFor(link ? QFileInfo(linkTarget).fileName() : name, dir, exec,
                                      special),
                    line, accent, link); // a link looks like what it points at
  }

  QRect text = r.adjusted(textStart(r), 0, -8, 0);

  if (m_detailed) {
    // Fields in their columns; the link target and [enc] move in beside
    // the name, where the size alone used to sit.
    QFont plain = option.font;
    const QFontMetrics pfm(plain);
    const QList<Column> cols = columns(pfm, r.width(), textStart(r));
    painter->setFont(plain);
    painter->setPen(dim);
    for (const Column &c : cols) {
      QString value;
      switch (c.field) {
      case Permissions: value = perms(dir, link, mode); break;
      case Owner: value = owner.isEmpty() ? QStringLiteral("-") : owner; break;
      case Size:
        if (dir)
          value = g_folderSizes && g_folderSizes->contains(absPath)
                    ? humanSize(g_folderSizes->value(absPath)) : QStringLiteral("-");
        else
          value = size >= 0 ? humanSize(size) : QStringLiteral("?");
        break;
      case Modified: value = stamp(modified); break;
      case Created: value = stamp(created); break;
      }
      const QRect cell(r.left() + c.left, r.top(), c.width, r.height());
      painter->drawText(cell, Qt::AlignVCenter | (c.field == Size ? Qt::AlignRight : Qt::AlignLeft),
                        pfm.elidedText(value, Qt::ElideRight, c.width));
    }
    const int nameRight = cols.isEmpty() ? text.right() : r.left() + cols.first().left - pfm.horizontalAdvance(QStringLiteral("  "));
    QRect nameRect = text;
    nameRect.setRight(nameRight);

    // Git: its own short-status letters (as `git status -s` prints them)
    // at the end of the name's room. Staged letter in the accent, the
    // worktree one plain, "??" new, a dot on a folder with changes in it.
    if (git.any() && !git.ignored) {
      const int slot = pfm.horizontalAdvance(QStringLiteral("MM")) + 10;
      const QRect badge(nameRect.right() - slot + 1, r.top(), slot, r.height());
      nameRect.setRight(badge.left() - 4);
      QFont b = plain;
      b.setBold(true);
      painter->setFont(b);
      const QFontMetrics bfm(b);
      const QColor white = pal.highlightedText().color();
      if (git.x == 'U' || git.y == 'U' || (git.x == 'A' && git.y == 'A') || (git.x == 'D' && git.y == 'D')) {
        painter->fillRect(badge.adjusted(2, 3, -2, -3), cursor ? white : accent);
        painter->setPen(cursor ? accent : white);
        painter->drawText(badge, Qt::AlignCenter, QStringLiteral("UU"));
      } else if (git.untracked) {
        painter->setPen(dim);
        painter->drawText(badge, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("??"));
      } else if (git.x != ' ' || git.y != ' ') {
        const int cw = bfm.horizontalAdvance(QLatin1Char('M'));
        const QRect xr(badge.right() - 2 * cw + 1, badge.top(), cw, badge.height());
        painter->setPen(cursor ? white : accent);
        painter->drawText(xr, Qt::AlignCenter, QString(QLatin1Char(git.x)));
        painter->setPen(fg);
        painter->drawText(xr.translated(cw, 0), Qt::AlignCenter, QString(QLatin1Char(git.y)));
      } else if (git.dirty) {
        painter->setPen(cursor ? white : accent);
        painter->drawText(badge, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("\u2022"));
      }
    }
    QString extra;
    if (link)
      extra = QStringLiteral(" -> ") + linkTarget;
    if (encrypted)
      extra += QStringLiteral("  [enc]");
    painter->setFont(font);
    const int suffixWidth = fm.horizontalAdvance(suffix);
    // The name gets the room first; whatever is left shows the extra.
    const QString shownName = fm.elidedText(name, Qt::ElideMiddle, nameRect.width() - suffixWidth);
    painter->setPen(fg);
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, shownName);
    const int afterName = fm.horizontalAdvance(shownName);
    painter->setPen(accent);
    painter->drawText(nameRect.adjusted(afterName, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, suffix);
    if (!extra.isEmpty()) {
      const QRect extraRect = nameRect.adjusted(afterName + suffixWidth, 0, 0, 0);
      painter->setFont(plain);
      painter->setPen(dim);
      painter->drawText(extraRect, Qt::AlignVCenter | Qt::AlignLeft,
                        pfm.elidedText(extra, Qt::ElideMiddle, extraRect.width()));
    }
    painter->restore();
    return;
  }

  int rightWidth = 0;
  if (!right.isEmpty()) {
    // A link target can be long; it never gets more than half the row.
    const QString shown = fm.elidedText(right, Qt::ElideMiddle, text.width() / 2);
    rightWidth = fm.horizontalAdvance(shown) + 12;
    painter->setPen(dim);
    painter->drawText(text, Qt::AlignVCenter | Qt::AlignRight, shown);
  }

  const QRect nameRect = text.adjusted(0, 0, -rightWidth, 0);
  const int suffixWidth = fm.horizontalAdvance(suffix);
  const QString shownName = fm.elidedText(name, Qt::ElideMiddle, nameRect.width() - suffixWidth);
  painter->setPen(fg);
  painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, shownName);
  painter->setPen(accent);
  painter->drawText(nameRect.adjusted(fm.horizontalAdvance(shownName), 0, 0, 0),
                    Qt::AlignVCenter | Qt::AlignLeft, suffix);

  painter->restore();
}

QSize RowDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
  return QSize(100, option.fontMetrics.height() + 6);
}

void RowDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                       const QModelIndex &) const
{
  editor->setGeometry(option.rect.adjusted(textStart(option.rect) - 3, 0, 0, 0));
}

FieldHeader::FieldHeader(QAbstractItemView *view, QWidget *parent)
  : QWidget(parent), m_view(view)
{
  setObjectName("fieldHeader");
}

QSize FieldHeader::sizeHint() const
{
  return QSize(100, fontMetrics().height() + 6);
}

void FieldHeader::paintEvent(QPaintEvent *)
{
  // The same layout the rows use, against the same width (the view's
  // viewport, so a scrollbar is allowed for) and the same row height.
  QPainter p(this);
  QColor base = palette().color(QPalette::Base);
  base.setAlphaF(float(g_backgroundAlpha));
  p.setCompositionMode(QPainter::CompositionMode_Source);
  p.fillRect(rect(), base);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  const QFontMetrics fm(font());
  const int rowHeight = fm.height() + 6;
  const int start = RowDelegate::nameStart(rowHeight);
  p.setPen(palette().color(QPalette::Highlight));
  QFont bold = font();
  bold.setBold(true);
  p.setFont(bold);
  const QRect row(0, 0, m_view->viewport()->width(), height());
  p.drawText(row.adjusted(start, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("NAME"));
  for (const RowDelegate::Column &c : RowDelegate::columns(fm, row.width(), start))
    p.drawText(QRect(c.left, 0, c.width, height()),
               Qt::AlignVCenter | (c.field == RowDelegate::Size ? Qt::AlignRight : Qt::AlignLeft),
               c.title);
}
