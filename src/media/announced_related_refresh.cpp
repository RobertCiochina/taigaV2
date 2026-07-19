/**
 * Taiga
 */

#include "media/announced_related_refresh.hpp"

#include <QDateTime>
#include <algorithm>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "media/announced_releases.hpp"

namespace anime {

namespace {

bool isAnchorStatus(const anime::list::Status s) {
  using anime::list::Status;
  return s == Status::Completed || s == Status::PlanToWatch || s == Status::Watching;
}

// The full anchor + sequel-frontier candidate set (no staleness filter), deduplicated.
// Shared by the refresh sweep and the schedule/countdown so both reason over the same titles.
QVector<int> collectAnnouncedRelatedCandidateIds() {
  QVector<int> out;
  QSet<int> seen;

  // 1) Anchor titles from the user's list (these are the roots that define what's "related to
  // you").
  for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
    const ListEntry& e = it.value();
    if (!isAnchorStatus(e.status)) continue;
    const int aid = e.anime_id;
    if (aid <= 0 || seen.contains(aid)) continue;
    seen.insert(aid);
    out.push_back(aid);
  }

  // 2) Sequel frontier: if an anchor already points at a sequel, new seasons will attach to the
  // end of that chain, not to the anchor again. Include sequel nodes too.
  for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
    const ListEntry& e = it.value();
    if (!isAnchorStatus(e.status)) continue;
    const Anime* a = anime::db.item(e.anime_id);
    if (!a) continue;
    for (const auto& rel : a->relations) {
      if (rel.type != RelationType::Sequel) continue;
      const int sid = rel.related_id;
      if (sid <= 0 || seen.contains(sid)) continue;
      seen.insert(sid);
      out.push_back(sid);
    }
  }

  return out;
}

}  // namespace

QVector<int> computeAnnouncedRelatedRefreshAnimeIds(const int max_count, const qint64 now_secs,
                                                    const qint64 stale_after_secs) {
  if (max_count <= 0) return {};

  struct Candidate {
    int id;
    qint64 last_modified;
  };

  QVector<Candidate> candidates;
  for (const int id : collectAnnouncedRelatedCandidateIds()) {
    const Anime* a = anime::db.item(id);
    if (!isStaleForAnnouncedRelatedRefresh(a, now_secs, stale_after_secs)) continue;
    candidates.push_back({id, a ? static_cast<qint64>(a->last_modified) : 0});
  }

  // Sort oldest-first so the least-recently-refreshed titles get priority when the cap is hit.
  // Items that have never been fetched (last_modified == 0) naturally sort first.
  std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
    return a.last_modified < b.last_modified;
  });

  const int n = std::min(max_count, static_cast<int>(candidates.size()));
  QVector<int> out;
  out.reserve(n);
  for (int i = 0; i < n; ++i) out.push_back(candidates[i].id);
  return out;
}

AnnouncedRelatedScanSchedule computeAnnouncedRelatedScanSchedule(const qint64 now_secs,
                                                                 const qint64 stale_after_secs) {
  AnnouncedRelatedScanSchedule schedule;
  const auto ids = collectAnnouncedRelatedCandidateIds();
  schedule.total_count = static_cast<int>(ids.size());

  for (const int id : ids) {
    const Anime* a = anime::db.item(id);
    if (isStaleForAnnouncedRelatedRefresh(a, now_secs, stale_after_secs)) {
      ++schedule.due_now_count;
      continue;
    }
    // Not stale ⇒ has a known cache and a real fetch timestamp; it becomes due 30 days after.
    const qint64 due = static_cast<qint64>(a->relations_fetched_at) + stale_after_secs;
    if (schedule.next_due_secs < 0 || due < schedule.next_due_secs) {
      schedule.next_due_secs = due;
    }
  }

  return schedule;
}

QSet<int> computeVisibleAnnouncedReleaseCandidateIds(const QSet<int>& dismissed,
                                                     const bool show_mature) {
  QSet<int> ids;
  const auto cands = computeAnnouncedReleaseCandidates(dismissed);
  for (const auto& c : cands) {
    const Anime* a = anime::db.item(c.anime_id);
    if (!a) continue;
    if (!show_mature && anime::isNsfw(*a)) continue;
    ids.insert(c.anime_id);
  }
  return ids;
}

}  // namespace anime
