/**
 * Taiga — background refresh for "new seasons" / related anime detection.
 */

#pragma once

#include <QSet>
#include <QVector>

namespace anime {

struct Details;

/// True when an anchor/sequel node should be refreshed for announced-related detection.
bool isStaleForAnnouncedRelatedRefresh(const Details* item, qint64 now_secs,
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
