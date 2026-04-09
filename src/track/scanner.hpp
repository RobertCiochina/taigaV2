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

/// True once a library scan has completed in this session.
/// Used by Home "Up next" to avoid showing misleading empty/partial results at startup.
bool libraryScanHasResults();

/// Best-effort: load the most recent on-disk availability index from cache.
/// Returns true when a cache was loaded and the index is now usable immediately.
bool loadLibraryEpisodeIndexCache();

/// Best-effort: persist the current on-disk availability index to cache.
/// Useful on shutdown so the next startup can be instant even if no scan runs.
void saveLibraryEpisodeIndexCache();

/// Debugging helpers for the library episode index cache.
/// (Used to diagnose startup "Up next" being incomplete before a scan finishes.)
QString libraryEpisodeIndexCacheLastError();
QString libraryEpisodeIndexCacheLastInfo();
QString libraryEpisodeIndexCacheDebugLog();

/// Append a line to the cache diagnostics log.
void appendLibraryEpisodeIndexCacheDebugLine(const QString& msg);

/// Persist cache after a completed scan, tagged with a source label.
/// \p allow_regress should be true for authoritative scans (post-sync, manual, watcher).
void saveLibraryEpisodeIndexCacheAfterScan(const QString& source, bool allow_regress);

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

/// True when episode (watched + 1) exists in the library scan index.
bool nextEpisodeIsOnDisk(int anime_id, const anime::Details* anime, const anime::list::Entry* entry);

/// Walks configured library folders, parses filenames, and counts how many match the recognition DB.
/// Stops after \a max_entries filesystem entries (files only) to keep the UI responsive.
/// If \a allow_regress_apply is false, a smaller scan result will not overwrite an already-loaded
/// index (used for the startup-pre-sync scan so Home stays populated from cache until post-sync).
LibraryScanSummary scanLibraryFolders(const std::vector<std::string>& folders, int max_entries,
                                     bool allow_regress_apply = true);

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number);
std::optional<QString> findFolder(const QString& path, const int anime_id);

/// Best-effort: remove empty directory levels within configured library roots.
/// - If \p dir_or_file_path is a file path, its parent directory is used.
/// - Only removes directories that are empty (ignoring common OS junk files like desktop.ini).
/// - Never removes the library root itself.
void cleanupEmptyLibraryDirectoriesFromPath(const QString& dir_or_file_path);

/// Best-effort: find the library folder for \p anime_id (within each configured root),
/// then remove it and any empty parent directories up to (but not including) the root.
/// Safe: only deletes directories that are empty at the time of deletion.
void cleanupEmptyLibraryDirectoriesForAnime(int anime_id);

}  // namespace track
