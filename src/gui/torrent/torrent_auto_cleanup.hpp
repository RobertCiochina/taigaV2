#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QFileSystemWatcher>

namespace gui {

/// Auto-download cleanup: delete unrecognized video files from the torrent client download folder.
/// Safety constraints:
/// - Runs only when enabled in settings.
/// - Runs only when torrent "create subfolder" is enabled.
/// - Operates only under the configured torrent client download root and only for registered
///   per-anime subfolders that Taiga created/used for auto-download.
class TorrentAutoCleanup final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentAutoCleanup)

public:
  explicit TorrentAutoCleanup(QObject* parent = nullptr);

  /// Run cleanup after a completed library scan.
  /// Scans all immediate subfolders under the torrent client download root (safe only when
  /// create-subfolder is enabled).
  void runCleanupAfterLibraryScan(const QString& scan_reason_label);

private:
  void ensureWatcherArmed();
  void scheduleCleanup();
  void runCleanup();

  QString downloadRoot() const;
  bool enabledAndSafe() const;

  QTimer* debounce_ = nullptr;
  QFileSystemWatcher* watcher_ = nullptr;

  QString last_reason_;
};

TorrentAutoCleanup* torrentAutoCleanup();

}  // namespace gui

