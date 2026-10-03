// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, tabs: several places open at once, each keeping its own
//   back/forward history (the cursor is remembered per folder anyway).
//   One set of columns shows the current tab; switching saves this one
//   and goes to the other's place. The strip of tabs under the top bar
//   only appears once there's more than one.
//
//   Ctrl+T new, Ctrl+W close, Ctrl+Tab / Ctrl+Shift+Tab next / previous,
//   Alt+1..9 straight to one; click a tab to switch, middle-click to close.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "ArchiveModel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>

namespace {

// Middle-click closes, as in a browser.
class TabButton : public QToolButton
{
public:
  TabButton(std::function<void()> close, QWidget *parent)
    : QToolButton(parent), m_close(std::move(close)) {}

protected:
  void mouseReleaseEvent(QMouseEvent *e) override
  {
    if (e->button() == Qt::MiddleButton) {
      m_close();
      return;
    }
    QToolButton::mouseReleaseEvent(e);
  }

private:
  std::function<void()> m_close;
};

QString tabName(const QString &location)
{
  if (location.contains(QLatin1Char('\x1f'))) {
    const QString file = QFileInfo(location.section(QLatin1Char('\x1f'), 0, 0)).fileName();
    const QString inner = location.section(QLatin1Char('\x1f'), 1);
    return inner.isEmpty() ? file : file + QLatin1Char('/') + inner.section(QLatin1Char('/'), -1);
  }
  const QString name = QFileInfo(location).fileName();
  return name.isEmpty() ? location : name;
}

} // namespace

QWidget *MainWindow::buildTabBar()
{
  m_tabBar = new QWidget(this);
  m_tabBar->setObjectName("tabBar");
  m_tabLayout = new QHBoxLayout(m_tabBar);
  m_tabLayout->setContentsMargins(0, 4, 4, 0);
  m_tabLayout->setSpacing(0);
  m_tabs = {Tab{}};
  m_tab = 0;
  m_tabBar->hide();
  return m_tabBar;
}

void MainWindow::newTab(const QString &where)
{
  m_tabs[m_tab] = Tab{location(), m_back, m_forward};
  m_tabs.insert(m_tab + 1, Tab{where, {}, {}});
  switchTab(m_tab + 1);
}

void MainWindow::closeTab(int index)
{
  if (m_tabs.size() <= 1) {
    flash(tr("the last tab stays open"));
    return;
  }
  if (index == m_tab) {
    m_tabs.removeAt(index);
    m_tab = -1; // nothing to save on the way out
    switchTab(qMin(index, int(m_tabs.size()) - 1));
  } else {
    m_tabs.removeAt(index);
    if (index < m_tab)
      --m_tab;
    updateTabBar();
  }
}

void MainWindow::switchTab(int index)
{
  if (index < 0 || index >= m_tabs.size())
    return;
  if (m_tab >= 0 && m_tab < m_tabs.size()) {
    if (index == m_tab)
      return;
    m_tabs[m_tab] = Tab{location(), m_back, m_forward};
  }
  m_tab = index;
  const Tab t = m_tabs[index];
  // Arrive without touching the history: then the tab's own comes back.
  goToLocation(t.location, false);
  m_back = t.back;
  m_forward = t.forward;
  updateHistoryButtons();
  updateTabBar();
}

void MainWindow::updateTabBar()
{
  if (!m_tabBar || m_tabs.isEmpty())
    return;
  if (m_tab >= 0 && m_tab < m_tabs.size())
    m_tabs[m_tab].location = location();
  while (QLayoutItem *item = m_tabLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  m_tabBar->setVisible(m_tabs.size() > 1);
  if (m_tabs.size() <= 1)
    return;
  for (int i = 0; i < m_tabs.size(); ++i) {
    auto *b = new TabButton([this, i] { closeTab(i); }, m_tabBar);
    b->setObjectName(i == m_tab ? "tabCurrent" : "tab");
    b->setText(QStringLiteral("%1  %2").arg(i + 1).arg(tabName(m_tabs[i].location)));
    b->setToolTip(m_tabs[i].location.section(QLatin1Char('\x1f'), 0, 0)
                  + tr("\nmiddle-click or Ctrl+W closes"));
    b->setFocusPolicy(Qt::NoFocus);
    b->setFont(m_tabBar->font());
    connect(b, &QToolButton::clicked, this, [this, i] { switchTab(i); });
    m_tabLayout->addWidget(b);
  }
  m_tabLayout->addStretch(1);
}
