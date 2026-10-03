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
//   Zoom and the middle column's field switches live here too: Ctrl+wheel (or Ctrl+= - 0) over the files sizes
//   everything but the shell, which zooms itself the same way.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "RowDelegate.h"
#include "TermView.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QFontInfo>
#include <QListView>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVariantAnimation>

#include "Zoom.h"

void MainWindow::buildShell()
{
  // The area holds the shell, and for a while a program instead
  // (bulk rename's editor) on a second page.
  m_termStack = new QStackedWidget(this);
  m_term = new TermView(m_termStack);
  m_termStack->addWidget(m_term);
  m_termStack->hide();

  m_vsplit = new QSplitter(Qt::Vertical, this);
  m_vsplit->setObjectName("vsplit");
  m_vsplit->setChildrenCollapsible(false);
  m_vsplit->setHandleWidth(2);
  m_vsplit->addWidget(m_split);
  m_vsplit->addWidget(m_termStack);
  m_vsplit->setStretchFactor(0, 3);
  m_vsplit->setStretchFactor(1, 2);
  QSettings settings;
  if (!m_vsplit->restoreState(settings.value("vsplit").toByteArray()))
    m_vsplit->setSizes({420, 260});
  m_termStack->hide(); // restoreState can show it; the shell starts on demand

  connect(m_term, &TermView::cwdChanged, this, &MainWindow::onShellCwd);
  connect(m_term, &TermView::finished, this, &MainWindow::onShellFinished);
  connect(m_term, &TermView::toggleFocusRequested, this, &MainWindow::toggleShellFocus);
  connect(m_term, &TermView::detachRequested, this, &MainWindow::detachShell);
  connect(m_term, &TermView::hideRequested, this, &MainWindow::hideShell);
}

void MainWindow::showShell(bool focus)
{
  m_termStack->show();
  m_termStack->setCurrentWidget(m_term);
  m_shellBtn->setChecked(true);
  if (!m_term->isRunning())
    m_term->start(m_currentDir);
  if (focus)
    m_term->setFocus();
}

void MainWindow::hideShell()
{
  // Hidden, not ended: the shell keeps running and comes back as it was.
  if (m_termStack->currentWidget() != m_term)
    return; // an editor of ours is open in it: leave it to finish
  m_termStack->hide();
  m_shellBtn->setChecked(false);
  m_view->setFocus();
}

void MainWindow::toggleShellPane()
{
  if (m_termStack->isVisible())
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
  if (m_termStack->currentWidget() == m_term)
    m_termStack->hide();
  m_shellBtn->setChecked(false);
  if (hadFocus || detached)
    m_view->setFocus();
  flash(detached ? tr("shell moved to foot; ` starts a new one here") : tr("shell ended"));
}

// ------------------------------------------------------------------- zoom

void MainWindow::zoomUi(int steps)
{
  // Steps from where a running animation is heading, so notches add up.
  const int base = m_uiZoomAnim->state() == QAbstractAnimation::Running
                     ? qRound(m_uiZoomAnim->endValue().toReal()) : m_uiPx;
  const int home = QFontInfo(QApplication::font()).pixelSize();
  const int target = steps == 0 ? Zoom::step(home, 0) : Zoom::step(base, steps);
  const qreal from = m_uiZoomAnim->state() == QAbstractAnimation::Running
                       ? m_uiZoomAnim->currentValue().toReal() : qreal(m_uiPx);
  m_uiZoomAnim->stop();
  m_uiPx = target;
  m_uiZoomAnim->setStartValue(from);
  m_uiZoomAnim->setEndValue(qreal(target));
  m_uiZoomAnim->start();
  flash(tr("zoom %1px").arg(target));
}

// Fractional sizes while animating: Terminus has none, so those frames
// take the nearest size it does have; the last frame is a ladder size.
// Set on every widget, not just the top one: widgets the stylesheet
// styles don't inherit a font change from their parent.
void MainWindow::applyUiFont(qreal px)
{
  QFont f = QApplication::font();
  f.setPixelSize(qMax(6, qRound(px)));
  if (centralWidget()->font() == f)
    return;
  centralWidget()->setFont(f);
  for (QWidget *w : centralWidget()->findChildren<QWidget *>())
    if (w != m_termStack && !m_termStack->isAncestorOf(w))
      w->setFont(f);
}

// ----------------------------------------------------------------- fields

void MainWindow::toggleField(int field)
{
  const int fields = RowDelegate::fields() ^ field;
  RowDelegate::setFields(fields);
  for (QAction *a : m_actFields)
    a->setChecked(fields & a->data().toInt());
  // Titles over the columns once there's more than the size to tell apart.
  m_fieldHeader->setVisible(fields & ~RowDelegate::Size);
  m_fieldHeader->update();
  m_view->viewport()->update();
  QSettings().setValue("fields", fields);

  QStringList on;
  for (QAction *a : m_actFields)
    if (a->isChecked())
      on << a->text().section(QLatin1Char('\t'), 0, 0).toLower();
  flash(on.isEmpty() ? tr("fields: name only") : tr("fields: %1").arg(on.join(QStringLiteral(", "))));
}
