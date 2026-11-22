// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   SpaceTree: the left-hand column's other face (zt). The folder tree
//   SpaceView's scan builds, biggest first, with each one's size, its share
//   of the folder above (as a bar), how many files are under it and when
//   anything under it last changed. Sizes still being counted are dimmed;
//   a folder that could not be read is in the urgent colour.
//
//   The model reads the scan's tree while the scan thread grows it: each
//   node's children are copied out under the tree's lock when first asked
//   for, and copied again on refresh(), so the view always sees a list
//   that holds still between refreshes.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QTreeView>
#include <QVector>

#include "SpaceView.h"

class SpaceTreeModel : public QAbstractItemModel
{
  Q_OBJECT

public:
  enum Column { Name, Size, Share, Files, Changed, ColumnCount };
  static constexpr int ShareRole = Qt::UserRole + 1;

  SpaceTreeModel(SpaceView *space, QObject *parent = nullptr);

  void drop();    // the tree is about to be freed
  void refresh(); // a new tree, or the live one's latest
  Node *nodeOf(const QModelIndex &idx) const;
  QModelIndex indexOf(Node *n, int column = 0) const;
  QString pathOf(const Node *n) const;

  QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex & = {}) const override { return ColumnCount; }
  bool hasChildren(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &idx, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  const QVector<Node *> &kids(const Node *n) const;

  SpaceView *m_space;
  Node *m_root = nullptr;
  unsigned m_gen = ~0u;
  mutable QHash<const Node *, QVector<Node *>> m_kids; // copied out under the lock
  mutable QHash<const Node *, int> m_row;
};

class SpaceTree : public QTreeView
{
  Q_OBJECT

public:
  SpaceTree(SpaceView *space, QWidget *parent = nullptr);

  // The browser is here: open the tree down to it and put it in view.
  void showPath(const QString &path);

signals:
  void picked(const QString &path, bool isDir);

private:
  void onTreeChanged();
  void showEvent(QShowEvent *e) override;
  void reveal();

  SpaceView *m_space;
  SpaceTreeModel *m_model;
  QString m_want; // to show once the scan has reached it
  unsigned m_gen = ~0u;
  QElapsedTimer m_since; // a live tree is refreshed a few times a second, not every frame
  QTimer m_later;        // ... and one held back is done when its turn comes
};
