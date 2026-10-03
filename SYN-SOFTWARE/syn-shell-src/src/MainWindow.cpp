#include "MainWindow.h"
#include "Archive.h"
#include "ArchiveModel.h"
#include "FileOps.h"
#include "FileSortProxy.h"
#include "Preview.h"
#include "RowDelegate.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCompleter>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {

const char kCopiedFilesMime[] = "x-special/gnome-copied-files";

// Every colour is a palette role, which qt6ct fills from the active SYN
// theme: window = SYN_BG, base = SYN_BG_ALT, button = SYN_PANEL,
// light = SYN_PANEL_HOVER, highlight = SYN_ACCENT, mid = SYN_BORDER,
// text = SYN_TEXT. White on the accent matches labwc's active menu item.
const char kStyle[] = R"(
QMainWindow, #central { background: palette(window); }

#header { background: palette(window); border-bottom: 2px solid palette(highlight); }
#header QToolButton, #header QLabel[seg="true"] {
  background: palette(button); color: palette(text); border: none;
  padding: 3px 9px; margin: 4px 0 4px 4px;
}
#header QToolButton:hover { background: palette(light); }
#header QToolButton:disabled { color: palette(mid); }
#header QToolButton:checked { background: palette(highlight); color: #ffffff; }
#header QLabel#keys { background: palette(highlight); color: #ffffff; }
#header QToolButton#crumb { background: transparent; padding: 3px 3px; margin: 4px 0; }
#header QToolButton#crumb:hover { background: palette(light); color: #ffffff; }
#header QToolButton#crumbCurrent {
  background: palette(highlight); color: #ffffff; font-weight: bold;
  padding: 3px 7px; margin: 4px 0;
}
#header QLabel#crumbSep { color: palette(highlight); padding: 0 1px; }
#header QToolButton#crumbArchive {
  background: palette(button); color: palette(highlight); font-weight: bold;
  padding: 3px 7px; margin: 4px 0;
}
#header QLabel#job { background: palette(highlight); color: #ffffff; }
#header QLineEdit {
  background: palette(base); color: palette(text); border: 1px solid palette(highlight);
  padding: 2px 6px; margin: 4px 0 4px 4px;
}

QListView { background: palette(window); border: none; outline: 0; }
QListView#parentPane { background: palette(base); }
QSplitter::handle { background: palette(button); }
QLineEdit { selection-background-color: palette(highlight); selection-color: #ffffff; }

#previewHead {
  background: palette(base); color: palette(highlight); font-weight: bold;
  padding: 3px 8px;
}
QPlainTextEdit#previewText { background: palette(window); color: palette(text); border: none; }
QLabel#previewImage { background: palette(window); color: palette(mid); }

#footer { background: palette(base); border-top: 1px solid palette(button); }
QLabel#detail { color: palette(text); padding: 3px 8px; }
QLabel#detail[flash="info"] { color: #ffffff; }
QLabel#detail[flash="error"] { background: palette(highlight); color: #ffffff; }
QLabel#mime { color: palette(highlight); padding: 3px 8px; }
QLabel#promptLabel { color: palette(highlight); font-weight: bold; padding: 3px 0 3px 8px; }
QLineEdit#prompt { background: palette(base); color: palette(text); border: none; padding: 3px 2px; }

QMenu { background: palette(window); border: 3px solid palette(base); padding: 0; }
QMenu::item { color: palette(highlight); background: transparent; padding: 4px 24px 4px 12px; }
QMenu::item:selected { background: palette(highlight); color: #ffffff; }
QMenu::item:disabled { color: palette(mid); }
QMenu::indicator { width: 0; }
QLabel#menuHead {
  background: palette(base); color: palette(highlight); font-weight: bold;
  padding: 4px 12px;
}
)";

const char kHelp[] = R"(  MOVE
  j k  ↓ ↑        cursor down / up
  gg G            top / bottom
  h  ←  Backspace parent directory
  l  →  Enter     open: enter a folder, xdg-open a file
  H L             history back / forward
  gh ~            home        gr   /
  gm              /run/media  (mounted drives)

  MARK
  Space           mark / unmark, move down
  Ctrl+A          mark everything here
  uv  Esc         clear marks

  FILES  (on the marks, or the cursor if nothing is marked)
  yy  Ctrl+C      copy        dd  Ctrl+X   cut
  pp  Ctrl+V      paste       yp           copy path
  cw  F2          rename      dD  Delete   delete
  F7              new folder

  FIND / VIEW
  /  n N          search, next, previous
  zh  Ctrl+H      hidden files
  Ctrl+L          type a path
  S               terminal here
  R  F5           refresh
  ?               this

  ARCHIVES  (zip 7z rar tar.* iso cpio deb rpm cab lha xar ar, .gz .xz .zst ...)
  l               walk into it like a folder (asks for a password if it needs one)
  X               on an archive: extract here, into a folder only if it needs one
                  inside one: extract the marks (or the cursor) beside the archive
  yy              inside one: copy out, then pp anywhere
  r               open in the default app instead (mpv, feh, ...)
  :compress NAME.tar.zst   pack the marks into a new archive (.zip .7z .tar.* too)
  :extract [DEST]          extract to DEST
  Esc             cancel a running extract / compress

  COMMANDS  (:)
  :cd PATH   :mkdir NAME   :touch NAME   :rename NAME
  :delete    :term         :!COMMAND (runs in a terminal)
  :hidden    :help         :q
)";

QString expandPath(const QString &input, const QString &base)
{
  QString p = input.trimmed();
  if (p == QLatin1String("~"))
    return QDir::homePath();
  if (p.startsWith(QLatin1String("~/")))
    p = QDir::homePath() + p.mid(1);
  return QDir::cleanPath(QDir(base).absoluteFilePath(p));
}

QString permString(const QFileInfo &i)
{
  const auto p = i.permissions();
  QString s;
  s += i.isSymLink() ? QLatin1Char('l') : i.isDir() ? QLatin1Char('d') : QLatin1Char('-');
  s += p & QFile::ReadOwner ? 'r' : '-';
  s += p & QFile::WriteOwner ? 'w' : '-';
  s += p & QFile::ExeOwner ? 'x' : '-';
  s += p & QFile::ReadGroup ? 'r' : '-';
  s += p & QFile::WriteGroup ? 'w' : '-';
  s += p & QFile::ExeGroup ? 'x' : '-';
  s += p & QFile::ReadOther ? 'r' : '-';
  s += p & QFile::WriteOther ? 'w' : '-';
  s += p & QFile::ExeOther ? 'x' : '-';
  return s;
}

QString usage(const QStorageInfo &st)
{
  return humanSize(st.bytesTotal() - st.bytesAvailable()) + QLatin1Char('/')
       + humanSize(st.bytesTotal());
}

void repolish(QWidget *w)
{
  w->style()->unpolish(w);
  w->style()->polish(w);
}

} // namespace

MainWindow::MainWindow(const QString &startPath, QWidget *parent)
  : QMainWindow(parent)
{
  QSettings settings;

  m_model = new QFileSystemModel(this);
  m_model->setReadOnly(false); // read-only would silently disable inline rename
  m_model->setRootPath(QDir::rootPath());
  m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System
                     | (settings.value("showHidden", false).toBool() ? QDir::Hidden
                                                                       : QDir::Filters()));
  m_proxy = new FileSortProxy(m_model, this);

  auto makeView = [this](const char *name) {
    auto *v = new QListView(this);
    v->setObjectName(name);
    v->setModel(m_proxy);
    v->setItemDelegate(new RowDelegate(v));
    v->setUniformItemSizes(true);
    // Marks are made by Space / Ctrl-click and the selection model
    // directly, never by the view's own click-to-select.
    v->setSelectionMode(QAbstractItemView::NoSelection);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->setMouseTracking(true);
    return v;
  };
  m_parentView = makeView("parentPane");
  m_parentView->setFocusPolicy(Qt::NoFocus);
  m_view = makeView("currentPane");
  m_view->setFocusPolicy(Qt::StrongFocus);
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  m_preview = new Preview(m_proxy, this);

  m_split = new QSplitter(Qt::Horizontal, this);
  m_split->setChildrenCollapsible(false);
  m_split->setHandleWidth(1);
  m_split->addWidget(m_parentView);
  m_split->addWidget(m_view);
  m_split->addWidget(m_preview);
  m_split->setStretchFactor(0, 1);
  m_split->setStretchFactor(1, 3);
  m_split->setStretchFactor(2, 4);
  if (!m_split->restoreState(settings.value("split").toByteArray()))
    m_split->setSizes({170, 420, 560});

  buildActions();

  auto *central = new QWidget(this);
  central->setObjectName("central");
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(buildHeader());
  layout->addWidget(m_split, 1);
  layout->addWidget(buildFooter());
  setCentralWidget(central);

  m_previewTimer = new QTimer(this);
  m_previewTimer->setSingleShot(true);
  m_previewTimer->setInterval(40); // holding j shouldn't decode every file passed
  connect(m_previewTimer, &QTimer::timeout, this, &MainWindow::updatePreview);

  m_flashTimer = new QTimer(this);
  m_flashTimer->setSingleShot(true);
  m_flashTimer->setInterval(2500);
  connect(m_flashTimer, &QTimer::timeout, this, [this] {
    m_detail->setProperty("flash", QString());
    repolish(m_detail);
    updateInfo();
  });

  connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onCursorChanged);
  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &MainWindow::updateInfo);
  connect(m_view, &QListView::doubleClicked, this, &MainWindow::openCursor);
  connect(m_view, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);
  connect(m_parentView, &QListView::clicked, this, [this](const QModelIndex &idx) {
    if (idx.model() != m_proxy) {
      enterArchiveDir(ArchiveSession::pathOf(idx), true);
      return;
    }
    const QString path = m_proxy->pathOf(idx);
    if (m_inArchive && path == m_session->file())
      enterArchiveDir(QString(), true);
    else
      navigateTo(path);
  });
  connect(m_preview->dirView(), &QListView::clicked, this, [this](const QModelIndex &idx) {
    if (idx.model() != m_proxy) {
      const QString target = ArchiveSession::pathOf(idx);
      enterArchiveDir(target.contains(QLatin1Char('/')) ? target.section(QLatin1Char('/'), 0, -2)
                                                        : QString(), true);
      m_pendingPath = target;
      tryPending();
      return;
    }
    const QString target = m_proxy->pathOf(idx);
    if (navigateTo(m_preview->path())) {
      m_pendingPath = target;
      tryPending();
    }
  });

  connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &path) {
    if (path == m_currentDir && !m_inArchive) {
      tryPending();
      updateInfo();
    }
  });
  // These three only concern the disk view; inside an archive the disk
  // still changes (extracting beside it) but must not move the cursor.
  connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent) {
    if (!m_inArchive && parent == m_view->rootIndex()) {
      tryPending();
      updateInfo();
    }
  });
  // A re-sort moves rows with a layout change rather than an insert.
  connect(m_proxy, &QAbstractItemModel::layoutChanged, this, [this] {
    if (m_cursorAuto && !m_inArchive)
      tryPending();
  });
  connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex &parent) {
    if (m_inArchive || parent != m_view->rootIndex())
      return;
    if (!cursor().isValid())
      setCursorRow(m_lastRow);
    updateInfo();
  });
  connect(m_model, &QFileSystemModel::fileRenamed, this,
          [this](const QString &dir, const QString &, const QString &newName) {
    if (dir == m_currentDir && !m_inArchive) {
      m_pendingPath = dir + QLatin1Char('/') + newName;
      tryPending();
    }
  });

  m_view->installEventFilter(this);
  m_view->viewport()->installEventFilter(this);

  applyStyle();
  setWindowIcon(QIcon::fromTheme(QStringLiteral("system-file-manager")));
  if (!restoreGeometry(settings.value("geometry").toByteArray()))
    resize(1100, 700);

  const QString start = expandPath(startPath, QDir::currentPath());
  if (!navigateTo(start, false))
    navigateTo(QDir::homePath(), false);
  // Launched on an archive (xdg-open, a double-click elsewhere): open it,
  // the way an archive manager would, with the folder holding it behind.
  const QString startMime = QMimeDatabase().mimeTypeForFile(start).name();
  if (QFileInfo(start).isFile()
      && (Archive::isArchiveMime(startMime) || Archive::isCompressedFileMime(startMime)))
    openArchive(start, QString(), true);
  m_view->setFocus();
}

// ---------------------------------------------------------------- building

void MainWindow::buildActions()
{
  auto act = [this](const QString &text, const QString &keys, std::function<void()> fn) {
    auto *a = new QAction(keys.isEmpty() ? text : text + QLatin1Char('\t') + keys, this);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
  };

  m_actOpen      = act(tr("Open"), "l", [this] { openCursor(); });
  m_actTerminal  = act(tr("Terminal here"), "S", [this] { openTerminal(m_currentDir); });
  m_actCopy      = act(tr("Copy"), "yy", [this] { yank(false); });
  m_actCut       = act(tr("Cut"), "dd", [this] { yank(true); });
  m_actPaste     = act(tr("Paste"), "pp", [this] { paste(); });
  m_actRename    = act(tr("Rename"), "cw", [this] { renameCursor(); });
  m_actDelete    = act(tr("Delete"), "dD", [this] { deleteTargets(); });
  m_actCopyPath  = act(tr("Copy path"), "yp", [this] { copyPaths(); });
  m_actMark      = act(tr("Mark"), "Space", [this] { toggleMark(); });
  m_actClearMarks = act(tr("Clear marks"), "uv", [this] { clearMarks(); });
  m_actNewFolder = act(tr("New folder"), "F7", [this] { newEntry(true); });
  m_actNewFile   = act(tr("New file"), ":touch", [this] { newEntry(false); });
  m_actBack      = act(tr("Back"), "H", [this] { goBack(); });
  m_actForward   = act(tr("Forward"), "L", [this] { goForward(); });
  m_actUp        = act(tr("Parent"), "h", [this] { navigateUp(); });
  m_actHome      = act(tr("Home"), "gh", [this] { navigateTo(QDir::homePath()); });
  m_actRoot      = act(tr("Root"), "gr", [this] { navigateTo(QDir::rootPath()); });
  m_actRefresh   = act(tr("Refresh"), "R", [this] {
    if (m_inArchive) {
      // Read the archive again, wherever in it we are.
      const QString file = m_session->file();
      const QString inner = m_arcDir;
      m_lastCursor[m_currentDir] = file;
      navigateTo(m_currentDir, false); // out first: the views let go of the old model
      m_session.reset();
      openArchive(file, inner, false);
      return;
    }
    // QFileSystemModel's watcher misses some changes (network mounts,
    // FUSE); re-pointing the root makes it re-read the directory.
    const QString dir = m_currentDir;
    m_model->setRootPath(QString());
    m_view->setRootIndex(m_proxy->mapFromSource(m_model->setRootPath(dir)));
    updateDisk();
    flash(tr("refreshed"));
  });
  m_actHelp      = act(tr("Keys"), "?", [this] { showHelp(); });
  m_actHidden    = act(tr("Hidden files"), "zh", [this] {
    setShowHidden(!(m_model->filter() & QDir::Hidden));
  });
  m_actHidden->setCheckable(true);
  m_actExtract   = act(tr("Extract here"), "X", [this] { extractHere(); });
  m_actExtractTo = act(tr("Extract to..."), ":extract", [this] {
    openPrompt(Prompt::Command, QStringLiteral("extract "));
  });
  m_actCompress  = act(tr("Compress..."), ":compress", [this] {
    const QStringList t = targets();
    const QString stem = t.size() == 1 ? QFileInfo(t.first()).fileName()
                                       : QFileInfo(m_currentDir).fileName();
    openPrompt(Prompt::Command, QStringLiteral("compress ") + stem + QStringLiteral(".tar.zst"));
  });
  m_actOpenExternal = act(tr("Open in default app"), "r", [this] { openExternal(); });

  m_bindings = {
    {"j", [this] { moveCursor(1); }},
    {"k", [this] { moveCursor(-1); }},
    {"gg", [this] { setCursorRow(0); }},
    {"G", [this] { setCursorRow(m_view->model()->rowCount(m_view->rootIndex()) - 1); }},
    {"h", [this] { navigateUp(); }},
    {"l", [this] { openCursor(); }},
    {"H", [this] { goBack(); }},
    {"L", [this] { goForward(); }},
    {"~", [this] { navigateTo(QDir::homePath()); }},
    {"gh", [this] { navigateTo(QDir::homePath()); }},
    {"gr", [this] { navigateTo(QDir::rootPath()); }},
    {"gm", [this] {
      const QString media = QStringLiteral("/run/media/") + qEnvironmentVariable("USER");
      navigateTo(QFileInfo::exists(media) ? media : QStringLiteral("/mnt"));
    }},
    {" ", [this] { toggleMark(); }},
    {"uv", [this] { clearMarks(); }},
    {"yy", [this] { yank(false); }},
    {"dd", [this] { yank(true); }},
    {"pp", [this] { paste(); }},
    {"yp", [this] { copyPaths(); }},
    {"cw", [this] { renameCursor(); }},
    {"dD", [this] { deleteTargets(); }},
    {"zh", [this] { m_actHidden->trigger(); }},
    {"/", [this] { openPrompt(Prompt::Search); }},
    {":", [this] { openPrompt(Prompt::Command); }},
    {"n", [this] { search(m_lastSearch, cursor().row() + 1, 1); }},
    {"N", [this] { search(m_lastSearch, cursor().row() - 1, -1); }},
    {"S", [this] { openTerminal(m_currentDir); }},
    {"R", [this] { m_actRefresh->trigger(); }},
    {"?", [this] { showHelp(); }},
    {"X", [this] { extractHere(); }},
    {"r", [this] { openExternal(); }},
  };
}

QWidget *MainWindow::buildHeader()
{
  auto *header = new QWidget(this);
  header->setObjectName("header");
  auto *h = new QHBoxLayout(header);
  h->setContentsMargins(0, 0, 4, 0);
  h->setSpacing(0);

  m_backBtn = new QToolButton(header);
  m_backBtn->setText(QStringLiteral("‹"));
  m_backBtn->setToolTip(tr("Back (H)"));
  connect(m_backBtn, &QToolButton::clicked, this, &MainWindow::goBack);
  m_fwdBtn = new QToolButton(header);
  m_fwdBtn->setText(QStringLiteral("›"));
  m_fwdBtn->setToolTip(tr("Forward (L)"));
  connect(m_fwdBtn, &QToolButton::clicked, this, &MainWindow::goForward);

  m_crumbs = new QWidget(header);
  m_crumbs->setCursor(Qt::IBeamCursor);
  m_crumbs->setToolTip(tr("Click to type a path (Ctrl+L)"));
  m_crumbLayout = new QHBoxLayout(m_crumbs);
  m_crumbLayout->setContentsMargins(8, 0, 0, 0);
  m_crumbLayout->setSpacing(0);
  m_crumbs->installEventFilter(this);

  m_pathEdit = new QLineEdit(header);
  auto *completer = new QCompleter(m_pathEdit);
  auto *dirModel = new QFileSystemModel(completer);
  dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  dirModel->setRootPath(QDir::rootPath());
  completer->setModel(dirModel);
  m_pathEdit->setCompleter(completer);
  m_pathEdit->installEventFilter(this);
  connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
    const QString target = expandPath(m_pathEdit->text(), m_currentDir);
    endPathEdit();
    navigateTo(target);
  });

  m_pathStack = new QStackedWidget(header);
  m_pathStack->addWidget(m_crumbs);
  m_pathStack->addWidget(m_pathEdit);
  m_pathStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

  auto segment = [header](const char *name) {
    auto *l = new QLabel(header);
    l->setObjectName(name);
    l->setProperty("seg", true);
    return l;
  };
  m_keySeg = segment("keys");
  m_keySeg->hide();
  m_posSeg = segment("pos");
  m_markSeg = segment("marks");
  m_markSeg->hide();
  m_jobSeg = segment("job");
  m_jobSeg->setToolTip(tr("Esc cancels"));
  m_jobSeg->hide();
  m_fsSeg = segment("fs");

  m_hiddenBtn = new QToolButton(header);
  m_hiddenBtn->setText(QStringLiteral(".*"));
  m_hiddenBtn->setToolTip(tr("Show hidden files (zh / Ctrl+H)"));
  m_hiddenBtn->setCheckable(true);
  m_hiddenBtn->setChecked(m_model->filter() & QDir::Hidden);
  m_actHidden->setChecked(m_hiddenBtn->isChecked());
  connect(m_hiddenBtn, &QToolButton::clicked, this, &MainWindow::setShowHidden);

  auto *helpBtn = new QToolButton(header);
  helpBtn->setText(QStringLiteral("?"));
  helpBtn->setToolTip(tr("Keys (?)"));
  connect(helpBtn, &QToolButton::clicked, this, &MainWindow::showHelp);

  for (QWidget *w : {static_cast<QWidget *>(m_backBtn), static_cast<QWidget *>(m_fwdBtn),
                     static_cast<QWidget *>(m_hiddenBtn), static_cast<QWidget *>(helpBtn)})
    w->setFocusPolicy(Qt::NoFocus);

  h->addWidget(m_backBtn);
  h->addWidget(m_fwdBtn);
  h->addWidget(m_pathStack, 1);
  h->addWidget(m_keySeg);
  h->addWidget(m_posSeg);
  h->addWidget(m_markSeg);
  h->addWidget(m_jobSeg);
  h->addWidget(m_fsSeg);
  h->addWidget(m_hiddenBtn);
  h->addWidget(helpBtn);
  return header;
}

QWidget *MainWindow::buildFooter()
{
  m_footer = new QStackedWidget(this);
  m_footer->setObjectName("footer");

  auto *info = new QWidget(m_footer);
  auto *ih = new QHBoxLayout(info);
  ih->setContentsMargins(0, 0, 0, 0);
  ih->setSpacing(0);
  m_detail = new QLabel(info);
  m_detail->setObjectName("detail");
  m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_mime = new QLabel(info);
  m_mime->setObjectName("mime");
  ih->addWidget(m_detail, 1);
  ih->addWidget(m_mime);

  auto *prompt = new QWidget(m_footer);
  auto *ph = new QHBoxLayout(prompt);
  ph->setContentsMargins(0, 0, 0, 0);
  ph->setSpacing(0);
  m_promptLabel = new QLabel(prompt);
  m_promptLabel->setObjectName("promptLabel");
  m_prompt = new QLineEdit(prompt);
  m_prompt->setObjectName("prompt");
  m_prompt->installEventFilter(this);
  ph->addWidget(m_promptLabel);
  ph->addWidget(m_prompt, 1);

  connect(m_prompt, &QLineEdit::textEdited, this, [this](const QString &text) {
    // Typing narrows the match; once nothing matches, the cursor goes
    // back where the search started rather than staying on a near miss.
    if (m_promptKind == Prompt::Search && !search(text, m_searchOrigin, 1))
      setCursorRow(m_searchOrigin);
  });
  connect(m_prompt, &QLineEdit::returnPressed, this, [this] {
    const QString text = m_prompt->text();
    const Prompt kind = m_promptKind;
    closePrompt();
    if (kind == Prompt::Search)
      m_lastSearch = text;
    else
      runCommand(text);
  });

  m_footer->addWidget(info);
  m_footer->addWidget(prompt);
  m_footer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  return m_footer;
}

void MainWindow::applyStyle()
{
  // palette() references are resolved when the sheet is applied, so a
  // live theme switch needs the sheet set again to pick up new colours.
  setStyleSheet(QString());
  setStyleSheet(QString::fromLatin1(kStyle));
}

// -------------------------------------------------------------- navigation

bool MainWindow::navigateTo(const QString &path, bool recordHistory)
{
  QString p = QDir::cleanPath(path);
  QFileInfo fi(p);
  if (!fi.exists()) {
    flash(tr("no such directory: %1").arg(p), true);
    return false;
  }
  if (!fi.isDir()) {
    // A file: go to the folder it's in, cursor on the file.
    m_lastCursor[fi.absolutePath()] = fi.absoluteFilePath();
    p = fi.absolutePath();
    fi = QFileInfo(p);
  }
  if (!fi.isReadable() || !fi.isExecutable()) {
    flash(tr("permission denied: %1").arg(p), true);
    return false;
  }

  if (p != m_currentDir || m_inArchive) {
    leaveLocation(recordHistory);
    if (m_inArchive)
      leaveArchive();
    m_currentDir = p;
    m_lastRow = 0;
    m_cursorAuto = true;

    m_view->setRootIndex(m_proxy->mapFromSource(m_model->setRootPath(p)));
    const QModelIndex here = m_proxy->indexOf(p);
    m_parentView->setRootIndex(here.parent());
    m_parentView->selectionModel()->setCurrentIndex(here, QItemSelectionModel::NoUpdate);
    m_parentView->scrollTo(here, QAbstractItemView::PositionAtCenter);

    rebuildCrumbs();
    updateDisk();
    updateHistoryButtons();

    QString title = p;
    if (title.startsWith(QDir::homePath()))
      title.replace(0, QDir::homePath().size(), QStringLiteral("~"));
    setWindowTitle(QStringLiteral("syn-shell: ") + title);
  }

  m_pendingPath = m_lastCursor.value(p);
  m_pendingEdit = false;
  tryPending();
  updateInfo();
  m_previewTimer->start();
  return true;
}

void MainWindow::goBack()
{
  if (m_back.isEmpty())
    return;
  const QString target = m_back.takeLast();
  m_forward << location();
  goToLocation(target, false);
}

void MainWindow::goForward()
{
  if (m_forward.isEmpty())
    return;
  const QString target = m_forward.takeLast();
  m_back << location();
  goToLocation(target, false);
}

void MainWindow::updateHistoryButtons()
{
  m_backBtn->setEnabled(!m_back.isEmpty());
  m_fwdBtn->setEnabled(!m_forward.isEmpty());
  m_actBack->setEnabled(!m_back.isEmpty());
  m_actForward->setEnabled(!m_forward.isEmpty());
}

QString MainWindow::location() const
{
  if (m_inArchive)
    return m_session->file() + QLatin1Char('\x1f') + m_arcDir;
  return m_currentDir;
}

void MainWindow::goToLocation(const QString &loc, bool recordHistory)
{
  if (loc.contains(QLatin1Char('\x1f')))
    openArchive(loc.section(QLatin1Char('\x1f'), 0, 0), loc.section(QLatin1Char('\x1f'), 1),
                recordHistory);
  else
    navigateTo(loc, recordHistory);
}

// Called before any change of location: remember where the cursor was,
// push history, and drop the marks (they belong to the folder they were
// made in).
void MainWindow::leaveLocation(bool recordHistory)
{
  const QString loc = location();
  if (loc.isEmpty())
    return; // first navigation at startup
  if (cursor().isValid())
    m_lastCursor[loc] = cursorKey(cursor());
  if (recordHistory) {
    m_back << loc;
    m_forward.clear();
  }
  m_view->selectionModel()->clear();
}

QString MainWindow::cursorKey(const QModelIndex &idx) const
{
  return m_inArchive ? ArchiveSession::pathOf(idx) : m_proxy->pathOf(idx);
}

QModelIndex MainWindow::indexForKey(const QString &key) const
{
  return m_inArchive ? m_session->indexOf(key) : m_proxy->indexOf(key);
}

// QAbstractItemView::setModel makes a new selection model and leaves the
// old one to the caller; the current pane's signals hang off it.
void MainWindow::setViewModel(QListView *view, QAbstractItemModel *model)
{
  if (view->model() == model)
    return;
  QItemSelectionModel *old = view->selectionModel();
  view->setModel(model);
  delete old;
  if (view == m_view) {
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onCursorChanged);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateInfo);
  }
}

void MainWindow::navigateUp()
{
  if (m_inArchive) {
    if (m_arcDir.isEmpty()) {
      // Out of the archive, cursor on the archive file.
      m_lastCursor[m_currentDir] = m_session->file();
      navigateTo(m_currentDir);
      return;
    }
    const QString parent = m_arcDir.contains(QLatin1Char('/'))
                             ? m_arcDir.section(QLatin1Char('/'), 0, -2) : QString();
    m_lastCursor[m_session->file() + QLatin1Char('\x1f') + parent] = m_arcDir;
    enterArchiveDir(parent, true);
    return;
  }
  if (m_currentDir == QDir::rootPath())
    return;
  const QString parent = QFileInfo(m_currentDir).absolutePath();
  m_lastCursor[parent] = m_currentDir;
  navigateTo(parent);
}

void MainWindow::openCursor()
{
  const QModelIndex c = cursor();
  if (!c.isValid())
    return;
  if (m_inArchive) {
    const QString entry = ArchiveSession::pathOf(c);
    if (m_session->isDir(entry))
      enterArchiveDir(entry, true);
    else
      openEntry(entry);
    return;
  }
  const QString path = m_proxy->pathOf(c);
  if (QFileInfo(path).isDir()) {
    navigateTo(path);
    return;
  }
  if (Archive::isArchiveMime(QMimeDatabase().mimeTypeForFile(path).name())) {
    openArchive(path, QString(), true);
    return;
  }
  // The system's own MIME associations (xdg-open underneath) decide what
  // opens a file; this app owns no "open with" table.
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openTerminal(const QString &dir, const QString &command)
{
  QStringList args{QStringLiteral("-D"), dir};
  if (!command.isEmpty())
    args << QStringLiteral("--hold") << QStringLiteral("sh") << QStringLiteral("-c") << command;
  if (!QProcess::startDetached(QStringLiteral("foot"), args))
    flash(tr("could not start foot"), true);
}

void MainWindow::rebuildCrumbs()
{
  while (QLayoutItem *item = m_crumbLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  struct Crumb { QString label, path; };
  QList<Crumb> crumbs;
  const QString home = QDir::homePath();
  QString base, rest;
  if (m_currentDir == home || m_currentDir.startsWith(home + QLatin1Char('/'))) {
    crumbs.append({QStringLiteral("~"), home});
    base = home;
    rest = m_currentDir.mid(home.size());
  } else {
    crumbs.append({QStringLiteral("/"), QStringLiteral("/")});
    rest = m_currentDir;
  }
  for (const QString &part : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
    base += QLatin1Char('/') + part;
    crumbs.append({part, base});
  }
  // Inside an archive the trail carries on through it: the archive file,
  // then its folders, each a location goToLocation() understands.
  int archiveCrumb = -1;
  if (m_inArchive) {
    const QString key = m_session->file() + QLatin1Char('\x1f');
    archiveCrumb = crumbs.size();
    crumbs.append({QFileInfo(m_session->file()).fileName(), key});
    QString inner;
    for (const QString &part : m_arcDir.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
      inner += (inner.isEmpty() ? QString() : QStringLiteral("/")) + part;
      crumbs.append({part, key + inner});
    }
  }

  // Deep paths keep the first crumb and the last few; the gap opens the
  // path editor.
  const int keep = 4;
  const bool elide = crumbs.size() > keep + 2;

  for (int i = 0; i < crumbs.size(); ++i) {
    if (elide && i > 0 && i < crumbs.size() - keep) {
      if (i == 1) {
        auto *gap = new QToolButton(m_crumbs);
        gap->setObjectName("crumb");
        gap->setText(QStringLiteral("/ … "));
        gap->setFocusPolicy(Qt::NoFocus);
        connect(gap, &QToolButton::clicked, this, &MainWindow::beginPathEdit);
        m_crumbLayout->addWidget(gap);
      }
      continue;
    }
    if (i > 0 && crumbs[i - 1].label != QLatin1String("/")
        && !(elide && i == crumbs.size() - keep)) {
      auto *sep = new QLabel(QStringLiteral("/"), m_crumbs);
      sep->setObjectName("crumbSep");
      m_crumbLayout->addWidget(sep);
    }
    auto *b = new QToolButton(m_crumbs);
    b->setObjectName(i == crumbs.size() - 1 ? "crumbCurrent"
                     : i == archiveCrumb   ? "crumbArchive" : "crumb");
    b->setText(crumbs[i].label);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::PointingHandCursor);
    const QString target = crumbs[i].path;
    connect(b, &QToolButton::clicked, this, [this, target] { goToLocation(target, true); });
    m_crumbLayout->addWidget(b);
  }
  m_crumbLayout->addStretch(1);
}

void MainWindow::beginPathEdit()
{
  m_pathEdit->setText(m_currentDir == QDir::rootPath() ? m_currentDir
                                                        : m_currentDir + QLatin1Char('/'));
  m_pathStack->setCurrentWidget(m_pathEdit);
  m_pathEdit->setFocus();
}

void MainWindow::endPathEdit()
{
  if (m_pathStack->currentWidget() != m_pathEdit)
    return;
  m_pathStack->setCurrentWidget(m_crumbs);
  m_view->setFocus();
}

// ------------------------------------------------------------------ cursor

QModelIndex MainWindow::cursor() const
{
  const QModelIndex c = m_view->currentIndex();
  return c.isValid() && c.parent() == m_view->rootIndex() ? c : QModelIndex();
}

void MainWindow::setCursorRow(int row)
{
  const QModelIndex root = m_view->rootIndex();
  const int n = m_view->model()->rowCount(root);
  if (n == 0)
    return;
  const QModelIndex idx = m_view->model()->index(qBound(0, row, n - 1), 0, root);
  m_view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
  m_view->scrollTo(idx);
}

void MainWindow::moveCursor(int delta)
{
  const QModelIndex c = cursor();
  setCursorRow(c.isValid() ? c.row() + delta : 0);
}

void MainWindow::onCursorChanged()
{
  if (cursor().isValid())
    m_lastRow = cursor().row();
  m_previewTimer->start();
  updateInfo();
}

void MainWindow::tryPending()
{
  const QModelIndex root = m_view->rootIndex();
  if (!m_pendingPath.isEmpty()) {
    const QModelIndex idx = indexForKey(m_pendingPath);
    if (idx.isValid() && idx.parent() == root) {
      m_view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
      m_view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
      if (m_pendingEdit)
        m_view->edit(idx);
      m_pendingPath.clear();
      m_pendingEdit = false;
      m_cursorAuto = false;
      return;
    }
  }
  if (m_cursorAuto && m_pendingPath.isEmpty())
    setCursorRow(0);
  else if (!cursor().isValid())
    setCursorRow(m_lastRow);
}

void MainWindow::toggleMark()
{
  const QModelIndex c = cursor();
  if (!c.isValid())
    return;
  m_view->selectionModel()->select(c, QItemSelectionModel::Toggle);
  moveCursor(1);
}

void MainWindow::clearMarks()
{
  m_view->selectionModel()->clearSelection();
}

QStringList MainWindow::targets() const
{
  QStringList paths;
  if (m_inArchive)
    return paths; // see archiveTargets()
  for (const QModelIndex &idx : m_view->selectionModel()->selectedIndexes())
    if (idx.parent() == m_view->rootIndex())
      paths << m_proxy->pathOf(idx);
  if (paths.isEmpty() && cursor().isValid())
    paths << m_proxy->pathOf(cursor());
  return paths;
}

bool MainWindow::search(const QString &needle, int from, int step)
{
  if (needle.isEmpty())
    return false;
  m_cursorAuto = false;
  const QModelIndex root = m_view->rootIndex();
  const int n = m_view->model()->rowCount(root);
  for (int i = 0; i < n; ++i) {
    const int row = (((from + step * i) % n) + n) % n;
    if (m_view->model()->index(row, 0, root).data().toString().contains(needle, Qt::CaseInsensitive)) {
      setCursorRow(row);
      return true;
    }
  }
  flash(tr("not found: %1").arg(needle), true);
  return false;
}

// ------------------------------------------------------------------- files

void MainWindow::renameCursor()
{
  if (refuseInArchive())
    return;
  const QModelIndex c = cursor();
  if (c.isValid())
    m_view->edit(c);
}

void MainWindow::deleteTargets()
{
  if (refuseInArchive())
    return;
  const QStringList paths = targets();
  if (paths.isEmpty())
    return;
  FileOps::deleteEntries(this, paths);
  clearMarks();
  updateDisk();
}

void MainWindow::yank(bool cut)
{
  if (m_inArchive) {
    if (cut) {
      refuseInArchive();
      return;
    }
    // Copy out of an archive: extract to a temp folder, yank that.
    const QStringList entries = archiveTargets();
    if (entries.isEmpty())
      return;
    clearMarks();
    extractEntries(entries, tempSlot(), tr("copying out"), [this](const QStringList &paths) {
      auto *mime = new QMimeData;
      QList<QUrl> urls;
      QByteArray marker = "copy";
      for (const QString &p : paths) {
        urls << QUrl::fromLocalFile(p);
        marker += '\n' + urls.last().toEncoded();
      }
      mime->setUrls(urls);
      mime->setData(kCopiedFilesMime, marker);
      mime->setText(paths.join(QLatin1Char('\n')));
      QApplication::clipboard()->setMimeData(mime);
      flash(tr("%n copied out, pp pastes", nullptr, paths.size()));
    });
    return;
  }
  const QStringList paths = targets();
  if (paths.isEmpty())
    return;

  // text/uri-list plus the GNOME cut/copy marker that pcmanfm, thunar,
  // nautilus and dolphin all read, so a yank here pastes there and back.
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  QByteArray marker = cut ? "cut" : "copy";
  for (const QString &p : paths) {
    urls << QUrl::fromLocalFile(p);
    marker += '\n' + urls.last().toEncoded();
  }
  mime->setUrls(urls);
  mime->setData(kCopiedFilesMime, marker);
  mime->setText(paths.join(QLatin1Char('\n')));
  QApplication::clipboard()->setMimeData(mime);

  clearMarks();
  flash(cut ? tr("%n cut", nullptr, paths.size()) : tr("%n yanked", nullptr, paths.size()));
}

void MainWindow::paste()
{
  if (refuseInArchive())
    return;
  const QMimeData *mime = QApplication::clipboard()->mimeData();
  QStringList paths;
  if (mime)
    for (const QUrl &url : mime->urls())
      if (url.isLocalFile())
        paths << url.toLocalFile();
  if (paths.isEmpty()) {
    flash(tr("nothing to paste"), true);
    return;
  }

  const bool cut = mime->data(kCopiedFilesMime).startsWith("cut");
  m_pendingPath = m_currentDir + QLatin1Char('/') + QFileInfo(paths.first()).fileName();
  if (cut) {
    FileOps::moveEntries(this, paths, m_currentDir);
    QApplication::clipboard()->clear();
  } else {
    FileOps::copyEntries(this, paths, m_currentDir);
  }
  tryPending();
  updateDisk();
}

void MainWindow::copyPaths()
{
  QStringList paths = targets();
  if (m_inArchive) {
    paths.clear();
    for (const QString &e : archiveTargets())
      paths << m_session->file() + QLatin1Char('/') + e;
  }
  if (paths.isEmpty())
    return;
  QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
  flash(paths.size() == 1 ? paths.first() : tr("%n paths copied", nullptr, paths.size()));
}

void MainWindow::newEntry(bool folder)
{
  if (refuseInArchive())
    return;
  const QString stem = folder ? QStringLiteral("new-folder") : QStringLiteral("new-file");
  QString path = m_currentDir + QLatin1Char('/') + stem;
  for (int i = 2; QFileInfo(path).exists() || QFileInfo(path).isSymLink(); ++i)
    path = m_currentDir + QLatin1Char('/') + stem + QLatin1Char('-') + QString::number(i);

  bool ok;
  if (folder) {
    ok = QDir().mkdir(path);
  } else {
    QFile f(path);
    ok = f.open(QIODevice::WriteOnly | QIODevice::NewOnly);
  }
  if (!ok) {
    flash(tr("could not create %1 (permission denied?)").arg(QFileInfo(path).fileName()), true);
    return;
  }
  m_pendingPath = path;
  m_pendingEdit = true;
  tryPending();
}

void MainWindow::setShowHidden(bool show)
{
  QDir::Filters f = m_model->filter();
  f.setFlag(QDir::Hidden, show);
  m_model->setFilter(f);
  if (m_session)
    m_session->proxy()->setShowHidden(show);
  m_hiddenBtn->setChecked(show);
  m_actHidden->setChecked(show);
  tryPending();
  updateInfo();
  flash(show ? tr("hidden files shown") : tr("hidden files hidden"));
}

// ------------------------------------------------------------ prompt / cmd

void MainWindow::openPrompt(Prompt kind, const QString &text)
{
  m_promptKind = kind;
  m_promptLabel->setText(kind == Prompt::Search ? QStringLiteral("/") : QStringLiteral(":"));
  m_prompt->setText(text);
  m_searchOrigin = cursor().isValid() ? cursor().row() : 0;
  m_footer->setCurrentIndex(1);
  m_prompt->setFocus();
}

void MainWindow::closePrompt()
{
  if (m_promptKind == Prompt::None)
    return;
  m_promptKind = Prompt::None;
  m_footer->setCurrentIndex(0);
  m_view->setFocus();
}

void MainWindow::runCommand(const QString &input)
{
  const QString line = input.trimmed();
  if (line.isEmpty())
    return;
  if (line.startsWith(QLatin1Char('!'))) {
    openTerminal(m_currentDir, line.mid(1).trimmed());
    return;
  }

  const int space = line.indexOf(QLatin1Char(' '));
  const QString cmd = space < 0 ? line : line.left(space);
  const QString arg = space < 0 ? QString() : line.mid(space + 1).trimmed();

  if (cmd == QLatin1String("cd")) {
    navigateTo(arg.isEmpty() ? QDir::homePath() : expandPath(arg, m_currentDir));
  } else if (cmd == QLatin1String("mkdir") || cmd == QLatin1String("touch")) {
    if (arg.isEmpty()) {
      newEntry(cmd == QLatin1String("mkdir"));
      return;
    }
    const QString path = expandPath(arg, m_currentDir);
    bool ok;
    if (cmd == QLatin1String("mkdir")) {
      ok = QDir().mkpath(path);
    } else {
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile f(path);
      ok = f.exists() || f.open(QIODevice::WriteOnly);
    }
    if (!ok) {
      flash(tr("%1: could not create %2").arg(cmd, arg), true);
      return;
    }
    // Cursor onto whatever of the new path sits directly in this folder.
    const QString rel = QDir(m_currentDir).relativeFilePath(path);
    if (!rel.startsWith(QLatin1String("..")))
      m_pendingPath = m_currentDir + QLatin1Char('/') + rel.section(QLatin1Char('/'), 0, 0);
    tryPending();
  } else if (cmd == QLatin1String("rename") || cmd == QLatin1String("mv")) {
    if (refuseInArchive())
      return;
    const QModelIndex c = cursor();
    if (!c.isValid() || arg.isEmpty()) {
      flash(tr("rename: needs a cursor and a new name"), true);
      return;
    }
    const QString from = m_proxy->pathOf(c);
    const QString to = expandPath(arg, m_currentDir);
    if (QFileInfo::exists(to)) {
      flash(tr("rename: %1 already exists").arg(arg), true);
      return;
    }
    if (!QFile::rename(from, to)) {
      flash(tr("rename: failed (permission denied?)"), true);
      return;
    }
    m_pendingPath = to;
    tryPending();
  } else if (cmd == QLatin1String("extract") || cmd == QLatin1String("x")) {
    const QString dest = arg.isEmpty() ? m_currentDir : expandPath(arg, m_currentDir);
    if (m_inArchive) {
      const QStringList entries = archiveTargets();
      extractEntries(entries, dest, tr("extracting"), [this, dest](const QStringList &paths) {
        flash(tr("%n extracted into %1", nullptr, paths.size()).arg(dest));
      });
    } else if (cursor().isValid()) {
      extractArchiveFile(m_proxy->pathOf(cursor()), arg.isEmpty() ? QString() : dest, QString());
    }
  } else if (cmd == QLatin1String("compress") || cmd == QLatin1String("pack")) {
    compress(arg);
  } else if (cmd == QLatin1String("delete") || cmd == QLatin1String("rm")) {
    deleteTargets();
  } else if (cmd == QLatin1String("term") || cmd == QLatin1String("shell")) {
    openTerminal(m_currentDir);
  } else if (cmd == QLatin1String("hidden")) {
    m_actHidden->trigger();
  } else if (cmd == QLatin1String("help")) {
    showHelp();
  } else if (cmd == QLatin1String("q") || cmd == QLatin1String("quit")) {
    close();
  } else {
    flash(tr("not a command: %1  (? lists them)").arg(cmd), true);
  }
}

// -------------------------------------------------------------------- keys

bool MainWindow::handleKey(QKeyEvent *ev)
{
  const Qt::KeyboardModifiers mods = ev->modifiers() & ~Qt::KeypadModifier;
  const int key = ev->key();
  m_cursorAuto = false;
  const int page = qMax(1, m_view->viewport()->height() / qMax(1, m_view->sizeHintForRow(0)) - 1);

  auto done = [this] {
    m_keys.clear();
    m_keySeg->hide();
    return true;
  };

  if (mods & Qt::ControlModifier) {
    switch (key) {
    case Qt::Key_C: yank(false); break;
    case Qt::Key_X: yank(true); break;
    case Qt::Key_V: paste(); break;
    case Qt::Key_H: m_actHidden->trigger(); break;
    case Qt::Key_L: beginPathEdit(); break;
    case Qt::Key_R: m_actRefresh->trigger(); break;
    case Qt::Key_A: {
      const QModelIndex root = m_view->rootIndex();
      const int n = m_view->model()->rowCount(root);
      if (n > 0)
        m_view->selectionModel()->select(
          QItemSelection(m_view->model()->index(0, 0, root),
                         m_view->model()->index(n - 1, 0, root)),
          QItemSelectionModel::Select);
      break;
    }
    default: return false;
    }
    return done();
  }
  if (mods & Qt::AltModifier) {
    switch (key) {
    case Qt::Key_Left: goBack(); break;
    case Qt::Key_Right: goForward(); break;
    case Qt::Key_Up: navigateUp(); break;
    case Qt::Key_Home: navigateTo(QDir::homePath()); break;
    default: return false;
    }
    return done();
  }

  switch (key) {
  case Qt::Key_Down: moveCursor(1); return done();
  case Qt::Key_Up: moveCursor(-1); return done();
  case Qt::Key_PageDown: moveCursor(page); return done();
  case Qt::Key_PageUp: moveCursor(-page); return done();
  case Qt::Key_Home: setCursorRow(0); return done();
  case Qt::Key_End: setCursorRow(m_view->model()->rowCount(m_view->rootIndex()) - 1); return done();
  case Qt::Key_Left:
  case Qt::Key_Backspace: navigateUp(); return done();
  case Qt::Key_Right:
  case Qt::Key_Return:
  case Qt::Key_Enter: openCursor(); return done();
  case Qt::Key_Delete: deleteTargets(); return done();
  case Qt::Key_F2: renameCursor(); return done();
  case Qt::Key_F5: m_actRefresh->trigger(); return done();
  case Qt::Key_F7: newEntry(true); return done();
  case Qt::Key_Menu: {
    const QModelIndex c = cursor();
    showContextMenu(c.isValid() ? m_view->visualRect(c).center() : QPoint(10, 10));
    return done();
  }
  case Qt::Key_Escape:
    if (m_jobCancel && m_keys.isEmpty()) {
      *m_jobCancel = true;
      flash(tr("cancelling %1").arg(m_jobLabel));
    } else if (m_keys.isEmpty()) {
      clearMarks();
    }
    return done();
  default:
    break;
  }

  const QString text = ev->text();
  if (text.isEmpty() || !text.at(0).isPrint())
    return false;

  m_keys += text;
  if (const auto it = m_bindings.constFind(m_keys); it != m_bindings.constEnd()) {
    const auto fn = it.value();
    done();
    fn();
    return true;
  }
  for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
    if (it.key().startsWith(m_keys)) {
      m_keySeg->setText(m_keys);
      m_keySeg->show();
      return true;
    }
  }
  return done(); // not a binding: swallow it, don't let the view type-search
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_view && event->type() == QEvent::KeyPress)
    return handleKey(static_cast<QKeyEvent *>(event));

  if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonPress) {
    auto *me = static_cast<QMouseEvent *>(event);
    m_cursorAuto = false;
    const QModelIndex idx = m_view->indexAt(me->position().toPoint());
    if (idx.isValid() && me->button() == Qt::LeftButton) {
      if (me->modifiers() & Qt::ControlModifier) {
        m_view->selectionModel()->select(idx, QItemSelectionModel::Toggle);
      } else if ((me->modifiers() & Qt::ShiftModifier) && cursor().isValid()) {
        m_view->selectionModel()->select(QItemSelection(cursor(), idx),
                                         QItemSelectionModel::Select);
      }
    }
    return false; // the view still moves the cursor to the clicked row
  }

  if (watched == m_crumbs && event->type() == QEvent::MouseButtonPress) {
    beginPathEdit();
    return true;
  }

  if (watched == m_pathEdit) {
    if (event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
      endPathEdit();
      return true;
    }
    if (event->type() == QEvent::FocusOut
        && static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason)
      endPathEdit();
  }

  if (watched == m_prompt) {
    if (event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
      closePrompt();
      return true;
    }
    if (event->type() == QEvent::FocusOut)
      closePrompt();
  }

  return QMainWindow::eventFilter(watched, event);
}

// -------------------------------------------------------------------- menu

void MainWindow::showContextMenu(const QPoint &pos)
{
  const QModelIndex idx = m_view->indexAt(pos);
  if (idx.isValid())
    m_view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
  const int count = !idx.isValid() ? 0 : m_inArchive ? archiveTargets().size() : targets().size();

  const QMimeData *clip = QApplication::clipboard()->mimeData();
  m_actPaste->setEnabled(clip && clip->hasUrls());
  m_actRename->setEnabled(count == 1);

  QMenu menu(this);
  auto head = [&menu](const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName("menuHead");
    auto *wa = new QWidgetAction(&menu);
    wa->setDefaultWidget(label);
    menu.addAction(wa);
  };

  if (m_inArchive) {
    head(idx.isValid() ? (count > 1 ? tr("%1 MARKED").arg(count)
                                    : idx.data().toString().toUpper().left(28))
                       : QFileInfo(m_session->file()).fileName().toUpper().left(28));
    if (idx.isValid()) {
      m_actOpen->setText(tr("Open\tl"));
      menu.addAction(m_actOpen);
      menu.addAction(m_actOpenExternal);
    }
    head(tr("ARCHIVE"));
    m_actExtract->setText(idx.isValid() ? tr("Extract beside archive\tX")
                                        : tr("Extract all beside archive\tX"));
    menu.addAction(m_actExtract);
    menu.addAction(m_actExtractTo);
    if (idx.isValid()) {
      m_actCopy->setText(tr("Copy out\tyy"));
      menu.addAction(m_actCopy);
      menu.addAction(m_actCopyPath);
      menu.addAction(m_view->selectionModel()->isSelected(idx) ? m_actClearMarks : m_actMark);
    }
  } else if (idx.isValid()) {
    const QString path = m_proxy->pathOf(idx);
    const bool archive = Archive::isArchiveMime(QMimeDatabase().mimeTypeForFile(path).name())
                      || Archive::isCompressedFileMime(QMimeDatabase().mimeTypeForFile(path).name());
    head(count > 1 ? tr("%1 MARKED").arg(count)
                   : m_proxy->infoOf(idx).fileName().toUpper().left(28));
    m_actOpen->setText(archive ? tr("Browse archive\tl") : tr("Open\tl"));
    menu.addAction(m_actOpen);
    if (archive || !m_proxy->infoOf(idx).isDir())
      menu.addAction(m_actOpenExternal);
    menu.addAction(m_actTerminal);
    if (archive) {
      head(tr("ARCHIVE"));
      m_actExtract->setText(tr("Extract here\tX"));
      menu.addAction(m_actExtract);
      menu.addAction(m_actExtractTo);
    }
    head(tr("EDIT"));
    m_actCopy->setText(tr("Copy\tyy"));
    menu.addAction(m_actTerminal);
    head(tr("EDIT"));
    menu.addAction(m_actCopy);
    menu.addAction(m_actCut);
    menu.addAction(m_actPaste);
    menu.addAction(m_actRename);
    menu.addAction(m_actDelete);
    menu.addAction(m_actCopyPath);
    menu.addAction(m_actCompress);
    menu.addAction(m_view->selectionModel()->isSelected(idx) ? m_actClearMarks : m_actMark);
  } else {
    head(tr("CREATE"));
    menu.addAction(m_actNewFolder);
    menu.addAction(m_actNewFile);
    menu.addAction(m_actPaste);
    menu.addAction(m_actTerminal);
  }

  head(tr("GO"));
  menu.addAction(m_actBack);
  menu.addAction(m_actForward);
  menu.addAction(m_actUp);
  menu.addAction(m_actHome);
  menu.addAction(m_actRoot);
  for (const QString &root : deviceRoots()) {
    if (root == QDir::rootPath())
      continue;
    auto *a = menu.addAction(QFileInfo(root).fileName() + QLatin1Char('\t')
                             + usage(QStorageInfo(root)));
    connect(a, &QAction::triggered, this, [this, root] { navigateTo(root); });
  }

  head(tr("VIEW"));
  menu.addAction(m_actHidden);
  menu.addAction(m_actRefresh);
  menu.addAction(m_actHelp);

  menu.exec(m_view->viewport()->mapToGlobal(pos));
}

QStringList MainWindow::deviceRoots() const
{
  QStringList roots;
  for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
    if (!v.isValid() || !v.isReady())
      continue;
    const QString r = v.rootPath();
    if (r == QLatin1String("/") || r == QLatin1String("/home")
        || r.startsWith(QLatin1String("/run/media/")) || r.startsWith(QLatin1String("/media/"))
        || r.startsWith(QLatin1String("/mnt/")) || r == QLatin1String("/mnt"))
      roots << r;
  }
  return roots;
}

// -------------------------------------------------------------------- info

QString MainWindow::detailLine(const QString &path) const
{
  const QFileInfo i(path);
  QString line = permString(i) + QStringLiteral("  ") + i.owner() + QLatin1Char(':') + i.group()
               + QStringLiteral("  ") + (i.isDir() ? QStringLiteral("-") : humanSize(i.size()))
               + QStringLiteral("  ")
               + i.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
               + QStringLiteral("  ") + (i.fileName().isEmpty() ? path : i.fileName());
  if (i.isSymLink())
    line += QStringLiteral(" -> ") + i.symLinkTarget();
  return line;
}

void MainWindow::updateInfo()
{
  const QModelIndex root = m_view->rootIndex();
  const int n = m_view->model()->rowCount(root);
  const QModelIndex c = cursor();
  m_posSeg->setText(n ? QStringLiteral("%1/%2").arg(c.isValid() ? c.row() + 1 : 0).arg(n)
                      : tr("empty"));

  const QModelIndexList marks = m_view->selectionModel()->selectedIndexes();
  if (marks.isEmpty()) {
    m_markSeg->hide();
  } else {
    qint64 total = 0;
    for (const QModelIndex &idx : marks) {
      if (m_inArchive) {
        if (!idx.data(EntryRole::IsDir).toBool())
          total += qMax<qint64>(0, idx.data(EntryRole::Size).toLongLong());
      } else if (const QFileInfo fi = m_proxy->infoOf(idx); !fi.isDir()) {
        total += fi.size();
      }
    }
    m_markSeg->setText(tr("%1 marked  %2").arg(marks.size()).arg(humanSize(total)));
    m_markSeg->show();
  }

  if (m_inArchive) {
    m_mime->setText(c.isValid() && !c.data(EntryRole::IsDir).toBool()
                      ? QMimeDatabase().mimeTypeForFile(c.data().toString(),
                                                        QMimeDatabase::MatchExtension).name()
                      : QString());
    if (!m_flashTimer->isActive())
      m_detail->setText(entryDetail(c));
    return;
  }
  const QString path = c.isValid() ? m_proxy->pathOf(c) : m_currentDir;
  m_mime->setText(c.isValid() ? QMimeDatabase().mimeTypeForFile(path).name() : QString());
  if (!m_flashTimer->isActive())
    m_detail->setText(detailLine(path));
}

void MainWindow::updateDisk()
{
  if (m_inArchive) {
    const Archive::Listing &l = m_session->listing();
    const QString type = QMimeDatabase().suffixForFileName(m_session->file());
    m_fsSeg->setText((type.isEmpty() ? tr("archive") : type) + QStringLiteral("  ")
                     + humanSize(m_session->totalSize()) + QStringLiteral(" in ")
                     + humanSize(QFileInfo(m_session->file()).size()));
    m_fsSeg->setToolTip(l.format + (l.filters.isEmpty() ? QString()
                                    : QStringLiteral(" + ") + l.filters.join(QStringLiteral(" + ")))
                        + tr("\n%n entries", nullptr, l.entries.size())
                        + (l.encrypted ? tr(", encrypted") : QString()));
    return;
  }
  const QStorageInfo st(m_currentDir);
  m_fsSeg->setText(QString::fromLatin1(st.fileSystemType()) + QStringLiteral("  ") + usage(st));
  m_fsSeg->setToolTip(st.rootPath() + QStringLiteral("  ") + QString::fromLatin1(st.device())
                      + QStringLiteral("\n") + humanSize(st.bytesAvailable()) + tr(" free"));
}

void MainWindow::updatePreview()
{
  cancelPreview();
  const QModelIndex c = cursor();
  if (!c.isValid()) {
    m_preview->showText(QString(), QString());
    return;
  }
  if (m_inArchive) {
    const QString entry = ArchiveSession::pathOf(c);
    if (m_session->isDir(entry))
      m_preview->showDir(m_session->proxy(), m_session->indexOf(entry),
                         QStringLiteral("DIR  ") + c.data().toString() + QLatin1Char('/'));
    else
      previewEntry(entry);
    return;
  }
  const QString path = m_proxy->pathOf(c);
  const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
  if (QFileInfo(path).isFile()
      && (Archive::isArchiveMime(mime) || Archive::isCompressedFileMime(mime)))
    previewArchiveFile(path);
  else
    m_preview->showPath(path);
}

void MainWindow::showHelp()
{
  m_previewTimer->stop();
  m_preview->showText(tr("KEYS"), QString::fromUtf8(kHelp));
}

void MainWindow::flash(const QString &message, bool error)
{
  m_detail->setText(message);
  m_detail->setProperty("flash", error ? "error" : "info");
  repolish(m_detail);
  m_flashTimer->start();
}

// ------------------------------------------------------------------ window

void MainWindow::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::ApplicationPaletteChange)
    applyStyle();
  QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  // The worker threads poll these; the pool waits for them on exit.
  if (m_jobCancel)
    *m_jobCancel = true;
  cancelPreview();
  QSettings settings;
  settings.setValue("geometry", saveGeometry());
  settings.setValue("split", m_split->saveState());
  settings.setValue("showHidden", bool(m_model->filter() & QDir::Hidden));
  QMainWindow::closeEvent(event);
}
