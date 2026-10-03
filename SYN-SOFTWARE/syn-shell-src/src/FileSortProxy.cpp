#include "FileSortProxy.h"

FileSortProxy::FileSortProxy(QFileSystemModel *source, QObject *parent)
  : QSortFilterProxyModel(parent)
{
  m_collator.setNumericMode(true);
  m_collator.setCaseSensitivity(Qt::CaseInsensitive);
  setSourceModel(source);
  setDynamicSortFilter(true); // rows that arrive or get renamed re-sort
  sort(0, Qt::AscendingOrder);
}

bool FileSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
  const bool leftDir = fs()->isDir(left);
  if (leftDir != fs()->isDir(right))
    return leftDir;
  return m_collator.compare(fs()->fileName(left), fs()->fileName(right)) < 0;
}
