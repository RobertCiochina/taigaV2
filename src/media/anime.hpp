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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "base/chrono.hpp"

namespace anime {

enum class AgeRating {
  Unknown,
  G,
  PG,
  PG13,
  R17,
  R18,
};

enum class Status {
  Unknown,
  FinishedAiring,
  Airing,
  NotYetAired,
};

enum class Type {
  Unknown,
  Tv,
  Ova,
  Movie,
  Special,
  Ona,
  Music,
};

enum class TitleLanguage {
  Romaji,
  English,
  Native,
};

constexpr std::array<Status, 3> kStatuses{
    Status::FinishedAiring,
    Status::Airing,
    Status::NotYetAired,
};

constexpr std::array<Type, 6> kTypes{
    Type::Tv, Type::Ova, Type::Movie, Type::Special, Type::Ona, Type::Music,
};

constexpr int kMaxEpisodeCount = 1900;
constexpr int kUnknownEpisodeCount = -1;
constexpr int kUnknownEpisodeLength = -1;
constexpr int kUnknownId = 0;
constexpr double kUnknownScore = 0.0;

struct Titles {
  std::string romaji;
  std::string english;
  std::string japanese;
  std::vector<std::string> synonyms;
};

enum class RelationType {
  Unknown,
  Prequel,
  Sequel,
  Alternative,
  SideStory,
  Parent,
  SpinOff,
  Summary,
  Character,
  Other,
};

struct RelationEdge {
  int related_id = kUnknownId;  // AniList media id
  RelationType type = RelationType::Unknown;
};

/// Whether relation edges from a full media fetch are known (see `relations_json` in media.sqlite).
enum class RelationsCache : std::uint8_t {
  Unknown,     // NULL/empty column — list/search sync or never fetched with Media.relations
  KnownEmpty,  // Full fetch returned zero anime relation edges (`[]`)
  Cached,      // Populated edges from a full fetch (`[{...}]`)
};

struct Details {
  int id = kUnknownId;
  // std::map<sync::ServiceId, std::string> uids;
  // sync::ServiceId source = sync::ServiceId::Unknown;
  std::time_t last_modified = 0;
  int episode_count = kUnknownEpisodeCount;
  int episode_length = kUnknownEpisodeLength;
  AgeRating age_rating = AgeRating::Unknown;
  Status status = Status::Unknown;
  Type type = Type::Unknown;
  FuzzyDate date_started;
  FuzzyDate date_finished;
  float score = 0.0f;
  int popularity_rank = 0;
  std::string image_url;
  std::string slug;
  std::string synopsis;
  std::string trailer_id;
  Titles titles;
  std::vector<std::string> genres;
  std::vector<std::string> producers;
  std::vector<std::string> studios;
  std::vector<std::string> tags;
  int last_aired_episode = 0;
  std::time_t next_episode_time = 0;
  std::vector<RelationEdge> relations;
  RelationsCache relations_cache = RelationsCache::Unknown;
};

/// Primary string for list display / sorting for the given language (fallback to romaji).
inline std::string preferredListTitleString(const Details& a, const TitleLanguage lang) {
  switch (lang) {
    case TitleLanguage::English:
      if (!a.titles.english.empty()) return a.titles.english;
      return a.titles.romaji;
    case TitleLanguage::Native:
      if (!a.titles.japanese.empty()) return a.titles.japanese;
      return a.titles.romaji;
    case TitleLanguage::Romaji:
    default:
      return a.titles.romaji;
  }
}

}  // namespace anime

using Anime = anime::Details;
