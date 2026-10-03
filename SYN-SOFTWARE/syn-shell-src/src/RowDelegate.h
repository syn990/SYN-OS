// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   RowDelegate: paints one QFileSystemModel row the way `ls -F` would
//   print it, no icons: directories bold with a trailing "/", symlinks
//   "@" plus their target, executables "*", dotfiles dimmed, file size
//   right-aligned. The view's current index (the cursor) gets the solid
//   accent bar; selected rows are marks, shown as a bar in the left
//   gutter, the way ranger keeps the cursor and its marks apart.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QStyledItemDelegate>

class RowDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override;

  // The inline rename editor starts at the name column, past the gutter.
  void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const override;
};

// 7.0G, 512B, 13K: the same unit style waybar's disk module uses.
QString humanSize(qint64 bytes);
