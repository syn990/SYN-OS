// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, shell half: the terminal area under the three columns.
//   ` shows or hides it, Ctrl+` moves the keyboard between it and the
//   files, Ctrl+Shift+D sends the running shell to foot. The two halves
//   follow each other: the shell's OSC 7 moves the browser, and the
//   browser moving asks the shell to cd (it only does when it's idle at a
//   prompt; see PtySession). Inside an archive the shell waits in the
//   folder that holds it.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "TermView.h"

#include <QFileInfo>
#include <QListView>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>

void MainWindow::buildShell()
{
  m_term = new TermView(this);
  m_term->hide();

  m_vsplit = new QSplitter(Qt::Vertical, this);
  m_vsplit->setObjectName("vsplit");
  m_vsplit->setChildrenCollapsible(false);
  m_vsplit->setHandleWidth(2);
  m_vsplit->addWidget(m_split);
  m_vsplit->addWidget(m_term);
  m_vsplit->setStretchFactor(0, 3);
  m_vsplit->setStretchFactor(1, 2);
  QSettings settings;
  if (!m_vsplit->restoreState(settings.value("vsplit").toByteArray()))
    m_vsplit->setSizes({420, 260});
  m_term->hide(); // restoreState can show it; the shell starts on demand

  connect(m_term, &TermView::cwdChanged, this, &MainWindow::onShellCwd);
  connect(m_term, &TermView::finished, this, &MainWindow::onShellFinished);
  connect(m_term, &TermView::toggleFocusRequested, this, &MainWindow::toggleShellFocus);
  connect(m_term, &TermView::detachRequested, this, &MainWindow::detachShell);
  connect(m_term, &TermView::hideRequested, this, &MainWindow::hideShell);
}

void MainWindow::showShell(bool focus)
{
  m_term->show();
  m_shellBtn->setChecked(true);
  if (!m_term->isRunning())
    m_term->start(m_currentDir);
  if (focus)
    m_term->setFocus();
}

void MainWindow::hideShell()
{
  // Hidden, not ended: the shell keeps running and comes back as it was.
  m_term->hide();
  m_shellBtn->setChecked(false);
  m_view->setFocus();
}

void MainWindow::toggleShellPane()
{
  if (m_term->isVisible())
    hideShell();
  else
    showShell(true);
}

void MainWindow::toggleShellFocus()
{
  if (m_term->isVisible() && m_term->hasFocus())
    m_view->setFocus();
  else
    showShell(true);
}

void MainWindow::detachShell()
{
  QString error;
  if (!m_term->detachToFoot(&error))
    flash(tr("shell to foot: %1").arg(error), true);
}

void MainWindow::syncShell()
{
  if (m_term && m_term->isRunning() && !m_currentDir.isEmpty() && m_term->cwd() != m_currentDir)
    m_term->requestCd(m_currentDir);
}

void MainWindow::onShellCwd(const QString &dir)
{
  if (dir == m_currentDir || !QFileInfo(dir).isDir())
    return;
  navigateTo(dir); // leaves an archive too: the shell went somewhere else
}

void MainWindow::onShellFinished(bool detached)
{
  const bool hadFocus = m_term->hasFocus();
  m_term->hide();
  m_shellBtn->setChecked(false);
  if (hadFocus || detached)
    m_view->setFocus();
  flash(detached ? tr("shell moved to foot; ` starts a new one here") : tr("shell ended"));
}
