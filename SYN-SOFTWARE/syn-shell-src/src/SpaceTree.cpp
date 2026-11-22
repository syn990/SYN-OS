// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   SpaceTree: see SpaceTree.h.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "SpaceTree.h"

#include <QDateTime>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>

namespace {

// The share column: a bar as long as the share, the figure over it.
class ShareDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
  {
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);
    const QString text = o.text;
    o.text.clear();
    QStyledItemDelegate::paint(p, o, idx); // the row's background and selection
    const double share = idx.data(SpaceTreeModel::ShareRole).toDouble();
    const QRect r = opt.rect.adjusted(3, 3, -3, -3);
    p->save();
    p->fillRect(r, opt.palette.color(QPalette::Base));
    p->fillRect(QRect(r.left(), r.top(), int(r.width() * qBound(0.0, share, 1.0)), r.height()),
                opt.palette.color(QPalette::Highlight));
    p->setPen(opt.palette.color(QPalette::Text));
    p->drawText(r.adjusted(0, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, text);
    p->restore();
  }
};

QPixmap swatch(const unsigned char rgb[3])
{
  static QHash<quint32, QPixmap> cache;
  const quint32 key = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
  auto it = cache.find(key);
  if (it != cache.end())
    return *it;
  QPixmap px(10, 10);
  px.fill(QColor(rgb[0], rgb[1], rgb[2]));
  return *cache.insert(key, px);
}

} // namespace

// ------------------------------------------------------------------ model

SpaceTreeModel::SpaceTreeModel(SpaceView *space, QObject *parent)
  : QAbstractItemModel(parent), m_space(space)
{
}

void SpaceTreeModel::drop()
{
  beginResetModel();
  m_root = nullptr;
  m_kids.clear();
  m_row.clear();
  endResetModel();
}

const QVector<Node *> &SpaceTreeModel::kids(const Node *n) const
{
  auto it = m_kids.find(n);
  if (it != m_kids.end())
    return *it;
  QVector<Node *> v;
  space_lock(m_space->app());
  v.reserve(n->nkids);
  for (int i = 0; i < n->nkids; ++i)
    v << n->kids[i];
  space_unlock(m_space->app());
  for (int i = 0; i < v.size(); ++i)
    m_row.insert(v[i], i);
  return *m_kids.insert(n, v);
}

void SpaceTreeModel::refresh()
{
  App *app = m_space->app();
  const unsigned gen = space_generation(app);
  if (gen != m_gen) {
    beginResetModel();
    m_kids.clear();
    m_row.clear();
    space_lock(app);
    m_root = space_root(app);
    space_unlock(app);
    m_gen = gen;
    endResetModel();
    return;
  }
  if (!m_root)
    return;

  // The same nodes, in the order the scan has them now, and any new ones.
  emit layoutAboutToBeChanged();
  const QModelIndexList before = persistentIndexList();
  QVector<Node *> nodes;
  nodes.reserve(before.size());
  for (const QModelIndex &i : before)
    nodes << nodeOf(i);
  space_lock(app);
  for (auto it = m_kids.begin(); it != m_kids.end(); ++it) {
    const Node *n = it.key();
    it->resize(n->nkids);
    for (int i = 0; i < n->nkids; ++i)
      (*it)[i] = n->kids[i];
  }
  space_unlock(app);
  m_row.clear();
  for (auto it = m_kids.cbegin(); it != m_kids.cend(); ++it)
    for (int i = 0; i < it->size(); ++i)
      m_row.insert(it->at(i), i);
  QModelIndexList after;
  after.reserve(before.size());
  for (int i = 0; i < before.size(); ++i)
    after << indexOf(nodes[i], before[i].column());
  changePersistentIndexList(before, after);
  emit layoutChanged();
}

Node *SpaceTreeModel::nodeOf(const QModelIndex &idx) const
{
  return idx.isValid() ? static_cast<Node *>(idx.internalPointer()) : nullptr;
}

QModelIndex SpaceTreeModel::indexOf(Node *n, int column) const
{
  if (!n || !m_root)
    return {};
  if (n == m_root)
    return createIndex(0, column, n);
  if (!m_row.contains(n) && n->parent)
    kids(n->parent);
  const int row = m_row.value(n, -1);
  return row < 0 ? QModelIndex() : createIndex(row, column, n);
}

QString SpaceTreeModel::pathOf(const Node *n) const
{
  char buf[4096];
  node_path(n, buf, sizeof buf);
  return QString::fromUtf8(buf);
}

QModelIndex SpaceTreeModel::index(int row, int column, const QModelIndex &parent) const
{
  if (!m_root || row < 0 || column < 0 || column >= ColumnCount)
    return {};
  if (!parent.isValid())
    return row == 0 ? createIndex(0, column, m_root) : QModelIndex();
  const QVector<Node *> &v = kids(nodeOf(parent));
  return row < v.size() ? createIndex(row, column, v[row]) : QModelIndex();
}

QModelIndex SpaceTreeModel::parent(const QModelIndex &child) const
{
  Node *n = nodeOf(child);
  if (!n || n == m_root)
    return {};
  return indexOf(n->parent);
}

int SpaceTreeModel::rowCount(const QModelIndex &parent) const
{
  if (!parent.isValid())
    return m_root ? 1 : 0;
  if (parent.column() != 0)
    return 0;
  Node *n = nodeOf(parent);
  return n->is_dir ? int(kids(n).size()) : 0;
}

bool SpaceTreeModel::hasChildren(const QModelIndex &parent) const
{
  if (!parent.isValid())
    return m_root != nullptr;
  if (parent.column() != 0)
    return false;
  const Node *n = nodeOf(parent);
  if (!n->is_dir)
    return false;
  auto it = m_kids.find(n);
  return it != m_kids.end() ? !it->isEmpty() : n->nkids > 0;
}

QVariant SpaceTreeModel::data(const QModelIndex &idx, int role) const
{
  const Node *n = nodeOf(idx);
  if (!n)
    return {};
  const int col = idx.column();
  const double share = n->parent && n->parent->size ? double(n->size) / double(n->parent->size) : 1.0;
  switch (role) {
  case Qt::DisplayRole:
    switch (col) {
    case Name:
      return n == m_root ? QString::fromUtf8(n->name) : QString::fromUtf8(n->name) + (n->is_dir ? QStringLiteral("/") : QString());
    case Size: {
      char buf[32];
      format_size(n->size, buf, sizeof buf);
      return QString::fromUtf8(buf);
    }
    case Share: return QString::number(share * 100.0, 'f', 1) + QLatin1Char('%');
    case Files: return QString::number(qulonglong(n->files));
    case Changed:
      return n->mtime > 0 ? QDateTime::fromSecsSinceEpoch(n->mtime).toString(QStringLiteral("yyyy-MM-dd")) : QString();
    }
    break;
  case ShareRole: return share;
  case Qt::TextAlignmentRole:
    return col == Name ? QVariant() : QVariant(Qt::AlignRight | Qt::AlignVCenter);
  case Qt::DecorationRole:
    if (col == Name) {
      unsigned char rgb[3];
      space_colour(m_space->app(), n, rgb);
      return swatch(rgb);
    }
    break;
  case Qt::ForegroundRole:
    if (n->unread) {
      unsigned char rgb[3];
      space_colour(m_space->app(), n, rgb); // the urgent colour
      return QColor(rgb[0], rgb[1], rgb[2]);
    }
    if (!n->done && col != Name)
      return QPalette().color(QPalette::Disabled, QPalette::Text); // still counting
    break;
  case Qt::ToolTipRole:
    return pathOf(n) + (n->unread ? tr("\n(could not be read)") : QString());
  }
  return {};
}

QVariant SpaceTreeModel::headerData(int section, Qt::Orientation o, int role) const
{
  if (o != Qt::Horizontal)
    return {};
  if (role == Qt::TextAlignmentRole)
    return section == Name ? QVariant(Qt::AlignLeft | Qt::AlignVCenter) : QVariant(Qt::AlignRight | Qt::AlignVCenter);
  if (role != Qt::DisplayRole)
    return {};
  static const char *names[ColumnCount] = {"Name", "Size", "Of parent", "Files", "Changed"};
  return tr(names[section]);
}

// ------------------------------------------------------------------- view

SpaceTree::SpaceTree(SpaceView *space, QWidget *parent)
  : QTreeView(parent), m_space(space), m_model(new SpaceTreeModel(space, this))
{
  setObjectName("spaceTree");
  setModel(m_model);
  setItemDelegateForColumn(SpaceTreeModel::Share, new ShareDelegate(this));
  setUniformRowHeights(true);
  setAllColumnsShowFocus(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setIndentation(12);
  setFocusPolicy(Qt::ClickFocus);
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(SpaceTreeModel::Name, QHeaderView::Stretch);
  for (int c = SpaceTreeModel::Size; c < SpaceTreeModel::ColumnCount; ++c)
    header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  header()->resizeSection(SpaceTreeModel::Share, 90);
  header()->setSectionResizeMode(SpaceTreeModel::Share, QHeaderView::Fixed);

  // The tree is freed straight after this: let go of it first.
  connect(space, &SpaceView::treeDropped, m_model, &SpaceTreeModel::drop, Qt::DirectConnection);
  connect(space, &SpaceView::treeChanged, this, &SpaceTree::onTreeChanged);
  auto pick = [this](const QModelIndex &idx) {
    if (Node *n = m_model->nodeOf(idx))
      emit picked(m_model->pathOf(n), n->is_dir);
  };
  connect(this, &QTreeView::clicked, this, pick);
  connect(this, &QTreeView::activated, this, pick);
  m_since.start();
  m_later.setSingleShot(true);
  connect(&m_later, &QTimer::timeout, this, &SpaceTree::onTreeChanged);
}

void SpaceTree::showEvent(QShowEvent *e)
{
  QTreeView::showEvent(e);
  m_since.invalidate(); // whatever it missed while hidden, now
  onTreeChanged();
}

void SpaceTree::onTreeChanged()
{
  const unsigned gen = space_generation(m_space->app());
  const bool fresh = gen != m_gen;
  if (!fresh && m_since.isValid() && m_since.elapsed() < 400) {
    if (!m_later.isActive()) // the live tree: a few times a second is plenty
      m_later.start(int(400 - m_since.elapsed()));
    return;
  }
  m_gen = gen;
  m_since.restart();
  if (!isVisible() && !fresh)
    return;
  m_model->refresh();
  if (fresh)
    expand(m_model->index(0, 0));
  reveal();
}

void SpaceTree::showPath(const QString &path)
{
  m_want = path;
  reveal();
}

void SpaceTree::reveal()
{
  if (m_want.isEmpty())
    return;
  App *app = m_space->app();
  space_lock(app);
  Node *n = space_find(app, m_want.toUtf8().constData());
  space_unlock(app);
  if (!n)
    return; // the scan has not got there yet: again when it grows
  const QModelIndex idx = m_model->indexOf(n);
  if (!idx.isValid())
    return;
  for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
    expand(p);
  if (n->is_dir)
    expand(idx);
  setCurrentIndex(idx);
  scrollTo(idx, QAbstractItemView::PositionAtCenter);
  m_want.clear();
}
