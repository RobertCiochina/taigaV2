#include "auto_download_rules.hpp"

#include <algorithm>
#include <optional>
#include <vector>

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

bool isJustAiredRelease(const std::int64_t now_secs, const std::int64_t air_at_secs,
                        const std::int64_t window_secs) {
  if (air_at_secs <= 0 || window_secs <= 0) return false;
  if (now_secs < air_at_secs) return false;
  return now_secs - air_at_secs <= window_secs;
}

std::optional<std::int64_t> detectJustAiredAt(const std::int64_t now_secs,
                                              const std::int64_t current_next_episode_time,
                                              const std::int64_t previous_next_episode_time,
                                              const std::int64_t last_poll_secs,
                                              const std::int64_t window_secs) {
  if (isJustAiredRelease(now_secs, current_next_episode_time, window_secs)) {
    return current_next_episode_time;
  }
  if (previous_next_episode_time > 0 && previous_next_episode_time != current_next_episode_time &&
      isJustAiredRelease(now_secs, previous_next_episode_time, window_secs)) {
    return previous_next_episode_time;
  }
  // Crossed from still-upcoming at last poll to already aired (sleep / long gap).
  if (last_poll_secs > 0 && previous_next_episode_time > last_poll_secs &&
      previous_next_episode_time <= now_secs) {
    return previous_next_episode_time;
  }
  return std::nullopt;
}

int inferJustAiredEpisode(const int last_aired_episode, const int watched_episodes,
                          const bool schedule_already_advanced) {
  if (schedule_already_advanced && last_aired_episode > 0) return last_aired_episode;
  if (last_aired_episode > 0) return last_aired_episode + 1;
  return std::max(1, watched_episodes + 1);
}

std::int64_t delayedAutoDownloadDueAt(const std::int64_t air_at_secs,
                                      const std::int64_t delay_secs) {
  return air_at_secs + std::max<std::int64_t>(1, delay_secs);
}

int lastAiredForDelayedAutoDownload(const int computed_last_aired,
                                    const int recorded_aired_episode) {
  return std::max(computed_last_aired, recorded_aired_episode);
}

namespace {

bool delayedAutoDownloadJobLess(const DelayedAutoDownloadJob& a, const DelayedAutoDownloadJob& b) {
  if (a.due_at_secs != b.due_at_secs) return a.due_at_secs < b.due_at_secs;
  return a.anime_id < b.anime_id;
}

}  // namespace

void insertDelayedAutoDownloadJob(std::vector<DelayedAutoDownloadJob>& queue,
                                  DelayedAutoDownloadJob job) {
  const auto it = std::lower_bound(queue.begin(), queue.end(), job, delayedAutoDownloadJobLess);
  queue.insert(it, std::move(job));
}

std::vector<DelayedAutoDownloadJob> takeNextDelayedAutoDownloadJobs(
    std::vector<DelayedAutoDownloadJob>& queue, const std::int64_t now_secs) {
  std::vector<DelayedAutoDownloadJob> taken;
  if (queue.empty() || queue.front().due_at_secs > now_secs) return taken;
  const std::int64_t due = queue.front().due_at_secs;
  while (!queue.empty() && queue.front().due_at_secs == due) {
    taken.push_back(queue.front());
    queue.erase(queue.begin());
  }
  return taken;
}

std::vector<DelayedAutoDownloadJob> peekNextDelayedAutoDownloadJobs(
    const std::vector<DelayedAutoDownloadJob>& queue, const std::int64_t now_secs) {
  std::vector<DelayedAutoDownloadJob> taken;
  if (queue.empty() || queue.front().due_at_secs > now_secs) return taken;
  const std::int64_t due = queue.front().due_at_secs;
  for (const auto& job : queue) {
    if (job.due_at_secs != due) break;
    taken.push_back(job);
  }
  return taken;
}

std::int64_t soonestDelayedAutoDownloadDue(const std::vector<DelayedAutoDownloadJob>& queue) {
  return queue.empty() ? 0 : queue.front().due_at_secs;
}

}  // namespace taiga
