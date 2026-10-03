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
  showBytes(head, f.read(kTextLimit));
}

void Preview::showBytes(const QString &title, const QByteArray &data)
{
  m_title->setText(title);
  if (data.left(4096).contains('\0'))
    m_text->setPlainText(hexDump(data.left(kHexLimit)));
  else
    m_text->setPlainText(QString::fromUtf8(data.left(kTextLimit)));
  m_stack->setCurrentWidget(m_text);
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
  showBytes(head, data);
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
