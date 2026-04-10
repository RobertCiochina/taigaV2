#pragma once

#include <QString>
#include <QStringList>

#include "media/anime_season.hpp"
#include "sync/service.hpp"

namespace taiga {

/// Stable key for "season browse" catalogs cached in the local DB.
/// Format: "<service-slug>:<year>:<season-int>" (e.g. "anilist:2026:1").
QString seasonBrowseCacheKey(sync::ServiceId service, int year, anime::SeasonName season);

bool seasonBrowseCacheContains(const QStringList& loaded_keys, const QString& key);
QStringList seasonBrowseCacheAdd(QStringList loaded_keys, const QString& key);

/// Returns true if the app should perform a network fetch for this key.
bool shouldFetchSeasonBrowse(const QStringList& loaded_keys, const QString& key, bool force_refresh);

}  // namespace taiga

