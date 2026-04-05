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

#include "anilist.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRestReply>
#include <ranges>

#include "base/file.hpp"
#include "base/log.hpp"
#include "base/string.hpp"
#include "media/anime_db.hpp"
#include "sync/anilist_parsers.hpp"
#include "sync/anilist_utils.hpp"
#include "taiga/accounts.hpp"
#include "taiga/user_feedback.hpp"

// AniList API documentation:
// https://docs.anilist.co/

namespace sync::anilist {

Service::Service() : sync::Service{} {
  api_.setBaseUrl(QUrl{"https://graphql.anilist.co"});

  if (const auto token = taiga::accounts.anilistToken(); !token.empty()) {
    api_.setBearerToken(QByteArray::fromStdString(token));
  }
}

Service* Service::instance() {
  static Service service;
  return &service;
}

////////////////////////////////////////////////////////////////////////////////

void Service::authenticateUser() {
  const QJsonDocument data{QJsonObject{
      {"query", gql("Viewer")},
  }};

  const auto callback = [this](QRestReply& reply) {
    if (isError(reply)) {
      handleError(reply);
      // @TODO: Set authenticated state and emit signal
      return;
    }

    const auto viewer = reply.readJson().and_then([](const QJsonDocument& json) {
      return std::make_optional(json["data"]["Viewer"].toObject());
    });

    if (!viewer) {
      handleError(reply, "Could not parse user object.");
      // @TODO: Set authenticated state and emit signal
      return;
    }

    taiga::accounts.setAnilistUsername((*viewer)["name"].toString().toStdString());
    // @TODO: Set rating system setting using viewer["mediaListOptions"]["scoreFormat"]

    // @TODO: Set authenticated state and emit signal
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

void Service::fetchAnime(const int id) {
  const QJsonDocument data{{
      {"query", gql("Media")},
      {"variables", QJsonObject{{"id", id}}},
  }};

  const auto callback = [this](QRestReply& reply) {
    if (isError(reply)) {
      handleError(reply);
      return;
    }

    const auto item = reply.readJson().and_then(
        [](const QJsonDocument& json) { return parseMedia(json["data"]["Media"]); });

    if (!item) {
      handleError(reply, "Could not parse media object.");
      return;
    }

    anime::db.updateItem(*item);
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

void Service::search(const QString& query) {
  const QJsonDocument data{{
      {"query", gql("MediaSearch")},
      {"variables", QJsonObject{{"query", query}}},
  }};

  const auto callback = [this](QRestReply& reply) {
    if (isError(reply)) {
      handleError(reply);
      return;
    }

    const auto items = reply.readJson().and_then([](const QJsonDocument& json) {
      const auto value = json["data"]["Page"]["media"];
      if (!value.isArray()) return std::optional<QList<std::optional<Anime>>>{};
      return std::make_optional(value.toArray() | std::views::transform(parseMedia) |
                                std::ranges::to<QList>());
    });

    if (!items) {
      handleError(reply, "Could not parse search results.");
      return;
    }

    for (const auto& item : *items) {
      if (item) anime::db.updateItem(*item);
    }
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

void Service::fetchSeasonBrowse(const anime::SeasonName seasonName, const int year,
                                ListFetchComplete on_complete) {
  const auto finish = [on_complete](const bool ok, QString msg) {
    if (on_complete) on_complete(ok, std::move(msg));
  };
  if (seasonName == anime::SeasonName::Unknown) {
    finish(false, QStringLiteral("Invalid season."));
    return;
  }
  if (year < 1940 || year > 2100) {
    finish(false, QStringLiteral("Invalid year."));
    return;
  }
  fetchSeasonMediaSearchPage(seasonName, year, 1, 0, std::move(finish));
}

void Service::fetchSeasonMediaSearchPage(const anime::SeasonName seasonName, const int year, const int page,
                                         const int items_so_far, ListFetchComplete on_complete) {
  const QJsonDocument data{QJsonObject{
      {"query", gql(QStringLiteral("MediaSearch"))},
      {"variables", QJsonObject{{QStringLiteral("season"), fromSeasonName(seasonName)},
                                {QStringLiteral("seasonYear"), year},
                                {QStringLiteral("page"), page}}},
  }};

  const auto callback = [this, seasonName, year, page, items_so_far,
                         on_complete = std::move(on_complete)](QRestReply& reply) mutable {
    if (isError(reply)) {
      const QString err =
          reply.errorString().isEmpty() ? QStringLiteral("Network error") : reply.errorString();
      if (on_complete) on_complete(false, err);
      return;
    }

    const auto doc = reply.readJson();
    if (!doc.has_value()) {
      if (on_complete) on_complete(false, QStringLiteral("Empty AniList response."));
      return;
    }

    const QJsonObject root = doc->object();
    if (const auto errors = root["errors"]; errors.isArray() && !errors.toArray().isEmpty()) {
      const auto msg = errors.toArray().first().toObject()["message"].toString();
      if (on_complete) on_complete(false, msg);
      return;
    }

    const QJsonObject pageObj = root["data"].toObject()["Page"].toObject();
    const QJsonArray media = pageObj["media"].toArray();
    int n = 0;
    for (const auto& m : media) {
      if (const auto item = parseMedia(m)) {
        anime::db.updateItem(*item);
        ++n;
      }
    }
    const int sum = items_so_far + n;
    const QJsonObject pageInfo = pageObj["pageInfo"].toObject();
    if (pageInfo["hasNextPage"].toBool()) {
      fetchSeasonMediaSearchPage(seasonName, year, page + 1, sum, std::move(on_complete));
      return;
    }
    if (on_complete) on_complete(true, QStringLiteral("%1 titles updated").arg(sum));
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

void Service::fetchListEntries(ListFetchComplete on_complete) {
  const auto finish = [on_complete](const bool ok, QString message) {
    if (on_complete) on_complete(ok, std::move(message));
  };

  const auto user = QString::fromStdString(taiga::accounts.anilistUsername());
  if (user.isEmpty()) {
    finish(false, QStringLiteral("AniList username is missing; sign in first."));
    return;
  }
  if (taiga::accounts.anilistToken().empty()) {
    finish(false, QStringLiteral("AniList access token is missing."));
    return;
  }

  const QJsonDocument data{QJsonObject{
      {"query", gql("MediaListCollection")},
      {"variables", QJsonObject{{"userName", user}}},
  }};

  const auto callback = [this, finish](QRestReply& reply) {
    if (isError(reply)) {
      handleError(reply);
      finish(false, reply.errorString().isEmpty() ? QStringLiteral("Network error") : reply.errorString());
      return;
    }

    const auto doc = reply.readJson();
    if (!doc.has_value()) {
      finish(false, QStringLiteral("Empty response."));
      return;
    }

    const auto root = doc->object();
    if (const auto errors = root["errors"]; errors.isArray() && !errors.toArray().isEmpty()) {
      const auto msg = errors.toArray().first().toObject()["message"].toString();
      handleError(reply, msg);
      finish(false, msg);
      return;
    }

    const auto collection = root["data"].toObject()["MediaListCollection"].toObject();
    const auto lists = collection["lists"].toArray();
    int count = 0;
    for (const auto& listVal : lists) {
      const auto entries = listVal.toObject()["entries"].toArray();
      for (const auto& entryVal : entries) {
        const auto entryObj = entryVal.toObject();
        if (entryObj.isEmpty()) continue;

        const auto media = entryObj["media"].toObject();
        if (!media.isEmpty()) {
          if (const auto item = parseMedia(QJsonValue(media))) {
            anime::db.updateItem(*item);
          }
        }

        if (const auto parsed = parseMediaListEntry(entryObj, 0)) {
          anime::db.updateEntry(*parsed);
          ++count;
        }
      }
    }

    finish(true, QStringLiteral("%1 entries updated").arg(count));
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

void Service::saveListEntry(const ListEntry& entry) {
  if (taiga::accounts.anilistToken().empty()) return;

  QJsonObject variables;
  if (entry.id > 0) {
    variables["id"] = static_cast<int>(entry.id);
  }
  variables["mediaId"] = entry.anime_id;
  variables["status"] = fromListStatus(entry.status);
  variables["scoreRaw"] = entry.score;
  variables["progress"] = entry.watched_episodes;
  variables["repeat"] = entry.rewatched_times;
  variables["private"] = entry.is_private;
  variables["notes"] = QString::fromStdString(entry.notes);

  if (static_cast<bool>(entry.date_started)) {
    variables["startedAt"] = fromFuzzyDate(entry.date_started);
  } else {
    variables["startedAt"] = QJsonValue{};
  }
  if (static_cast<bool>(entry.date_completed)) {
    variables["completedAt"] = fromFuzzyDate(entry.date_completed);
  } else {
    variables["completedAt"] = QJsonValue{};
  }

  const QJsonDocument data{QJsonObject{
      {"query", gql("SaveMediaListEntry")},
      {"variables", variables},
  }};

  const auto callback = [this, anime_id = entry.anime_id](QRestReply& reply) {
    if (isError(reply)) {
      handleError(reply);
      taiga::userFeedback(
          QStringLiteral("Could not update AniList: %1")
              .arg(reply.errorString().isEmpty() ? QStringLiteral("Unknown error") : reply.errorString()),
          true);
      return;
    }

    const auto doc = reply.readJson();
    if (!doc.has_value()) {
      handleError(reply, "Empty response.");
      taiga::userFeedback(QStringLiteral("Could not update AniList: empty response."), true);
      return;
    }

    const auto root = doc->object();
    if (const auto errors = root["errors"]; errors.isArray() && !errors.toArray().isEmpty()) {
      const auto msg = errors.toArray().first().toObject()["message"].toString();
      handleError(reply, msg);
      taiga::userFeedback(QStringLiteral("Could not update AniList: %1").arg(msg), true);
      return;
    }

    const auto saved = root["data"].toObject()["SaveMediaListEntry"].toObject();
    if (saved.isEmpty()) {
      handleError(reply, "SaveMediaListEntry returned no data.");
      taiga::userFeedback(QStringLiteral("Could not update AniList: no data returned."), true);
      return;
    }

    const auto parsed = parseMediaListEntry(saved, anime_id);
    if (!parsed) {
      handleError(reply, "Could not parse list entry.");
      taiga::userFeedback(QStringLiteral("Could not update AniList: invalid response."), true);
      return;
    }

    anime::db.updateEntry(*parsed);

    const auto media = saved["media"].toObject();
    if (!media.isEmpty()) {
      if (const auto item = parseMedia(QJsonValue(media))) {
        anime::db.updateItem(*item);
      }
    }
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

void Service::deleteListEntry(const int anime_id) {
  const auto listEntry = anime::db.entry(anime_id);

  if (!listEntry) return;

  if (taiga::accounts.anilistToken().empty() || listEntry->id <= 0) {
    anime::db.deleteEntry(anime_id);
    return;
  }

  const QJsonDocument data{{
      {"query", gql("DeleteMediaListEntry")},
      {"variables", QJsonObject{{"id", static_cast<int>(listEntry->id)}}},
  }};

  const auto callback = [this, anime_id](QRestReply& reply) {
    if (isError(reply) && reply.httpStatus() != 404) {
      handleError(reply);
      taiga::userFeedback(
          QStringLiteral("Could not remove from AniList: %1")
              .arg(reply.errorString().isEmpty() ? QStringLiteral("Unknown error") : reply.errorString()),
          true);
      return;
    }

    anime::db.deleteEntry(anime_id);
  };

  manager_.post(api_.createRequest(), data, this, callback);
}

////////////////////////////////////////////////////////////////////////////////

QString Service::gql(const QString& name) const {
  return base::readFile(u":/gql/anilist/%1.gql"_s.arg(name));
}

bool Service::isError(const QRestReply& reply) const {
  return !reply.isHttpStatusSuccess() || reply.hasError();
  // @TODO: Check DDoS protection
}

void Service::handleError(const QRestReply& reply, const QString& message) const {
  if (reply.hasError()) LOGE("{}", reply.errorString().toStdString());
  if (!message.isEmpty()) LOGE("{}", message.toStdString());
  // @TODO: Parse body for "errors" array
  // @TODO: Emit signal
}

}  // namespace sync::anilist
