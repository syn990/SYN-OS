// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, part: the field bar and the sort order. The bar runs under
//   the tabs: a box per field (each one the same switch as zp, zo, zs, zm,
//   zc and the menu's Fields) and SORT, which picks the order every
//   listing uses. A field switched on that the middle column has no room
//   for widens the column into the preview; one that still can't fit has
//   its box greyed, so a tick never just silently does nothing.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "FileSortProxy.h"
#include "RowDelegate.h"
#include "SortOrder.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>

namespace {

void repolish(QWidget *w)
{
  w->style()->unpolish(w);
  w->style()->polish(w);
}

// The bar's names for the fields: the column titles, not shouting.
QString boxLabel(int field)
{
  switch (field) {
  case RowDelegate::Permissions: return QObject::tr("Perms");
  case RowDelegate::Owner: return QObject::tr("Owner");
  case RowDelegate::Size: return QObject::tr("Size");
  case RowDelegate::Modified: return QObject::tr("Modified");
  case RowDelegate::Created: return QObject::tr("Created");
  }
  return QString();
}

} // namespace

void MainWindow::buildSortActions()
{
  m_sortMenu = new QMenu(tr("Sort"), this);
  const struct { SortOrder::Key key; const char *keys; } sorts[] = {
    {SortOrder::Name, "on"},
    {SortOrder::Size, "os"},
    {SortOrder::Modified, "om"},
    {SortOrder::Created, "oc"},
  };
  // Each key both ways, as ranger has them: on, and oN the other way round.
  for (const auto &s : sorts) {
    if (s.key != SortOrder::Name)
      m_sortMenu->addSeparator();
    for (const bool reversed : {false, true}) {
      QString keys = QLatin1String(s.keys);
      if (reversed)
        keys[1] = keys[1].toUpper();
      const int key = s.key;
      auto *a = new QAction(this);
      a->setData(QVariantList{key, reversed, keys});
      connect(a, &QAction::triggered, this, [this, key, reversed] { setSort(key, reversed); });
      m_sortMenu->addAction(a);
      m_actSorts << a;
      m_bindings.insert(keys, [this, key, reversed] { setSort(key, reversed); });
    }
  }
  m_bindings.insert(QStringLiteral("or"), [this] { setSort(SortOrder::key(), !SortOrder::reversed()); });

  m_actFieldBar = new QAction(tr("Field bar") + QStringLiteral("\tzb"), this);
  m_actFieldBar->setCheckable(true);
  m_actFieldBar->setChecked(QSettings().value("fieldBar", true).toBool());
  connect(m_actFieldBar, &QAction::triggered, this, [this](bool on) {
    m_fieldBar->setVisible(on);
    QSettings().setValue("fieldBar", on);
  });
  m_bindings.insert(QStringLiteral("zb"), [this] { m_actFieldBar->trigger(); });
}

QWidget *MainWindow::buildFieldBar()
{
  m_fieldBar = new QWidget(this);
  m_fieldBar->setObjectName("fieldBar");
  m_fieldBar->setVisible(m_actFieldBar->isChecked());
  auto *h = new QHBoxLayout(m_fieldBar);
  h->setContentsMargins(4, 0, 4, 0);
  h->setSpacing(0);

  // A box per field, kept in step with its action whichever way it's
  // switched. No focus: the keys stay with the files.
  for (QAction *a : m_actFields) {
    const int field = a->data().toInt();
    auto *box = new QCheckBox(boxLabel(field), m_fieldBar);
    box->setFocusPolicy(Qt::NoFocus);
    box->setChecked(a->isChecked());
    box->setProperty("field", field);
    box->setProperty("tip", tr("%1 column (%2)").arg(a->text().section(QLatin1Char('\t'), 0, 0),
                                                     a->text().section(QLatin1Char('\t'), 1)));
    box->setToolTip(box->property("tip").toString());
    connect(box, &QCheckBox::clicked, a, &QAction::trigger);
    connect(a, &QAction::toggled, box, &QCheckBox::setChecked);
    h->addWidget(box);
    m_fieldBoxes << box;
  }
  h->addStretch(1);

  auto *title = new QLabel(tr("SORT"), m_fieldBar);
  title->setObjectName("fieldBarTitle");
  h->addWidget(title);
  m_sortBtn = new QToolButton(m_fieldBar);
  m_sortBtn->setFocusPolicy(Qt::NoFocus);
  m_sortBtn->setPopupMode(QToolButton::InstantPopup);
  m_sortBtn->setMenu(m_sortMenu);
  m_sortBtn->setToolTip(tr("The order every column lists in (on os om oc, or to turn it round; "
                           "a click on a column's title works too)"));
  h->addWidget(m_sortBtn);
  updateSortUi();
  return m_fieldBar;
}

void MainWindow::updateFieldBar()
{
  // The same layout the rows get, so a box greys exactly when its column
  // is the one the row painter left out.
  const QFontMetrics fm(m_view->font());
  int shown = 0;
  for (const RowDelegate::Column &c : RowDelegate::columns(fm, m_view->viewport()->width(),
                                                           RowDelegate::nameStart(fm.height() + 6)))
    shown |= c.field;
  for (QCheckBox *box : m_fieldBoxes) {
    const int field = box->property("field").toInt();
    const bool squeezed = (RowDelegate::fields() & field) && !(shown & field);
    if (box->property("squeezed").toBool() == squeezed)
      continue;
    box->setProperty("squeezed", squeezed);
    box->setToolTip(squeezed ? tr("%1: on, but the middle column has no room for it. "
                                  "Drag the column wider, or zoom out (Ctrl+-).").arg(boxLabel(field))
                             : box->property("tip").toString());
    repolish(box);
  }
}

void MainWindow::fitFields()
{
  // Widen the middle column into the preview, as far as leaving the
  // preview a quarter of the width; the bar greys whatever still won't fit.
  const QFontMetrics fm(m_view->font());
  const int need = RowDelegate::neededWidth(fm, RowDelegate::nameStart(fm.height() + 6))
                   - m_view->viewport()->width();
  QList<int> sizes = m_split->sizes();
  if (need <= 0 || sizes.size() != 3)
    return;
  const int total = sizes[0] + sizes[1] + sizes[2];
  const int give = qMin(need, sizes[2] - total / 4);
  if (give <= 0)
    return;
  sizes[1] += give;
  sizes[2] -= give;
  m_split->setSizes(sizes);
}

void MainWindow::setSort(int key, bool reversed)
{
  const auto k = SortOrder::Key(key);
  SortOrder::set(k, reversed);
  QSettings settings;
  settings.setValue("sort", SortOrder::id(k));
  settings.setValue("sortReversed", reversed);
  updateSortUi();
  m_fieldHeader->update();
  if (cursor().isValid())
    m_view->scrollTo(cursor());
  flash(tr("sorted by %1, %2").arg(SortOrder::label(k).toLower(), SortOrder::direction(k, reversed)));
}

void MainWindow::sortByTitle(int field)
{
  int key = SortOrder::Name;
  if (field == RowDelegate::Size)
    key = SortOrder::Size;
  else if (field == RowDelegate::Modified)
    key = SortOrder::Modified;
  else if (field == RowDelegate::Created)
    key = SortOrder::Created;
  // The sorted title again turns it round; another starts its usual way.
  setSort(key, key == SortOrder::key() && !SortOrder::reversed());
}

void MainWindow::updateSortUi()
{
  for (QAction *a : m_actSorts) {
    const QVariantList d = a->data().toList();
    const auto key = SortOrder::Key(d.at(0).toInt());
    const bool reversed = d.at(1).toBool();
    const bool current = key == SortOrder::key() && reversed == SortOrder::reversed();
    a->setText((current ? QStringLiteral("▸ ") : QString()) + SortOrder::label(key) + QStringLiteral(", ")
               + SortOrder::direction(key, reversed) + QLatin1Char('\t') + d.at(2).toString());
  }
  if (m_sortBtn)
    m_sortBtn->setText(SortOrder::label(SortOrder::key()).toLower() + QLatin1Char(' ')
                       + SortOrder::direction(SortOrder::key(), SortOrder::reversed()) + QStringLiteral(" ▾"));
}
