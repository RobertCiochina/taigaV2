/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
 */

#pragma once

#include <QString>
#include <string>

namespace anime::list {

struct MalXmlImportResult {
  int updated = 0;
  /// Anime ID from XML not present in the local database (e.g. AniList-keyed library).
  int skipped_unknown_anime = 0;
  /// Row missing required fields or invalid list status.
  int skipped_invalid_row = 0;
  QString error;

  [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// Merge MyAnimeList export XML into the local list for anime rows whose **local** `anime.id`
/// equals `series_animedb_id` (typical when the active catalog uses MAL media IDs).
MalXmlImportResult importFromMyAnimeListXml(const std::string& path);

}  // namespace anime::list
