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

class QRestReply;

namespace sync::myanimelist {

using ListFetchComplete = std::function<void(bool ok, QString message)>;

class Service final : public sync::Service {
public:
  Service();

  static Service* instance();

  void fetchListEntries(ListFetchComplete on_complete = {});

  void fetchSeasonBrowse(anime::SeasonName season, int year, ListFetchComplete on_complete = {});

  void fetchAnime(int id);
  void saveListEntry(const ListEntry& entry);
  void deleteListEntry(int anime_id);

private:
  void fetchListPage(int offset, int entries_so_far, ListFetchComplete on_complete,
                     bool allow_token_refresh = true);
  void fetchSeasonPage(const QString& season_slug, int year, int offset, int items_so_far,
                       ListFetchComplete on_complete, bool allow_token_refresh = true);
  void refreshAccessToken(std::function<void(bool ok, QString err)> done);

  void saveListEntryImpl(const ListEntry& entry, bool allow_token_refresh);
  void deleteListEntryImpl(int anime_id, bool allow_token_refresh);
  void fetchAnimeImpl(int id, bool allow_token_refresh);

  static bool isError(const QRestReply& reply);
  static QString extractErrorMessage(QRestReply& reply);
  void handleError(const QRestReply& reply, const QString& message = {}) const;
};

}  // namespace sync::myanimelist
