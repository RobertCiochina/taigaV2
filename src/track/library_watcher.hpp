/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace track {

/// Taiga v1 `anime/folders/watch/enabled` — debounced rescan when library roots change on disk.
class LibraryFolderWatcher final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LibraryFolderWatcher)

public:
  explicit LibraryFolderWatcher(QObject* parent = nullptr);
  void refreshFromSettings();

signals:
  void debouncedRescanTriggered();

private:
  void scheduleRescan();
  void onDirectoryChanged(const QString& path);
  void rebuildWatchedDirectories();

  QFileSystemWatcher* watcher_ = nullptr;
  QTimer* debounce_ = nullptr;
};

LibraryFolderWatcher* libraryFolderWatcher();

}  // namespace track
