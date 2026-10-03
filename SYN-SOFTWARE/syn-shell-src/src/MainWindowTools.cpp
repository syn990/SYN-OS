// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, tools on files: permissions (+x, -x, :chmod 755 or
//   :chmod g+w, undoable) and Open With, every installed app that can
//   open a file, from the same mimeinfo.cache xdg-open reads, by the
//   file's type, its other names and the types it descends from (any text
//   editor for a C file), the default first.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "FileSortProxy.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QListView>
#include <QMenu>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <sys/stat.h>

namespace {

// ---- permissions ----------------------------------------------------------

// "755", "0644", or chmod's symbolic form: "+x", "u+w,go-r", "a=rX".
bool applyModeSpec(const QString &spec, mode_t current, bool dir, mode_t *out)
{
  const QString s = spec.trimmed();
  static const QRegularExpression octal(QStringLiteral("^0?[0-7]{3,4}$"));
  if (octal.match(s).hasMatch()) {
    *out = mode_t(s.toUInt(nullptr, 8)) & 07777;
    return true;
  }
  static const QRegularExpression clause(QStringLiteral("^([ugoa]*)([-+=])([rwxXst]*)$"));
  mode_t mode = current & 07777;
  for (const QString &part : s.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    const QRegularExpressionMatch m = clause.match(part);
    if (!m.hasMatch())
      return false;
    QString who = m.captured(1);
    if (who.isEmpty() || who.contains(QLatin1Char('a')))
      who = QStringLiteral("ugo");
    mode_t bits = 0;
    for (const QChar c : m.captured(3)) {
      mode_t rwx = 0;
      if (c == QLatin1Char('r'))
        rwx = 4;
      else if (c == QLatin1Char('w'))
        rwx = 2;
      else if (c == QLatin1Char('x') || (c == QLatin1Char('X') && (dir || (current & 0111))))
        rwx = 1;
      for (const QChar w : who) {
        const int shift = w == QLatin1Char('u') ? 6 : w == QLatin1Char('g') ? 3 : 0;
        bits |= rwx << shift;
        if (c == QLatin1Char('s') && w != QLatin1Char('o'))
          bits |= w == QLatin1Char('u') ? S_ISUID : S_ISGID;
        if (c == QLatin1Char('t'))
          bits |= S_ISVTX;
      }
    }
    mode_t mask = 0;
    for (const QChar w : who)
      mask |= w == QLatin1Char('u') ? 04700 : w == QLatin1Char('g') ? 02070 : 01007;
    const QChar op = m.captured(2).at(0);
    if (op == QLatin1Char('+'))
      mode |= bits;
    else if (op == QLatin1Char('-'))
      mode &= ~bits;
    else
      mode = (mode & ~mask) | bits;
  }
  *out = mode;
  return true;
}

// ---- open with ------------------------------------------------------------

struct App
{
  QString id;
  QString name;
  QString exec;
  bool terminal = false;
};

QStringList dataDirs()
{
  QStringList dirs{qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share"))};
  dirs << qEnvironmentVariable("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"))
            .split(QLatin1Char(':'), Qt::SkipEmptyParts);
  return dirs;
}

// type -> apps declaring it, from every mimeinfo.cache, the user's first.
const QHash<QString, QStringList> &handlers()
{
  static QHash<QString, QStringList> map;
  static QDateTime stamp;
  // Read again when a cache changed (an app installed since).
  QDateTime newest;
  for (const QString &d : dataDirs())
    newest = qMax(newest, QFileInfo(d + QStringLiteral("/applications/mimeinfo.cache")).lastModified());
  if (newest == stamp && !map.isEmpty())
    return map;
  stamp = newest;
  map.clear();
  for (const QString &d : dataDirs()) {
    QFile f(d + QStringLiteral("/applications/mimeinfo.cache"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    while (!f.atEnd()) {
      const QString line = QString::fromUtf8(f.readLine()).trimmed();
      const int eq = line.indexOf(QLatin1Char('='));
      if (eq <= 0 || line.startsWith(QLatin1Char('[')))
        continue;
      QStringList &apps = map[line.left(eq)];
      for (const QString &a : line.mid(eq + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts))
        if (!apps.contains(a))
          apps << a;
    }
  }
  return map;
}

bool readApp(const QString &id, App *app)
{
  for (const QString &d : dataDirs()) {
    QFile f(d + QStringLiteral("/applications/") + id);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    bool entry = false;
    app->id = id;
    while (!f.atEnd()) {
      const QString line = QString::fromUtf8(f.readLine()).trimmed();
      if (line.startsWith(QLatin1Char('['))) {
        entry = line == QLatin1String("[Desktop Entry]");
        continue;
      }
      if (!entry)
        continue;
      if (line.startsWith(QLatin1String("Name=")) && app->name.isEmpty())
        app->name = line.mid(5);
      else if (line.startsWith(QLatin1String("Exec=")) && app->exec.isEmpty())
        app->exec = line.mid(5);
      else if (line == QLatin1String("Terminal=true"))
        app->terminal = true;
    }
    return !app->exec.isEmpty();
  }
  return false;
}

// Exec= split the way the desktop entry spec quotes it.
QStringList splitExec(const QString &exec)
{
  QStringList out;
  QString cur;
  bool quoted = false, any = false;
  for (int i = 0; i < exec.size(); ++i) {
    const QChar c = exec.at(i);
    if (quoted && c == QLatin1Char('\\') && i + 1 < exec.size()) {
      cur += exec.at(++i);
    } else if (c == QLatin1Char('"')) {
      quoted = !quoted;
      any = true;
    } else if (c == QLatin1Char(' ') && !quoted) {
      if (any || !cur.isEmpty())
        out << cur;
      cur.clear();
      any = false;
    } else {
      cur += c;
    }
  }
  if (any || !cur.isEmpty())
    out << cur;
  return out;
}

// Starts `app` on `files`: once with all of them for %F/%U, once each
// for %f/%u, in foot if it's a terminal program.
bool launch(const App &app, const QStringList &files)
{
  const QStringList tmpl = splitExec(app.exec);
  if (tmpl.isEmpty())
    return false;
  const bool many = app.exec.contains(QLatin1String("%F")) || app.exec.contains(QLatin1String("%U"));
  const bool coded = many || app.exec.contains(QLatin1String("%f")) || app.exec.contains(QLatin1String("%u"));
  QList<QStringList> runs;
  if (many || !coded)
    runs << files;
  else
    for (const QString &f : files)
      runs << QStringList{f};

  bool ok = true;
  for (const QStringList &run : runs) {
    QStringList args;
    for (const QString &t : tmpl) {
      if (t == QLatin1String("%F") || t == QLatin1String("%U")) {
        for (const QString &f : run)
          args << (t == QLatin1String("%U") ? QUrl::fromLocalFile(f).toString() : f);
      } else if (t == QLatin1String("%f") || t == QLatin1String("%u")) {
        args << (t == QLatin1String("%u") ? QUrl::fromLocalFile(run.first()).toString() : run.first());
      } else if (t == QLatin1String("%i") || t == QLatin1String("%c") || t == QLatin1String("%k")
                 || (t.size() == 2 && t.startsWith(QLatin1Char('%')))) {
        continue; // icon, name, desktop file: nothing an app needs to open a file
      } else {
        QString a = t;
        a.replace(QLatin1String("%%"), QLatin1String("%"));
        args << a;
      }
    }
    if (!coded)
      args << run;
    if (app.terminal)
      args = QStringList{QStringLiteral("foot"), QStringLiteral("-e")} + args;
    ok = QProcess::startDetached(args.first(), args.mid(1), QFileInfo(run.first()).absolutePath()) && ok;
  }
  return ok;
}

// Every app for a file: its type, the type's other names and every type
// it descends from, nearest first; the system default leading.
QList<App> appsFor(const QString &path)
{
  const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
  QStringList types{mime.name()};
  types << mime.aliases() << mime.allAncestors();
  QList<App> apps;
  QSet<QString> seenId, seenName;
  for (const QString &t : types) {
    for (const QString &id : handlers().value(t)) {
      if (seenId.contains(id))
        continue;
      seenId.insert(id);
      App a;
      if (!readApp(id, &a) || seenName.contains(a.name))
        continue; // one per name: google-chrome and its com.google.Chrome twin
      seenName.insert(a.name);
      apps << a;
    }
  }
  // xdg-mime's own answer for the default, so this agrees with xdg-open.
  QProcess q;
  q.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mime.name()});
  if (q.waitForFinished(1500)) {
    const QString def = QString::fromUtf8(q.readAllStandardOutput()).trimmed();
    for (int i = 0; i < apps.size(); ++i)
      if (apps[i].id == def) {
        apps.move(i, 0);
        apps[0].name += QObject::tr("  [DEFAULT]");
        break;
      }
  }
  return apps;
}

} // namespace

void MainWindow::changeMode(const QString &spec)
{
  if (refuseInArchive())
    return;
  const QStringList paths = targets();
  if (paths.isEmpty())
    return;
  FileJobs::Pairs before;
  QStringList failed;
  QString shown;
  for (const QString &p : paths) {
    struct stat st;
    if (lstat(QFile::encodeName(p).constData(), &st) != 0 || S_ISLNK(st.st_mode))
      continue; // a link's own mode means nothing
    mode_t mode;
    if (!applyModeSpec(spec, st.st_mode, S_ISDIR(st.st_mode), &mode)) {
      flash(tr("chmod: \"%1\" isn't a mode (755, +x, u+w,go-r...)").arg(spec), true);
      return;
    }
    if (mode == (st.st_mode & 07777))
      continue;
    if (::chmod(QFile::encodeName(p).constData(), mode) != 0) {
      failed << QFileInfo(p).fileName();
      continue;
    }
    before << FileJobs::Pair{p, QString::number(st.st_mode & 07777, 8)};
    shown = QString::number(mode, 8);
  }
  if (!before.isEmpty())
    pushUndo(UndoStep::Chmoded, before, tr("permission change on %n item(s)", nullptr, int(before.size())));
  if (!failed.isEmpty())
    flash(tr("chmod: not allowed on %1").arg(failed.join(QStringLiteral(", "))), true);
  else if (before.isEmpty())
    flash(tr("chmod: nothing to change"));
  else
    flash(before.size() == 1 ? tr("%1 is now %2").arg(QFileInfo(before.first().from).fileName(), shown)
                             : tr("%n item(s) changed  (Ctrl+Z undoes)", nullptr, int(before.size())));
  m_view->viewport()->update();
  updateInfo();
}

void MainWindow::undoModes(const FileJobs::Pairs &pairs)
{
  for (const FileJobs::Pair &p : pairs)
    ::chmod(QFile::encodeName(p.from).constData(), mode_t(p.to.toUInt(nullptr, 8)));
  m_view->viewport()->update();
  updateInfo();
}

void MainWindow::fillOpenWith(QMenu *menu)
{
  const QStringList files = targets();
  if (files.isEmpty())
    return;
  const QList<App> apps = appsFor(files.first());
  for (const App &a : apps) {
    QAction *act = menu->addAction(a.name);
    connect(act, &QAction::triggered, this, [this, a, files] {
      if (!launch(a, files))
        flash(tr("couldn't start %1").arg(a.name), true);
    });
  }
  if (apps.isEmpty())
    menu->addAction(tr("nothing installed says it opens this"))->setEnabled(false);
  menu->addSeparator();
  menu->addAction(tr("Other command..."), this, [this] {
    openPrompt(Prompt::Command, QStringLiteral("openwith "));
  });
}

void MainWindow::showOpenWith()
{
  if (m_inArchive || targets().isEmpty())
    return;
  QMenu menu(this);
  fillOpenWith(&menu);
  const QModelIndex c = cursor();
  const QPoint at = c.isValid() ? m_view->visualRect(c).bottomLeft() + QPoint(40, 0) : QPoint(40, 40);
  menu.exec(m_view->viewport()->mapToGlobal(at));
}

void MainWindow::openWithCommand(const QString &command)
{
  const QStringList files = targets();
  const QStringList args = splitExec(command);
  if (files.isEmpty() || args.isEmpty())
    return;
  if (!QProcess::startDetached(args.first(), args.mid(1) + files, m_currentDir))
    flash(tr("couldn't start %1").arg(args.first()), true);
}
