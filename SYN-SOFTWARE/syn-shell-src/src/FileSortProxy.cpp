#include "FileSortProxy.h"
#ifdef Q_OS_WIN
#include "FileJobs.h"
#endif
#include "SortOrder.h"

FileSortProxy::FileSortProxy(QFileSystemModel *source, QObject *parent)
  : QSortFilterProxyModel(parent)
{
  m_collator.setNumericMode(true);
  m_collator.setCaseSensitivity(Qt::CaseInsensitive);
  setSourceModel(source);
  setDynamicSortFilter(true); // rows that arrive or get renamed re-sort
  sort(0, Qt::AscendingOrder); // always: SortOrder reverses inside lessThan, past the folders
  SortOrder::track(this);
}

qint64 FileSortProxy::valueOf(const QModelIndex &idx) const
{
  switch (SortOrder::key()) {
  case SortOrder::Name: break;
  case SortOrder::Size:
    if (fs()->isDir(idx))
      return m_folderSizes ? m_folderSizes->value(fs()->filePath(idx), -1) : -1;
    return fs()->size(idx);
  case SortOrder::Modified: return SortOrder::stamp(fs()->lastModified(idx));
  case SortOrder::Created: return SortOrder::stamp(fs()->fileInfo(idx).birthTime());
  }
  return 0;
}

bool FileSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
  const bool leftDir = fs()->isDir(left);
  if (leftDir != fs()->isDir(right))
    return leftDir;
  if (SortOrder::key() != SortOrder::Name)
    if (const int c = SortOrder::compare(valueOf(left), valueOf(right)))
      return c < 0;
  return SortOrder::nameBefore(m_collator.compare(fs()->fileName(left), fs()->fileName(right)));
}
#ifdef Q_OS_WIN

bool FileSortProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
  const QModelIndex idx = fs()->index(row, 0, parent);
  const QString name = fs()->fileName(idx);
  return !((name.startsWith(QLatin1String("$I"))
            || name.compare(QLatin1String("desktop.ini"), Qt::CaseInsensitive) == 0)
           && FileJobs::isInTrash(fs()->filePath(idx)));
}
#endif
