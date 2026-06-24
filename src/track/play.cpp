/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "play.hpp"

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <random>

#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/media.hpp"
#include "track/scanner.hpp"

namespace track {

namespace {

// Launch the configured player (or OS default) for `path`. On success, tell media detection that
// Taiga itself initiated playback so list-update tracking works even if the external player can't
// be read (empty window title, unreadable handles, etc.).
bool launchAndArm(const int animeId, const int number, const QString& path) {
  const QString exe = QString::fromStdString(taiga::settings.mediaPlayerExecutablePath()).trimmed();
  const bool ok = (!exe.isEmpty() && QFileInfo::exists(exe))
                      ? QProcess::startDetached(exe, QStringList{path})
                      : QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  if (ok) track::media::detection()->notifyTaigaLaunched(animeId, number, path);
  return ok;
}

}  // namespace

bool playEpisode(int animeId, int number) {
  QElapsedTimer t;
  t.start();
  const bool diag = taiga::settings.cacheDiagnosticsEnabled();

  // Fast path: use the library scan index (episode -> file path) if available.
  if (const auto cached = libraryEpisodePath(animeId, number)) {
    if (QFileInfo::exists(*cached)) {
      if (diag) qDebug() << "playEpisode: cached path hit in" << t.elapsed() << "ms:" << *cached;
      return launchAndArm(animeId, number, *cached);
    }
    // Stale cache entry (file moved/deleted). Drop it so next attempt can fall back.
    removeLibraryEpisodePath(animeId, number);
  }

  const auto libraryFolders = taiga::settings.libraryFolders();

  for (const auto& folder : libraryFolders) {
    const auto episodePath = findEpisode(QString::fromStdString(folder), animeId, number);
    if (episodePath) {
      if (diag) qDebug() << "playEpisode: found by scan in" << t.elapsed() << "ms:" << *episodePath;
      return launchAndArm(animeId, number, *episodePath);
    }
  }

  if (diag) {
    qDebug() << "playEpisode: not found after" << t.elapsed() << "ms (animeId=" << animeId
             << "ep=" << number << ")";
  }
  return false;
}

bool playNextEpisode(int animeId) {
  QElapsedTimer t;
  t.start();
  const bool diag = taiga::settings.cacheDiagnosticsEnabled();
  const auto item = anime::db.item(animeId);

  if (!item) return false;

  const auto entry = anime::db.entry(animeId);

  // When `episode_count` is unknown (`kUnknownEpisodeCount` is -1), never clamp watched against it:
  // `std::min(watched, -1)` would be -1 and we'd try to play episode 0 (Home "Up next" uses
  // `watched + 1` and would list a title whose next file exists, but Play would fail).
  const int totalEpisodes = item->episode_count;
  const int watched = entry ? entry->watched_episodes : 0;
  int lastWatched = watched;
  if (totalEpisodes > 0) {
    lastWatched = std::min(watched, totalEpisodes);
  }
  const int nextEpisode = lastWatched + 1;
  if (nextEpisode < 1) return false;

  const bool ok = playEpisode(animeId, nextEpisode);
  if (diag) {
    qDebug() << "playNextEpisode:" << (ok ? "ok" : "fail") << "in" << t.elapsed()
             << "ms (animeId=" << animeId << "next=" << nextEpisode << ")";
  }
  return ok;
}

bool playRandomFromListing() {
  const auto& entries = anime::db.entries();
  if (entries.empty()) return false;

  thread_local std::mt19937 gen{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, static_cast<int>(entries.size()) - 1);
  return playNextEpisode(entries[static_cast<size_t>(dist(gen))].anime_id);
}

}  // namespace track
