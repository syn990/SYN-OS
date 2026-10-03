#include "RowDelegate.h"
#include "ArchiveModel.h"
#include "SynIcons.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QPainter>

namespace {

constexpr int kGutter = 10; // mark bar + breathing room before the name

bool g_icons = true;

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
  QString name, linkTarget;
  bool dir, link, exec, encrypted = false, special = false;
  qint64 size;
  const QVariant fileInfo = index.data(QFileSystemModel::FileInfoRole);
  if (fileInfo.isValid()) {
    const QFileInfo info = fileInfo.value<QFileInfo>();
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
  if (name.startsWith(QLatin1Char('.')) && !cursor)
    fg.setAlphaF(0.55);

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
