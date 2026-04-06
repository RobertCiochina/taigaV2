/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "http_announce.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <anitomy.hpp>

#include "media/anime_db.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"

namespace taiga::http_announce {

namespace {

QString listStatusLabel(const anime::list::Status st) {
  switch (st) {
    case anime::list::Status::Watching:
      return QStringLiteral("watching");
    case anime::list::Status::Completed:
      return QStringLiteral("completed");
    case anime::list::Status::OnHold:
      return QStringLiteral("on_hold");
    case anime::list::Status::Dropped:
      return QStringLiteral("dropped");
    case anime::list::Status::PlanToWatch:
      return QStringLiteral("plan_to_watch");
    case anime::list::Status::NotInList:
    default:
      return QStringLiteral("unknown");
  }
}

QString currentServiceUsername() {
  switch (sync::currentServiceId()) {
    case sync::ServiceId::AniList:
      return QString::fromStdString(taiga::accounts.anilistUsername());
    case sync::ServiceId::MyAnimeList:
      return QString::fromStdString(taiga::accounts.myanimelistUsername());
    case sync::ServiceId::Kitsu: {
      const QString u = QString::fromStdString(taiga::accounts.kitsuUsername());
      if (!u.isEmpty()) return u;
      return QString::fromStdString(taiga::accounts.kitsuEmail());
    }
    case sync::ServiceId::Unknown:
    default:
      return {};
  }
}

QString buildBody(const track::Episode& episode, const anime::Details& anime) {
  QString tmpl = QString::fromStdString(taiga::settings.announceHttpBodyFormat());
  const QString ep =
      QString::fromStdString(episode.element(anitomy::ElementKind::Episode, std::string{"?"}));
  const QString title_parsed =
      QString::fromStdString(episode.element(anitomy::ElementKind::Title));
  const QString romaji = QString::fromStdString(anime.titles.romaji);
  const QString total =
      anime.episode_count > 0 ? QString::number(anime.episode_count) : QStringLiteral("?");
  QString score = QStringLiteral("0");
  QString playstatus = QStringLiteral("unknown");
  if (const anime::list::Entry* e = anime::db.entry(anime.id)) {
    if (e->is_private) return {};
    score = QString::number(e->score);
    playstatus = listStatusLabel(e->status);
  }

  auto enc = [](const QString& s) { return QString::fromUtf8(QUrl::toPercentEncoding(s)); };

  tmpl.replace(QStringLiteral("%title%"), enc(romaji.isEmpty() ? title_parsed : romaji),
               Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%name%"), enc(title_parsed), Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%episode%"), enc(ep), Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%total%"), enc(total), Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%score%"), enc(score), Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%user%"), enc(currentServiceUsername()), Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%playstatus%"), enc(playstatus), Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%image%"), enc(QString::fromStdString(anime.image_url)),
               Qt::CaseInsensitive);
  tmpl.replace(QStringLiteral("%animeurl%"), enc(sync::animePageUrl(anime.id)), Qt::CaseInsensitive);
  return tmpl;
}

QString g_last_http_sig;

}  // namespace

void postRecognizedEpisodeIfConfigured(const track::Episode& episode) {
  if (!taiga::settings.sharingEnabled()) return;
  if (!taiga::settings.announceHttpEnabled()) return;
  const QString url_s = QString::fromStdString(taiga::settings.announceHttpUrl()).trimmed();
  if (url_s.isEmpty()) return;
  const int aid = episode.animeId();
  if (aid <= 0) return;
  const auto* anime = anime::db.item(aid);
  if (!anime) return;

  const QString body = buildBody(episode, *anime);
  if (body.isEmpty()) return;

  const QString sig =
      QStringLiteral("%1|%2|%3").arg(aid).arg(
          QString::fromStdString(episode.element(anitomy::ElementKind::Episode)), body);
  if (sig == g_last_http_sig) return;
  g_last_http_sig = sig;

  QUrl url{url_s};
  if (!url.isValid() || url.scheme().isEmpty()) return;

  QNetworkRequest req{url};
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
  taiga::applyCommonHeaders(req);
  taiga::network()->post(req, body.toUtf8());
}

}  // namespace taiga::http_announce
