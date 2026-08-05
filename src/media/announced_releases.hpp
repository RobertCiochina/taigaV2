/**
 * Taiga — candidates for “Announced releases” (sequel seasons linked from your list).
 */

#pragma once

#include <QSet>
#include <QString>
#include <QVector>

namespace anime {

struct AnnouncedReleaseCandidate {
  /// Upcoming / newly airing sequel on AniList.
  int anime_id = 0;
  /// A Completed or Planning title on your list with a Sequel edge to `anime_id`.
  int anchor_anime_id = 0;
};

/// Direct Sequel relations only; respects `dismissed` and list rows (hides when already
/// Planning / Watching / Completed). Requires local `Anime` rows with relations + status.
QVector<AnnouncedReleaseCandidate> computeAnnouncedReleaseCandidates(const QSet<int>& dismissed);

/// Count candidates that would be visible to the user after filtering by mature-content settings.
/// This matches the visibility logic used by the Home banner and navigation indicator.
int countVisibleAnnouncedReleaseCandidates(const QSet<int>& dismissed, bool show_mature);

/// For each Completed / Planning / Watching list row whose cached `Anime.relations` includes a
/// Sequel edge to an id not yet present in `anime::db`, enqueue a Media fetch (AniList only).
/// When `force` is true, bypasses the 24h redundant-fetch skip.
void prefetchMissingAnnouncedSequelMediaFromAnchors(bool force = false);

/// True when a Completed/Planning entry references a Sequel id not yet in `anime::db` (AniList).
bool hasAnnouncedSequelAnchorsAwaitingMediaFetch();

}  // namespace anime
