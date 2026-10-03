#include "FileOps.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

// syn_bar_notify.c is a plain-C sibling source (see CMakeLists.txt):
// extern "C" so this links against its unmangled symbols. Only
// syn-bar-core itself makes the sound; this asks it for a named meaning.
extern "C" {
#include "syn_bar_notify.h"
}

namespace FileOps {

namespace {

QString nameOf(const QString &path)
{
  return QFileInfo(path).fileName();
}

// True if destDir is srcPath or inside it: pasting a folder into itself.
bool insideItself(const QString &srcPath, const QString &destDir)
{
  const QString src = QDir(srcPath).absolutePath();
  const QString dest = QDir(destDir).absolutePath();
  return dest == src || dest.startsWith(src + QLatin1Char('/'));
}

} // namespace

std::optional<FileJobs::Conflict> askTransfer(QWidget *parent, const QStringList &paths,
                                              const QString &destDir, bool move)
{
  const QString title = move ? QObject::tr("Move") : QObject::tr("Copy");
  QStringList clashes;
  for (const QString &p : paths) {
    if (QFileInfo(p).isDir() && !QFileInfo(p).isSymLink() && insideItself(p, destDir)) {
      QMessageBox::warning(parent, title,
                           QObject::tr("Can't put \"%1\" inside itself.").arg(nameOf(p)));
      return std::nullopt;
    }
    const bool sameFolder = QFileInfo(p).absolutePath() == QDir(destDir).absolutePath();
    if (sameFolder && move) {
      QMessageBox::warning(parent, title,
                           QObject::tr("\"%1\" is already here.").arg(nameOf(p)));
      return std::nullopt;
    }
    // A copy into its own folder becomes "name (2)" by itself: no clash.
    const QString dst = destDir + QLatin1Char('/') + nameOf(p);
    if (!sameFolder && (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink()))
      clashes << nameOf(p);
  }
  if (clashes.isEmpty())
    return FileJobs::Conflict::KeepBoth;

  QMessageBox box(parent);
  box.setWindowTitle(title);
  box.setIcon(QMessageBox::Question);
  box.setText(QObject::tr("%n item(s) already here:", nullptr, int(clashes.size())));
  box.setInformativeText(clashes.mid(0, 12).join(QLatin1Char('\n'))
                         + (clashes.size() > 12 ? QObject::tr("\n... and %1 more").arg(clashes.size() - 12)
                                                : QString()));
  QPushButton *replace = box.addButton(QObject::tr("Replace"), QMessageBox::DestructiveRole);
  QPushButton *both = box.addButton(QObject::tr("Keep both"), QMessageBox::AcceptRole);
  QPushButton *skip = box.addButton(QObject::tr("Skip those"), QMessageBox::ActionRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(both);
  box.exec();
  if (box.clickedButton() == replace)
    return FileJobs::Conflict::Replace;
  if (box.clickedButton() == both)
    return FileJobs::Conflict::KeepBoth;
  if (box.clickedButton() == skip)
    return FileJobs::Conflict::Skip;
  return std::nullopt;
}

bool confirmPermanentDelete(QWidget *parent, const QStringList &paths)
{
  QStringList names;
  for (const QString &p : paths.mid(0, 12))
    names << nameOf(p);
  if (paths.size() > 12)
    names << QObject::tr("... and %1 more").arg(paths.size() - 12);
  return QMessageBox::question(parent, QObject::tr("Delete permanently"),
                               QObject::tr("Delete %n item(s) for good? This skips the trash "
                                           "and can't be undone.\n\n%1",
                                           nullptr, int(paths.size()))
                                 .arg(names.join(QLatin1Char('\n'))),
                               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
         == QMessageBox::Yes;
}

void soundSuccess()
{
  syn_bar_notify_meaning("SUCCESS");
}

void soundFailure()
{
  syn_bar_notify_meaning("FAIL");
}

} // namespace FileOps
