#include "season_browse_cache.hpp"

namespace taiga {

QString seasonBrowseCacheKey(const sync::ServiceId service, const int year,
                             const anime::SeasonName season) {
  return QString("%1:%2:%3")
      .arg(sync::serviceSlug(service))
      .arg(year)
      .arg(static_cast<int>(season));
}

bool seasonBrowseCacheContains(const QStringList& loaded_keys, const QString& key) {
  for (const QString& k : loaded_keys) {
    if (k == key) return true;
  }
  return false;
}

QStringList seasonBrowseCacheAdd(QStringList loaded_keys, const QString& key) {
  if (key.isEmpty()) return loaded_keys;
  if (seasonBrowseCacheContains(loaded_keys, key)) return loaded_keys;
  loaded_keys.append(key);
  return loaded_keys;
}

bool shouldFetchSeasonBrowse(const QStringList& loaded_keys, const QString& key,
                            const bool force_refresh) {
  if (force_refresh) return true;
  return !seasonBrowseCacheContains(loaded_keys, key);
}

}  // namespace taiga

