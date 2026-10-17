#include "SortOrder.h"

#include <QCoreApplication>
#include <QList>
#include <QPointer>
#include <QSortFilterProxyModel>

#include <limits>

namespace {

SortOrder::Key g_key = SortOrder::Name;
bool g_reversed = false;
QList<QPointer<QSortFilterProxyModel>> g_proxies;

QString tr(const char *text)
{
  return QCoreApplication::translate("SortOrder", text);
}

} // namespace

namespace SortOrder {

void set(Key key, bool reversed)
{
  if (key == g_key && reversed == g_reversed)
    return;
  g_key = key;
  g_reversed = reversed;
  g_proxies.removeAll(nullptr);
  for (const QPointer<QSortFilterProxyModel> &p : g_proxies)
    p->invalidate();
}

Key key()
{
  return g_key;
}

bool reversed()
{
  return g_reversed;
}

void track(QSortFilterProxyModel *proxy)
{
  g_proxies.removeAll(nullptr);
  g_proxies << proxy;
}

int compare(qint64 left, qint64 right)
{
  if (left == right)
    return 0;
  const int c = left > right ? -1 : 1; // biggest, newest first
  return g_reversed ? -c : c;
}

bool nameBefore(int byName)
{
  return g_key == Name && g_reversed ? byName > 0 : byName < 0;
}

qint64 stamp(const QDateTime &t)
{
  return t.isValid() ? t.toMSecsSinceEpoch() : std::numeric_limits<qint64>::min();
}

QString id(Key key)
{
  switch (key) {
  case Name: break;
  case Size: return QStringLiteral("size");
  case Modified: return QStringLiteral("modified");
  case Created: return QStringLiteral("created");
  }
  return QStringLiteral("name");
}

Key fromId(const QString &id)
{
  for (Key k : {Size, Modified, Created})
    if (id == SortOrder::id(k))
      return k;
  return Name;
}

QString label(Key key)
{
  switch (key) {
  case Name: break;
  case Size: return tr("Size");
  case Modified: return tr("Modified");
  case Created: return tr("Created");
  }
  return tr("Name");
}

QString direction(Key key, bool reversed)
{
  switch (key) {
  case Name: break;
  case Size: return reversed ? tr("small→big") : tr("big→small");
  case Modified:
  case Created: return reversed ? tr("old→new") : tr("new→old");
  }
  return reversed ? tr("Z→A") : tr("A→Z");
}

bool ascending(Key key, bool reversed)
{
  return (key == Name) != reversed;
}

} // namespace SortOrder
