// ------------------------------------------------------------------------------
//                     S Y N - F I L E M A N A G E R
//
//   FileSortProxy: the order every column lists in. Directories first,
//   then names in natural order (file2 before file10, case ignored), the
//   way ranger sorts. A proxy rather than QFileSystemModel::sort(), which
//   only ever re-sorts the one folder the model treats as its root: the
//   parent and preview columns list other folders and stayed in readdir
//   order. Also maps between view indexes and paths for its callers.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-FILEMANAGER (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QCollator>
#include <QFileInfo>
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

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
  QCollator m_collator;
};
