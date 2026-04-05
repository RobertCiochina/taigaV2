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

#include "anime_list_utils.hpp"

#include <algorithm>

#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "media/anime_utils.hpp"

namespace anime::list {

float getProgressRatio(const Details* item, const Entry* entry) {
    const auto progress = (entry ? entry->watched_episodes : 0);
    const auto total = (item ? item->episode_count : 0);
    if (!total) return 0.8f;
    return std::min(progress / static_cast<float>(total), 1.0f);
}

int lastAiredEpisodeForProgress(const Details& item, const Entry* entry) {
  if (isFinishedAiring(item)) {
    return item.episode_count > 0 ? item.episode_count : 0;
  }
  int n = 0;
  if (entry) n = std::max(n, entry->watched_episodes);
  n = std::max(n, item.last_aired_episode);
  return n;
}

void getProgressBarRatios(const Details* item, const Entry* entry, float& ratio_aired,
                          float& ratio_watched) {
  ratio_aired = 0.0f;
  ratio_watched = 0.0f;
  if (!item) return;

  const int eps_total = anime::estimateEpisodeCount(*item, 0);
  const int eps_aired = lastAiredEpisodeForProgress(*item, entry);
  const int eps_watched = entry ? entry->watched_episodes : 0;

  if (eps_total > 0) {
    if (eps_aired > 0) ratio_aired = eps_aired / static_cast<float>(eps_total);
    if (eps_watched > 0) ratio_watched = eps_watched / static_cast<float>(eps_total);
  } else {
    if (eps_aired > 0) ratio_aired = eps_aired > eps_watched ? 0.85f : 0.8f;
    if (eps_watched > 0)
      ratio_watched = std::min(0.8f, (0.8f * eps_watched) / static_cast<float>(std::max(eps_aired, 1)));
  }

  ratio_aired = std::min(ratio_aired, 1.0f);
  ratio_watched = std::min(ratio_watched, 1.0f);
}

}  // namespace anime::list
