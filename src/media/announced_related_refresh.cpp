/**
 * Taiga
 */

#include "media/announced_related_refresh.hpp"

#include <QDateTime>

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

bool isStaleOrMissingRelations(const Anime* a, const qint64 now_secs, const qint64 stale_after_secs) {
  if (!a) return true;
  if (a->relations.empty()) return true;
  if (stale_after_secs <= 0) return false;
  const qint64 age = now_secs - static_cast<qint64>(a->last_modified);
  return age > stale_after_secs;
}

}  // namespace

QVector<int> computeAnnouncedRelatedRefreshAnimeIds(const int max_count, const qint64 now_secs,
                                                    const qint64 stale_after_secs) {
  QVector<int> out;
  if (max_count <= 0) return out;

  QSet<int> seen;
  out.reserve(max_count);

  // 1) Anchor titles from the user's list (these are the roots that define what's "related to you").
  for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
    const ListEntry& e = it.value();
    if (!isAnchorStatus(e.status)) continue;

    const int aid = e.anime_id;
    if (aid <= 0 || seen.contains(aid)) continue;

    const Anime* a = anime::db.item(aid);
    if (!isStaleOrMissingRelations(a, now_secs, stale_after_secs)) continue;

    seen.insert(aid);
    out.push_back(aid);
    if (out.size() >= max_count) return out;
  }

  // 2) Sequel frontier: if an anchor already points at a sequel, new seasons will attach to the
  // end of that chain, not to the anchor again. Refresh sequel nodes too (minimal depth, capped).
  for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
    const ListEntry& e = it.value();
    if (!isAnchorStatus(e.status)) continue;

    const Anime* a = anime::db.item(e.anime_id);
    if (!a) continue;

    for (const auto& rel : a->relations) {
      if (rel.type != RelationType::Sequel) continue;
      const int sid = rel.related_id;
      if (sid <= 0 || seen.contains(sid)) continue;

      const Anime* s = anime::db.item(sid);
      if (!isStaleOrMissingRelations(s, now_secs, stale_after_secs)) continue;

      seen.insert(sid);
      out.push_back(sid);
      if (out.size() >= max_count) return out;
    }
  }

  return out;
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

