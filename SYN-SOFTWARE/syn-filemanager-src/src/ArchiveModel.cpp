#include "ArchiveModel.h"

#include <QFileInfo>

ArchiveSortProxy::ArchiveSortProxy(QObject *parent)
  : QSortFilterProxyModel(parent)
{
  m_collator.setNumericMode(true);
  m_collator.setCaseSensitivity(Qt::CaseInsensitive);
  setRecursiveFilteringEnabled(false);
}

void ArchiveSortProxy::setShowHidden(bool show)
{
  beginFilterChange();
  m_showHidden = show;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool ArchiveSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
  const bool leftDir = left.data(EntryRole::IsDir).toBool();
  if (leftDir != right.data(EntryRole::IsDir).toBool())
    return leftDir;
  return m_collator.compare(left.data().toString(), right.data().toString()) < 0;
}

bool ArchiveSortProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
  if (m_showHidden)
    return true;
  return !sourceModel()->index(row, 0, parent).data().toString().startsWith(QLatin1Char('.'));
}

ArchiveSession::ArchiveSession(const QString &file, const Archive::Listing &listing,
                               const QString &password)
  : m_file(file), m_password(password), m_listing(listing)
{
  const QFileInfo fi(file);
  m_fileSize = fi.size();
  m_fileTime = fi.lastModified();

  m_model = new QStandardItemModel;
  for (const Archive::Entry &e : listing.entries) {
    QStandardItem *item = e.dir ? ensureDir(e.path) : nullptr;
    if (!item) {
      // A file entry, or a duplicate path (tar can append a newer copy;
      // the last one is what extraction leaves on disk, so it wins).
      item = m_items.value(e.path);
      if (!item) {
        const int slash = e.path.lastIndexOf(QLatin1Char('/'));
        QStandardItem *parent = slash < 0 ? m_model->invisibleRootItem()
                                          : ensureDir(e.path.left(slash));
        item = new QStandardItem(e.path.mid(slash + 1));
        item->setEditable(false);
        parent->appendRow(item);
        m_items.insert(e.path, item);
      }
      item->setData(e.path, EntryRole::Path);
      item->setData(false, EntryRole::IsDir);
      m_totalSize += qMax<qint64>(0, e.size);
    }
    item->setData(e.size, EntryRole::Size);
    item->setData(e.link, EntryRole::Link);
    item->setData(e.mode, EntryRole::Mode);
    item->setData(e.mtime, EntryRole::MTime);
    item->setData(e.encrypted, EntryRole::Encrypted);
  }

  m_proxy = new ArchiveSortProxy;
  m_proxy->setSourceModel(m_model);
  m_proxy->sort(0, Qt::AscendingOrder);
}

ArchiveSession::~ArchiveSession()
{
  delete m_proxy;
  delete m_model;
}

QStandardItem *ArchiveSession::ensureDir(const QString &path)
{
  if (QStandardItem *existing = m_items.value(path)) {
    if (existing->data(EntryRole::IsDir).toBool())
      return existing;
  }
  const int slash = path.lastIndexOf(QLatin1Char('/'));
  QStandardItem *parent = slash < 0 ? m_model->invisibleRootItem() : ensureDir(path.left(slash));
  QStandardItem *item = m_items.value(path);
  if (!item) {
    item = new QStandardItem(path.mid(slash + 1));
    item->setEditable(false);
    parent->appendRow(item);
    m_items.insert(path, item);
  }
  item->setData(path, EntryRole::Path);
  item->setData(true, EntryRole::IsDir);
  item->setData(qint64(-1), EntryRole::Size);
  return item;
}

bool ArchiveSession::isStale() const
{
  const QFileInfo fi(m_file);
  return !fi.exists() || fi.size() != m_fileSize || fi.lastModified() != m_fileTime;
}

QModelIndex ArchiveSession::indexOf(const QString &path) const
{
  if (path.isEmpty())
    return QModelIndex();
  QStandardItem *item = m_items.value(path);
  return item ? m_proxy->mapFromSource(item->index()) : QModelIndex();
}

bool ArchiveSession::isDir(const QString &path) const
{
  if (path.isEmpty())
    return true;
  QStandardItem *item = m_items.value(path);
  return item && item->data(EntryRole::IsDir).toBool();
}

QString ArchiveSession::pathOf(const QModelIndex &idx)
{
  return idx.data(EntryRole::Path).toString();
}
