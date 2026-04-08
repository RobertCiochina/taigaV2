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

#include "scanner.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <optional>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"

namespace track {

namespace {

std::unordered_map<int, std::unordered_set<int>> g_library_episodes;
// Manual overrides from Library UI — not cleared by scanLibraryFolders.
std::unordered_map<int, std::unordered_set<int>> g_manual_episodes;

/// Map S00Exx-style absolute numbers onto 1..N for short specials when xx > N (common with TVDB-style
/// numbering). Skips unknown or non-season-0 releases.
int storageEpisodeNumber(const int anime_id, const track::Episode& episode) {
  int ep = QString::fromStdString(episode.element(anitomy::ElementKind::Episode, {})).toInt();
  if (ep < 1) return 0;
  const std::string season_str = episode.element(anitomy::ElementKind::Season, {});
  const bool is_s0 = season_str == "0" || season_str == "00";
  const Anime* item = anime::db.item(anime_id);
  if (!item || !is_s0 || item->episode_count < 1) return ep;
  if (ep > item->episode_count && item->episode_count <= 12) {
    return ((ep - 1) % item->episode_count) + 1;
  }
  return ep;
}

}  // namespace

const std::unordered_map<int, std::unordered_set<int>>& libraryEpisodeAvailability() {
  return g_library_episodes;
}

bool libraryHasLocalEpisode(const int anime_id, const int episode_number) {
  if (episode_number < 1) return false;
  const auto it = g_library_episodes.find(anime_id);
  if (it != g_library_episodes.end() && it->second.contains(episode_number)) return true;
  const auto it2 = g_manual_episodes.find(anime_id);
  return it2 != g_manual_episodes.end() && it2->second.contains(episode_number);
}

void removeLibraryEpisode(const int anime_id, const int episode_number) {
  if (episode_number < 1) return;
  const auto it = g_library_episodes.find(anime_id);
  if (it == g_library_episodes.end()) return;
  it->second.erase(episode_number);
  if (it->second.empty()) {
    g_library_episodes.erase(it);
  }
}

void addManualLibraryEpisode(const int anime_id, int episode) {
  if (episode < 1) episode = 1;
  g_manual_episodes[anime_id].insert(episode);
}

void removeManualLibraryEpisode(const int anime_id) {
  g_manual_episodes.erase(anime_id);
}

bool nextEpisodeIsOnDisk(const int anime_id, const anime::Details* anime, const anime::list::Entry* entry) {
  if (!anime || !entry) return false;
  const int next = entry->watched_episodes + 1;
  if (next < 1) return false;
  if (anime->episode_count > 0 && next > anime->episode_count) return false;
  return libraryHasLocalEpisode(anime_id, next);
}

LibraryScanSummary scanLibraryFolders(const std::vector<std::string>& folders,
                                      const int max_entries) {
  LibraryScanSummary s;
  if (max_entries <= 0) return s;

  g_library_episodes.clear();

  const qint64 min_bytes = taiga::settings.libraryScanMinFileSizeBytes();

  static const QSet<QString> kVideoExt{
      QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"),
      QStringLiteral("wmv"), QStringLiteral("mov"), QStringLiteral("webm"),
      QStringLiteral("m4v"), QStringLiteral("ogm"), QStringLiteral("ts"),
  };

  for (const auto& folder : folders) {
    const QDir root{QString::fromStdString(folder)};
    if (!root.exists()) continue;

    QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      if (s.entries_visited >= max_entries) return s;
      const QFileInfo fi(it.next());
      ++s.entries_visited;

      if (!kVideoExt.contains(fi.suffix().toLower())) continue;
      if (min_bytes > 0 && fi.size() < min_bytes) continue;
      ++s.video_files;

      auto episode =
          recognition::parseFileInfo(fi, {}, taiga::settings.libraryScanLookupParentDirectories());
      const int aid = recognition::identify(episode);
      if (aid != anime::kUnknownId) {
        ++s.recognized;
        const int ep = storageEpisodeNumber(aid, episode);
        if (ep > 0) {
          g_library_episodes[aid].insert(ep);
        } else {
          // No episode number in filename (movie, batch, or single-file release).
          // Treat as episode 1 so the entry appears in "Up next" and library lookups.
          g_library_episodes[aid].insert(1);
        }
      }
    }
  }

  s.series_with_local_episodes = static_cast<int>(g_library_episodes.size());
  return s;
}

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number) {
  QDirIterator it{path, QDir::Files, QDirIterator::Subdirectories};
  const qint64 min_bytes = taiga::settings.libraryScanMinFileSizeBytes();

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isFile()) continue;
    if (min_bytes > 0 && info.size() < min_bytes) continue;

    auto episode =
        recognition::parseFileInfo(info, {}, taiga::settings.libraryScanLookupParentDirectories());

    if (recognition::identify(episode) != anime_id) continue;

    if (storageEpisodeNumber(anime_id, episode) != episode_number) continue;

    return info.filePath();
  }

  return std::nullopt;
}

std::optional<QString> findFolder(const QString& path, const int anime_id) {
  QDirIterator it{path, QDir::Dirs, QDirIterator::Subdirectories};

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isDir()) continue;

    auto episode =
        recognition::parseFileInfo(info, {}, taiga::settings.libraryScanLookupParentDirectories());

    if (track::recognition::identify(episode) != anime_id) continue;

    return info.filePath();
  }

  return std::nullopt;
}

}  // namespace track
