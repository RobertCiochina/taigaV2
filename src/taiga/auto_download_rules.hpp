#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace anime {
struct Details;
}

namespace taiga {

inline constexpr std::int64_t kReleaseEventJustAiredWindowSeconds = 5 * 60;

/// Best-effort upper bound for "episodes that have aired" for auto-download.
/// Conservative by design: avoids guessing that future episodes aired when schedule metadata is
/// missing.
int computeLastAiredEpisodeForAutoDownload(const anime::Details& item, int watched_episodes,
                                           std::int64_t now_secs);

bool isJustAiredRelease(std::int64_t now_secs, std::int64_t air_at_secs,
                        std::int64_t window_secs = kReleaseEventJustAiredWindowSeconds);

/// Air timestamp to enqueue: current next_episode_time if it just aired, or the previous
/// next_episode_time if AniList already advanced to the following episode.
std::optional<std::int64_t> detectJustAiredAt(
    std::int64_t now_secs, std::int64_t current_next_episode_time,
    std::int64_t previous_next_episode_time,
    std::int64_t window_secs = kReleaseEventJustAiredWindowSeconds);

/// Episode number that just aired, from metadata at detection time.
int inferJustAiredEpisode(int last_aired_episode, int watched_episodes);

std::int64_t delayedAutoDownloadDueAt(std::int64_t air_at_secs, std::int64_t delay_secs);

/// last_aired used for a delayed run: never below the episode recorded when the airing was seen.
int lastAiredForDelayedAutoDownload(int computed_last_aired, int recorded_aired_episode);

/// FIFO of delayed RSS jobs, ordered by due time (air + delay). Same due time stays together.
struct DelayedAutoDownloadJob {
  int anime_id = 0;
  std::int64_t due_at_secs = 0;
  int aired_episode = 0;
};

void insertDelayedAutoDownloadJob(std::vector<DelayedAutoDownloadJob>& queue,
                                  DelayedAutoDownloadJob job);

/// Pops the front job if it is due, plus any following jobs with the same due time.
/// Later jobs that are also overdue stay queued (true FIFO, one airing-time group per run).
std::vector<DelayedAutoDownloadJob> takeNextDelayedAutoDownloadJobs(
    std::vector<DelayedAutoDownloadJob>& queue, std::int64_t now_secs);

std::int64_t soonestDelayedAutoDownloadDue(const std::vector<DelayedAutoDownloadJob>& queue);

}  // namespace taiga
