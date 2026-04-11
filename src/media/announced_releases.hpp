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

}  // namespace anime
