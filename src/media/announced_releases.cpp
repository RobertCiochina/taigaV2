/**
 * Taiga
 */

#include "media/announced_releases.hpp"

#include <QSet>
#include <algorithm>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_utils.hpp"
#include "sync/service.hpp"

namespace anime {
namespace {

qint64 startDateKey(const Anime* a) {
  if (!a || a->date_started.empty()) return 0;
  return (static_cast<qint64>(static_cast<int>(a->date_started.year())) * 10000) +
         (static_cast<qint64>(static_cast<unsigned>(a->date_started.month())) * 100) +
         static_cast<qint64>(static_cast<unsigned>(a->date_started.day()));
}

bool sequelQualifiesForAnnouncedTab(const Anime* s) {
  if (!s) return false;
  return s->status == Status::NotYetAired || s->status == Status::Airing;
}

bool listEntryHidesFromAnnouncedTab(const ListEntry* e) {
  if (!e) return false;
  using anime::list::Status;
  return e->status == Status::PlanToWatch || e->status == Status::Watching ||
         e->status == Status::Completed;
}

QSet<int> missingSequelIdsFromAnnouncedAnchors() {
  QSet<int> missing;
  if (sync::currentServiceId() != sync::ServiceId::AniList) return missing;

  for (const auto& e : anime::db.entries()) {
    if (e.status != anime::list::Status::Completed &&
        e.status != anime::list::Status::PlanToWatch && e.status != anime::list::Status::Watching) {
      continue;
    }
    const Anime* a = anime::db.item(e.anime_id);
    if (!a) continue;
    for (const auto& rel : a->relations) {
      if (rel.type != RelationType::Sequel) continue;
      const int sid = rel.related_id;
      if (sid <= 0 || anime::db.item(sid)) continue;
      missing.insert(sid);
    }
  }
  return missing;
}

}  // namespace

QVector<AnnouncedReleaseCandidate> computeAnnouncedReleaseCandidates(const QSet<int>& dismissed) {
  QVector<AnnouncedReleaseCandidate> out;
  if (sync::currentServiceId() != sync::ServiceId::AniList) return out;

  QSet<int> seen;
  for (const auto& e : anime::db.entries()) {
    if (e.status != anime::list::Status::Completed &&
        e.status != anime::list::Status::PlanToWatch && e.status != anime::list::Status::Watching) {
      continue;
    }
    const Anime* a = anime::db.item(e.anime_id);
    if (!a) continue;
    for (const auto& rel : a->relations) {
      if (rel.type != RelationType::Sequel) continue;
      const int sid = rel.related_id;
      if (sid <= 0 || dismissed.contains(sid) || seen.contains(sid)) continue;
      const Anime* s = anime::db.item(sid);
      if (!s) continue;
      if (!sequelQualifiesForAnnouncedTab(s)) continue;
      if (const ListEntry* se = anime::db.entry(sid)) {
        if (listEntryHidesFromAnnouncedTab(se)) continue;
      }
      seen.insert(sid);
      out.push_back({sid, e.anime_id});
    }
  }

  std::sort(out.begin(), out.end(),
            [](const AnnouncedReleaseCandidate& x, const AnnouncedReleaseCandidate& y) {
              const Anime* ax = anime::db.item(x.anime_id);
              const Anime* ay = anime::db.item(y.anime_id);
              const qint64 kx = startDateKey(ax);
              const qint64 ky = startDateKey(ay);
              if (kx != ky) return kx < ky;
              return x.anime_id < y.anime_id;
            });

  return out;
}

int countVisibleAnnouncedReleaseCandidates(const QSet<int>& dismissed, const bool show_mature) {
  const auto cands = computeAnnouncedReleaseCandidates(dismissed);
  int visible = 0;
  for (const auto& c : cands) {
    const Anime* a = anime::db.item(c.anime_id);
    if (!a) continue;
    if (!show_mature && anime::isNsfw(*a)) continue;
    ++visible;
  }
  return visible;
}

bool hasAnnouncedSequelAnchorsAwaitingMediaFetch() {
  return !missingSequelIdsFromAnnouncedAnchors().isEmpty();
}

void prefetchMissingAnnouncedSequelMediaFromAnchors(const bool force) {
  for (const int sid : missingSequelIdsFromAnnouncedAnchors()) {
    if (force)
      sync::fetchAnimeForced(sid);
    else
      sync::fetchAnime(sid);
  }
}

}  // namespace anime
