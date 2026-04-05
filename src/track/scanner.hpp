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

#pragma once

#include <QString>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace track {

struct LibraryScanSummary {
  int entries_visited = 0;
  int video_files = 0;
  int recognized = 0;
  /// Anime ids for which at least one local episode path was recorded (same scan as above).
  int series_with_local_episodes = 0;
};

/// Episode numbers seen on disk for each anime id during the last `scanLibraryFolders` call.
const std::unordered_map<int, std::unordered_set<int>>& libraryEpisodeAvailability();

bool libraryHasLocalEpisode(int anime_id, int episode_number);

/// Walks configured library folders, parses filenames, and counts how many match the recognition DB.
/// Stops after \a max_entries filesystem entries (files only) to keep the UI responsive.
LibraryScanSummary scanLibraryFolders(const std::vector<std::string>& folders, int max_entries);

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number);
std::optional<QString> findFolder(const QString& path, const int anime_id);

}  // namespace track
