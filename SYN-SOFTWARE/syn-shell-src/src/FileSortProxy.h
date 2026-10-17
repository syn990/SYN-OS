// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   FileSortProxy: the order every column lists in. Directories first,
//   then by SortOrder's key: names in natural order (file2 before file10,
//   case ignored) the way ranger sorts, or size, modified, created. A
//   folder's size is the one du measured, if it has been. A proxy rather
//   than QFileSystemModel::sort(), which
//   only ever re-sorts the one folder the model treats as its root: the
//   parent and preview columns list other folders and stayed in readdir
//   order. Also maps between view indexes and paths for its callers.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QCollator>
#include <QFileInfo>
#include <QHash>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>

class FileSortProxy : public QSortFilterProxyModel
{
public:
  explicit FileSortProxy(QFileSystemModel *source, QObject *parent = nullptr);

  QFileSystemModel *fs() const { return static_cast<QFileSystemModel *>(sourceModel()); }
  QModelIndex indexOf(const QString &path) const { return mapFromSource(fs()->index(path)); }
  QString pathOf(const QModelIndex &idx) const { return fs()->filePath(mapToSource(idx)); }
  QFileInfo infoOf(const QModelIndex &idx) const { return fs()->fileInfo(mapToSource(idx)); }
  void setFolderSizes(const QHash<QString, qint64> *sizes) { m_folderSizes = sizes; }

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
#ifdef Q_OS_WIN
  // The Recycle Bin's bookkeeping ($I records, desktop.ini) isn't listed.
  bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
#endif

private:
  qint64 valueOf(const QModelIndex &idx) const;

  QCollator m_collator;
  const QHash<QString, qint64> *m_folderSizes = nullptr;
};
