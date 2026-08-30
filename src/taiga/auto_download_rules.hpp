#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace anime {
struct Details;
}

namespace taiga {

inline constexpr std::int64_t kReleaseEventJustAiredWindowSeconds = 5 * 60;
inline constexpr std::int64_t kDelayedAutoDownloadMetadataReuseSeconds = 10 * 60;

/// Minimum spacing between delayed sync+auto-download cycles. Titles airing minutes apart would
/// otherwise each get their own sync+scan+RSS cycle; jobs that come due inside the gap wait for it.
inline constexpr std::int64_t kDelayedAutoDownloadMinCycleGapSeconds = 20 * 60;

/// Jobs older than this are dropped when restoring the persisted queue (stale after a long
/// shutdown; the airing is either long since downloaded or no longer worth chasing).
inline constexpr std::int64_t kDelayedAutoDownloadMaxRestoreAgeSeconds = 24 * 60 * 60;

/// Best-effort upper bound for "episodes that have aired" for auto-download.
/// Conservative by design: avoids guessing that future episodes aired when schedule metadata is
/// missing.
int computeLastAiredEpisodeForAutoDownload(const anime::Details& item, int watched_episodes,
                                           std::int64_t now_secs);

bool isJustAiredRelease(std::int64_t now_secs, std::int64_t air_at_secs,
                        std::int64_t window_secs = kReleaseEventJustAiredWindowSeconds);

/// Value to store as the next remembered air time.
/// If the service jumps to a later future episode, keep the previous timestamp until it is past.
std::int64_t rememberNextEpisodeTime(std::int64_t now_secs, std::int64_t current_next_episode_time,
                                     std::int64_t previous_next_episode_time);

/// Air timestamp to enqueue.
/// `last_poll_secs` is the previous release-check time (0 on first poll); used to catch airings
/// that crossed from future to past while the app was asleep.
std::optional<std::int64_t> detectJustAiredAt(
    std::int64_t now_secs, std::int64_t current_next_episode_time,
    std::int64_t previous_next_episode_time, std::int64_t last_poll_secs = 0,
    std::int64_t window_secs = kReleaseEventJustAiredWindowSeconds);

/// Episode number that just aired, from metadata at detection time.
/// When the service already advanced `next_episode_time`, `last_aired_episode` is usually the
/// episode that aired — do not add one.
int inferJustAiredEpisode(int last_aired_episode, int watched_episodes,
                          bool schedule_already_advanced = false);

std::int64_t delayedAutoDownloadDueAt(std::int64_t air_at_secs, std::int64_t delay_secs);

/// Season pack is only for catching up a whole cour with nothing on disk yet.
bool allowSeasonPackAutoDownload(int local_ep_count, int missing_count, int effective_last,
                                 int episode_count);

/// last_aired used for a delayed run: never below the episode recorded when the airing was seen.
int lastAiredForDelayedAutoDownload(int computed_last_aired, int recorded_aired_episode);

/// FIFO of delayed RSS jobs, ordered by due time (air + delay). Same due time stays together.
struct DelayedAutoDownloadJob {
  int anime_id = 0;
  std::int64_t due_at_secs = 0;
  int aired_episode = 0;
};

/// Soonest due time for this anime in the delayed FIFO, if any.
std::optional<std::int64_t> pendingDelayedAutoDownloadDueAt(
    const std::vector<DelayedAutoDownloadJob>& queue, int anime_id);

void insertDelayedAutoDownloadJob(std::vector<DelayedAutoDownloadJob>& queue,
                                  DelayedAutoDownloadJob job);

/// Pops the front job if it is due, plus any following jobs with the same due time.
/// Later jobs that are also overdue stay queued (true FIFO, one airing-time group per run).
std::vector<DelayedAutoDownloadJob> takeNextDelayedAutoDownloadJobs(
    std::vector<DelayedAutoDownloadJob>& queue, std::int64_t now_secs);

std::vector<DelayedAutoDownloadJob> peekNextDelayedAutoDownloadJobs(
    const std::vector<DelayedAutoDownloadJob>& queue, std::int64_t now_secs);

/// Pops every job with `due_at_secs <= now` (catch-up when several staggered dues have elapsed).
/// Safe with respect to the release delay: a job is only ever returned after its own due time.
std::vector<DelayedAutoDownloadJob> takeDueDelayedAutoDownloadJobs(
    std::vector<DelayedAutoDownloadJob>& queue, std::int64_t now_secs);

/// Same selection as `takeDueDelayedAutoDownloadJobs` without removing anything, so a caller can
/// check what a cycle would cover before committing to run it.
std::vector<DelayedAutoDownloadJob> peekDueDelayedAutoDownloadJobs(
    const std::vector<DelayedAutoDownloadJob>& queue, std::int64_t now_secs);

std::int64_t soonestDelayedAutoDownloadDue(const std::vector<DelayedAutoDownloadJob>& queue);

/// When the next sync+auto-download cycle may start: the soonest due time, pushed out so cycles
/// stay at least `gap_secs` apart. Returns 0 when the queue is empty.
/// `last_cycle_start_secs` is 0 when no cycle has run yet, which imposes no gap.
std::int64_t nextDelayedAutoDownloadCycleAt(
    const std::vector<DelayedAutoDownloadJob>& queue, std::int64_t last_cycle_start_secs,
    std::int64_t gap_secs = kDelayedAutoDownloadMinCycleGapSeconds);

}  // namespace taiga
