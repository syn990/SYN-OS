// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Preview: the right-hand column. Shows whatever the cursor is on: a
//   directory's listing (a third view over the same QFileSystemModel), a
//   text file's first 64K, an image scaled to fit, or a hexdump -C style
//   dump of anything binary. Code is coloured by KSyntaxHighlighting
//   (Kate's engine, 300+ languages) with a theme written from the SYN
//   palette, when SYN-SHELL was built with it. A title line above names
//   what's shown. Inside
//   an archive the same pane shows entry folders and entry bytes.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QWidget>

class FileSortProxy;
class QAbstractItemModel;
class QModelIndex;
class QLabel;
class QListView;
class QPlainTextEdit;
class QStackedWidget;

class Preview : public QWidget
{
  Q_OBJECT

public:
  Preview(FileSortProxy *model, QWidget *parent = nullptr);

  void showPath(const QString &path);
  // Free text in the preview column: the key reference, error messages.
  void showText(const QString &title, const QString &text);
  void clear();
  // A folder from any model (an archive's), shown with the same rows.
  void showDir(QAbstractItemModel *model, const QModelIndex &root, const QString &title);
  // Bytes from somewhere other than a file on disk (an archive entry):
  // image, text or hex by what `name` and the bytes say they are.
  void showData(const QString &name, qint64 size, const QByteArray &data);

  QListView *dirView() const { return m_dir; }
  QString path() const { return m_path; }

protected:
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void showImage();
  void showBytes(const QString &title, const QByteArray &data, const QString &name);
  // Colours m_text for `name`'s language, or not at all (plain, hex).
  void highlight(const QString &name);
  void dropHighlighter();

  FileSortProxy *m_model;
  QLabel *m_title;
  QStackedWidget *m_stack;
  QListView *m_dir;
  QPlainTextEdit *m_text;
  QLabel *m_image;
  QString m_path;
  bool m_isImage = false;
  QByteArray m_imageData; // when the image came from showData, not a path
  QObject *m_highlighter = nullptr; // a KSyntaxHighlighting::SyntaxHighlighter, if built with it
  QString m_language;
};
