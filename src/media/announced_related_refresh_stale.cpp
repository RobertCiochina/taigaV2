/**
 * Taiga — stale selection for announced-related refresh (isolated for unit tests).
 */

#include "media/anime.hpp"
#include "media/announced_related_refresh.hpp"

namespace anime {

bool isStaleForAnnouncedRelatedRefresh(const Anime* a, const qint64 now_secs,
                                       const qint64 stale_after_secs) {
  if (!a) return true;
  if (stale_after_secs <= 0) return false;

  // NULL/legacy column or list/search sync without Media.relations — need a full fetch.
  if (a->relations_cache == RelationsCache::Unknown) return true;

  // Age from the last real relations fetch, NOT `last_modified` (which routine list syncs
  // bump on every write). This keeps the 30-day cadence per title regardless of sync frequency.
  const qint64 age = now_secs - static_cast<qint64>(a->relations_fetched_at);
  return age > stale_after_secs;
}

}  // namespace anime
