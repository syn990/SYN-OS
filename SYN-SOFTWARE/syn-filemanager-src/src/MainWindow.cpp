#include "MainWindow.h"
#include "FileOps.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(const QString &startPath, QWidget *parent)
  : QMainWindow(parent)
{
  setWindowTitle(tr("SYN-OS"));

  m_model = new QFileSystemModel(this);
  m_model->setReadOnly(false); // QFileSystemModel defaults to read-only,
                               // which silently disables inline rename.
  m_model->setRootPath(QDir::rootPath());
  m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

  m_view = new QTreeView(this);
  m_view->setModel(m_model);
  m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_view->setSortingEnabled(true);
  m_view->sortByColumn(0, Qt::AscendingOrder);
  // Deliberately no Stretch mode on any column: a stretch column resizes
  // itself as a side effect of dragging a neighboring column's boundary,
  // which reads as the drag direction being inverted. Every column is
  // independently Interactive so a resize only ever affects the one
  // boundary actually being dragged.
  m_view->header()->setStretchLastSection(false);
  m_view->header()->setSectionResizeMode(QHeaderView::Interactive);
  m_view->header()->resizeSection(0, 300);
  // Explicit rather than relying on QTreeView's default trigger set —
  // F2 is driven entirely by our own QShortcut + m_view->edit() call
  // below, not the view's own built-in F2 handling, so EditKeyPressed
  // is deliberately left out here to avoid the two mechanisms racing.
  m_view->setEditTriggers(QAbstractItemView::DoubleClicked
                           | QAbstractItemView::SelectedClicked);
  m_view->setFocusPolicy(Qt::StrongFocus);

  m_pathBar = new QLineEdit(this);
  connect(m_pathBar, &QLineEdit::returnPressed, this, &MainWindow::navigateToPathBar);

  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->addWidget(m_pathBar);
  layout->addWidget(m_view);
  setCentralWidget(central);

  m_statusLabel = new QLabel(this);
  statusBar()->addWidget(m_statusLabel);

  auto *toolbar = addToolBar(tr("Navigation"));
  toolbar->addAction(tr("Up"), this, &MainWindow::navigateUp);
  toolbar->addAction(tr("Home"), this, &MainWindow::navigateHome);
  toolbar->addSeparator();
  toolbar->addAction(tr("Rename"), this, &MainWindow::renameSelected);
  toolbar->addAction(tr("Delete"), this, &MainWindow::deleteSelected);
  toolbar->addAction(tr("Copy"), this, &MainWindow::copySelected);
  toolbar->addAction(tr("Cut"), this, &MainWindow::cutSelected);
  toolbar->addAction(tr("Paste"), this, &MainWindow::pasteClipboard);

  connect(m_view, &QTreeView::doubleClicked, this, &MainWindow::onDoubleClicked);
  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &MainWindow::onSelectionChanged);

  new QShortcut(QKeySequence::Delete, this, this, &MainWindow::deleteSelected);
  new QShortcut(QKeySequence(Qt::Key_F2), this, this, &MainWindow::renameSelected);
  new QShortcut(QKeySequence::Copy, this, this, &MainWindow::copySelected);
  new QShortcut(QKeySequence::Cut, this, this, &MainWindow::cutSelected);
  new QShortcut(QKeySequence::Paste, this, this, &MainWindow::pasteClipboard);
  new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), this, this, &MainWindow::navigateUp);

  setRoot(startPath);
  onSelectionChanged();
}

void MainWindow::setRoot(const QString &path)
{
  QDir dir(path);
  if (!dir.exists()) {
    m_statusLabel->setText(tr("No such directory: %1").arg(path));
    return;
  }
  if (!dir.isReadable()) {
    m_statusLabel->setText(tr("Permission denied: %1").arg(path));
    return;
  }

  const QModelIndex idx = m_model->setRootPath(path);
  m_view->setRootIndex(idx);
  m_pathBar->setText(path);
  m_statusLabel->clear();
}

void MainWindow::navigateUp()
{
  QDir dir(m_pathBar->text());
  if (dir.cdUp())
    setRoot(dir.absolutePath());
}

void MainWindow::navigateHome()
{
  setRoot(QDir::homePath());
}

void MainWindow::navigateToPathBar()
{
  setRoot(m_pathBar->text());
}

void MainWindow::onDoubleClicked(const QModelIndex &index)
{
  const QString path = m_model->filePath(index);
  QFileInfo info(path);
  if (info.isDir()) {
    setRoot(path);
    return;
  }
  // Hands off to the system's own MIME/XDG file association (xdg-open
  // under the hood) rather than this app owning any "open with" logic.
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::onSelectionChanged()
{
  const int count = selectedPaths().size();
  if (count == 0)
    m_statusLabel->setText(tr("%1 item(s)").arg(m_model->rowCount(m_view->rootIndex())));
  else
    m_statusLabel->setText(tr("%1 selected").arg(count));
}

QStringList MainWindow::selectedPaths() const
{
  QStringList paths;
  const auto rows = m_view->selectionModel()->selectedRows(0);
  for (const QModelIndex &idx : rows)
    paths << m_model->filePath(idx);
  return paths;
}

// QShortcut's default WindowShortcut context fires even while the tree
// view's inline rename editor has focus, so Delete/Backspace typed while
// renaming would otherwise delete the selection instead of editing its
// text. The item delegate's editor is a real child QWidget of m_view
// (not m_view itself) while editing is in progress, so checking the
// currently focused widget's ancestry is the public way to detect this
// (QAbstractItemView::state()/EditingState are protected).
bool MainWindow::isInlineEditorActive() const
{
  QWidget *focused = QApplication::focusWidget();
  return focused && focused != m_view && m_view->isAncestorOf(focused);
}

void MainWindow::renameSelected()
{
  const auto rows = m_view->selectionModel()->selectedRows(0);
  if (rows.size() != 1) {
    if (rows.size() > 1)
      QMessageBox::information(this, tr("Rename"), tr("Select exactly one item to rename."));
    return;
  }
  m_view->edit(rows.first());
}

void MainWindow::deleteSelected()
{
  if (isInlineEditorActive())
    return;

  const QStringList paths = selectedPaths();
  if (paths.isEmpty())
    return;
  FileOps::deleteEntries(this, paths);
}

void MainWindow::copySelected()
{
  if (isInlineEditorActive())
    return;
  m_clipboard = selectedPaths();
  m_clipboardIsCut = false;
}

void MainWindow::cutSelected()
{
  if (isInlineEditorActive())
    return;
  m_clipboard = selectedPaths();
  m_clipboardIsCut = true;
}

void MainWindow::pasteClipboard()
{
  if (isInlineEditorActive())
    return;
  if (m_clipboard.isEmpty())
    return;

  const QString destDir = m_pathBar->text();
  if (m_clipboardIsCut) {
    FileOps::moveEntries(this, m_clipboard, destDir);
    m_clipboard.clear();
  } else {
    FileOps::copyEntries(this, m_clipboard, destDir);
  }
}
