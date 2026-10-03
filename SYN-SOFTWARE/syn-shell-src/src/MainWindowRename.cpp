// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, bulk rename: the marked names (or every name in the
//   folder, with nothing marked) go one per line into a file, which opens
//   in $VISUAL / $EDITOR (nano if neither is set: SYN-SHELL starts from
//   the desktop, not from a shell that read .zshrc) right in the shell
//   area. Save and quit, check the list of changes, and they're applied.
//
//   Applied in two passes, everything to a temporary name and then to its
//   new one, so swaps and chains (a -> b, b -> a) work; if a rename fails
//   part-way, what was done is put back. One Ctrl+Z undoes the lot.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "FileSortProxy.h"
#include "TermView.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QListView>
#include <QMessageBox>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QToolButton>

#include <algorithm>
#include <unistd.h>

namespace {

bool lexists(const QString &path)
{
  const QFileInfo fi(path);
  return fi.exists() || fi.isSymLink();
}

QStringList editorCommand()
{
  for (const char *var : {"VISUAL", "EDITOR"}) {
    const QStringList cmd = qEnvironmentVariable(var).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!cmd.isEmpty() && !QStandardPaths::findExecutable(cmd.first()).isEmpty())
      return cmd;
  }
  for (const char *ed : {"nano", "nvim", "vim", "vi"})
    if (!QStandardPaths::findExecutable(QLatin1String(ed)).isEmpty())
      return {QLatin1String(ed)};
  return {};
}

} // namespace

QString MainWindow::renameAll(const FileJobs::Pairs &pairs)
{
  QSet<QString> leaving;
  for (const FileJobs::Pair &p : pairs)
    leaving.insert(p.from);
  for (const FileJobs::Pair &p : pairs) {
    if (!lexists(p.from))
      return tr("%1 isn't there any more").arg(QFileInfo(p.from).fileName());
    if (lexists(p.to) && !leaving.contains(p.to))
      return tr("%1 already exists").arg(QFileInfo(p.to).fileName());
  }

  // Pass one: every file to a name nothing else has.
  QList<QPair<QString, QString>> parked; // temporary -> original
  for (int i = 0; i < pairs.size(); ++i) {
    const QString temp = QFileInfo(pairs[i].from).absolutePath()
                       + QStringLiteral("/.syn-rename-%1-%2").arg(getpid()).arg(i);
    if (!QDir().rename(pairs[i].from, temp)) {
      for (const auto &back : parked)
        QDir().rename(back.first, back.second);
      return tr("couldn't rename %1 (permission denied?)").arg(QFileInfo(pairs[i].from).fileName());
    }
    parked << qMakePair(temp, pairs[i].from);
  }
  // Pass two: each to its new name.
  QStringList failed;
  for (int i = 0; i < pairs.size(); ++i) {
    if (!QDir().rename(parked[i].first, pairs[i].to)) {
      QDir().rename(parked[i].first, parked[i].second); // at least back where it was
      failed << QFileInfo(pairs[i].from).fileName();
    }
  }
  return failed.isEmpty() ? QString()
                          : tr("couldn't rename %1").arg(failed.join(QStringLiteral(", ")));
}

void MainWindow::bulkRename()
{
  if (refuseInArchive())
    return;
  if (m_task && m_task->isRunning()) {
    flash(tr("a bulk rename is already open below"), true);
    return;
  }
  const QStringList editor = editorCommand();
  if (editor.isEmpty()) {
    flash(tr("bulk rename needs an editor: set $EDITOR (nano, vim...)"), true);
    return;
  }

  // The marks in the order shown, or with none, the whole folder.
  const QModelIndex root = m_view->rootIndex();
  QModelIndexList rows = m_view->selectionModel()->selectedIndexes();
  rows.erase(std::remove_if(rows.begin(), rows.end(),
                            [&](const QModelIndex &i) { return i.parent() != root; }),
             rows.end());
  if (rows.isEmpty())
    for (int r = 0; r < m_view->model()->rowCount(root); ++r)
      rows << m_view->model()->index(r, 0, root);
  std::sort(rows.begin(), rows.end(),
            [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
  if (rows.isEmpty())
    return;

  m_bulkDir = m_currentDir;
  m_bulkNames.clear();
  for (const QModelIndex &i : rows)
    m_bulkNames << m_proxy->infoOf(i).fileName();
  if (std::any_of(m_bulkNames.cbegin(), m_bulkNames.cend(),
                  [](const QString &n) { return n.contains(QLatin1Char('\n')); })) {
    flash(tr("a name here has a line break in it: rename that one with cw"), true);
    return;
  }

  QTemporaryFile file(QDir::tempPath() + QStringLiteral("/syn-shell-rename-XXXXXX.txt"));
  file.setAutoRemove(false);
  if (!file.open()) {
    flash(tr("bulk rename: can't write a temporary file"), true);
    return;
  }
  file.write(m_bulkNames.join(QLatin1Char('\n')).toUtf8() + '\n');
  file.close();
  m_bulkFile = file.fileName();

  if (!m_task) {
    m_task = new TermView(m_termStack);
    m_termStack->addWidget(m_task);
    connect(m_task, &TermView::finished, this, [this](bool) { finishBulkRename(); });
    connect(m_task, &TermView::zoomChanged, m_term, [this](int px) { m_term->setFontPixels(px, false); });
  }
  m_task->setFontPixels(m_term->fontPixels(), false);
  m_bulkPaneWasOpen = m_termStack->isVisible();
  m_termStack->setCurrentWidget(m_task);
  m_termStack->show();
  m_shellBtn->setChecked(true);
  m_task->start(m_bulkDir, editor + QStringList{m_bulkFile});
  m_task->setFocus();
  flash(editor.first() == QLatin1String("nano")
          ? tr("edit the names, then Ctrl+O Enter to save and Ctrl+X to finish")
          : tr("edit the names, save and quit the editor to finish"));
}

void MainWindow::finishBulkRename()
{
  // The shell area back as it was.
  m_termStack->setCurrentWidget(m_term);
  if (!m_bulkPaneWasOpen) {
    m_termStack->hide();
    m_shellBtn->setChecked(false);
  }
  m_view->setFocus();

  QFile file(m_bulkFile);
  QStringList lines;
  if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
  file.close();
  QFile::remove(m_bulkFile);
  while (!lines.isEmpty() && lines.last().isEmpty() && lines.size() > m_bulkNames.size())
    lines.removeLast(); // the editor's final newline

  if (lines.size() != m_bulkNames.size()) {
    flash(tr("%1 names went in and %2 came back: nothing renamed")
            .arg(m_bulkNames.size()).arg(lines.size()), true);
    return;
  }

  FileJobs::Pairs pairs;
  QSet<QString> finals;
  for (int i = 0; i < lines.size(); ++i) {
    const QString to = lines[i];
    if (to.isEmpty() || to == QLatin1String(".") || to == QLatin1String("..")
        || to.contains(QLatin1Char('/'))) {
      flash(tr("line %1 isn't a usable name (\"%2\"): nothing renamed").arg(i + 1).arg(to), true);
      return;
    }
    if (finals.contains(to)) {
      flash(tr("%1 appears twice: nothing renamed").arg(to), true);
      return;
    }
    finals.insert(to);
    if (to != m_bulkNames[i])
      pairs << FileJobs::Pair{m_bulkDir + QLatin1Char('/') + m_bulkNames[i],
                              m_bulkDir + QLatin1Char('/') + to};
  }
  if (pairs.isEmpty()) {
    flash(tr("no names changed"));
    return;
  }

  QStringList changes;
  for (const FileJobs::Pair &p : pairs.mid(0, 25))
    changes << QFileInfo(p.from).fileName() + QStringLiteral("  ->  ") + QFileInfo(p.to).fileName();
  if (pairs.size() > 25)
    changes << tr("... and %1 more").arg(pairs.size() - 25);
  if (QMessageBox::question(this, tr("Bulk rename"),
                            tr("Rename %n item(s)?\n\n%1", nullptr, int(pairs.size()))
                              .arg(changes.join(QLatin1Char('\n'))),
                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes)
      != QMessageBox::Yes)
    return;

  m_view->setFocus(); // the dialog had the keyboard; give it back to the files
  const QString error = renameAll(pairs);
  if (!error.isEmpty()) {
    flash(tr("bulk rename: %1").arg(error), true);
    return;
  }
  clearMarks();
  pushUndo(UndoStep::Renamed, pairs, tr("rename of %n item(s)", nullptr, int(pairs.size())));
  flash(tr("%n renamed  (Ctrl+Z undoes)", nullptr, int(pairs.size())));
  if (m_bulkDir == m_currentDir) {
    m_pendingPath = pairs.first().to;
    tryPending();
  }
}
