/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
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

#include "recognition.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <algorithm>
#include <anitomy.hpp>
#include <format>
#include <ranges>
#include <vector>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition_cache.hpp"
#include "track/recognition_normalize.hpp"

namespace track::recognition {

namespace {

void stripIgnoredSubstrings(std::string& s) {
  const QString raw =
      QString::fromStdString(taiga::settings.recognitionIgnoredSubstrings()).trimmed();
  if (raw.isEmpty()) return;
  QString qs = QString::fromStdString(s);
  static const QRegularExpression kSep{QStringLiteral("[\\n\\r,;]+")};
  const QStringList parts = raw.split(kSep, Qt::SkipEmptyParts);
  for (QString tok : parts) {
    tok = tok.trimmed();
    if (tok.isEmpty()) continue;
    qs.replace(tok, QString(), Qt::CaseInsensitive);
  }
  s = qs.toStdString();
}

}  // namespace

Episode parse(std::string_view input, const anitomy::Options options) {
  Episode episode;

  std::string work{input};
  stripIgnoredSubstrings(work);
  auto elements = anitomy::parse(work, options);
  episode.setElements(elements);

  return episode;
}

Episode parseFileInfo(const QFileInfo& info, const anitomy::Options options,
                      const bool use_parent_directory_title_hint) {
  const auto fileName = info.fileName().toStdString();

  Episode episode = track::recognition::parse(fileName, options);

  if (use_parent_directory_title_hint) {
    // Season 0 (S00Exx) marks specials/OVAs stored in their own named folder.
    // Prefer the parent directory name as the title in this case, because the
    // scene abbreviation in the filename (e.g. "Iseleve") typically refers to the
    // main series while the folder name matches the actual special AniList entry.
    const auto season_str = episode.element(anitomy::ElementKind::Season);
    const bool isSeason0 = !season_str.empty() &&
                           (season_str == "0" || season_str == "00");

    if (!episode.contains(anitomy::ElementKind::Title) || isSeason0) {
      auto dirName = info.dir().dirName().toStdString();
      stripIgnoredSubstrings(dirName);
      if (!dirName.empty()) {
        if (isSeason0) {
          // Override the scene abbreviation title with the folder name.
          episode.setElement(anitomy::ElementKind::Title, dirName);
        } else {
          episode.addElement(anitomy::ElementKind::Title, dirName);
        }
      }
    }
  }

  return episode;
}

int identify(Episode& episode) {
  cache()->init();

  const auto title = episode.element(anitomy::ElementKind::Title);
  const auto normalizedTitle = normalize(title);

  // Season number extracted by anitomy (e.g. "04" from "S04E01"; toInt() gives 4).
  const auto season_str = episode.element(anitomy::ElementKind::Season);
  const int season_num = season_str.empty() ? 0 : QString::fromStdString(season_str).toInt();

  std::vector<Cache::Data::Match> matches;

  // Primary lookup by title alone.
  if (const auto data = cache()->find(normalizedTitle)) {
    matches.append_range(data->matches | std::views::values | std::ranges::to<std::vector>());
  }

  // Season-aware secondary lookup: "Title Season N" normalises to the same key as
  // "Title Part N" / "Title Nth Season" etc., so season-specific DB entries are
  // preferred over an ambiguously-named base season.
  if (season_num > 1) {
    const auto titleWithSeason =
        normalize(std::format("{} season {}", title, season_num));
    if (const auto data = cache()->find(titleWithSeason)) {
      for (const auto& [id, match] : data->matches) {
        constexpr float kSeasonBoost = 0.6f;
        const float boosted = match.weight + kSeasonBoost;
        const auto it = std::ranges::find_if(
            matches, [id = id](const Cache::Data::Match& m) { return m.id == id; });
        if (it == matches.end()) {
          matches.push_back({.id = id, .weight = boosted});
        } else if (it->weight < boosted) {
          it->weight = boosted;
        }
      }
    }
  }

  std::ranges::sort(matches, std::ranges::greater{}, &Cache::Data::Match::weight);

  for (const auto& match : matches) {
    if (isValidMatch(match.id, episode)) return match.id;
  }

  return anime::kUnknownId;
}

bool isValidMatch(const int id, const Episode& episode) {
  const auto item = anime::db.item(id);

  if (!item) return false;

  const auto is_valid_episode_number = [&episode, &item]() {
    const auto number = episode.element(anitomy::ElementKind::Episode);

    if (number.empty()) {
      if (item->episode_count == 1)
        return true;  // single-episode anime can do without an episode number

      const auto extension = episode.element(anitomy::ElementKind::FileExtension);
      if (extension.empty()) return true;  // batch release
    }

    const int value = QString::fromStdString(number).toInt();
    if (value <= item->episode_count) return true;  // in range

    if (item->episode_count < 1) return true;  // episode count is unknown, so anything goes

    return false;  // out of range
  };

  if (!is_valid_episode_number()) return false;

  return true;
}

}  // namespace track::recognition
