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

#include "anilist_parsers.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include "base/chrono.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "media/anime_season.hpp"

namespace sync::anilist {

FuzzyDate parseFuzzyDate(const QJsonValue& json) {
  if (json.isNull() || !json.isObject()) return FuzzyDate{};
  const auto o = json.toObject();
  if (o["year"].isNull() && o["month"].isNull() && o["day"].isNull()) return FuzzyDate{};
  return FuzzyDate{
      std::chrono::year{o["year"].toInt()},
      std::chrono::month{o["month"].toVariant().toUInt()},
      std::chrono::day{o["day"].toVariant().toUInt()},
  };
}

anime::list::Status parseListStatus(const QString& value) {
  using anime::list::Status;
  // clang-format off
  const QMap<QString, Status> table{
      {"CURRENT",   Status::Watching},
      {"PLANNING",  Status::PlanToWatch},
      {"COMPLETED", Status::Completed},
      {"DROPPED",   Status::Dropped},
      {"PAUSED",    Status::OnHold},
  };
  // clang-format on
  return table.value(value, Status::NotInList);
}

float parseScore(int value) {
  return static_cast<float>(value) / 10.0f;
}

anime::Status parseStatus(const QString& value) {
  using anime::Status;
  // clang-format off
  static const QMap<QString, Status> table{
      {"FINISHED",         Status::FinishedAiring},
      {"RELEASING",        Status::Airing},
      {"NOT_YET_RELEASED", Status::NotYetAired},
      {"CANCELLED",        Status::NotYetAired},
  };
  // clang-format on
  return table.value(value, Status::Unknown);
}

anime::Type parseType(const QString& value) {
  using anime::Type;
  // clang-format off
  static const QMap<QString, Type> table{
      {"TV",       Type::Tv},
      {"TV_SHORT", Type::Tv},
      {"MOVIE",    Type::Movie},
      {"SPECIAL",  Type::Special},
      {"OVA",      Type::Ova},
      {"ONA",      Type::Ona},
      {"MUSIC",    Type::Music},
  };
  // clang-format on
  return table.value(value, Type::Unknown);
}

////////////////////////////////////////////////////////////////////////////////

std::optional<Anime> parseMedia(const QJsonValue& json) {
  const int id = json["id"].toInt();

  if (!id) return std::nullopt;

  Anime item{
      .id = id,
      .last_modified = QDateTime::currentSecsSinceEpoch(),
      .episode_count = json["episodes"].toInt(),
      .episode_length = json["duration"].toInt(),
      .status = parseStatus(json["status"].toString()),
      .type = parseType(json["format"].toString()),
      .date_started = parseFuzzyDate(json["startDate"]),
      .date_finished = parseFuzzyDate(json["endDate"]),
      .score = parseScore(json["averageScore"].toInt()),
      .popularity_rank = json["popularity"].toInt(),
      .image_url = json["coverImage"]["extraLarge"].toString().toStdString(),
      .synopsis = json["description"].toString().toStdString(),
      .titles{
          .romaji = json["title"]["romaji"].toString().toStdString(),
          .english = json["title"]["english"].toString().toStdString(),
      },
  };

  const auto nativeTitle = json["title"]["native"].toString().toStdString();
  if (!nativeTitle.empty()) {
    if (json["countryOfOrigin"] == "JP") {
      item.titles.japanese = nativeTitle;
    } else {
      item.titles.synonyms.emplace_back(nativeTitle);
    }
  }

  if (json["trailer"]["site"] == "youtube") {
    item.trailer_id = json["trailer"]["id"].toString().toStdString();
  }

  for (const auto value : json["genres"].toArray()) {
    auto genre = value.toString().toStdString();
    if (!genre.empty()) item.genres.emplace_back(genre);
  }

  for (const auto value : json["synonyms"].toArray()) {
    auto synonym = value.toString().toStdString();
    if (!synonym.empty()) item.titles.synonyms.emplace_back(synonym);
  }

  for (const auto value : json["tags"].toArray()) {
    const auto tag = value.toObject();
    if (tag["isMediaSpoiler"].toBool()) continue;
    auto name = tag["name"].toString().toStdString();
    if (!name.empty()) item.tags.emplace_back(name);
  }

  for (const auto value : json["studios"]["edges"].toArray()) {
    const auto edge = value.toObject();
    if (edge["isMain"].toBool()) {
      item.studios.push_back(edge["node"]["name"].toString().toStdString());
    } else {
      item.producers.push_back(edge["node"]["name"].toString().toStdString());
    }
  }

  const QJsonObject next_ep = json["nextAiringEpisode"].toObject();
  if (!next_ep.isEmpty()) {
    const int next_num = next_ep["episode"].toInt();
    if (next_num > 0) {
      item.last_aired_episode = next_num - 1;
    }
    item.next_episode_time =
        static_cast<std::time_t>(next_ep["airingAt"].toVariant().toLongLong());
  } else {
    item.next_episode_time = 0;
  }

  return item;
}

std::optional<ListEntry> parseMediaListEntry(const QJsonObject& json, const int media_id_fallback) {
  if (json.isEmpty()) return std::nullopt;

  ListEntry e{};
  e.id = json["id"].toVariant().toLongLong();
  e.watched_episodes = json["progress"].toInt();
  e.score = json["score"].toInt();
  e.status = parseListStatus(json["status"].toString());
  e.is_private = json["private"].toBool();
  e.rewatched_times = json["repeat"].toInt();
  e.notes = json["notes"].toString().toStdString();
  e.date_started = parseFuzzyDate(json["startedAt"]);
  e.date_completed = parseFuzzyDate(json["completedAt"]);
  e.last_updated = static_cast<std::time_t>(json["updatedAt"].toInt());

  const auto media = json["media"].toObject();
  e.anime_id = media["id"].toInt();
  if (e.anime_id == 0 && media_id_fallback != 0) {
    e.anime_id = media_id_fallback;
  }

  return e;
}

}  // namespace sync::anilist
