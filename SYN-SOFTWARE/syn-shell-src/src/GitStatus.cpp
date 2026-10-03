#include "GitStatus.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

GitStatus::GitStatus(QObject *parent)
  : QObject(parent)
{
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(300);
  connect(&m_debounce, &QTimer::timeout, this, &GitStatus::run);
  m_poll.setInterval(5000);
  connect(&m_poll, &QTimer::timeout, this, &GitStatus::run);
}

QString GitStatus::findRoot(const QString &dir)
{
  QString d = QDir(dir).absolutePath();
  for (;;) {
    if (QFileInfo::exists(d + QStringLiteral("/.git")))
      return d;
    const QString up = QFileInfo(d).absolutePath();
    if (up == d || d.isEmpty())
      return QString();
    d = up;
  }
}

void GitStatus::setEnabled(bool on)
{
  m_enabled = on;
  if (!on) {
    m_poll.stop();
    m_marks.clear();
    m_ignoredDirs.clear();
    m_valid = false;
    emit updated();
  } else {
    m_root.clear();
  }
}

void GitStatus::setFolder(const QString &dir)
{
  if (!m_enabled)
    return;
  const QString root = findRoot(dir);
  if (root == m_root) {
    if (!root.isEmpty())
      poke();
    return;
  }
  m_root = root;
  m_valid = false;
  m_marks.clear();
  m_ignoredDirs.clear();
  if (root.isEmpty()) {
    m_poll.stop();
    emit updated();
    return;
  }
  run();
  m_poll.start();
}

void GitStatus::poke()
{
  if (m_enabled && !m_root.isEmpty())
    m_debounce.start();
}

void GitStatus::run()
{
  if (!m_enabled || m_root.isEmpty())
    return;
  if (m_proc) {
    m_again = true; // one already running: go once more after it
    return;
  }
  m_proc = new QProcess(this);
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
  env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
  m_proc->setProcessEnvironment(env);
  const QString root = m_root;
  connect(m_proc, &QProcess::finished, this, [this, root](int code) {
    const QByteArray out = m_proc->readAllStandardOutput();
    m_proc->deleteLater();
    m_proc = nullptr;
    // A repository that takes a while gets asked less often.
    m_poll.setInterval(int(qBound(qint64(5000), qint64(m_took.elapsed() * 20), qint64(60000))));
    if (root == m_root) {
      if (code == 0) {
        parse(out);
        m_valid = true;
      } else {
        m_valid = false; // not a repository after all, or git missing
      }
      emit updated();
    }
    if (m_again) {
      m_again = false;
      m_debounce.start();
    }
  });
  connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
    if (e != QProcess::FailedToStart)
      return;
    m_proc->deleteLater();
    m_proc = nullptr;
    m_enabled = false; // no git on this system: stop trying
    m_poll.stop();
    emit updated();
  });
  m_took.start();
  m_proc->start(QStringLiteral("git"),
                {QStringLiteral("-C"), root, QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                 QStringLiteral("-z"), QStringLiteral("--branch"), QStringLiteral("--untracked-files=normal"),
                 QStringLiteral("--ignored=matching")});
  QTimer::singleShot(10000, m_proc, [p = m_proc] { p->kill(); });
}

void GitStatus::parse(const QByteArray &out)
{
  m_marks.clear();
  m_ignoredDirs.clear();
  m_branch.clear();
  m_ahead = m_behind = m_staged = m_changed = m_untracked = m_conflicts = 0;

  auto markDirty = [this](QString path) {
    // Every folder from the change up to the top of the repository.
    for (;;) {
      const int slash = path.lastIndexOf(QLatin1Char('/'));
      if (slash <= 0)
        break;
      path.truncate(slash);
      if (path.size() < m_root.size())
        break;
      Mark &m = m_marks[path];
      if (m.dirty)
        break; // the rest above it is already marked
      m.dirty = true;
    }
  };

  const QList<QByteArray> records = out.split('\0');
  for (int i = 0; i < records.size(); ++i) {
    const QByteArray &r = records.at(i);
    if (r.isEmpty())
      continue;
    if (r.startsWith("# branch.head ")) {
      m_branch = QString::fromUtf8(r.mid(14));
    } else if (r.startsWith("# branch.ab ")) {
      const QList<QByteArray> ab = r.mid(12).split(' ');
      if (ab.size() == 2) {
        m_ahead = ab[0].mid(1).toInt();
        m_behind = ab[1].mid(1).toInt();
      }
    } else if (r.startsWith('1') || r.startsWith('2') || r.startsWith('u')) {
      // "1 XY sub mH mI mW hH hI path", "2 ... Xscore path" (then the
      // original path as the next record), "u XY ... h3 path".
      // The path is everything after that many fields: it may hold spaces.
      const int fields = r.startsWith('1') ? 8 : r.startsWith('2') ? 9 : 10;
      int pos = 0;
      for (int f = 0; f < fields && pos >= 0; ++f) {
        pos = int(r.indexOf(' ', pos));
        if (pos >= 0)
          ++pos;
      }
      if (pos <= 0 || r.size() < 4)
        continue;
      const QByteArray xy = r.mid(2, 2);
      const QString rel = QString::fromUtf8(r.mid(pos));
      const QString abs = m_root + QLatin1Char('/') + rel;
      Mark &m = m_marks[abs];
      m.x = xy.at(0) == '.' ? ' ' : xy.at(0);
      m.y = xy.at(1) == '.' ? ' ' : xy.at(1);
      if (r.startsWith('u')) {
        ++m_conflicts;
      } else {
        if (m.x != ' ')
          ++m_staged;
        if (m.y != ' ')
          ++m_changed;
      }
      markDirty(abs);
      if (r.startsWith('2'))
        ++i; // skip the original path
    } else if (r.startsWith("? ") || r.startsWith("! ")) {
      QString rel = QString::fromUtf8(r.mid(2));
      const bool dir = rel.endsWith(QLatin1Char('/'));
      if (dir)
        rel.chop(1);
      const QString abs = m_root + QLatin1Char('/') + rel;
      Mark &m = m_marks[abs];
      if (r.startsWith('?')) {
        m.untracked = true;
        ++m_untracked;
        markDirty(abs);
      } else {
        m.ignored = true;
        if (dir)
          m_ignoredDirs.insert(abs);
      }
    }
  }
}

GitStatus::Mark GitStatus::markFor(const QString &absolutePath) const
{
  if (!inRepo())
    return Mark();
  const auto it = m_marks.constFind(absolutePath);
  if (it != m_marks.constEnd())
    return it.value();
  // Inside an ignored folder: ignored too.
  if (!m_ignoredDirs.isEmpty()) {
    QString p = absolutePath;
    for (int slash = p.lastIndexOf(QLatin1Char('/')); slash > m_root.size();
         slash = p.lastIndexOf(QLatin1Char('/'))) {
      p.truncate(slash);
      if (m_ignoredDirs.contains(p)) {
        Mark m;
        m.ignored = true;
        return m;
      }
    }
  }
  return Mark();
}
