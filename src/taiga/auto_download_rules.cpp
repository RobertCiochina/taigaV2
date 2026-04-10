#include "auto_download_rules.hpp"

#include "media/anime.hpp"

namespace taiga {

int computeLastAiredEpisodeForAutoDownload(const anime::Details& item, const int watched_episodes,
                                          const std::int64_t now_secs) {
  // If the service provides a last-aired value, trust it.
  if (item.last_aired_episode > 0) return item.last_aired_episode;

  // If we know the next episode is in the future, nothing beyond watched has aired yet.
  if (item.next_episode_time > 0 && static_cast<std::int64_t>(item.next_episode_time) > now_secs) {
    return watched_episodes;
  }

  // If the show is finished airing, episode_count is a safe upper bound when known.
  if (item.status == anime::Status::FinishedAiring && item.episode_count > 0) {
    return item.episode_count;
  }

  // Missing schedule metadata: don't assume future episodes aired.
  return watched_episodes;
}

}  // namespace taiga

