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

#include "kitsu_parsers.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>

#include "base/chrono.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "sync/service.hpp"


namespace sync::kitsu {

anime::AgeRating parseAgeRating(const QString& value) {
  static const QMap<QString, anime::AgeRating> table{
      {"G", anime::AgeRating::G},
      {"PG", anime::AgeRating::PG},
      {"R", anime::AgeRating::R17},
      {"R18", anime::AgeRating::R18},
  };
  return table.value(value.toUpper(), anime::AgeRating::Unknown);
}

double parseScore(const QString& value) {
  return value.toDouble() / 10.0;
}

double fromScore(const double value) {
  return value * 10.0;
}

anime::Status parseStatus(const QString& value) {
  // clang-format off
  static const QMap<QString, anime::Status> table{
      {"current", anime::Status::Airing},
      {"finished", anime::Status::FinishedAiring},
      {"tba", anime::Status::NotYetAired},
      {"unreleased", anime::Status::NotYetAired},
      {"upcoming", anime::Status::NotYetAired},
  };
  // clang-format on
  return table.value(value, anime::Status::Unknown);
}

anime::Type parseType(const QString& value) {
  // clang-format off
  static const QMap<QString, anime::Type> table{
      {"TV", anime::Type::Tv},
      {"special", anime::Type::Special},
      {"OVA", anime::Type::Ova},
      {"ONA", anime::Type::Ona},
      {"movie", anime::Type::Movie},
      {"music", anime::Type::Music},
  };
  // clang-format on
  return table.value(value, anime::Type::Unknown);
}

QString parseListDate(const QString& value) {
  return value.size() >= 10 ? value.first(10) : QString{};
}

QString fromListDate(const QString& value) {
  return value + "T00:00:00.000Z";
}

std::time_t parseListLastUpdated(const QString& value) {
  return QDateTime::fromString(value, Qt::DateFormat::ISODate).toSecsSinceEpoch();
}

anime::list::Status parseListStatus(const QString& value) {
  // clang-format off
  static const QMap<QString, anime::list::Status> table{
      {"current", anime::list::Status::Watching},
      {"planned", anime::list::Status::PlanToWatch},
      {"completed", anime::list::Status::Completed},
      {"on_hold", anime::list::Status::OnHold},
      {"dropped", anime::list::Status::Dropped},
  };
  // clang-format on
  return table.value(value, anime::list::Status::NotInList);
}

QString fromListStatus(const anime::list::Status value) {
  // clang-format off
  switch (value) {
    case anime::list::Status::NotInList: return "";
    case anime::list::Status::Watching: return "current";
    case anime::list::Status::Completed: return "completed";
    case anime::list::Status::OnHold: return "on_hold";
    case anime::list::Status::Dropped: return "dropped";
    case anime::list::Status::PlanToWatch: return "planned";
  }
  // clang-format on
  return "";
}

std::optional<Anime> parseAnimeResource(const QJsonObject& data) {
  const int id = data["id"].toString().toInt();
  if (!id) return std::nullopt;

  const QJsonObject a = data["attributes"].toObject();
  Anime item{
      .id = id,
      .last_modified = QDateTime::currentSecsSinceEpoch(),
      .episode_count =
          a["episodeCount"].isNull() ? anime::kUnknownEpisodeCount : a["episodeCount"].toInt(),
      .episode_length =
          a["episodeLength"].isNull() ? anime::kUnknownEpisodeLength : a["episodeLength"].toInt(),
      .age_rating = parseAgeRating(a["ageRating"].toString()),
      .status = parseStatus(a["status"].toString()),
      .type = parseType(a["subtype"].toString()),
      .date_started = FuzzyDate(parseListDate(a["startDate"].toString()).toStdString()),
      .date_finished = FuzzyDate(parseListDate(a["endDate"].toString()).toStdString()),
      .score = static_cast<float>(parseScore(a["averageRating"].toString())),
      .popularity_rank = a["popularityRank"].toInt(),
      .image_url = a["posterImage"].toObject()["small"].toString().toStdString(),
      .slug = a["slug"].toString().toStdString(),
      .synopsis = a["synopsis"].toString().toStdString(),
      .trailer_id = a["youtubeVideoId"].toString().toStdString(),
      .titles{.romaji = a["canonicalTitle"].toString().toStdString()},
  };

  const QJsonObject titles = a["titles"].toObject();
  if (titles.contains("en_jp")) item.titles.romaji = titles["en_jp"].toString().toStdString();
  if (titles.contains("en")) item.titles.english = titles["en"].toString().toStdString();
  if (titles.contains("ja_jp")) item.titles.japanese = titles["ja_jp"].toString().toStdString();

  for (const QJsonValue& t : a["abbreviatedTitles"].toArray()) {
    if (!t.isString()) continue;
    const std::string s = t.toString().toStdString();
    if (!s.empty()) item.titles.synonyms.push_back(s);
  }

  return item;
}

std::optional<ListEntry> parseLibraryEntryResource(const QJsonObject& data) {
  const QString libIdStr = data["id"].toString();
  const qint64 libId = libIdStr.toLongLong();
  if (!libId) return std::nullopt;

  const int anime_id = data["relationships"]
                           .toObject()["anime"]
                           .toObject()["data"]
                           .toObject()["id"]
                           .toString()
                           .toInt();
  if (!anime_id) return std::nullopt;

  const QJsonObject attrs = data["attributes"].toObject();
  ListEntry e{};
  e.id = libId;
  e.anime_id = anime_id;
  e.watched_episodes = attrs["progress"].toInt();
  const int r20 = attrs["ratingTwenty"].toInt();
  e.score = (r20 > 0) ? (r20 * anime::list::kScoreMax / 20) : 0;
  e.status = parseListStatus(attrs["status"].toString());
  e.is_private = attrs["private"].toBool();
  e.rewatched_times = attrs["reconsumeCount"].toInt();
  e.rewatching = attrs["reconsuming"].toBool();
  e.date_started = FuzzyDate(parseListDate(attrs["startedAt"].toString()).toStdString());
  e.date_completed = FuzzyDate(parseListDate(attrs["finishedAt"].toString()).toStdString());
  e.last_updated = parseListLastUpdated(attrs["updatedAt"].toString());
  e.notes = attrs["notes"].toString().toStdString();
  return e;
}

}  // namespace sync::kitsu
