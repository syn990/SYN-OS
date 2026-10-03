// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Jobs: everything slow SYN-SHELL does to files (copy, move, delete,
//   trash, restore, extract, compress) runs here, on a pool of its own so
//   a big copy never holds up the previews, and the window stays usable.
//   Several can run at once; JobsWindow lists them.
//
//   A job's worker writes its progress into a Progress (atomics, plus a
//   lock for the strings) and polls its cancel flag; the UI side reads it
//   on a 10 Hz tick instead of being sent a signal per file. When the
//   worker returns, its `done` callback runs on the UI thread.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace Jobs {

struct Progress
{
  std::atomic<qint64> done{0};
  std::atomic<qint64> total{0}; // 0 while unknown
  std::atomic<int> items{0};
  std::atomic<int> itemsTotal{0};
  std::atomic_bool cancel{false};
  std::atomic_bool bytes{true};  // done/total count bytes (else items)
  std::atomic_bool counts{true}; // show done/total at all (not for a bare percentage)

  void setCurrent(const QString &s)
  {
    QMutexLocker lock(&m_mutex);
    m_current = s;
  }
  QString current() const
  {
    QMutexLocker lock(&m_mutex);
    return m_current;
  }
  void addError(const QString &e)
  {
    QMutexLocker lock(&m_mutex);
    m_errors << e;
  }
  QStringList errors() const
  {
    QMutexLocker lock(&m_mutex);
    return m_errors;
  }
  // 0..1, or -1 while the total isn't known yet.
  double fraction() const
  {
    const qint64 t = total.load();
    return t > 0 ? qBound(0.0, double(done.load()) / double(t), 1.0) : -1.0;
  }

private:
  mutable QMutex m_mutex;
  QString m_current;
  QStringList m_errors;
};

enum class State { Running, Done, Failed, Cancelled };

struct Job
{
  int id = 0;
  QString title;           // "Copying 3 items to Downloads"
  QString summary;         // set when it ends: "3 copied", "2 failed"
  State state = State::Running;
  QDateTime started;
  qint64 elapsedMs = 0;    // frozen when it ends
  QElapsedTimer clock;
  std::shared_ptr<Progress> progress = std::make_shared<Progress>();
};
using JobPtr = std::shared_ptr<Job>;

class Manager : public QObject
{
  Q_OBJECT

public:
  using Work = std::function<void(Progress &)>; // on a worker thread
  using Done = std::function<void(Job &)>;      // on the UI thread

  explicit Manager(QObject *parent = nullptr);
  ~Manager() override;

  JobPtr start(const QString &title, Work work, Done done = {});
  const QList<JobPtr> &jobs() const { return m_jobs; }
  int running() const;
  // Of everything running: 0..1, -1 if nothing knows its total yet.
  double overall() const;
  void cancelAll();
  void clearFinished();
  // Waits for the workers (they've been told to stop): used on quit.
  void waitForAll(int msecs);

signals:
  void jobsChanged(); // a job was added, ended or cleared
  void tick();        // progress moved (10 Hz while anything runs)

private:
  QThreadPool m_pool;
  QTimer m_tick;
  QList<JobPtr> m_jobs;
  int m_nextId = 1;
};

} // namespace Jobs
