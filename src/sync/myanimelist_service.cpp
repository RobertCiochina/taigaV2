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

#include "myanimelist_service.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRestReply>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "base/log.hpp"
#include "media/anime_db.hpp"
#include "media/anime_season.hpp"
#include "sync/myanimelist.hpp"
#include "sync/myanimelist_parsers.hpp"
#include "sync/myanimelist_utils.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/user_feedback.hpp"

// https://myanimelist.net/apiconfig/references/api/v2

namespace sync::myanimelist {

namespace {

std::optional<int> offsetFromPagingNext(const QJsonObject& root) {
  const QString next = root["paging"].toObject()["next"].toString();
  if (next.isEmpty()) return std::nullopt;
  const QUrl u(next);
  bool ok = false;
  const int o = QUrlQuery(u.query()).queryItemValue(QStringLiteral("offset")).toInt(&ok);
  if (!ok) return std::nullopt;
  return o;
}

QString animelistFieldsParameter() {
  static const QString kAnime =
      QStringLiteral("alternative_titles,average_episode_duration,end_date,genres,id,main_picture,mean,"
                     "media_type,num_episodes,popularity,rating,start_date,status,studios,synopsis,title");
  static const QString kList =
      QStringLiteral("comments,finish_date,is_rewatching,num_times_rewatched,num_episodes_watched,score,"
                     "start_date,status,updated_at");
  return kAnime + QStringLiteral(",list_status{") + kList + QStringLiteral("}");
}

QString malAnimeDetailFields() {
  return QStringLiteral("alternative_titles,average_episode_duration,end_date,genres,id,main_picture,mean,"
                        "media_type,num_episodes,popularity,rating,start_date,status,studios,synopsis,title");
}

QString malListStatusResponseFields() {
  return QStringLiteral("comments,finish_date,is_rewatching,num_times_rewatched,num_episodes_watched,score,"
                        "start_date,status,updated_at");
}

QString malSeasonSlug(const anime::SeasonName s) {
  switch (s) {
    case anime::SeasonName::Winter:
      return QStringLiteral("winter");
    case anime::SeasonName::Spring:
      return QStringLiteral("spring");
    case anime::SeasonName::Summer:
      return QStringLiteral("summer");
    case anime::SeasonName::Fall:
      return QStringLiteral("fall");
    case anime::SeasonName::Unknown:
      break;
  }
  return {};
}

}  // namespace

Service::Service() : sync::Service{} {
  api_.setBaseUrl(QUrl{QStringLiteral("https://api.myanimelist.net/v2/")});
}

Service* Service::instance() {
  static Service service;
  return &service;
}

void Service::fetchListEntries(ListFetchComplete on_complete) {
  const auto finish = [on_complete](const bool ok, QString message) {
    if (on_complete) on_complete(ok, std::move(message));
  };

  if (taiga::accounts.myanimelistAccessToken().empty()) {
    finish(false, QStringLiteral("MyAnimeList access token is missing; sign in first."));
    return;
  }

  api_.setBearerToken(QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));
  fetchListPage(0, 0, std::move(finish));
}

void Service::refreshAccessToken(std::function<void(bool ok, QString err)> done) {
  const std::string rt = taiga::accounts.myanimelistRefreshToken();
  if (rt.empty()) {
    if (done) done(false, QStringLiteral("MyAnimeList refresh token is missing."));
    return;
  }

  QUrlQuery form;
  form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
  form.addQueryItem(QStringLiteral("refresh_token"), QString::fromStdString(rt));
  form.addQueryItem(QStringLiteral("client_id"), QString::fromUtf8(kClientId));

  QUrl url(QStringLiteral("https://myanimelist.net/v1/oauth2/token"));
  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/x-www-form-urlencoded"));

  QNetworkReply* reply = taiga::network()->post(req, form.query(QUrl::FullyEncoded).toUtf8());
  connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)]() mutable {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      if (done) done(false, reply->errorString());
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const std::string access = root["access_token"].toString().toStdString();
    const std::string refresh = root["refresh_token"].toString().toStdString();
    if (access.empty()) {
      if (done) done(false, QStringLiteral("MyAnimeList token refresh failed."));
      return;
    }
    taiga::accounts.setMyanimelistAccessToken(access);
    if (!refresh.empty()) taiga::accounts.setMyanimelistRefreshToken(refresh);
    if (done) done(true, {});
  });
}

void Service::fetchListPage(const int offset, const int entries_so_far,
                            ListFetchComplete on_complete, const bool allow_token_refresh) {
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
  query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
  query.addQueryItem(QStringLiteral("nsfw"), QStringLiteral("true"));
  query.addQueryItem(QStringLiteral("fields"), animelistFieldsParameter());

  QUrl relative(QStringLiteral("users/@me/animelist"));
  relative.setQuery(query);

  api_.setBearerToken(QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));

  const auto callback = [this, offset, entries_so_far, allow_token_refresh,
                         on_complete = std::move(on_complete)](QRestReply& reply) mutable {
    if (isError(reply)) {
      if (allow_token_refresh && reply.httpStatus() == 401 &&
          !taiga::accounts.myanimelistRefreshToken().empty()) {
        refreshAccessToken([this, offset, entries_so_far, on_complete = std::move(on_complete)](
                               const bool ok, const QString& err) mutable {
          if (!ok) {
            if (on_complete) on_complete(false, err);
            return;
          }
          api_.setBearerToken(QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));
          fetchListPage(offset, entries_so_far, std::move(on_complete), false);
        });
        return;
      }
      const QString err = extractErrorMessage(reply);
      handleError(reply, err);
      if (on_complete) {
        on_complete(false, err.isEmpty() ? QStringLiteral("MyAnimeList request failed.") : err);
      }
      return;
    }

    const auto doc = reply.readJson();
    if (!doc.has_value()) {
      if (on_complete) on_complete(false, QStringLiteral("Empty MyAnimeList response."));
      return;
    }

    const QJsonObject root = doc->object();
    const QJsonArray data = root["data"].toArray();
    int page_count = 0;
    for (const QJsonValue& row : data) {
      const QJsonObject obj = row.toObject();
      const QJsonObject node = obj["node"].toObject();
      const QJsonObject list_status = obj["list_status"].toObject();
      if (node.isEmpty() || list_status.isEmpty()) continue;

      if (const auto item = parseAnimeNode(node)) {
        anime::db.updateItem(*item);
      }
      const int aid = node["id"].toInt();
      if (const auto entry = parseLibraryListStatus(list_status, aid)) {
        anime::db.updateEntry(*entry);
        ++page_count;
      }
    }

    const int total = entries_so_far + page_count;
    if (const auto next_off = offsetFromPagingNext(root)) {
      fetchListPage(*next_off, total, std::move(on_complete), allow_token_refresh);
      return;
    }

    if (on_complete) {
      on_complete(true, QStringLiteral("%1 entries updated").arg(total));
    }
  };

  manager_.get(api_.createRequest(relative.toString()), this, callback);
}

void Service::fetchSeasonBrowse(const anime::SeasonName season, const int year, ListFetchComplete on_complete) {
  const auto finish = [on_complete](const bool ok, QString msg) {
    if (on_complete) on_complete(ok, std::move(msg));
  };
  if (taiga::accounts.myanimelistAccessToken().empty()) {
    finish(false, QStringLiteral("MyAnimeList access token is missing."));
    return;
  }
  const QString slug = malSeasonSlug(season);
  if (slug.isEmpty() || year < 1940 || year > 2100) {
    finish(false, QStringLiteral("Invalid season or year."));
    return;
  }
  fetchSeasonPage(slug, year, 0, 0, std::move(finish), true);
}

void Service::fetchSeasonPage(const QString& season_slug, const int year, const int offset,
                              const int items_so_far, ListFetchComplete on_complete,
                              const bool allow_token_refresh) {
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("limit"), QStringLiteral("500"));
  query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
  query.addQueryItem(QStringLiteral("nsfw"), QStringLiteral("true"));
  query.addQueryItem(QStringLiteral("fields"), malAnimeDetailFields());

  QUrl relative(QStringLiteral("anime/season/%1/%2").arg(year).arg(season_slug));
  relative.setQuery(query);

  api_.setBearerToken(QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));

  const auto callback = [this, season_slug, year, offset, items_so_far, allow_token_refresh,
                         on_complete = std::move(on_complete)](QRestReply& reply) mutable {
    if (isError(reply)) {
      if (allow_token_refresh && reply.httpStatus() == 401 &&
          !taiga::accounts.myanimelistRefreshToken().empty()) {
        refreshAccessToken([this, season_slug, year, offset, items_so_far,
                            on_complete = std::move(on_complete)](const bool ok, const QString& err) mutable {
          if (!ok) {
            if (on_complete) on_complete(false, err);
            return;
          }
          api_.setBearerToken(QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));
          fetchSeasonPage(season_slug, year, offset, items_so_far, std::move(on_complete), false);
        });
        return;
      }
      const QString err = extractErrorMessage(reply);
      handleError(reply, err);
      if (on_complete) {
        on_complete(false, err.isEmpty() ? QStringLiteral("MyAnimeList season request failed.") : err);
      }
      return;
    }

    const auto doc = reply.readJson();
    if (!doc.has_value()) {
      if (on_complete) on_complete(false, QStringLiteral("Empty MyAnimeList response."));
      return;
    }

    const QJsonObject root = doc->object();
    const QJsonArray data = root["data"].toArray();
    int n = 0;
    for (const QJsonValue& row : data) {
      const QJsonObject node = row.toObject()["node"].toObject();
      if (node.isEmpty()) continue;
      if (const auto item = parseAnimeNode(node)) {
        anime::db.updateItem(*item);
        ++n;
      }
    }

    const int total = items_so_far + n;
    if (const auto next_off = offsetFromPagingNext(root)) {
      fetchSeasonPage(season_slug, year, *next_off, total, std::move(on_complete), allow_token_refresh);
      return;
    }

    if (on_complete) {
      on_complete(true, QStringLiteral("%1 titles updated").arg(total));
    }
  };

  manager_.get(api_.createRequest(relative.toString()), this, callback);
}

bool Service::isError(const QRestReply& reply) {
  return !reply.isHttpStatusSuccess() || reply.hasError();
}

QString Service::extractErrorMessage(QRestReply& reply) {
  if (reply.hasError()) return reply.errorString();

  const auto doc = reply.readJson();
  if (!doc.has_value()) return {};

  const QJsonObject o = doc->object();
  QString msg = o["message"].toString();
  if (msg.isEmpty()) {
    msg = o["error"].toString();
  }
  return msg;
}

void Service::handleError(const QRestReply& reply, const QString& message) const {
  if (reply.hasError()) LOGE("{}", reply.errorString().toStdString());
  if (!message.isEmpty()) LOGE("{}", message.toStdString());
}

void Service::fetchAnime(const int id) {
  if (taiga::accounts.myanimelistAccessToken().empty()) return;
  fetchAnimeImpl(id, true);
}

void Service::fetchAnimeImpl(const int id, const bool allow_token_refresh) {
  QUrl url(QStringLiteral("https://api.myanimelist.net/v2/anime/%1").arg(id));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("fields"), malAnimeDetailFields());
  url.setQuery(q);

  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") +
                                      QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));

  QNetworkReply* reply = taiga::network()->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, id, allow_token_refresh] {
    reply->deleteLater();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (allow_token_refresh && code == 401 &&
        !taiga::accounts.myanimelistRefreshToken().empty()) {
      refreshAccessToken([this, id](const bool ok, const QString& err) {
        if (!ok) {
          taiga::userFeedback(
              QStringLiteral("Could not refresh MyAnimeList login: %1").arg(err.isEmpty()
                                                                                  ? QStringLiteral("Unknown")
                                                                                  : err),
              true);
          return;
        }
        fetchAnimeImpl(id, false);
      });
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      taiga::userFeedback(
          QStringLiteral("Could not load anime from MyAnimeList: %1").arg(reply->errorString()), true);
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    if (const auto item = parseAnimeNode(root)) {
      anime::db.updateItem(*item);
    } else {
      taiga::userFeedback(QStringLiteral("Could not parse anime from MyAnimeList."), true);
    }
  });
}

void Service::saveListEntry(const ListEntry& entry) {
  if (taiga::accounts.myanimelistAccessToken().empty()) return;
  saveListEntryImpl(entry, true);
}

void Service::saveListEntryImpl(const ListEntry& entry, const bool allow_token_refresh) {
  QUrl url(QStringLiteral("https://api.myanimelist.net/v2/anime/%1/my_list_status").arg(entry.anime_id));
  QUrlQuery fq;
  fq.addQueryItem(QStringLiteral("fields"), malListStatusResponseFields());
  url.setQuery(fq);

  QUrlQuery form;
  form.addQueryItem(QStringLiteral("status"), fromListStatus(entry.status));
  form.addQueryItem(QStringLiteral("score"), QString::number(fromListScore(entry.score)));
  form.addQueryItem(QStringLiteral("num_watched_episodes"), QString::number(entry.watched_episodes));
  if (static_cast<bool>(entry.date_started)) {
    form.addQueryItem(QStringLiteral("start_date"),
                      QString::fromStdString(entry.date_started.to_string()));
  }
  if (static_cast<bool>(entry.date_completed)) {
    form.addQueryItem(QStringLiteral("finish_date"),
                      QString::fromStdString(entry.date_completed.to_string()));
  }
  form.addQueryItem(QStringLiteral("is_rewatching"),
                    entry.rewatching ? QStringLiteral("true") : QStringLiteral("false"));
  form.addQueryItem(QStringLiteral("num_times_rewatched"), QString::number(entry.rewatched_times));
  if (!entry.notes.empty()) {
    form.addQueryItem(QStringLiteral("comments"), QString::fromStdString(entry.notes));
  }

  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") +
                                      QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/x-www-form-urlencoded"));

  const QByteArray body = form.query(QUrl::FullyEncoded).toUtf8();
  QNetworkReply* reply = taiga::network()->sendCustomRequest(req, QByteArrayLiteral("PATCH"), body);
  connect(reply, &QNetworkReply::finished, this, [this, reply, entry, allow_token_refresh] {
    reply->deleteLater();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (allow_token_refresh && code == 401 &&
        !taiga::accounts.myanimelistRefreshToken().empty()) {
      refreshAccessToken([this, entry](const bool ok, const QString& err) {
        if (!ok) {
          taiga::userFeedback(
              QStringLiteral("Could not update MyAnimeList (login): %1").arg(err.isEmpty()
                                                                                 ? QStringLiteral("Unknown")
                                                                                 : err),
              true);
          return;
        }
        saveListEntryImpl(entry, false);
      });
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      taiga::userFeedback(
          QStringLiteral("Could not update MyAnimeList: %1").arg(reply->errorString()), true);
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    if (const auto parsed = parseLibraryListStatus(root, entry.anime_id)) {
      anime::db.updateEntry(*parsed);
    } else {
      const QString msg = root["message"].toString();
      taiga::userFeedback(
          QStringLiteral("Could not update MyAnimeList: %1")
              .arg(msg.isEmpty() ? QStringLiteral("Invalid response") : msg),
          true);
    }
  });
}

void Service::deleteListEntry(const int anime_id) {
  if (!anime::db.entry(anime_id)) return;
  if (taiga::accounts.myanimelistAccessToken().empty()) {
    anime::db.deleteEntry(anime_id);
    return;
  }
  deleteListEntryImpl(anime_id, true);
}

void Service::deleteListEntryImpl(const int anime_id, const bool allow_token_refresh) {
  QUrl url(QStringLiteral("https://api.myanimelist.net/v2/anime/%1/my_list_status").arg(anime_id));
  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") +
                                      QByteArray::fromStdString(taiga::accounts.myanimelistAccessToken()));

  QNetworkReply* reply = taiga::network()->sendCustomRequest(req, QByteArrayLiteral("DELETE"), QByteArray());
  connect(reply, &QNetworkReply::finished, this, [this, reply, anime_id, allow_token_refresh] {
    reply->deleteLater();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (allow_token_refresh && code == 401 &&
        !taiga::accounts.myanimelistRefreshToken().empty()) {
      refreshAccessToken([this, anime_id](const bool ok, const QString& err) {
        if (!ok) {
          taiga::userFeedback(
              QStringLiteral("Could not remove from MyAnimeList (login): %1")
                  .arg(err.isEmpty() ? QStringLiteral("Unknown") : err),
              true);
          return;
        }
        deleteListEntryImpl(anime_id, false);
      });
      return;
    }
    if (reply->error() != QNetworkReply::NoError && code != 404) {
      taiga::userFeedback(
          QStringLiteral("Could not remove from MyAnimeList: %1").arg(reply->errorString()), true);
      return;
    }
    anime::db.deleteEntry(anime_id);
  });
}

}  // namespace sync::myanimelist
