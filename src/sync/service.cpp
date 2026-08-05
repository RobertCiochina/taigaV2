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

#include "service.hpp"

#include <QApplication>
#include <QMap>
#include <QTimer>
#include <functional>
#include <memory>
#include <unordered_map>

#include "media/anime_db.hpp"
#include "sync/anilist.hpp"
#include "sync/anilist_utils.hpp"
#include "sync/kitsu_service.hpp"
#include "sync/kitsu_utils.hpp"
#include "sync/myanimelist_service.hpp"
#include "sync/myanimelist_utils.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"


namespace {

struct PendingListSaveSlot {
  std::unique_ptr<QTimer> timer;
  ListEntry entry{};
};

std::unordered_map<int, PendingListSaveSlot> g_pending_list_saves;

void saveListEntryImmediately(const ListEntry& entry) {
  switch (sync::currentServiceId()) {
    case sync::ServiceId::MyAnimeList:
      sync::myanimelist::Service::instance()->saveListEntry(entry);
      break;
    case sync::ServiceId::Kitsu:
      sync::kitsu::Service::instance()->saveListEntry(entry);
      break;
    case sync::ServiceId::AniList:
      if (!taiga::accounts.anilistToken().empty()) {
        sync::anilist::Service::instance()->saveListEntry(entry);
      }
      break;
    case sync::ServiceId::Unknown:
      break;
  }
}

void onPendingListSaveTimeout(const int anime_id) {
  const auto it = g_pending_list_saves.find(anime_id);
  if (it == g_pending_list_saves.end()) return;
  const ListEntry copy = it->second.entry;
  g_pending_list_saves.erase(it);
  saveListEntryImmediately(copy);
}

}  // namespace

namespace sync {

Service::Service() : QObject{qApp}, manager_{taiga::network()} {
  api_.setCommonHeaders(taiga::NetworkAccessManager::commonHeaders());
}

ServiceId currentServiceId() {
  const auto slug = QString::fromStdString(taiga::settings.service());
  return serviceIdFromSlug(slug);
}

ServiceId serviceIdFromSlug(const QString& slug) {
  static const QMap<QString, ServiceId> services{
      {"myanimelist", ServiceId::MyAnimeList},
      {"kitsu", ServiceId::Kitsu},
      {"anilist", ServiceId::AniList},
  };
  return services.value(slug, ServiceId::Unknown);
}

QString serviceName(const ServiceId serviceId) {
  // clang-format off
  switch (serviceId) {
    case ServiceId::MyAnimeList: return "MyAnimeList";
    case ServiceId::Kitsu: return "Kitsu";
    case ServiceId::AniList: return "AniList";  
  }
  // clang-format on
  return "Taiga";
}

QString serviceSlug(const ServiceId serviceId) {
  // clang-format off
  switch (serviceId) {
    case ServiceId::MyAnimeList: return "myanimelist";
    case ServiceId::Kitsu: return "kitsu";
    case ServiceId::AniList: return "anilist";
  }
  // clang-format on
  return "taiga";
}

void fetchAnime(const int id) {
  switch (currentServiceId()) {
    case ServiceId::MyAnimeList:
      myanimelist::Service::instance()->fetchAnime(id);
      break;
    case ServiceId::Kitsu:
      kitsu::Service::instance()->fetchAnime(id);
      break;
    case ServiceId::AniList:
      anilist::Service::instance()->fetchAnime(id, false);
      break;
    case ServiceId::Unknown:
      break;
  }
}

void fetchAnimeForced(const int id) {
  switch (currentServiceId()) {
    case ServiceId::MyAnimeList:
      myanimelist::Service::instance()->fetchAnime(id);
      break;
    case ServiceId::Kitsu:
      kitsu::Service::instance()->fetchAnime(id);
      break;
    case ServiceId::AniList:
      anilist::Service::instance()->fetchAnime(id, true);
      break;
    case ServiceId::Unknown:
      break;
  }
}

void saveListEntry(const ListEntry& entry) {
  const int delay_s = taiga::settings.syncListUpdateDelaySeconds();
  if (delay_s <= 0) {
    g_pending_list_saves.erase(entry.anime_id);
    saveListEntryImmediately(entry);
    return;
  }

  auto& slot = g_pending_list_saves[entry.anime_id];
  if (!slot.timer) {
    slot.timer = std::make_unique<QTimer>();
    slot.timer->setSingleShot(true);
    const int aid = entry.anime_id;
    QObject::connect(slot.timer.get(), &QTimer::timeout, qApp,
                     [aid] { onPendingListSaveTimeout(aid); });
  }
  slot.entry = entry;
  slot.timer->stop();
  slot.timer->start(delay_s * 1000);
}

void flushPendingListSaves() {
  std::unordered_map<int, PendingListSaveSlot> pending;
  pending.swap(g_pending_list_saves);
  for (auto& [id, slot] : pending) {
    (void)id;
    if (slot.timer) slot.timer->stop();
    saveListEntryImmediately(slot.entry);
  }
}

void deleteListEntry(const int anime_id) {
  g_pending_list_saves.erase(anime_id);

  switch (currentServiceId()) {
    case ServiceId::MyAnimeList:
      myanimelist::Service::instance()->deleteListEntry(anime_id);
      break;
    case ServiceId::Kitsu:
      kitsu::Service::instance()->deleteListEntry(anime_id);
      break;
    case ServiceId::AniList:
      anilist::Service::instance()->deleteListEntry(anime_id);
      break;
    case ServiceId::Unknown:
      anime::db.deleteEntry(anime_id);
      break;
  }
}

bool remoteListAccessConfigured() {
  switch (currentServiceId()) {
    case ServiceId::AniList:
      return !taiga::accounts.anilistUsername().empty() && !taiga::accounts.anilistToken().empty();
    case ServiceId::MyAnimeList:
      return !taiga::accounts.myanimelistAccessToken().empty();
    case ServiceId::Kitsu: {
      const bool has_user =
          !taiga::accounts.kitsuEmail().empty() || !taiga::accounts.kitsuUsername().empty();
      return has_user && !taiga::accounts.kitsuPassword().empty();
    }
    case ServiceId::Unknown:
      return false;
  }
  return false;
}

void fetchSeasonBrowse(const anime::SeasonName season, const int year,
                       std::function<void(bool ok, QString message)> on_complete) {
  switch (currentServiceId()) {
    case ServiceId::MyAnimeList:
      myanimelist::Service::instance()->fetchSeasonBrowse(season, year, std::move(on_complete));
      break;
    case ServiceId::Kitsu:
      kitsu::Service::instance()->fetchSeasonBrowse(season, year, std::move(on_complete));
      break;
    case ServiceId::AniList:
      anilist::Service::instance()->fetchSeasonBrowse(season, year, std::move(on_complete));
      break;
    case ServiceId::Unknown:
      if (on_complete) {
        on_complete(false, QStringLiteral("No sync service is configured."));
      }
      break;
  }
}

void fetchListEntries(std::function<void(bool ok, QString message)> on_complete) {
  switch (currentServiceId()) {
    case ServiceId::MyAnimeList:
      myanimelist::Service::instance()->fetchListEntries(std::move(on_complete));
      break;
    case ServiceId::Kitsu:
      kitsu::Service::instance()->fetchListEntries(std::move(on_complete));
      break;
    case ServiceId::AniList:
      anilist::Service::instance()->fetchListEntries(std::move(on_complete));
      break;
    case ServiceId::Unknown:
      if (on_complete) {
        on_complete(false, QStringLiteral("No sync service is configured."));
      }
      break;
  }
}

QString animePageUrl(const int id) {
  switch (currentServiceId()) {
    case ServiceId::MyAnimeList:
      return QString::fromStdString(myanimelist::animePageUrl(id));
    case ServiceId::Kitsu:
      return QString::fromStdString(kitsu::animePageUrl(id));
    case ServiceId::AniList:
      return QString::fromStdString(anilist::animePageUrl(id));
    case ServiceId::Unknown:
      break;
  }
  return {};
}

}  // namespace sync
