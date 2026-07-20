// ------------------------------------------------------------------------------
//                     S Y N - F I L E M A N A G E R
//
//   MainWindow: QTreeView + QFileSystemModel wiring, toolbar, and
//   keyboard shortcuts. Delegates the actual filesystem work to FileOps
//   so this class only owns UI state.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-FILEMANAGER (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QMainWindow>
#include <QStringList>

class QFileSystemModel;
class QTreeView;
class QLineEdit;
class QLabel;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(const QString &startPath, QWidget *parent = nullptr);

private slots:
  void navigateUp();
  void navigateHome();
  void navigateToPathBar();
  void onDoubleClicked(const QModelIndex &index);
  void onSelectionChanged();
  void renameSelected();
  void deleteSelected();
  void copySelected();
  void cutSelected();
  void pasteClipboard();

private:
  void setRoot(const QString &path);
  QStringList selectedPaths() const;
  bool isInlineEditorActive() const;

  QFileSystemModel *m_model;
  QTreeView *m_view;
  QLineEdit *m_pathBar;
  QLabel *m_statusLabel;

  QStringList m_clipboard;
  bool m_clipboardIsCut = false;
};
