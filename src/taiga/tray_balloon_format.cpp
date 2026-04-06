/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "tray_balloon_format.hpp"

#include <QString>

#include <anitomy.hpp>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"

namespace taiga::tray_balloon {

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

}  // namespace

QString formatTemplate(QString tmpl, const track::Episode& episode, const anime::Details* anime) {
  tmpl.replace(QStringLiteral("\\n"), QStringLiteral("\n"), Qt::CaseInsensitive);

  const QString ep_raw = QString::fromStdString(episode.element(anitomy::ElementKind::Episode));
  const QString epn = ep_raw.isEmpty() ? QStringLiteral("?") : ep_raw;
  const QString name = QString::fromStdString(episode.element(anitomy::ElementKind::Title));
  const QString group = QString::fromStdString(episode.element(anitomy::ElementKind::ReleaseGroup));

  QString romaji;
  QString english;
  QString native;
  QString title_show;
  QString total;
  QString score;
  QString watched;
  QString image;
  QString animeurl;
  QString playstatus;
  QString season;

  if (anime) {
    romaji = QString::fromStdString(anime->titles.romaji);
    english = QString::fromStdString(anime->titles.english);
    native = QString::fromStdString(anime->titles.japanese);
    title_show =
        QString::fromStdString(anime::preferredListTitleString(*anime, taiga::settings.listTitleLanguage()));
    if (title_show.trimmed().isEmpty()) title_show = romaji.isEmpty() ? name : romaji;

    if (anime->episode_count > 0) total = QString::number(anime->episode_count);
    image = QString::fromStdString(anime->image_url);
    animeurl = sync::animePageUrl(anime->id);
    if (!anime->date_started.empty()) {
      season = QString::fromStdString(anime->date_started.to_string());
    }
    if (const anime::list::Entry* e = anime::db.entry(anime->id)) {
      score = e->score > 0 ? QString::number(e->score) : QString();
      watched = QString::number(e->watched_episodes);
      playstatus = listStatusLabel(e->status);
    }
  } else {
    title_show = name;
    romaji = name;
  }

  if (playstatus.isEmpty()) playstatus = QStringLiteral("unknown");

  const auto repl = [&](const QString& key, const QString& val) {
    tmpl.replace(key, val, Qt::CaseInsensitive);
  };

  repl(QStringLiteral("%title%"), title_show);
  repl(QStringLiteral("%romaji%"), romaji);
  repl(QStringLiteral("%english%"), english);
  repl(QStringLiteral("%native%"), native);
  repl(QStringLiteral("%name%"), name);
  repl(QStringLiteral("%episode%"), epn);
  repl(QStringLiteral("%total%"), total.isEmpty() ? QStringLiteral("?") : total);
  repl(QStringLiteral("%score%"), score);
  repl(QStringLiteral("%watched%"), watched.isEmpty() ? QStringLiteral("0") : watched);
  repl(QStringLiteral("%user%"), currentServiceUsername());
  repl(QStringLiteral("%playstatus%"), playstatus);
  repl(QStringLiteral("%image%"), image);
  repl(QStringLiteral("%animeurl%"), animeurl);
  repl(QStringLiteral("%group%"), group);
  repl(QStringLiteral("%season%"), season);
  return tmpl;
}

}  // namespace taiga::tray_balloon
