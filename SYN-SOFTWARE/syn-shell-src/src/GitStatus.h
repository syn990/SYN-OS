// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   GitStatus: what git thinks of the folder being shown. The repository
//   is found by walking up for a .git (no process for folders outside
//   one), then `git status --porcelain=v2 --branch` runs in the
//   background (GIT_OPTIONAL_LOCKS=0, so it never fights a git in the
//   shell for the index lock) and its answer becomes:
//
//     - a Mark per path, git's own short-status letters (X staged, Y not),
//       untracked "??", ignored, and "dirty" for every folder above a
//       change, so a closed folder still says something inside changed;
//     - the branch, commits ahead and behind, and counts for the strip.
//
//   Asked again (debounced) whenever the window thinks something moved,
//   and every few seconds so a commit in the shell area shows up; a slow
//   repository is asked less often.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

class QProcess;

class GitStatus : public QObject
{
  Q_OBJECT

public:
  struct Mark
  {
    char x = ' ';   // staged state: M A D R C U, ' ' for none
    char y = ' ';   // worktree state
    bool untracked = false;
    bool ignored = false;
    bool dirty = false; // a folder with a change somewhere under it
    bool any() const { return x != ' ' || y != ' ' || untracked || ignored || dirty; }
  };

  explicit GitStatus(QObject *parent = nullptr);

  // The folder now shown: finds its repository, asks git if it changed.
  void setFolder(const QString &dir);
  // Something may have changed: ask again shortly.
  void poke();
  void setEnabled(bool on);
  bool enabled() const { return m_enabled; }

  bool inRepo() const { return m_enabled && !m_root.isEmpty() && m_valid; }
  QString root() const { return m_root; }
  QString branch() const { return m_branch; }
  int ahead() const { return m_ahead; }
  int behind() const { return m_behind; }
  int staged() const { return m_staged; }
  int changed() const { return m_changed; }
  int untracked() const { return m_untracked; }
  int conflicts() const { return m_conflicts; }
  Mark markFor(const QString &absolutePath) const;

  static QString findRoot(const QString &dir);

signals:
  void updated();

private:
  void run();
  void parse(const QByteArray &out);

  bool m_enabled = true;
  QString m_root;
  bool m_valid = false;
  QString m_branch;
  int m_ahead = 0, m_behind = 0, m_staged = 0, m_changed = 0, m_untracked = 0, m_conflicts = 0;
  QHash<QString, Mark> m_marks; // absolute path -> mark
  QSet<QString> m_ignoredDirs;  // ignored folders: everything under them too

  QProcess *m_proc = nullptr;
  bool m_again = false;
  QTimer m_debounce;
  QTimer m_poll;
  QElapsedTimer m_took;
};
