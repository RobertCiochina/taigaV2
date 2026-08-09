#include "auto_download_rules.hpp"

#include <algorithm>

#include "media/anime.hpp"
#include "media/anime_utils.hpp"

namespace taiga {

int computeLastAiredEpisodeForAutoDownload(const anime::Details& item, const int watched_episodes,
                                           const std::int64_t now_secs) {
  // Finished airing: prefer the known episode total over a stale mid-season last_aired value
  // left behind when nextAiringEpisode became null without a full recount.
  if (anime::isFinishedAiring(item) && item.episode_count > 0) {
    return std::max(item.last_aired_episode, item.episode_count);
  }

  // If the service provides a last-aired value, trust it.
  if (item.last_aired_episode > 0) return item.last_aired_episode;

  // If we know the next episode is in the future, nothing beyond watched has aired yet.
  if (item.next_episode_time > 0 && static_cast<std::int64_t>(item.next_episode_time) > now_secs) {
    return watched_episodes;
  }

  // Missing schedule metadata: don't assume future episodes aired.
  return watched_episodes;
}

}  // namespace taiga
