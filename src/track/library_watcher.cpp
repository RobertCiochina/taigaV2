/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "library_watcher.hpp"

#include <QApplication>
#include <QDir>

#include "taiga/settings.hpp"

namespace track {

LibraryFolderWatcher::LibraryFolderWatcher(QObject* parent) : QObject(parent) {
  watcher_ = new QFileSystemWatcher(this);
  debounce_ = new QTimer(this);
  debounce_->setSingleShot(true);
  debounce_->setInterval(2000);
  connect(debounce_, &QTimer::timeout, this, &LibraryFolderWatcher::debouncedRescanTriggered);
  connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
          &LibraryFolderWatcher::onDirectoryChanged);
}

void LibraryFolderWatcher::refreshFromSettings() {
  if (!watcher_) return;
  const QStringList prev = watcher_->directories();
  if (!prev.isEmpty()) {
    watcher_->removePaths(prev);
  }
  if (!taiga::settings.libraryWatchFoldersEnabled()) return;

  for (const auto& folder : taiga::settings.libraryFolders()) {
    const QDir root{QString::fromStdString(folder)};
    if (!root.exists()) continue;
    const QString path = root.absolutePath();
    if (!watcher_->directories().contains(path)) {
      watcher_->addPath(path);
    }
  }
}

void LibraryFolderWatcher::onDirectoryChanged(const QString&) {
  if (!taiga::settings.libraryWatchFoldersEnabled()) return;
  scheduleRescan();
}

void LibraryFolderWatcher::scheduleRescan() {
  if (!taiga::settings.libraryWatchFoldersEnabled()) return;
  debounce_->start();
}

LibraryFolderWatcher* libraryFolderWatcher() {
  static auto* w = new LibraryFolderWatcher(qApp);
  return w;
}

}  // namespace track
