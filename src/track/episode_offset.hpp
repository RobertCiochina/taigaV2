/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
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

namespace anime {
struct Details;
}

namespace track {

/// Release/file episode → list episode: `list = release - offset`.
/// Offset 0 means 1-based numbering matches the list entry.
///
/// Manual override (settings) wins. Otherwise, when AniList `last_aired_episode` is clearly
/// absolute (`> episode_count`) and the title is finished, infer
/// `offset = last_aired_episode - episode_count`.
int episodeOffset(int anime_id);
int episodeOffset(const anime::Details& item);

/// Inferred offset only (ignores manual override). 0 when ambiguous.
int inferredEpisodeOffset(const anime::Details& item);

/// True when a manual first-episode / offset override is stored for this anime.
bool hasManualEpisodeOffset(int anime_id);

/// release_ep in file/RSS space → list episode (1-based). Returns 0 if unmappable.
int toListEpisode(int anime_id, int release_ep);
int toListEpisode(const anime::Details& item, int release_ep);

/// list_ep → release/file/RSS episode number.
int toReleaseEpisode(int anime_id, int list_ep);
int toReleaseEpisode(const anime::Details& item, int list_ep);

/// Convert a service `last_aired_episode` (may be absolute) into list-relative last aired.
int toListLastAiredEpisode(const anime::Details& item, int last_aired_episode);

}  // namespace track
