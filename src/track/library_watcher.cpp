/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "library_watcher.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QQueue>

#include "taiga/settings.hpp"

namespace track {

LibraryFolderWatcher::LibraryFolderWatcher(QObject* parent) : QObject(parent) {
  watcher_ = new QFileSystemWatcher(this);
  debounce_ = new QTimer(this);
  debounce_->setSingleShot(true);
  // Faster than the old 2s delay so "episodes ready to watch" updates promptly after
  // downloads or file operations, while still coalescing bursts of filesystem events.
  debounce_->setInterval(500);
  connect(debounce_, &QTimer::timeout, this, &LibraryFolderWatcher::debouncedRescanTriggered);
  connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
          &LibraryFolderWatcher::onDirectoryChanged);
}

void LibraryFolderWatcher::refreshFromSettings() {
  if (!watcher_) return;
  if (!taiga::settings.libraryWatchFoldersEnabled()) {
    const QStringList prev = watcher_->directories();
    if (!prev.isEmpty()) watcher_->removePaths(prev);
    return;
  }

  rebuildWatchedDirectories();
}

void LibraryFolderWatcher::onDirectoryChanged(const QString&) {
  if (!taiga::settings.libraryWatchFoldersEnabled()) return;
  // If a new season/anime subfolder was created, it won't be watched yet.
  // Rebuild the watched directory list (bounded) so subsequent changes inside that folder
  // also trigger rescans.
  rebuildWatchedDirectories();
  scheduleRescan();
}

void LibraryFolderWatcher::scheduleRescan() {
  if (!taiga::settings.libraryWatchFoldersEnabled()) return;
  debounce_->start();
}

void LibraryFolderWatcher::rebuildWatchedDirectories() {
  if (!watcher_) return;
  const QStringList prev = watcher_->directories();
  if (!prev.isEmpty()) {
    watcher_->removePaths(prev);
  }

  // QFileSystemWatcher is not recursive; to detect changes inside season/anime subfolders,
  // we add subdirectories too (bounded by depth and total count).
  //
  // Guardrails: avoid watching an unbounded number of folders on huge libraries.
  constexpr int kMaxWatchedDirs = 2500;
  constexpr int kMaxDepth = 5;  // root=0, its children=1, etc.

  QStringList to_watch;
  to_watch.reserve(kMaxWatchedDirs);

  struct Node { QString path; int depth; };
  QQueue<Node> q;

  for (const auto& folder : taiga::settings.libraryFolders()) {
    const QDir root{QString::fromStdString(folder)};
    if (!root.exists()) continue;
    const QString root_path = QDir::cleanPath(root.absolutePath());
    if (root_path.isEmpty()) continue;
    q.enqueue(Node{root_path, 0});
  }

  while (!q.isEmpty() && to_watch.size() < kMaxWatchedDirs) {
    const Node n = q.dequeue();
    if (n.path.isEmpty()) continue;
    if (!QDir(n.path).exists()) continue;
    if (!to_watch.contains(n.path, Qt::CaseInsensitive)) {
      to_watch << n.path;
    }
    if (n.depth >= kMaxDepth) continue;

    QDir d(n.path);
    const QFileInfoList children =
        d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : children) {
      if (to_watch.size() >= kMaxWatchedDirs) break;
      q.enqueue(Node{QDir::cleanPath(fi.absoluteFilePath()), n.depth + 1});
    }
  }

  if (!to_watch.isEmpty()) {
    watcher_->addPaths(to_watch);
  }
}

LibraryFolderWatcher* libraryFolderWatcher() {
  static auto* w = new LibraryFolderWatcher(qApp);
  return w;
}

}  // namespace track
