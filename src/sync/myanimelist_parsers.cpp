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

#include "myanimelist_parsers.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

#include "base/chrono.hpp"
#include "base/string.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"

namespace sync::myanimelist {

anime::AgeRating parseAgeRating(const QString& value) {
  using anime::AgeRating;
  // clang-format off
  static const QMap<QString, AgeRating> table{
      {"g", AgeRating::G},
      {"pg", AgeRating::PG},
      {"pg_13", AgeRating::PG13},
      {"r", AgeRating::R17},
      {"r+", AgeRating::R17},
      {"rx", AgeRating::R18},
  };
  // clang-format on
  return table.value(value.toLower(), AgeRating::Unknown);
}

FuzzyDate parseFuzzyDate(const QString& value) {
  // YYYY-MM-DD
  if (value.size() >= 10) return FuzzyDate(value.toStdString());
  // YYYY-MM
  if (value.size() == 7) return FuzzyDate(u"%1-00"_s.arg(value).toStdString());
  // YYYY
  if (value.size() == 4) return FuzzyDate(u"%1-00-00"_s.arg(value).toStdString());
  return FuzzyDate{};
}

int parseEpisodeLength(int value) {
  const auto seconds = std::chrono::seconds{value};
  return std::chrono::duration_cast<std::chrono::minutes>(seconds).count();
}

anime::Status parseStatus(const QString& value) {
  using anime::Status;
  static const QMap<QString, Status> table{
      {"currently_airing", Status::Airing},
      {"finished_airing", Status::FinishedAiring},
      {"not_yet_aired", Status::NotYetAired},
  };
  return table.value(value.toLower(), Status::Unknown);
}

anime::Type parseType(const QString& value) {
  using anime::Type;
  // clang-format off
  static const QMap<QString, Type> table{
    {"unknown", Type::Unknown},
    {"tv", Type::Tv},
    {"ova", Type::Ova},
    {"movie", Type::Movie},
    {"special", Type::Special},
    {"ona", Type::Ona},
    {"music", Type::Music},
    {"cm", Type::Special},
    {"pv", Type::Special},
    {"tv_special", Type::Special},
  };
  // clang-format on
  return table.value(value.toLower(), Type::Unknown);
}

std::time_t parseListLastUpdated(const QString& value) {
  return QDateTime::fromString(value, Qt::DateFormat::ISODate).toSecsSinceEpoch();
}

int parseListScore(int value) {
  return value * (anime::list::kScoreMax / 10);
}

anime::list::Status parseListStatus(const QString& value) {
  using anime::list::Status;
  static const QMap<QString, Status> table{
      {"watching", Status::Watching},
      {"completed", Status::Completed},
      {"on_hold", Status::OnHold},
      {"dropped", Status::Dropped},
      {"plan_to_watch", Status::PlanToWatch},
  };
  return table.value(value.toLower(), Status::NotInList);
}

namespace {

void appendNameList(std::vector<std::string>& out, const QJsonArray& arr) {
  for (const auto& v : arr) {
    const auto name = v.toObject()["name"].toString().toStdString();
    if (!name.empty()) out.push_back(name);
  }
}

}  // namespace

std::optional<Anime> parseAnimeNode(const QJsonObject& json) {
  const int id = json["id"].toInt();
  if (!id) return std::nullopt;

  Anime item{
      .id = id,
      .last_modified = QDateTime::currentSecsSinceEpoch(),
      .episode_count = json["num_episodes"].toInt(anime::kUnknownEpisodeCount),
      .episode_length = parseEpisodeLength(json["average_episode_duration"].toInt()),
      .age_rating = parseAgeRating(json["rating"].toString()),
      .status = parseStatus(json["status"].toString()),
      .type = parseType(json["media_type"].toString()),
      .date_started = parseFuzzyDate(json["start_date"].toString()),
      .date_finished = parseFuzzyDate(json["end_date"].toString()),
      .score = static_cast<float>(json["mean"].toDouble()),
      .popularity_rank = json["popularity"].toInt(),
      .synopsis = json["synopsis"].toString().toStdString(),
      .titles{
          .romaji = json["title"].toString().toStdString(),
      },
  };

  if (const auto pic = json["main_picture"].toObject(); !pic.isEmpty()) {
    item.image_url = pic["medium"].toString().toStdString();
    if (item.image_url.empty()) {
      item.image_url = pic["large"].toString().toStdString();
    }
  }

  if (const auto alt = json["alternative_titles"].toObject(); !alt.isEmpty()) {
    item.titles.english = alt["en"].toString().toStdString();
    item.titles.japanese = alt["ja"].toString().toStdString();
    for (const auto& syn : alt["synonyms"].toArray()) {
      const auto s = syn.toString().toStdString();
      if (!s.empty()) item.titles.synonyms.push_back(s);
    }
  }

  appendNameList(item.genres, json["genres"].toArray());
  appendNameList(item.studios, json["studios"].toArray());

  return item;
}

std::optional<ListEntry> parseLibraryListStatus(const QJsonObject& json, const int anime_id) {
  if (!anime_id) return std::nullopt;

  ListEntry e{};
  e.id = anime::list::kUnknownId;
  e.anime_id = anime_id;
  e.status = parseListStatus(json["status"].toString());
  e.score = parseListScore(json["score"].toInt());
  e.watched_episodes = json["num_episodes_watched"].toInt();
  if (e.watched_episodes == 0) {
    e.watched_episodes = json["num_watched_episodes"].toInt();
  }
  e.rewatching = json["is_rewatching"].toBool();
  e.rewatched_times = json["num_times_rewatched"].toInt();
  e.date_started = parseFuzzyDate(json["start_date"].toString());
  e.date_completed = parseFuzzyDate(json["finish_date"].toString());
  e.last_updated = parseListLastUpdated(json["updated_at"].toString());
  e.notes = json["comments"].toString().toStdString();

  return e;
}

}  // namespace sync::myanimelist
