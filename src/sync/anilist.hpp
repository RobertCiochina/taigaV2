/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <functional>

#include <QString>

#include "media/anime_list.hpp"
#include "media/anime_season.hpp"
#include "sync/service.hpp"

namespace sync::anilist {

using ListFetchComplete = std::function<void(bool ok, QString message)>;

class Service final : public sync::Service {
public:
  Service();

  static Service* instance();

  void authenticateUser();
  void fetchAnime(const int id);
  void search(const QString& query);
  void fetchSeasonBrowse(anime::SeasonName season, int year, ListFetchComplete on_complete = {});
  void fetchListEntries(ListFetchComplete on_complete = {});
  /// Saves list entry to AniList; updates local DB from the response (resolves temporary negative ids).
  void saveListEntry(const ListEntry& entry);
  /// Removes by anime id; deletes locally when offline, local-only entry, or non-AniList service.
  void deleteListEntry(int anime_id);

private:
  void fetchSeasonMediaSearchPage(anime::SeasonName season, int year, int page, int items_so_far,
                                  ListFetchComplete on_complete);

  QString gql(const QString& name) const;

  bool isError(const QRestReply& reply) const;
  void handleError(const QRestReply& reply, const QString& message = {}) const;
};

}  // namespace sync::anilist
