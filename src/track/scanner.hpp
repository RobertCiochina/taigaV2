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

namespace anime {
struct Details;
}
namespace anime::list {
struct Entry;
}

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

/// Remove an episode from the on-disk availability index (best-effort).
/// Used when Taiga deletes a watched file so UI (Home "Up next", auto-download) updates immediately
/// without requiring a full rescan.
void removeLibraryEpisode(int anime_id, int episode_number);

/// Register a manually-assigned episode so libraryHasLocalEpisode() returns true for it.
/// Call after a UI override is applied. Not cleared by scanLibraryFolders.
void addManualLibraryEpisode(int anime_id, int episode);

/// Remove all manual episode registrations for an anime (e.g. when clearing an override).
void removeManualLibraryEpisode(int anime_id);

/// True when episode (watched + 1) exists in the library scan index (Taiga v1 "next episode available").
bool nextEpisodeIsOnDisk(int anime_id, const anime::Details* anime, const anime::list::Entry* entry);

/// Walks configured library folders, parses filenames, and counts how many match the recognition DB.
/// Stops after \a max_entries filesystem entries (files only) to keep the UI responsive.
LibraryScanSummary scanLibraryFolders(const std::vector<std::string>& folders, int max_entries);

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number);
std::optional<QString> findFolder(const QString& path, const int anime_id);

}  // namespace track
