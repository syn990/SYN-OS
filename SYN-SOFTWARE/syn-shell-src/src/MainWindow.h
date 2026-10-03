// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow: ranger's layout as a desktop window. Three columns over
//   one QFileSystemModel (through FileSortProxy): the parent directory,
//   the current one, and a preview of whatever the cursor is on. A waybar-style segment strip
//   runs across the top (history, path, pending keys, position, marks,
//   disk), an ls -l line for the cursor sits at the bottom, and that line
//   doubles as the / search and : command prompt.
//
//   Keyboard first, with ranger's keys (hjkl, gg/G, yy/dd/pp, cw, dD,
//   space to mark, / and :); every action is also on the labwc-style
//   right-click menu, which prints the key next to it. No toolbar.
//
//   The cursor (current index) and the marks (the selection) are kept
//   apart, as in ranger: moving never touches the marks, and an action
//   applies to the marks if there are any, otherwise to the cursor.
//
//   A terminal area along the bottom (TermView, MainWindowShell.cpp) runs
//   the user's shell, linked both ways to the browser, and can hand it to
//   foot without restarting it.
//
//   Archives open like folders (MainWindowArchive.cpp): the same columns
//   walk an ArchiveModel instead of the disk, read-only, with extract,
//   copy-out and compress done by libarchive on a worker thread.
//
//   Colours come from the application palette (qt6ct renders the active
//   SYN theme into it), so the stylesheet only names palette roles.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include "FileJobs.h"

#include <QHash>
#include <atomic>
#include <functional>
#include <memory>
#include <QMainWindow>
#include <QStringList>

class ArchiveSession;
class QAbstractItemModel;
class QAction;
class QVariantAnimation;
class QFileSystemModel;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListView;
class QSplitter;
class QStackedWidget;
class QTemporaryDir;
class QTimer;
class QToolButton;
class FileSortProxy;
class Preview;
class TermView;
class GitStatus;
class JobsWindow;
namespace Jobs { class Manager; struct Job; struct Progress; }
class FieldHeader;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(const QString &startPath, QWidget *parent = nullptr);
  ~MainWindow() override; // out of line: unique_ptrs to forward-declared types

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  enum class Prompt { None, Search, Command };

  void buildActions();
  QWidget *buildHeader();
  QWidget *buildFooter();
  void applyStyle();

  bool navigateTo(const QString &path, bool recordHistory = true);
  void goBack();
  void goForward();
  void navigateUp();
  void openCursor();
  void openTerminal(const QString &dir, const QString &command = QString());

  void renameCursor();
  void deleteTargets();
  void deletePermanently();
  void yank(bool cut);
  void paste();
  void copyPaths();
  void newEntry(bool folder);
  void setShowHidden(bool show);

  bool handleKey(QKeyEvent *ev);
  void moveCursor(int delta);
  void setCursorRow(int row);
  void toggleMark();
  void clearMarks();
  bool search(const QString &needle, int from, int step);

  void openPrompt(Prompt kind, const QString &text = QString());
  void closePrompt();
  void runCommand(const QString &line);

  void showContextMenu(const QPoint &pos);
  void beginPathEdit();
  void endPathEdit();
  void rebuildCrumbs();

  void onCursorChanged();
  void updatePreview();
  void updateInfo();
  void updateDisk();
  void tryPending();
  void flash(const QString &message, bool error = false);
  void showHelp();

  QModelIndex cursor() const;
  QStringList targets() const;
  QString detailLine(const QString &path) const;
  QStringList deviceRoots() const;

  // Locations: a folder on disk, or archive file + '\x1f' + folder inside.
  QString location() const;
  QString cursorKey(const QModelIndex &idx) const;
  QModelIndex indexForKey(const QString &key) const;
  void goToLocation(const QString &loc, bool recordHistory);
  void leaveLocation(bool recordHistory);
  void setViewModel(QListView *view, QAbstractItemModel *model);
  void updateHistoryButtons();

  // Archives (MainWindowArchive.cpp)
  void openArchive(const QString &file, const QString &inner, bool recordHistory,
                   const QString &password = QString());
  void enterArchiveDir(const QString &inner, bool recordHistory, bool leave = true);
  void leaveArchive();
  QStringList archiveTargets() const;
  bool refuseInArchive();
  void extractHere();
  void extractArchiveFile(const QString &file, const QString &dest, const QString &password);
  void extractEntries(const QStringList &entries, const QString &dest, const QString &verb,
                      std::function<void(const QStringList &)> done);
  void compress(const QString &name);
  void openEntry(const QString &entry);
  void openExternal();
  QString tempSlot();
  QString askPassword(const QString &file, bool retry);
  void previewArchiveFile(const QString &file);
  void previewEntry(const QString &entry);
  QString entryDetail(const QModelIndex &idx) const;
  bool startJob(const QString &label,
                std::function<void(const std::atomic_bool *, const std::function<void(double)> &)> work,
                std::function<void()> done);
  void cancelPreview();

  // File jobs, trash and undo (MainWindowFiles.cpp)
  struct UndoStep
  {
    enum Kind { Trashed, Moved, Copied, Renamed, Created } kind;
    FileJobs::Pairs pairs;
    QString label; // "trash of 3 items"
  };
  void buildJobs();
  void showJobs();
  void updateJobButton();
  void runFileJob(const QString &title, std::function<FileJobs::Pairs(Jobs::Progress &)> work,
                  std::function<void(const FileJobs::Pairs &, Jobs::Job &)> done);
  bool inTrashView() const;
  void restoreTargets();
  void emptyTrash();
  void openTrash();
  void pushUndo(UndoStep::Kind kind, const FileJobs::Pairs &pairs, const QString &label);
  void undo();

  // Tabs (MainWindowTabs.cpp): each its own place, history and cursor.
  struct Tab
  {
    QString location;
    QStringList back, forward;
  };
  QWidget *buildTabBar();
  void newTab(const QString &location);
  void closeTab(int index);
  void switchTab(int index);
  void updateTabBar();

  void updateGitSegment();
  QString gitWords(const QString &path) const;

  // Shell area (MainWindowShell.cpp)
  void buildShell();
  void toggleShellPane();
  void toggleShellFocus();
  void showShell(bool focus);
  void hideShell();
  void detachShell();
  void syncShell();
  void onShellCwd(const QString &dir);
  void onShellFinished(bool detached);

  // Bulk rename (MainWindowRename.cpp): names edited in $EDITOR in the
  // shell area, applied in two passes so swaps work, one undo for all.
  void bulkRename();
  void finishBulkRename();
  static QString renameAll(const FileJobs::Pairs &pairs);
  QString m_bulkDir;
  QStringList m_bulkNames;
  QString m_bulkFile;
  bool m_bulkPaneWasOpen = false;

  // Zoom of everything but the shell (the shell zooms itself)
  void zoomUi(int steps);
  void toggleField(int field);
  void applyUiFont(qreal px);

  QFileSystemModel *m_model;
  FileSortProxy *m_proxy;   // what every view shows: sorted, dirs first
  QSplitter *m_split;
  QSplitter *m_vsplit;   // the columns above, the shell area below
  TermView *m_term;
  QStackedWidget *m_termStack;
  TermView *m_task = nullptr; // a program in the shell area's place (bulk rename)
  QToolButton *m_shellBtn;
  QListView *m_parentView;
  QListView *m_view;
  FieldHeader *m_fieldHeader;
  Preview *m_preview;

  QStackedWidget *m_pathStack;
  QWidget *m_crumbs;
  QHBoxLayout *m_crumbLayout;
  QLineEdit *m_pathEdit;
  QToolButton *m_backBtn;
  QToolButton *m_fwdBtn;
  QLabel *m_keySeg;
  QLabel *m_posSeg;
  QLabel *m_markSeg;
  QLabel *m_fsSeg;
  QLabel *m_gitSeg;
  GitStatus *m_git;
  QToolButton *m_jobBtn;
  QToolButton *m_hiddenBtn;

  QStackedWidget *m_footer;
  QLabel *m_detail;
  QLabel *m_mime;
  QLabel *m_promptLabel;
  QLineEdit *m_prompt;
  Prompt m_promptKind = Prompt::None;

  QTimer *m_previewTimer;
  QTimer *m_flashTimer;

  QAction *m_actOpen, *m_actTerminal;
  QAction *m_actCopy, *m_actCut, *m_actPaste, *m_actRename, *m_actDelete, *m_actCopyPath;
  QAction *m_actMark, *m_actClearMarks;
  QAction *m_actNewFolder, *m_actNewFile;
  QAction *m_actBack, *m_actForward, *m_actUp, *m_actHome, *m_actRoot;
  QAction *m_actHidden, *m_actRefresh, *m_actHelp;
  QAction *m_actExtract, *m_actExtractTo, *m_actCompress, *m_actOpenExternal;
  QAction *m_actShell, *m_actDetachShell, *m_actIcons;
  QAction *m_actUndo, *m_actDeleteForever, *m_actRestore, *m_actEmptyTrash, *m_actTrash;
  QAction *m_actNewTab, *m_actOpenInTab, *m_actCloseTab, *m_actGit, *m_actBulkRename;
  QList<QAction *> m_actFields;

  QString m_currentDir;
  QStringList m_back;
  QStringList m_forward;
  QHash<QString, std::function<void()>> m_bindings;
  QString m_keys;            // pending multi-key sequence: g, y, d, p, c, z
  QString m_lastSearch;
  int m_searchOrigin = 0;
  int m_lastRow = 0;         // where the cursor falls back to when its row is deleted

  // Where the cursor last sat in each directory visited, so going back
  // into one (or up out of one) lands where you left it, as ranger does.
  QHash<QString, QString> m_lastCursor;

  // A path to put the cursor on (and optionally start renaming) once the
  // model has loaded it: QFileSystemModel populates asynchronously, so a
  // just-created file isn't an index yet at the moment we ask for it.
  QString m_pendingPath;
  bool m_pendingEdit = false;

  // True from entering a directory until the user moves or a pending path
  // lands: rows arrive and sort in batches, so "the top" keeps changing
  // while it loads and the cursor has to follow it there.
  bool m_cursorAuto = false;

  // The archive being browsed (kept after leaving, so going back in is
  // instant unless the file changed), and where in it the view is.
  std::unique_ptr<ArchiveSession> m_session;
  bool m_inArchive = false;
  QString m_arcDir;

  // Everything slow (copy, move, trash, extract, compress) runs as a job.
  Jobs::Manager *m_jobs;
  JobsWindow *m_jobsWindow;
  bool m_jobsAutoShown = false;
  QList<UndoStep> m_undo;

  QList<Tab> m_tabs;   // the current tab's entry is only brought up to date on leaving it
  int m_tab = 0;
  QWidget *m_tabBar;
  QHBoxLayout *m_tabLayout;
  // Previews of archives and their entries read on a worker too; a newer
  // preview cancels the one in flight and its result is dropped.
  std::shared_ptr<std::atomic_bool> m_previewCancel;
  quint64 m_previewGen = 0;
  // Where opened and copied-out entries are extracted; removed on exit.
  std::unique_ptr<QTemporaryDir> m_tempDir;
  int m_tempSlots = 0;

  int m_uiPx = 12;
  QVariantAnimation *m_uiZoomAnim;
  int m_uiWheelAcc = 0;
};
