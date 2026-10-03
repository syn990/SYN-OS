#include "JobsWindow.h"
#include "RowDelegate.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString duration(qint64 secs)
{
  if (secs < 0)
    return QStringLiteral("?");
  if (secs >= 3600)
    return QStringLiteral("%1:%2:%3").arg(secs / 3600).arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
                                     .arg(secs % 60, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

JobList::JobList(Jobs::Manager *jobs, QWidget *parent)
  : QWidget(parent), m_jobs(jobs)
{
  setObjectName("jobList");
  setAttribute(Qt::WA_OpaquePaintEvent);
}

int JobList::rowHeight() const
{
  return fontMetrics().height() * 3 + 22;
}

QSize JobList::sizeHint() const
{
  return QSize(520, qMax(1, int(m_jobs->jobs().size())) * rowHeight());
}

void JobList::refresh()
{
  // Speed: bytes since the last look, smoothed so it doesn't jitter.
  for (const Jobs::JobPtr &j : m_jobs->jobs()) {
    if (j->state != Jobs::State::Running)
      continue;
    Rate &r = m_rates[j->id];
    const qint64 now = j->clock.elapsed();
    const qint64 done = j->progress->done.load();
    if (now - r.lastMs >= 400) {
      const double instant = (done - r.lastDone) * 1000.0 / qMax<qint64>(1, now - r.lastMs);
      r.perSecond = r.lastMs == 0 ? instant : r.perSecond * 0.6 + instant * 0.4;
      r.lastMs = now;
      r.lastDone = done;
    }
  }
  m_phase = (m_phase + 1) % 40;
  setMinimumHeight(sizeHint().height());
  QWidget::update();
}

void JobList::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  const QPalette &pal = palette();
  p.fillRect(rect(), pal.color(QPalette::Window));
  const QFontMetrics fm(font());
  QFont bold = font();
  bold.setBold(true);
  const QColor text = pal.color(QPalette::Text);
  const QColor accent = pal.color(QPalette::Highlight);
  QColor dim = text;
  dim.setAlphaF(0.55);
  const int lh = fm.height();
  m_cancelHits.clear();

  if (m_jobs->jobs().isEmpty()) {
    p.setPen(dim);
    p.drawText(rect().adjusted(14, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("no jobs"));
    return;
  }

  int y = 0;
  // Newest first.
  const QList<Jobs::JobPtr> &jobs = m_jobs->jobs();
  for (auto it = jobs.crbegin(); it != jobs.crend(); ++it) {
    const Jobs::Job &j = **it;
    const Jobs::Progress &pr = *j.progress;
    const QRect row(0, y, width(), rowHeight());
    const QRect inner = row.adjusted(14, 8, -14, -8);
    y += rowHeight();
    p.fillRect(QRect(row.left(), row.bottom(), row.width(), 1), pal.color(QPalette::Button));

    // Line 1: what it is, and where it stands.
    QString tag;
    switch (j.state) {
    case Jobs::State::Running: {
      const double f = pr.fraction();
      tag = f < 0 ? tr("RUNNING") : QStringLiteral("%1%").arg(int(f * 100));
      break;
    }
    case Jobs::State::Done: tag = tr("DONE"); break;
    case Jobs::State::Failed: tag = tr("FAILED"); break;
    case Jobs::State::Cancelled: tag = tr("CANCELLED"); break;
    }
    p.setFont(bold);
    const int tagWidth = QFontMetrics(bold).horizontalAdvance(tag) + 12;
    const QRect tagRect(inner.right() - tagWidth + 1, inner.top(), tagWidth, lh);
    if (j.state == Jobs::State::Failed || j.state == Jobs::State::Running) {
      p.fillRect(tagRect, j.state == Jobs::State::Failed ? accent : pal.color(QPalette::Button));
      p.setPen(j.state == Jobs::State::Failed ? QColor(Qt::white) : text);
    } else {
      p.setPen(j.state == Jobs::State::Done ? accent : dim);
    }
    p.drawText(tagRect, Qt::AlignCenter, tag);
    p.setPen(j.state == Jobs::State::Running ? text : dim);
    p.drawText(QRect(inner.left(), inner.top(), inner.width() - tagWidth - 8, lh),
               Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(bold).elidedText(j.title, Qt::ElideRight, inner.width() - tagWidth - 8));
    p.setFont(font());

    // Line 2: the bar. A job that doesn't know its size yet gets a block
    // walking along it.
    const QRect bar(inner.left(), inner.top() + lh + 5, inner.width(), 5);
    p.fillRect(bar, pal.color(QPalette::Base));
    const double f = j.state == Jobs::State::Done ? 1.0 : pr.fraction();
    if (f >= 0) {
      p.fillRect(QRect(bar.left(), bar.top(), int(bar.width() * f), bar.height()),
                 j.state == Jobs::State::Running || j.state == Jobs::State::Done ? accent : dim);
    } else if (j.state == Jobs::State::Running) {
      const int block = bar.width() / 5;
      const int x = bar.left() + (bar.width() - block) * (m_phase < 20 ? m_phase : 40 - m_phase) / 20;
      p.fillRect(QRect(x, bar.top(), block, bar.height()), accent);
    }

    // Line 3: the file it's on, speed, time left; or how it ended.
    const QRect info(inner.left(), bar.bottom() + 6, inner.width(), lh);
    QString line;
    if (j.state == Jobs::State::Running) {
      QStringList parts;
      const qint64 done = pr.done.load(), total = pr.total.load();
      const double rate = m_rates.value(j.id).perSecond;
      if (pr.bytes.load() && total > 0) {
        parts << tr("%1 of %2").arg(humanSize(done), humanSize(total));
        if (rate > 1)
          parts << humanSize(qint64(rate)) + QStringLiteral("/s")
                << tr("%1 left").arg(duration(qint64((total - done) / rate)));
      } else if (total > 0 && pr.counts.load()) {
        parts << tr("%1 of %2").arg(done).arg(total);
      }
      parts << duration(j.clock.elapsed() / 1000);
      const QString stats = parts.join(QStringLiteral("  ·  "));
      const QString cancel = tr("[CANCEL]");
      const int cw = fm.horizontalAdvance(cancel);
      const QRect cancelRect(info.right() - cw + 1, info.top(), cw, lh);
      m_cancelHits.insert(j.id, cancelRect);
      p.setPen(accent);
      p.drawText(cancelRect, Qt::AlignVCenter | Qt::AlignRight, cancel);
      const int statsWidth = fm.horizontalAdvance(stats);
      p.setPen(dim);
      p.drawText(QRect(info.right() - cw - 16 - statsWidth, info.top(), statsWidth, lh),
                 Qt::AlignVCenter | Qt::AlignRight, stats);
      line = fm.elidedText(pr.current(), Qt::ElideMiddle, info.width() - cw - statsWidth - 32);
      p.setPen(text);
      p.drawText(info, Qt::AlignVCenter | Qt::AlignLeft, line);
    } else {
      const QStringList errors = pr.errors();
      line = j.summary;
      if (!errors.isEmpty())
        line += (line.isEmpty() ? QString() : QStringLiteral("  ·  ")) + errors.first()
              + (errors.size() > 1 ? tr("  (+%1 more)").arg(errors.size() - 1) : QString());
      line += QStringLiteral("  ·  ") + duration(j.elapsedMs / 1000);
      p.setPen(errors.isEmpty() ? dim : accent);
      p.drawText(info, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(line, Qt::ElideMiddle, info.width()));
    }
  }
  if (y < height())
    p.fillRect(QRect(0, y, width(), height() - y), pal.color(QPalette::Window));
}

void JobList::mousePressEvent(QMouseEvent *event)
{
  for (auto it = m_cancelHits.cbegin(); it != m_cancelHits.cend(); ++it) {
    if (!it.value().contains(event->position().toPoint()))
      continue;
    for (const Jobs::JobPtr &j : m_jobs->jobs())
      if (j->id == it.key())
        j->progress->cancel = true;
    return;
  }
}

JobsWindow::JobsWindow(Jobs::Manager *jobs, QWidget *parent)
  : QWidget(parent, Qt::Window), m_jobs(jobs)
{
  setObjectName("jobsWindow");
  setWindowTitle(tr("syn-shell: jobs"));
  resize(620, 360);

  // The same strip the main window has across its top.
  auto *bar = new QWidget(this);
  bar->setObjectName("header");
  auto *h = new QHBoxLayout(bar);
  h->setContentsMargins(0, 0, 4, 0);
  h->setSpacing(0);
  auto *title = new QLabel(tr("JOBS"), bar);
  title->setObjectName("keys"); // the accent segment
  title->setProperty("seg", true);
  m_count = new QLabel(bar);
  m_count->setProperty("seg", true);
  auto *clear = new QToolButton(bar);
  clear->setText(tr("clear finished"));
  connect(clear, &QToolButton::clicked, m_jobs, &Jobs::Manager::clearFinished);
  auto *cancelAll = new QToolButton(bar);
  cancelAll->setText(tr("cancel all"));
  connect(cancelAll, &QToolButton::clicked, m_jobs, &Jobs::Manager::cancelAll);
  h->addWidget(title);
  h->addWidget(m_count);
  h->addStretch(1);
  h->addWidget(clear);
  h->addWidget(cancelAll);

  m_list = new JobList(m_jobs, this);
  auto *scroll = new QScrollArea(this);
  scroll->setObjectName("jobScroll");
  scroll->setWidget(m_list);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  v->addWidget(bar);
  v->addWidget(scroll, 1);

  connect(m_jobs, &Jobs::Manager::tick, this, [this] { refresh(); });
  connect(m_jobs, &Jobs::Manager::jobsChanged, this, [this] { refresh(); });
  refresh();
}

void JobsWindow::refresh()
{
  const int running = m_jobs->running();
  const int total = int(m_jobs->jobs().size());
  m_count->setText(running ? tr("%1 running").arg(running)
                           : tr("%n finished", nullptr, total));
  m_list->refresh();
}
