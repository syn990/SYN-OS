#include "Preview.h"
#include "FileSortProxy.h"
#include "RowDelegate.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QImageReader>
#include <QLabel>
#include <QListView>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTextDocument>
#include <QVBoxLayout>

#ifdef SYN_HAVE_KSYNTAXHIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#endif

namespace {

constexpr qint64 kTextLimit = 64 * 1024;
constexpr qint64 kHexLimit = 4 * 1024;

// hexdump -C layout: offset, 16 bytes in two groups of 8, printable column.
QString hexDump(const QByteArray &data)
{
  QString out;
  for (qsizetype off = 0; off < data.size(); off += 16) {
    QString hex, ascii;
    for (int i = 0; i < 16; ++i) {
      if (i == 8)
        hex += QLatin1Char(' ');
      if (off + i < data.size()) {
        const auto c = static_cast<unsigned char>(data[off + i]);
        hex += QStringLiteral("%1 ").arg(c, 2, 16, QLatin1Char('0'));
        ascii += (c >= 0x20 && c < 0x7f) ? QLatin1Char(c) : QLatin1Char('.');
      } else {
        hex += QStringLiteral("   ");
      }
    }
    out += QStringLiteral("%1  %2 |%3|\n").arg(off, 8, 16, QLatin1Char('0')).arg(hex, ascii);
  }
  return out;
}

#ifdef SYN_HAVE_KSYNTAXHIGHLIGHTING
// The SYN theme as a KSyntaxHighlighting theme file, from the palette
// qt6ct fills with the active theme: keywords in the accent, strings and
// numbers in shades of it (lighter on a dark theme, darker on a light
// one), types and builtins bold, comments greyed.
QByteArray synTheme(const QPalette &pal)
{
  const QColor bg = pal.color(QPalette::Window);
  const QColor fg = pal.color(QPalette::Text);
  const QColor accent = pal.color(QPalette::Highlight);
  const bool dark = bg.lightness() < 128;
  auto mix = [](const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t),
                            float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
  };
  const QColor str = dark ? accent.lighter(160) : accent.darker(140);
  const QColor num = dark ? accent.lighter(130) : accent.darker(120);
  const QColor dim = mix(fg, bg, 0.5);
  const QColor op = mix(fg, bg, 0.2);
  auto style = [](const QColor &c, bool bold = false, bool italic = false) {
    QJsonObject o{{"text-color", c.name()}};
    if (bold)
      o.insert("bold", true);
    if (italic)
      o.insert("italic", true);
    return o;
  };
  const QJsonObject styles{
    {"Normal", style(fg)},
    {"Keyword", style(accent, true)},
    {"ControlFlow", style(accent, true)},
    {"Function", style(fg)},
    {"Variable", style(num)},
    {"Operator", style(op)},
    {"BuiltIn", style(fg, true)},
    {"Extension", style(accent)},
    {"Preprocessor", style(accent)},
    {"Attribute", style(accent)},
    {"Annotation", style(accent)},
    {"Import", style(accent)},
    {"DataType", style(fg, true)},
    {"DecVal", style(num)},
    {"BaseN", style(num)},
    {"Float", style(num)},
    {"Constant", style(num, true)},
    {"Char", style(str)},
    {"SpecialChar", style(num)},
    {"String", style(str)},
    {"VerbatimString", style(str)},
    {"SpecialString", style(str)},
    {"Comment", style(dim, false, true)},
    {"Documentation", style(dim, false, true)},
    {"CommentVar", style(dim, true, true)},
    {"RegionMarker", style(dim)},
    {"Information", style(accent, true)},
    {"Warning", style(accent, true)},
    {"Alert", style(accent, true)},
    {"Error", style(accent, true)},
    {"Others", style(fg)},
  };
  const QJsonObject editor{
    {"BackgroundColor", bg.name()},
    {"TextSelection", accent.name()},
  };
  return QJsonDocument(QJsonObject{
                         {"metadata", QJsonObject{{"name", "SYN"}, {"revision", 1}}},
                         {"text-styles", styles},
                         {"editor-colors", editor},
                       })
    .toJson();
}

// One repository for every preview: loading its index of definitions
// is the slow part. Its custom search path holds the SYN theme.
KSyntaxHighlighting::Repository &repository(const QPalette &pal, bool rewriteTheme)
{
  static KSyntaxHighlighting::Repository *repo = nullptr;
  const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
  const QString dir = (xdg.isEmpty() ? QDir::tempPath() : QString::fromLocal8Bit(xdg))
                    + QStringLiteral("/syn-shell/highlighting");
  if (!repo || rewriteTheme) {
    QDir().mkpath(dir + QStringLiteral("/themes"));
    QFile f(dir + QStringLiteral("/themes/syn.theme"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
      f.write(synTheme(pal));
  }
  if (!repo) {
    repo = new KSyntaxHighlighting::Repository;
    repo->addCustomSearchPath(dir);
  } else if (rewriteTheme) {
    repo->reload();
  }
  return *repo;
}
#endif

} // namespace

Preview::Preview(FileSortProxy *model, QWidget *parent)
  : QWidget(parent), m_model(model)
{
  m_title = new QLabel(this);
  m_title->setObjectName("previewHead");

  m_dir = new QListView(this);
  m_dir->setObjectName("previewDir");
  m_dir->setModel(model);
  m_dir->setItemDelegate(new RowDelegate(m_dir));
  m_dir->setFocusPolicy(Qt::NoFocus);
  m_dir->setSelectionMode(QAbstractItemView::NoSelection);
  m_dir->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_dir->setUniformItemSizes(true);
  m_dir->setMouseTracking(true);

  m_text = new QPlainTextEdit(this);
  m_text->setObjectName("previewText");
  m_text->setReadOnly(true);
  m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_text->setFocusPolicy(Qt::NoFocus);
  m_text->document()->setDocumentMargin(8);

  m_image = new QLabel(this);
  m_image->setObjectName("previewImage");
  m_image->setAlignment(Qt::AlignCenter);
  m_image->setMinimumSize(1, 1);

  m_stack = new QStackedWidget(this);
  m_stack->addWidget(m_dir);
  m_stack->addWidget(m_text);
  m_stack->addWidget(m_image);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_title);
  layout->addWidget(m_stack, 1);
}

void Preview::clear()
{
  m_path.clear();
  m_isImage = false;
  m_imageData.clear();
  m_title->setText(QString());
  m_text->clear();
  m_stack->setCurrentWidget(m_text);
}

void Preview::showText(const QString &title, const QString &text)
{
  m_path.clear();
  m_isImage = false;
  m_imageData.clear();
  m_title->setText(title);
  dropHighlighter();
  m_text->setPlainText(text);
  m_stack->setCurrentWidget(m_text);
}

void Preview::showPath(const QString &path)
{
  m_path = path;
  m_isImage = false;
  m_imageData.clear();
  const QFileInfo info(path);

  if (info.isDir()) {
    if (m_dir->model() != m_model)
      m_dir->setModel(m_model);
    const QModelIndex idx = m_model->indexOf(path);
    m_dir->setRootIndex(idx);
    m_dir->scrollToTop();
    m_title->setText(QStringLiteral("DIR  ") + info.fileName() + QLatin1Char('/'));
    if (!info.isReadable() || !info.isExecutable())
      showText(QStringLiteral("DIR  ") + info.fileName(), tr("permission denied"));
    else
      m_stack->setCurrentWidget(m_dir);
    return;
  }

  const QMimeType mime = QMimeDatabase().mimeTypeForFile(info);
  const QString head = mime.name() + QStringLiteral("  ") + humanSize(info.size());

  if (!info.isReadable()) {
    showText(head, tr("permission denied"));
    return;
  }

  if (mime.name().startsWith(QLatin1String("image/"))
      && QImageReader::supportedMimeTypes().contains(mime.name().toLatin1())) {
    m_isImage = true;
    m_title->setText(head);
    m_stack->setCurrentWidget(m_image);
    showImage();
    return;
  }

  // Character devices, fifos and sockets would block or never end.
  if (!info.isFile()) {
    showText(head, tr("special file"));
    m_path = path;
    return;
  }

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    showText(head, f.errorString());
    return;
  }
  showBytes(head, f.read(kTextLimit), info.fileName());
}

void Preview::showBytes(const QString &title, const QByteArray &data, const QString &name)
{
  m_title->setText(title);
  dropHighlighter();
  if (data.left(4096).contains('\0')) {
    m_text->setPlainText(hexDump(data.left(kHexLimit)));
  } else {
    m_text->setPlainText(QString::fromUtf8(data.left(kTextLimit)));
    highlight(name); // after the text: it only ever sees the text it colours
    if (!m_language.isEmpty())
      m_title->setText(title + QStringLiteral("  ") + m_language);
  }
  m_stack->setCurrentWidget(m_text);
}

// A new highlighter for every preview, attached once its text is in, and
// the old one gone before the text changes: the engine queues work on the
// blocks it has seen, and deleting it is what drops that queue (a block
// of replaced text crashes it).
void Preview::dropHighlighter()
{
  delete m_highlighter;
  m_highlighter = nullptr;
  m_language.clear();
}

void Preview::highlight(const QString &name)
{
  dropHighlighter();
#ifdef SYN_HAVE_KSYNTAXHIGHLIGHTING
  if (name.isEmpty())
    return;
  KSyntaxHighlighting::Repository &repo = repository(palette(), false);
  const KSyntaxHighlighting::Definition def = repo.definitionForFileName(name);
  // Plain text gets none: nothing to colour, and no "Normal Text" label.
  if (!def.isValid() || def.name() == QLatin1String("Normal Text"))
    return;
  auto *h = new KSyntaxHighlighting::SyntaxHighlighter(m_text->document());
  h->setTheme(repo.theme(QStringLiteral("SYN")));
  h->setDefinition(def);
  m_highlighter = h;
  m_language = def.name().toLower();
#else
  Q_UNUSED(name);
#endif
}

void Preview::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
#ifdef SYN_HAVE_KSYNTAXHIGHLIGHTING
  // A theme switch: write the SYN theme again from the new palette.
  if (event->type() == QEvent::PaletteChange) {
    KSyntaxHighlighting::Repository &repo = repository(palette(), true);
    if (auto *h = static_cast<KSyntaxHighlighting::SyntaxHighlighter *>(m_highlighter)) {
      const QString lang = h->definition().name();
      h->setTheme(repo.theme(QStringLiteral("SYN")));
      h->setDefinition(repo.definitionForName(lang));
      h->rehighlight();
    }
  }
#endif
}

void Preview::showDir(QAbstractItemModel *model, const QModelIndex &root, const QString &title)
{
  m_path.clear();
  m_isImage = false;
  m_imageData.clear();
  if (m_dir->model() != model)
    m_dir->setModel(model);
  m_dir->setRootIndex(root);
  m_dir->scrollToTop();
  m_title->setText(title);
  m_stack->setCurrentWidget(m_dir);
}

void Preview::showData(const QString &name, qint64 size, const QByteArray &data)
{
  m_path.clear();
  m_isImage = false;
  m_imageData.clear();
  const QMimeType mime = QMimeDatabase().mimeTypeForFileNameAndData(name, data);
  const QString head = mime.name() + QStringLiteral("  ")
                     + (size >= 0 ? humanSize(size) : QStringLiteral("?"));
  if (mime.name().startsWith(QLatin1String("image/"))
      && QImageReader::supportedMimeTypes().contains(mime.name().toLatin1())) {
    m_isImage = true;
    m_imageData = data;
    m_title->setText(head);
    m_stack->setCurrentWidget(m_image);
    showImage();
    return;
  }
  showBytes(head, data, name);
}

void Preview::showImageFile(const QString &imagePath, const QString &title)
{
  dropHighlighter();
  m_path = imagePath;
  m_imageData.clear();
  m_isImage = true;
  m_title->setText(title);
  m_stack->setCurrentWidget(m_image);
  showImage();
}

void Preview::showImage()
{
  QBuffer buffer(&m_imageData);
  QImageReader reader;
  if (m_imageData.isEmpty())
    reader.setFileName(m_path);
  else
    reader.setDevice(&buffer);
  reader.setAutoTransform(true);
  const QSize avail = m_image->size() - QSize(16, 16);
  QSize size = reader.size();
  // Decode straight at display size: a JPEG reader can skip most of the
  // work, which keeps scrolling past a folder of photos responsive.
  if (size.isValid() && (size.width() > avail.width() || size.height() > avail.height()))
    reader.setScaledSize(size.scaled(avail, Qt::KeepAspectRatio));
  const QImage img = reader.read();
  if (img.isNull()) {
    m_image->setText(reader.errorString());
    return;
  }
  m_image->setPixmap(QPixmap::fromImage(img));
}

void Preview::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  if (m_isImage)
    showImage();
}
