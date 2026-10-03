// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   RowDelegate: paints one QFileSystemModel row the way `ls -F` would
//   print it, behind a SynIcons file-type icon (switchable off): directories bold with a trailing "/", symlinks
//   "@" plus their target, executables "*", dotfiles dimmed, file size
//   right-aligned. The middle column's rows can show more fields, each
//   switchable (permissions, owner, size, modified, created), as aligned
//   columns at the right, with FieldHeader naming them above the view.
//   The view's current index (the cursor) gets the solid
//   accent bar; selected rows are marks, shown as a bar in the left
//   gutter, the way ranger keeps the cursor and its marks apart.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QList>
#include <QStyledItemDelegate>
#include <QWidget>

class QAbstractItemView;
class QFontMetrics;

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

  // One switch for every view's rows.
  static void setShowIcons(bool show);
  static bool showIcons();

  // The fields a detailed view's rows show, ls -l order left to right.
  enum Field { Permissions = 1, Owner = 2, Size = 4, Modified = 8, Created = 16 };
  static void setFields(int fields);
  static int fields();
  // The middle column is detailed; the parent and preview columns, too
  // narrow for it, show the name and size only.
  void setDetailed(bool detailed) { m_detailed = detailed; }

  struct Column
  {
    Field field;
    QString title;
    int left;
    int width;
  };
  // The switched-on fields that fit in a row this wide, placed from its
  // right edge; the least wanted drop out first when the name would get
  // too little room. FieldHeader lays its titles out with the same call.
  static QList<Column> columns(const QFontMetrics &fm, int rowWidth, int nameStart);
  static int nameStart(int rowHeight);

private:
  bool m_detailed = false;
};

// The titles over a detailed view's field columns.
class FieldHeader : public QWidget
{
public:
  FieldHeader(QAbstractItemView *view, QWidget *parent = nullptr);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QAbstractItemView *m_view;
};

// 7.0G, 512B, 13K: the same unit style waybar's disk module uses.
QString humanSize(qint64 bytes);
