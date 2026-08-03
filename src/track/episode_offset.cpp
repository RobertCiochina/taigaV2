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

#include "episode_offset.hpp"

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"

namespace track {
namespace {

int clampNonNegative(const int v) {
  return v > 0 ? v : 0;
}

}  // namespace

int inferredEpisodeOffset(const anime::Details& item) {
  // Only trust the absolute signal when the cour is finished: then last_aired is the absolute
  // final episode and offset = last_aired - episode_count (e.g. 54 - 14 = 40).
  if (item.status != anime::Status::FinishedAiring) return 0;
  if (item.episode_count < 1) return 0;
  if (item.last_aired_episode <= item.episode_count) return 0;
  return item.last_aired_episode - item.episode_count;
}

bool hasManualEpisodeOffset(const int anime_id) {
  if (anime_id <= 0) return false;
  return taiga::settings.hasAnimeEpisodeOffsetOverride(anime_id);
}

int episodeOffset(const anime::Details& item) {
  if (item.id > 0 && taiga::settings.hasAnimeEpisodeOffsetOverride(item.id)) {
    return clampNonNegative(taiga::settings.animeEpisodeOffsetOverride(item.id));
  }
  return inferredEpisodeOffset(item);
}

int episodeOffset(const int anime_id) {
  if (anime_id <= 0) return 0;
  if (taiga::settings.hasAnimeEpisodeOffsetOverride(anime_id)) {
    return clampNonNegative(taiga::settings.animeEpisodeOffsetOverride(anime_id));
  }
  const auto* item = anime::db.item(anime_id);
  if (!item) return 0;
  return inferredEpisodeOffset(*item);
}

int toListEpisode(const anime::Details& item, const int release_ep) {
  if (release_ep < 1) return 0;
  const int list = release_ep - episodeOffset(item);
  return list > 0 ? list : 0;
}

int toListEpisode(const int anime_id, const int release_ep) {
  if (release_ep < 1) return 0;
  const int list = release_ep - episodeOffset(anime_id);
  return list > 0 ? list : 0;
}

int toReleaseEpisode(const anime::Details& item, const int list_ep) {
  if (list_ep < 1) return 0;
  return list_ep + episodeOffset(item);
}

int toReleaseEpisode(const int anime_id, const int list_ep) {
  if (list_ep < 1) return 0;
  return list_ep + episodeOffset(anime_id);
}

int toListLastAiredEpisode(const anime::Details& item, const int last_aired_episode) {
  if (last_aired_episode < 1) return 0;
  const int offset = episodeOffset(item);
  if (offset > 0 && last_aired_episode > item.episode_count && item.episode_count > 0) {
    const int list = last_aired_episode - offset;
    if (list < 1) return 0;
    if (item.episode_count > 0 && list > item.episode_count) return item.episode_count;
    return list;
  }
  if (item.episode_count > 0 && last_aired_episode > item.episode_count) {
    return item.episode_count;
  }
  return last_aired_episode;
}

}  // namespace track
