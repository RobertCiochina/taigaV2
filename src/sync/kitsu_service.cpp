#include "kitsu_service.hpp"

#include <algorithm>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "base/log.hpp"
#include "kitsu_parsers.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "media/anime_season.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/user_feedback.hpp"

namespace sync::kitsu {

namespace {

constexpr auto kBase = "https://kitsu.app/api";
constexpr auto kJsonApi = "application/vnd.api+json";
constexpr auto kClientId =
    "dd031b32d2f56c990b1425efe6c42ad847e7fe3ab46bf1299f05ecd856bdb7dd";
constexpr auto kClientSecret =
    "54d7307928f63414defd96399fc31ba847961ceaecef3a5fd93144e960c0e151";
constexpr int kLibraryLimit = 500;

void setBearerJsonApi(QNetworkRequest& req, const QByteArray& bearer) {
  req.setRawHeader("Accept", kJsonApi);
  req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + bearer);
}

QString passwordPlain() {
  const QByteArray raw = QString::fromStdString(taiga::accounts.kitsuPassword()).toUtf8();
  const QByteArray dec = QByteArray::fromBase64(raw);
  if (dec.isEmpty() && !raw.isEmpty()) return QString::fromUtf8(raw);
  return QString::fromUtf8(dec);
}

std::optional<int> nextPageOffset(const QJsonObject& root) {
  const QString next = root["links"].toObject()["next"].toString();
  if (next.isEmpty()) return std::nullopt;
  const QUrl u(next);
  bool ok = false;
  const int o = QUrlQuery(u.query()).queryItemValue(QStringLiteral("page[offset]")).toInt(&ok);
  if (!ok) return std::nullopt;
  return o;
}

QString kitsuErrorMessage(const QJsonObject& root) {
  if (root.contains("error_description")) return root["error_description"].toString();
  const QJsonArray errs = root["errors"].toArray();
  if (errs.isEmpty()) return {};
  const QJsonObject e = errs.first().toObject();
  return QStringLiteral("%1: %2").arg(e["title"].toString(), e["detail"].toString());
}

QString kitsuSeasonFilter(const anime::SeasonName n) {
  switch (n) {
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

Service::Service(QObject* parent) : QObject(parent) {}

Service* Service::instance() {
  static Service s(qApp);
  return &s;
}

void Service::fetchListEntries(ListFetchComplete on_complete) {
  token_.clear();
  user_id_.clear();
  // Wrap the completion so all library pages share one SQLite transaction.
  anime::db.beginBatch();
  const auto batch_finish = [on_complete = std::move(on_complete)](const bool ok, QString msg) {
    anime::db.endBatch();
    if (on_complete) on_complete(ok, std::move(msg));
  };
  authenticate(std::move(batch_finish), true);
}

void Service::authenticate(ListFetchComplete then, const bool continue_with_library) {
  QString user = QString::fromStdString(taiga::accounts.kitsuEmail());
  if (user.isEmpty()) user = QString::fromStdString(taiga::accounts.kitsuUsername());
  if (user.isEmpty() || passwordPlain().isEmpty()) {
    if (then) then(false, QStringLiteral("Kitsu email/username or password is missing."));
    return;
  }

  QUrlQuery form;
  form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("password"));
  form.addQueryItem(QStringLiteral("username"), user);
  form.addQueryItem(QStringLiteral("password"), passwordPlain());
  form.addQueryItem(QStringLiteral("client_id"), QString::fromUtf8(kClientId));
  form.addQueryItem(QStringLiteral("client_secret"), QString::fromUtf8(kClientSecret));

  QUrl url(QStringLiteral("%1/oauth/token").arg(QLatin1String(kBase)));
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/x-www-form-urlencoded"));

  QNetworkReply* reply =
      taiga::network()->post(req, form.query(QUrl::FullyEncoded).toUtf8());
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, then = std::move(then), continue_with_library]() mutable {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
              if (then) then(false, reply->errorString());
              return;
            }
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const QJsonObject root = doc.object();
            token_ = root["access_token"].toString();
            if (token_.isEmpty()) {
              if (then) then(false, kitsuErrorMessage(root).isEmpty()
                                          ? QStringLiteral("Kitsu auth failed.")
                                          : kitsuErrorMessage(root));
              return;
            }
            fetchUserId(std::move(then), continue_with_library);
          });
}

void Service::fetchUserId(ListFetchComplete then, const bool continue_with_library) {
  QUrl url(QStringLiteral("%1/edge/users").arg(QLatin1String(kBase)));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("fields[users]"), QStringLiteral("slug"));
  q.addQueryItem(QStringLiteral("filter[self]"), QStringLiteral("true"));
  url.setQuery(q);

  QNetworkRequest req(url);
  setBearerJsonApi(req, token_.toUtf8());

  QNetworkReply* reply = taiga::network()->get(req);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, then = std::move(then), continue_with_library]() mutable {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
              if (then) then(false, reply->errorString());
              return;
            }
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonArray data = root["data"].toArray();
            if (data.isEmpty()) {
              if (then) then(false, QStringLiteral("Kitsu user not found."));
              return;
            }
            user_id_ = data.first().toObject()["id"].toString();
            if (user_id_.isEmpty()) {
              if (then) then(false, QStringLiteral("Invalid Kitsu user id."));
              return;
            }
            if (continue_with_library) {
              fetchLibraryPage(0, 0, std::move(then));
            } else if (then) {
              then(true, {});
            }
          });
}

void Service::fetchLibraryPage(const int offset, const int total, ListFetchComplete done) {
  QUrl url(QStringLiteral("%1/edge/library-entries").arg(QLatin1String(kBase)));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("filter[user_id]"), user_id_);
  q.addQueryItem(QStringLiteral("filter[kind]"), QStringLiteral("anime"));
  q.addQueryItem(QStringLiteral("include"), QStringLiteral("anime"));
  q.addQueryItem(QStringLiteral("page[offset]"), QString::number(offset));
  q.addQueryItem(QStringLiteral("page[limit]"), QString::number(kLibraryLimit));
  q.addQueryItem(
      QStringLiteral("fields[anime]"),
      QStringLiteral("abbreviatedTitles,ageRating,averageRating,canonicalTitle,endDate,episodeCount,"
                     "episodeLength,popularityRank,posterImage,slug,startDate,status,subtype,synopsis,"
                     "titles,youtubeVideoId"));
  q.addQueryItem(QStringLiteral("fields[libraryEntries]"),
                 QStringLiteral("finishedAt,notes,private,progress,ratingTwenty,reconsumeCount,"
                                "reconsuming,startedAt,status,updatedAt"));
  url.setQuery(q);

  QNetworkRequest req(url);
  setBearerJsonApi(req, token_.toUtf8());

  QNetworkReply* reply = taiga::network()->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, offset, total,
                                                  done = std::move(done)]() mutable {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      if (done) done(false, reply->errorString());
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    for (const auto& inc : root["included"].toArray()) {
      const QJsonObject o = inc.toObject();
      if (o["type"].toString() == QLatin1String("anime")) {
        if (const auto a = parseAnimeResource(o)) anime::db.updateItem(*a);
      }
    }
    int n = 0;
    for (const auto& row : root["data"].toArray()) {
      if (const auto e = parseLibraryEntryResource(row.toObject())) {
        anime::db.updateEntry(*e);
        ++n;
      }
    }
    const int sum = total + n;
    if (const auto next = nextPageOffset(root)) {
      fetchLibraryPage(*next, sum, std::move(done));
      return;
    }
    if (done) done(true, QStringLiteral("%1 entries updated").arg(sum));
  });
}

void Service::ensureSession(ListFetchComplete ready) {
  if (!token_.isEmpty() && !user_id_.isEmpty()) {
    if (ready) ready(true, {});
    return;
  }
  if (!token_.isEmpty()) {
    fetchUserId(std::move(ready), false);
    return;
  }
  authenticate(std::move(ready), false);
}

namespace {

QJsonObject kitsuListAttributes(const ListEntry& entry) {
  QJsonObject a;
  a["status"] = fromListStatus(entry.status);
  a["progress"] = entry.watched_episodes;
  if (entry.score > 0) {
    const int r20 = std::clamp((entry.score * 20 + anime::list::kScoreMax / 2) / anime::list::kScoreMax, 2,
                               20);
    a["ratingTwenty"] = r20;
  } else {
    a["ratingTwenty"] = QJsonValue::Null;
  }
  a["reconsuming"] = entry.rewatching;
  a["reconsumeCount"] = entry.rewatched_times;
  a["private"] = entry.is_private;
  a["notes"] = QString::fromStdString(entry.notes);
  if (static_cast<bool>(entry.date_started)) {
    a["startedAt"] = fromListDate(parseListDate(
        QString::fromStdString(entry.date_started.to_string()).left(10)));
  } else {
    a["startedAt"] = QJsonValue::Null;
  }
  if (static_cast<bool>(entry.date_completed)) {
    a["finishedAt"] = fromListDate(parseListDate(
        QString::fromStdString(entry.date_completed.to_string()).left(10)));
  } else {
    a["finishedAt"] = QJsonValue::Null;
  }
  return a;
}

}  // namespace

void Service::saveListEntry(const ListEntry& entry) {
  ensureSession([this, entry](const bool ok, const QString& err) mutable {
    if (!ok) {
      LOGE("{}", err.toStdString());
      taiga::userFeedback(
          QStringLiteral("Could not update Kitsu (session): %1").arg(err.isEmpty() ? QStringLiteral("Unknown")
                                                                                   : err),
          true);
      return;
    }

    const bool create = entry.id <= 0;
    QJsonObject data;
    data["type"] = QStringLiteral("libraryEntries");
    data["attributes"] = kitsuListAttributes(entry);
    if (create) {
      const QJsonObject anime_data{{QStringLiteral("type"), QStringLiteral("anime")},
                                   {QStringLiteral("id"), QString::number(entry.anime_id)}};
      const QJsonObject anime_rel{{QStringLiteral("data"), anime_data}};
      const QJsonObject user_data{{QStringLiteral("type"), QStringLiteral("users")},
                                  {QStringLiteral("id"), user_id_}};
      const QJsonObject user_rel{{QStringLiteral("data"), user_data}};
      data["relationships"] = QJsonObject{{QStringLiteral("anime"), anime_rel},
                                          {QStringLiteral("user"), user_rel}};
    } else {
      data["id"] = QString::number(entry.id);
    }

    QUrl url(create ? QStringLiteral("%1/edge/library-entries").arg(QLatin1String(kBase))
                    : QStringLiteral("%1/edge/library-entries/%2")
                          .arg(QLatin1String(kBase))
                          .arg(QString::number(entry.id)));
    QUrlQuery qp;
    qp.addQueryItem(QStringLiteral("include"), QStringLiteral("anime"));
    url.setQuery(qp);

    const QJsonDocument doc(QJsonObject{{QStringLiteral("data"), data}});
    const QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    taiga::applyCommonHeaders(req);
    setBearerJsonApi(req, token_.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, kJsonApi);

    QNetworkReply* reply =
        create ? taiga::network()->post(req, payload)
               : taiga::network()->sendCustomRequest(req, QByteArrayLiteral("PATCH"), payload);
    connect(reply, &QNetworkReply::finished, this, [reply] {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        taiga::userFeedback(
            QStringLiteral("Could not update Kitsu: %1").arg(reply->errorString()), true);
        return;
      }
      const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
      for (const auto& inc : root["included"].toArray()) {
        const QJsonObject o = inc.toObject();
        if (o["type"].toString() == QLatin1String("anime")) {
          if (const auto a = parseAnimeResource(o)) anime::db.updateItem(*a);
        }
      }
      const QJsonObject d = root["data"].toObject();
      if (const auto e = parseLibraryEntryResource(d)) {
        anime::db.updateEntry(*e);
      } else {
        taiga::userFeedback(QStringLiteral("Could not update Kitsu: invalid response."), true);
      }
    });
  });
}

void Service::deleteListEntry(const int anime_id) {
  const ListEntry* row = anime::db.entry(anime_id);
  if (!row) return;
  if (row->id <= 0) {
    anime::db.deleteEntry(anime_id);
    return;
  }

  ensureSession([this, anime_id, lib_id = row->id](const bool ok, const QString& err) mutable {
    if (!ok) {
      LOGE("{}", err.toStdString());
      taiga::userFeedback(
          QStringLiteral("Could not remove from Kitsu (session): %1")
              .arg(err.isEmpty() ? QStringLiteral("Unknown") : err),
          true);
      return;
    }
    QUrl url(QStringLiteral("%1/edge/library-entries/%2")
                 .arg(QLatin1String(kBase))
                 .arg(QString::number(lib_id)));
    QNetworkRequest req(url);
    taiga::applyCommonHeaders(req);
    setBearerJsonApi(req, token_.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, kJsonApi);

    QNetworkReply* reply = taiga::network()->sendCustomRequest(req, QByteArrayLiteral("DELETE"), QByteArray());
    connect(reply, &QNetworkReply::finished, this, [reply, anime_id] {
      reply->deleteLater();
      const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (reply->error() != QNetworkReply::NoError && code != 404) {
        taiga::userFeedback(
            QStringLiteral("Could not remove from Kitsu: %1").arg(reply->errorString()), true);
        return;
      }
      anime::db.deleteEntry(anime_id);
    });
  });
}

void Service::fetchSeasonBrowse(const anime::SeasonName season, const int year, ListFetchComplete on_complete) {
  const QString f = kitsuSeasonFilter(season);
  if (f.isEmpty() || year < 1940 || year > 2100) {
    if (on_complete) on_complete(false, QStringLiteral("Invalid season or year."));
    return;
  }
  fetchSeasonPage(f, year, 0, 0, std::move(on_complete));
}

void Service::fetchSeasonPage(const QString& season_filter, const int year, const int offset,
                              const int items_so_far, ListFetchComplete done) {
  QUrl url(QStringLiteral("%1/edge/anime").arg(QLatin1String(kBase)));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("filter[season]"), season_filter);
  q.addQueryItem(QStringLiteral("filter[season_year]"), QString::number(year));
  q.addQueryItem(QStringLiteral("page[offset]"), QString::number(offset));
  q.addQueryItem(QStringLiteral("page[limit]"), QString::number(kLibraryLimit));
  q.addQueryItem(QStringLiteral("sort"), QStringLiteral("-user_count"));
  q.addQueryItem(
      QStringLiteral("fields[anime]"),
      QStringLiteral("abbreviatedTitles,ageRating,averageRating,canonicalTitle,endDate,episodeCount,"
                     "episodeLength,popularityRank,posterImage,slug,startDate,status,subtype,synopsis,"
                     "titles,youtubeVideoId"));
  url.setQuery(q);

  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  req.setRawHeader("Accept", kJsonApi);
  if (!token_.isEmpty()) setBearerJsonApi(req, token_.toUtf8());

  QNetworkReply* reply = taiga::network()->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, season_filter, year, items_so_far,
                                                  done = std::move(done)]() mutable {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      if (done) done(false, reply->errorString());
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    int n = 0;
    for (const auto& v : root["data"].toArray()) {
      if (const auto a = parseAnimeResource(v.toObject())) {
        anime::db.updateItem(*a);
        ++n;
      }
    }
    const int total = items_so_far + n;
    if (const auto next = nextPageOffset(root)) {
      fetchSeasonPage(season_filter, year, *next, total, std::move(done));
      return;
    }
    if (done) done(true, QStringLiteral("%1 titles updated").arg(total));
  });
}

void Service::fetchAnime(const int id) {
  if (const Anime* existing = anime::db.item(id)) {
    if (anime::shouldSkipRedundantMediaFetch(*existing)) return;
  }
  QUrl url(QStringLiteral("%1/edge/anime/%2").arg(QLatin1String(kBase)).arg(id));
  QUrlQuery q;
  q.addQueryItem(
      QStringLiteral("fields[anime]"),
      QStringLiteral("abbreviatedTitles,ageRating,averageRating,canonicalTitle,endDate,episodeCount,"
                     "episodeLength,popularityRank,posterImage,slug,startDate,status,subtype,synopsis,"
                     "titles,youtubeVideoId"));
  url.setQuery(q);

  QNetworkRequest req(url);
  req.setRawHeader("Accept", kJsonApi);
  taiga::applyCommonHeaders(req);
  if (!token_.isEmpty()) setBearerJsonApi(req, token_.toUtf8());

  QNetworkReply* reply = taiga::network()->get(req);
  connect(reply, &QNetworkReply::finished, this, [reply] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return;
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonObject d = root["data"].toObject();
    if (const auto a = parseAnimeResource(d)) anime::db.updateItem(*a);
  });
}

}  // namespace sync::kitsu
