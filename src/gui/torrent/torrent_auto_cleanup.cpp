#include "torrent_auto_cleanup.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>

#include "media/anime.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/scanner.hpp"

namespace gui {

namespace {

bool isUnderRoot(const QString& path, const QString& root) {
  // Normalize separators for stable prefix checks. QDir::cleanPath can yield forward slashes
  // even on Windows; mixing it with QDir::separator() (backslash) breaks startsWith().
  const QString p = QDir::fromNativeSeparators(QDir::cleanPath(path));
  const QString r = QDir::fromNativeSeparators(QDir::cleanPath(root));
  if (p.isEmpty() || r.isEmpty()) return false;
  if (p.compare(r, Qt::CaseInsensitive) == 0) return true;
  return p.startsWith(r + QLatin1Char('/'), Qt::CaseInsensitive);
}

bool isVideoCandidate(const QFileInfo& fi) {
  if (!fi.exists() || !fi.isFile()) return false;
  const QString ext = fi.suffix().toLower();
  static const QSet<QString> kVideoExt{
      QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"),
      QStringLiteral("wmv"), QStringLiteral("mov"), QStringLiteral("webm"),
      QStringLiteral("m4v"), QStringLiteral("ogm"), QStringLiteral("ts"),
  };
  if (!kVideoExt.contains(ext)) return false;

  // Ignore common partial/incomplete markers.
  const QString name = fi.fileName().toLower();
  if (name.endsWith(QStringLiteral(".part")) || name.endsWith(QStringLiteral(".!qb"))) return false;

  const qint64 min_bytes = taiga::settings.libraryScanMinFileSizeBytes();
  if (min_bytes > 0 && fi.size() < min_bytes) return false;

  return true;
}

bool isRecognizedVideoFile(const QFileInfo& fi) {
  auto ep = track::recognition::parseFileInfo(
      fi, {}, taiga::settings.libraryScanLookupParentDirectories());
  const int aid = track::recognition::identify(ep);
  return aid != anime::kUnknownId;
}

struct DirVideoScan {
  int recognized = 0;
  int unrecognized = 0;
  int deleted = 0;
};

DirVideoScan deleteUnrecognizedVideosInDir(const QString& dir_path, QStringList* deleted_paths) {
  DirVideoScan r;
  int deleted = 0;
  QDir d(dir_path);
  if (!d.exists()) return r;
  const QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Name);
  for (const QFileInfo& fi : files) {
    if (!isVideoCandidate(fi)) continue;
    if (isRecognizedVideoFile(fi)) {
      ++r.recognized;
      continue;
    }
    ++r.unrecognized;
    if (QFile::remove(fi.absoluteFilePath())) {
      ++deleted;
      ++r.deleted;
      if (deleted_paths) deleted_paths->push_back(fi.absoluteFilePath());
    }
  }
  return r;
}

DirVideoScan scanAndDeleteUnrecognizedVideosRecursively(const QString& dir_path,
                                                        QStringList* deleted_paths) {
  DirVideoScan r;
  QDir root(dir_path);
  if (!root.exists()) return r;

  QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QFileInfo fi(it.next());
    if (!isVideoCandidate(fi)) continue;
    if (isRecognizedVideoFile(fi)) {
      ++r.recognized;
      continue;
    }
    ++r.unrecognized;
    if (QFile::remove(fi.absoluteFilePath())) {
      ++r.deleted;
      if (deleted_paths) deleted_paths->push_back(fi.absoluteFilePath());
    }
  }
  return r;
}

bool deleteDirIfEmpty(const QString& dir_path) {
  QDir d(dir_path);
  if (!d.exists()) return false;
  const QFileInfoList entries = d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
  if (!entries.isEmpty()) return false;
  return QDir().rmdir(dir_path);
}

int deleteEmptyDirsUnder(const QString& root_dir_path) {
  int deleted = 0;
  QDir root(root_dir_path);
  if (!root.exists()) return 0;

  // Gather all subdirectories so we can try to remove them bottom-up.
  QStringList dirs;
  QDirIterator it(root.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    dirs.push_back(QDir::cleanPath(it.next()));
  }
  // Remove deeper paths first.
  std::sort(dirs.begin(), dirs.end(),
            [](const QString& a, const QString& b) { return a.size() > b.size(); });

  for (const QString& p : dirs) {
    if (deleteDirIfEmpty(p)) ++deleted;
  }
  return deleted;
}

}  // namespace

TorrentAutoCleanup::TorrentAutoCleanup(QObject* parent) : QObject(parent) {
  watcher_ = new QFileSystemWatcher(this);
  debounce_ = new QTimer(this);
  debounce_->setSingleShot(true);
  debounce_->setInterval(1500);

  connect(debounce_, &QTimer::timeout, this, &TorrentAutoCleanup::runCleanup);
}

QString TorrentAutoCleanup::downloadRoot() const {
  return QDir::cleanPath(QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed());
}

bool TorrentAutoCleanup::enabledAndSafe() const {
  if (!taiga::settings.torrentAutoCleanupUnrecognizedDownloads()) return false;
  if (!taiga::settings.torrentDownloadCreateSubfolder()) return false;
  const QString root = downloadRoot();
  if (root.isEmpty() || !QDir(root).exists()) return false;
  return true;
}

void TorrentAutoCleanup::ensureWatcherArmed() {
  if (!watcher_) return;
  if (!enabledAndSafe()) return;

  const QString root = downloadRoot();
  if (root.isEmpty()) return;
  const QStringList cur = watcher_->directories();
  if (!cur.contains(root, Qt::CaseInsensitive)) {
    watcher_->addPath(root);
  }
}

void TorrentAutoCleanup::scheduleCleanup() {
  if (!enabledAndSafe()) return;
  debounce_->start();
}

void TorrentAutoCleanup::runCleanup() {
  if (!enabledAndSafe()) return;

  const bool diag = taiga::settings.cacheDiagnosticsEnabled();
  const QString root = downloadRoot();

  QDir root_dir(root);
  if (!root_dir.exists()) return;
  const QFileInfoList folders =
      root_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  if (diag) {
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("autocleanup: begin scan='%1' root='%2' folders=%3")
            .arg(last_reason_.left(80))
            .arg(root.left(180))
            .arg(folders.size()));
  }

  int total_seen_video = 0;
  int total_seen_recognized = 0;
  int total_seen_unrecognized = 0;
  int total_deleted_files = 0;
  int total_deleted_dirs = 0;

  // Also handle video files saved directly under the torrent root (some clients/settings ignore
  // the intended per-title subfolder). Only delete unrecognized files; never delete directories.
  {
    QStringList deleted_root;
    const DirVideoScan root_scan = deleteUnrecognizedVideosInDir(root, &deleted_root);
    total_seen_video += (root_scan.recognized + root_scan.unrecognized);
    total_seen_recognized += root_scan.recognized;
    total_seen_unrecognized += root_scan.unrecognized;
    total_deleted_files += deleted_root.size();
    if (diag && (root_scan.deleted > 0)) {
      track::appendLibraryEpisodeIndexCacheDebugLine(
          QStringLiteral("autocleanup: scan='%1' rootFiles deletedFiles=%2")
              .arg(last_reason_.left(80))
              .arg(deleted_root.size()));
    }
  }

  for (const QFileInfo& fi_folder : folders) {
    const QString folder = QDir::cleanPath(fi_folder.absoluteFilePath());
    if (folder.isEmpty()) continue;
    const bool exists = fi_folder.exists();
    const bool is_dir = fi_folder.isDir();
    if (!exists || !is_dir) continue;
    if (!isUnderRoot(folder, root) || folder.compare(root, Qt::CaseInsensitive) == 0) continue;

    QStringList deleted;
    // Delete unrecognized videos anywhere under the anime subfolder.
    const DirVideoScan folder_scan = scanAndDeleteUnrecognizedVideosRecursively(folder, &deleted);
    // Best-effort: also delete empty subdirectories that might remain after removing files.
    const int emptied = deleteEmptyDirsUnder(folder);

    total_seen_video += (folder_scan.recognized + folder_scan.unrecognized);
    total_seen_recognized += folder_scan.recognized;
    total_seen_unrecognized += folder_scan.unrecognized;
    const int deleted_dirs = emptied;

    if (diag && (folder_scan.deleted > 0 || !deleted.isEmpty() || deleted_dirs > 0)) {
      track::appendLibraryEpisodeIndexCacheDebugLine(
          QStringLiteral("autocleanup: scan='%1' folder='%2' seen=%3 rec=%4 unrec=%5 deletedFiles=%6 deletedDirs=%7")
              .arg(last_reason_.left(80))
              .arg(folder.left(180))
              .arg(folder_scan.recognized + folder_scan.unrecognized)
              .arg(folder_scan.recognized)
              .arg(folder_scan.unrecognized)
              .arg(deleted.size())
              .arg(deleted_dirs));
      constexpr int kMaxShown = 10;
      for (int i = 0; i < std::min(kMaxShown, static_cast<int>(deleted.size())); ++i) {
        track::appendLibraryEpisodeIndexCacheDebugLine(
            QStringLiteral("autocleanup:   deleted: %1").arg(deleted[i].left(220)));
      }
    }

    total_deleted_files += deleted.size();
    total_deleted_dirs += deleted_dirs;
  }

  if (diag) {
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("autocleanup: end scan='%1' seen=%2 rec=%3 unrec=%4 deletedFiles=%5 deletedDirs=%6")
            .arg(last_reason_.left(80))
            .arg(total_seen_video)
            .arg(total_seen_recognized)
            .arg(total_seen_unrecognized)
            .arg(total_deleted_files)
            .arg(total_deleted_dirs));
  }
}

void TorrentAutoCleanup::runCleanupAfterLibraryScan(const QString& scan_reason_label) {
  last_reason_ = scan_reason_label.trimmed();
  if (!enabledAndSafe()) {
    if (taiga::settings.cacheDiagnosticsEnabled()) {
      track::appendLibraryEpisodeIndexCacheDebugLine(
          QStringLiteral("autocleanup: skipped after scan '%1' (disabled or unsafe settings)")
              .arg(last_reason_.left(80)));
    }
    return;
  }

  ensureWatcherArmed();
  // Debounce to allow the scan-triggering download to finish writing files first.
  if (taiga::settings.cacheDiagnosticsEnabled()) {
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("autocleanup: scheduled after scan '%1'").arg(last_reason_.left(80)));
  }
  scheduleCleanup();
}

TorrentAutoCleanup* torrentAutoCleanup() {
  static auto* c = new TorrentAutoCleanup(qApp);
  return c;
}

}  // namespace gui

