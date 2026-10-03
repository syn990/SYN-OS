#include "Jobs.h"

#include <QCoreApplication>
#include <QPointer>

namespace Jobs {

Manager::Manager(QObject *parent)
  : QObject(parent)
{
  m_pool.setMaxThreadCount(4);
  m_tick.setInterval(100);
  connect(&m_tick, &QTimer::timeout, this, [this] {
    emit tick();
    if (running() == 0)
      m_tick.stop();
  });
}

Manager::~Manager()
{
  cancelAll();
  m_pool.waitForDone(3000);
}

JobPtr Manager::start(const QString &title, Work work, Done done)
{
  auto job = std::make_shared<Job>();
  job->id = m_nextId++;
  job->title = title;
  job->started = QDateTime::currentDateTime();
  job->clock.start();
  m_jobs << job;

  QPointer<Manager> self(this);
  auto progress = job->progress;
  m_pool.start([self, job, progress, work, done] {
    work(*progress);
    QMetaObject::invokeMethod(qApp, [self, job, progress, done] {
      if (!self)
        return;
      job->elapsedMs = job->clock.elapsed();
      if (progress->cancel.load())
        job->state = State::Cancelled;
      else if (!progress->errors().isEmpty())
        job->state = State::Failed;
      else
        job->state = State::Done;
      if (done)
        done(*job);
      emit self->jobsChanged();
      emit self->tick();
    }, Qt::QueuedConnection);
  });

  m_tick.start();
  emit jobsChanged();
  return job;
}

int Manager::running() const
{
  int n = 0;
  for (const JobPtr &j : m_jobs)
    if (j->state == State::Running)
      ++n;
  return n;
}

double Manager::overall() const
{
  qint64 done = 0, total = 0;
  for (const JobPtr &j : m_jobs) {
    if (j->state != State::Running)
      continue;
    const qint64 t = j->progress->total.load();
    if (t <= 0)
      continue;
    // Each job weighs the same, whether it counts bytes or items.
    done += qint64(1000.0 * j->progress->done.load() / t);
    total += 1000;
  }
  return total > 0 ? double(done) / double(total) : -1.0;
}

void Manager::cancelAll()
{
  for (const JobPtr &j : m_jobs)
    if (j->state == State::Running)
      j->progress->cancel = true;
}

void Manager::clearFinished()
{
  m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                              [](const JobPtr &j) { return j->state != State::Running; }),
               m_jobs.end());
  emit jobsChanged();
}

void Manager::waitForAll(int msecs)
{
  m_pool.waitForDone(msecs);
}

} // namespace Jobs
