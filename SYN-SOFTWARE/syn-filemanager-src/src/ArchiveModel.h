// ------------------------------------------------------------------------------
//                     S Y N - F I L E M A N A G E R
//
//   ArchiveModel: an archive's listing as a folder tree the same three
//   columns can walk. Directories an archive only implies (a tar holding
//   "a/b/c.txt" with no "a/" entry) are made up so every path has a
//   parent to stand in. ArchiveSortProxy orders it like FileSortProxy
//   orders the disk (folders first, natural names) and hides dotfiles
//   when the disk view does.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-FILEMANAGER (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include "Archive.h"

#include <QCollator>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

// Roles the row painter and the detail line read for an entry that has no
// QFileInfo (anything not on disk).
namespace EntryRole {
enum {
  Path = Qt::UserRole + 100, // path inside the archive
  IsDir,
  Size,     // qint64, -1 if the archive doesn't say
  Link,     // symlink target, empty otherwise
  Mode,     // uint st_mode bits
  MTime,    // QDateTime
  Encrypted,
};
}

class ArchiveSortProxy : public QSortFilterProxyModel
{
public:
  explicit ArchiveSortProxy(QObject *parent = nullptr);
  void setShowHidden(bool show);

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
  bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
  QCollator m_collator;
  bool m_showHidden = false;
};

// One opened archive: its listing, the tree built from it, and what's
// needed to come back to it (the password that worked, the file's
// size and mtime when read, so a changed file is read again).
class ArchiveSession
{
public:
  ArchiveSession(const QString &file, const Archive::Listing &listing, const QString &password);
  ~ArchiveSession();

  QString file() const { return m_file; }
  QString password() const { return m_password; }
  void setPassword(const QString &password) { m_password = password; }
  const Archive::Listing &listing() const { return m_listing; }
  ArchiveSortProxy *proxy() const { return m_proxy; }
  bool isStale() const;

  // "" is the archive's top level (an invalid index: the model root).
  QModelIndex indexOf(const QString &path) const;
  bool contains(const QString &path) const { return path.isEmpty() || m_items.contains(path); }
  bool isDir(const QString &path) const;
  static QString pathOf(const QModelIndex &idx);
  qint64 totalSize() const { return m_totalSize; }

private:
  QStandardItem *ensureDir(const QString &path);

  QString m_file;
  QString m_password;
  Archive::Listing m_listing;
  qint64 m_fileSize;
  QDateTime m_fileTime;
  qint64 m_totalSize = 0;
  QStandardItemModel *m_model;
  ArchiveSortProxy *m_proxy;
  QHash<QString, QStandardItem *> m_items;
};
