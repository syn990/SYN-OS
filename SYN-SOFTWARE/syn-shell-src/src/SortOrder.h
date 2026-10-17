// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   SortOrder: the one order every listing shares, the disk's columns and
//   an archive's alike. Folders always come first; within them, by name
//   (natural, A to Z), size (biggest first), modified or created (newest
//   first), any of them reversed, ties going by name. Each proxy reads
//   its own values and asks compare() which way they go; set() re-sorts
//   every proxy that registered with track().
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QDateTime>
#include <QString>

class QSortFilterProxyModel;

namespace SortOrder {

enum Key { Name, Size, Modified, Created };

void set(Key key, bool reversed);
Key key();
bool reversed();
void track(QSortFilterProxyModel *proxy);

// Two values of the current key (not Name): <0 left goes first, 0 a tie.
int compare(qint64 left, qint64 right);
// The name order, reversed when the key is Name and reversed is on.
bool nameBefore(int byName);
// An invalid time (no birth time kept) sorts as the oldest.
qint64 stamp(const QDateTime &t);

// "name", "size", ... for QSettings; an unknown id gives Name.
QString id(Key key);
Key fromId(const QString &id);
// "Modified", and the way it runs: "new→old", "A→Z", ...
QString label(Key key);
QString direction(Key key, bool reversed);
// Ascending in the usual sense (A first, smallest, oldest): the arrow's way.
bool ascending(Key key, bool reversed);

} // namespace SortOrder
