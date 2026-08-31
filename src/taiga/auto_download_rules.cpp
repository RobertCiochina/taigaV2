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

std::int64_t rememberNextEpisodeTime(const std::int64_t now_secs,
                                     const std::int64_t current_next_episode_time,
                                     const std::int64_t previous_next_episode_time) {
  if (previous_next_episode_time > now_secs &&
      (current_next_episode_time <= 0 || current_next_episode_time > previous_next_episode_time)) {
    return previous_next_episode_time;
  }
  if (current_next_episode_time > 0) return current_next_episode_time;
  return previous_next_episode_time;
}

std::optional<std::int64_t> detectJustAiredAt(const std::int64_t now_secs,
                                              const std::int64_t current_next_episode_time,
                                              const std::int64_t previous_next_episode_time,
                                              const std::int64_t last_poll_secs,
                                              const std::int64_t window_secs) {
  const auto crossedSinceLastPoll = [now_secs, last_poll_secs](const std::int64_t air_at) {
    return last_poll_secs > 0 && air_at > last_poll_secs && air_at <= now_secs;
  };
  if (crossedSinceLastPoll(current_next_episode_time)) return current_next_episode_time;
  if (crossedSinceLastPoll(previous_next_episode_time)) return previous_next_episode_time;

  // Stale timestamps that were already in the past at last poll: only the short window.
  if (isJustAiredRelease(now_secs, current_next_episode_time, window_secs)) {
    return current_next_episode_time;
  }
  if (previous_next_episode_time > 0 && previous_next_episode_time != current_next_episode_time &&
      isJustAiredRelease(now_secs, previous_next_episode_time, window_secs)) {
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

bool allowSeasonPackAutoDownload(const int local_ep_count, const int missing_count,
                                 const int effective_last, const int episode_count) {
  if (local_ep_count != 0) return false;
  if (missing_count < 3) return false;
  if (episode_count <= 0) return false;
  return effective_last >= episode_count;
}

std::optional<std::int64_t> pendingDelayedAutoDownloadDueAt(
    const std::vector<DelayedAutoDownloadJob>& queue, const int anime_id) {
  std::optional<std::int64_t> due;
  for (const auto& job : queue) {
    if (job.anime_id != anime_id) continue;
    if (!due || job.due_at_secs < *due) due = job.due_at_secs;
  }
  return due;
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

void upsertDelayedAutoDownloadJob(std::vector<DelayedAutoDownloadJob>& queue,
                                  DelayedAutoDownloadJob job) {
  queue.erase(std::remove_if(queue.begin(), queue.end(),
                             [id = job.anime_id](const DelayedAutoDownloadJob& j) {
                               return j.anime_id == id;
                             }),
              queue.end());
  insertDelayedAutoDownloadJob(queue, std::move(job));
}

std::optional<DelayedAutoDownloadJob> retryDelayedAutoDownloadJob(const DelayedAutoDownloadJob& job,
                                                                  const std::int64_t now_secs,
                                                                  const std::int64_t delay_secs,
                                                                  const int max_attempts) {
  if (job.anime_id <= 0) return std::nullopt;
  if (job.rss_attempts + 1 >= std::max(1, max_attempts)) return std::nullopt;
  DelayedAutoDownloadJob next = job;
  next.rss_attempts = job.rss_attempts + 1;
  next.due_at_secs = delayedAutoDownloadDueAt(now_secs, delay_secs);
  return next;
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

std::vector<DelayedAutoDownloadJob> takeDueDelayedAutoDownloadJobs(
    std::vector<DelayedAutoDownloadJob>& queue, const std::int64_t now_secs) {
  std::vector<DelayedAutoDownloadJob> taken;
  while (!queue.empty() && queue.front().due_at_secs <= now_secs) {
    taken.push_back(queue.front());
    queue.erase(queue.begin());
  }
  return taken;
}

std::vector<DelayedAutoDownloadJob> peekDueDelayedAutoDownloadJobs(
    const std::vector<DelayedAutoDownloadJob>& queue, const std::int64_t now_secs) {
  std::vector<DelayedAutoDownloadJob> taken;
  for (const auto& job : queue) {
    if (job.due_at_secs > now_secs) break;
    taken.push_back(job);
  }
  return taken;
}

std::int64_t soonestDelayedAutoDownloadDue(const std::vector<DelayedAutoDownloadJob>& queue) {
  return queue.empty() ? 0 : queue.front().due_at_secs;
}

std::int64_t nextDelayedAutoDownloadCycleAt(const std::vector<DelayedAutoDownloadJob>& queue,
                                            const std::int64_t last_cycle_start_secs,
                                            const std::int64_t gap_secs) {
  const std::int64_t soonest = soonestDelayedAutoDownloadDue(queue);
  if (soonest <= 0) return 0;
  if (last_cycle_start_secs <= 0 || gap_secs <= 0) return soonest;
  return std::max(soonest, last_cycle_start_secs + gap_secs);
}

}  // namespace taiga
