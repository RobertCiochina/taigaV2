/**
 * Taiga — background refresh for "new seasons" / related anime detection.
 */

#pragma once

#include <QSet>
#include <QVector>

namespace anime {

struct Details;

/// Per-title relations cache lifetime for announced-related detection (30 days).
/// Shared by the background sweep and the UI countdown so both agree on the cadence.
inline constexpr qint64 kAnnouncedRelatedStaleAfterSecs = 30LL * 24 * 60 * 60;

/// True when an anchor/sequel node should be refreshed for announced-related detection.
bool isStaleForAnnouncedRelatedRefresh(const Details* item, qint64 now_secs,
                                       qint64 stale_after_secs);

/// DB-derived view of when the announced-related sweep has work to do. Computed from each title's
/// own `relations_fetched_at`, so the countdown reflects real cache ages rather than a flat 30
/// days.
struct AnnouncedRelatedScanSchedule {
  int total_count = 0;    ///< Anchor + sequel-frontier titles being tracked.
  int due_now_count = 0;  ///< Titles already stale (unknown cache or age > stale_after).
  qint64 next_due_secs =
      -1;  ///< Epoch secs when the next not-yet-due title becomes stale; -1 if none.
};

/// Computes the scan schedule from current DB state (does not fetch anything).
AnnouncedRelatedScanSchedule computeAnnouncedRelatedScanSchedule(qint64 now_secs,
                                                                 qint64 stale_after_secs);

/// Picks a small set of anime ids to refresh from AniList so sequel relations stay current.
/// Strategy: include list anchors (Watching/Planning/Completed) with unknown or aged-out relation
/// cache, and sequel frontier nodes with the same rules,
/// and also include their sequel "frontier" nodes (depth-limited by max_count).
QVector<int> computeAnnouncedRelatedRefreshAnimeIds(int max_count, qint64 now_secs,
                                                    qint64 stale_after_secs);

/// Returns ids of candidates that would be visible in Announced releases (ignores text filter),
/// respecting `dismissed` and mature-content settings.
QSet<int> computeVisibleAnnouncedReleaseCandidateIds(const QSet<int>& dismissed, bool show_mature);

}  // namespace anime
