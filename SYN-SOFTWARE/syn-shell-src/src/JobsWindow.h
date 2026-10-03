// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   JobsWindow: its own window listing every copy, move, delete, trash,
//   extract and compress, running or finished: a bar for how far along,
//   the file it's on, the speed and time left, and [CANCEL] while it
//   runs. Opens by itself when a job is still going after a moment, and
//   from the JOBS segment in the main window's strip.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include "Jobs.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QScrollArea;

class JobList : public QWidget
{
public:
  explicit JobList(Jobs::Manager *jobs, QWidget *parent = nullptr);
  QSize sizeHint() const override;
  void refresh();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;

private:
  struct Rate
  {
    qint64 lastMs = 0;
    qint64 lastDone = 0;
    double perSecond = 0; // smoothed
  };
  int rowHeight() const;

  Jobs::Manager *m_jobs;
  QHash<int, Rate> m_rates;
  QHash<int, QRect> m_cancelHits; // job id -> its [CANCEL] in widget coords
  int m_phase = 0;                // walks the bar of a job whose size isn't known yet
};

class JobsWindow : public QWidget
{
public:
  explicit JobsWindow(Jobs::Manager *jobs, QWidget *parent = nullptr);

private:
  void refresh();

  Jobs::Manager *m_jobs;
  JobList *m_list;
  QLabel *m_count;
};
