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

#include <vector>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/recognition_normalize.hpp"
#include "track/recognition_titles.hpp"

namespace track {
namespace {

int clampNonNegative(const int v) {
  return v > 0 ? v : 0;
}

QString stageTitleOf(const anime::Details& item) {
  if (!item.titles.romaji.empty()) return QString::fromStdString(item.titles.romaji);
  if (!item.titles.english.empty()) return QString::fromStdString(item.titles.english);
  return QString::fromStdString(item.titles.japanese);
}

}  // namespace

int inferredEpisodeOffset(const anime::Details& item) {
  // Absolute last_aired above this cour's episode_count implies a multi-cour offset
  // (e.g. 54 - 14 = 40), whether the cour is still airing or already finished.
  int from_aired = 0;
  if (item.episode_count >= 1 && item.last_aired_episode > item.episode_count) {
    from_aired = item.last_aired_episode - item.episode_count;
  }

  std::vector<const anime::Details*> catalog;
  if (!recognition::courStageKey(stageTitleOf(item)).isEmpty()) {
    const auto& items = anime::db.items();
    catalog.reserve(static_cast<size_t>(items.size()));
    // QMap::asKeyValueRange() yields pairs by value. Pointers into that copy dangle.
    for (auto it = items.cbegin(); it != items.cend(); ++it) {
      catalog.push_back(&(*it));
    }
  }
  const int from_cour = earlierCourStageOffset(item, catalog);
  return from_aired > from_cour ? from_aired : from_cour;
}

int earlierCourStageOffset(const anime::Details& item,
                           const std::vector<const anime::Details*>& catalog) {
  const QString mine = stageTitleOf(item);
  if (recognition::courStageKey(mine).isEmpty()) return 0;
  const QString base = recognition::stripCourStageSuffix(mine);
  if (base.isEmpty() || item.date_started.empty()) return 0;
  const std::string base_key = recognition::normalize(base.toStdString());
  if (base_key.empty()) return 0;

  int sum = 0;
  for (const anime::Details* other : catalog) {
    if (!other || other->id == item.id) continue;
    if (other->episode_count < 1 || other->date_started.empty()) continue;
    if (!(other->date_started < item.date_started)) continue;
    const QString other_title = stageTitleOf(*other);
    if (recognition::courStageKey(other_title).isEmpty()) continue;
    const QString other_base = recognition::stripCourStageSuffix(other_title);
    if (recognition::normalize(other_base.toStdString()) != base_key) continue;
    sum += other->episode_count;
  }
  return sum;
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
