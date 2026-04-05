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
#include "media/anime_list.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"

namespace track {

namespace {

std::unordered_map<int, std::unordered_set<int>> g_library_episodes;

}  // namespace

const std::unordered_map<int, std::unordered_set<int>>& libraryEpisodeAvailability() {
  return g_library_episodes;
}

bool libraryHasLocalEpisode(const int anime_id, const int episode_number) {
  if (episode_number < 1) return false;
  const auto it = g_library_episodes.find(anime_id);
  return it != g_library_episodes.end() && it->second.contains(episode_number);
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
      ++s.video_files;

      auto episode = recognition::parseFileInfo(fi);
      const int aid = recognition::identify(episode);
      if (aid != anime::kUnknownId) {
        ++s.recognized;
        const int ep =
            QString::fromStdString(episode.element(anitomy::ElementKind::Episode, {})).toInt();
        if (ep > 0) g_library_episodes[aid].insert(ep);
      }
    }
  }

  s.series_with_local_episodes = static_cast<int>(g_library_episodes.size());
  return s;
}

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number) {
  QDirIterator it{path, QDir::Files, QDirIterator::Subdirectories};

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isFile()) continue;

    auto episode = recognition::parseFileInfo(info);

    if (QString::fromStdString(episode.element(anitomy::ElementKind::Episode)).toInt() !=
        episode_number) {
      continue;
    }

    if (track::recognition::identify(episode) != anime_id) continue;

    return info.filePath();
  }

  return std::nullopt;
}

std::optional<QString> findFolder(const QString& path, const int anime_id) {
  QDirIterator it{path, QDir::Dirs, QDirIterator::Subdirectories};

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isDir()) continue;

    auto episode = recognition::parseFileInfo(info);

    if (track::recognition::identify(episode) != anime_id) continue;

    return info.filePath();
  }

  return std::nullopt;
}

}  // namespace track
