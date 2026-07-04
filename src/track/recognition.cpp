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
#include <QStringList>
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

/// True when the folder name looks like a release pack (codec/resolution/group) rather than the
/// anime library folder — in that case a grandparent directory is a better title hint for S00
/// files. Do not use path length alone: long official English titles are common and would
/// false-positive.
bool directoryLooksLikeTorrentReleasePack(const QString& name) {
  const QString lower = name.toLower();
  static const QStringList kMarkers{
      QStringLiteral("1080p"),      QStringLiteral("720p"),        QStringLiteral("480p"),
      QStringLiteral("2160p"),      QStringLiteral("1440p"),       QStringLiteral("576p"),
      QStringLiteral("webrip"),     QStringLiteral("web-dl"),      QStringLiteral("webdl"),
      QStringLiteral("bluray"),     QStringLiteral("bdrip"),       QStringLiteral("dvdrip"),
      QStringLiteral("hdtv"),       QStringLiteral("ntsc"),        QStringLiteral("pal"),
      QStringLiteral("x264"),       QStringLiteral("x265"),        QStringLiteral("hevc"),
      QStringLiteral("h.264"),      QStringLiteral("h264"),        QStringLiteral("av1"),
      QStringLiteral("dual audio"), QStringLiteral("multi-audio"), QStringLiteral("aac"),
      QStringLiteral("flac"),       QStringLiteral("opus"),        QStringLiteral("10bit"),
      QStringLiteral("8bit"),
  };
  for (const QString& m : kMarkers) {
    if (lower.contains(m)) return true;
  }
  return false;
}

/// Some filenames only get season 0 from a literal `S00E##` token; ensure Season is set so folder
/// title logic and `identify` treat them as specials.
void inferSeasonZeroFromFilename(Episode& episode, const std::string& fileName) {
  if (!episode.element(anitomy::ElementKind::Season).empty()) return;
  const QString qfn = QString::fromStdString(fileName);
  static const QRegularExpression kS00(QStringLiteral(R"(\bS00\s*E\d+)"),
                                       QRegularExpression::CaseInsensitiveOption);
  if (kS00.match(qfn).hasMatch()) {
    episode.setElement(anitomy::ElementKind::Season, "0");
  }
}

/// Anitomy can emit a spurious `Episode = 0` from audio/codec tags in dot-delimited release names
/// (e.g. the `.0` of `AAC2.0`, or `5.1`), because it only recognises bracketed numbers as years and
/// falls back to the last free number otherwise. A bare episode 0 is never a real episode here —
/// genuine specials carry an explicit `S00` season — and it blocks identification of movies and
/// single-file releases (`isValidMatch` rejects any episode value < 1). Drop it so such files fall
/// back to the "no episode number" path, which maps single-episode anime (movies) to episode 1.
void dropSpuriousZeroEpisode(Episode& episode) {
  const auto numbers = episode.allElements(anitomy::ElementKind::Episode);
  if (numbers.size() != 1) return;  // leave multi-episode ranges (e.g. `00-12`) untouched
  if (QString::fromStdString(numbers.front()).toInt() != 0) return;
  const auto season = episode.element(anitomy::ElementKind::Season);
  if (season == "0" || season == "00") return;  // genuine S00E00 special
  episode.removeElement(anitomy::ElementKind::Episode);
}

}  // namespace

Episode parse(std::string_view input, const anitomy::Options options) {
  Episode episode;

  std::string work{input};
  stripIgnoredSubstrings(work);
  auto elements = anitomy::parse(work, options);
  episode.setElements(elements);
  dropSpuriousZeroEpisode(episode);

  return episode;
}

Episode parseFileInfo(const QFileInfo& info, const anitomy::Options options,
                      const bool use_parent_directory_title_hint) {
  const auto fileName = info.fileName().toStdString();

  Episode episode = track::recognition::parse(fileName, options);
  inferSeasonZeroFromFilename(episode, fileName);

  if (use_parent_directory_title_hint) {
    // Season 0 (S00Exx) marks specials/OVAs stored in their own named folder.
    // Prefer the parent directory name as the title in this case, because the
    // scene abbreviation in the filename (e.g. "Iseleve") typically refers to the
    // main series while the folder name matches the actual special AniList entry.
    const auto season_str = episode.element(anitomy::ElementKind::Season);
    const bool isSeason0 = !season_str.empty() && (season_str == "0" || season_str == "00");

    if (!episode.contains(anitomy::ElementKind::Title) || isSeason0) {
      std::string dirName;
      if (isSeason0) {
        QDir immediate = info.dir();
        QString folderTitle = immediate.dirName();
        QDir parentOfRelease = immediate;
        if (parentOfRelease.cdUp()) {
          const QString grand = parentOfRelease.dirName();
          if (!grand.isEmpty() && directoryLooksLikeTorrentReleasePack(folderTitle)) {
            folderTitle = grand;
          }
        }
        dirName = folderTitle.toStdString();
      } else {
        dirName = info.dir().dirName().toStdString();
      }
      stripIgnoredSubstrings(dirName);
      if (!dirName.empty()) {
        if (isSeason0) {
          // Override the scene abbreviation title with the folder name.
          episode.setElement(anitomy::ElementKind::Title, dirName);
        } else {
          episode.addElement(anitomy::ElementKind::Title, dirName);
        }
      }
    } else if (!season_str.empty()) {
      // The filename already has a title and a season number (e.g. S04E05).
      // Fansub releases often use the base series title ("Honzuki no Gekokujou")
      // even for later seasons, which makes the base season the strongest cache hit.
      // Adding the parent folder name as a secondary Title element lets identify()
      // disambiguate via the folder — e.g. "Ascendance of a Bookworm_ Adopted
      // Daughter of an Archduke" uniquely identifies the S4 entry.
      std::string dirName = info.dir().dirName().toStdString();
      stripIgnoredSubstrings(dirName);
      if (!dirName.empty()) {
        episode.addElement(anitomy::ElementKind::Title, dirName);
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

  // Folder / filesystem names are sometimes truncated vs AniList (e.g. final "s" dropped on long
  // paths).
  if (matches.empty() && title.size() > 16) {
    const QString qt = QString::fromStdString(title).trimmed();
    if (!qt.endsWith(QLatin1Char('s'), Qt::CaseInsensitive)) {
      if (const auto data = cache()->find(normalize(title + "s"))) {
        matches.append_range(data->matches | std::views::values | std::ranges::to<std::vector>());
      }
    }
  }

  // Season-aware secondary lookup: "Title Season N" normalises to the same key as
  // "Title Part N" / "Title Nth Season" etc., so season-specific DB entries are
  // preferred over an ambiguously-named base season.
  if (season_num > 1) {
    const auto titleWithSeason = normalize(std::format("{} season {}", title, season_num));
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

  // Secondary Title elements (e.g. parent folder name added by parseFileInfo when the filename
  // carries a season number).  Fansub filenames often reuse the base-series title for later seasons
  // (e.g. "Honzuki no Gekokujou - S04E05"), so the primary lookup hits S1; the folder name
  // ("Ascendance of a Bookworm_ Adopted Daughter of an Archduke") uniquely identifies S4.
  // Use a slightly reduced base weight so a clear primary match still wins over a folder hint,
  // but the season boost lifts the correct season-specific entry above the base series.
  {
    constexpr float kFolderTitleBaseWeight = 0.9f;
    const auto mergeMatch = [&](int id, float weight) {
      const auto it =
          std::ranges::find_if(matches, [id](const Cache::Data::Match& m) { return m.id == id; });
      if (it == matches.end()) {
        matches.push_back({.id = id, .weight = weight});
      } else if (it->weight < weight) {
        it->weight = weight;
      }
    };

    for (const auto& secondary :
         episode.allElements(anitomy::ElementKind::Title) | std::views::drop(1)) {
      const auto normSecondary = normalize(secondary);
      if (normSecondary.empty() || normSecondary == normalizedTitle) continue;

      if (const auto data = cache()->find(normSecondary)) {
        for (const auto& [id, match] : data->matches) {
          mergeMatch(id, match.weight * kFolderTitleBaseWeight);
        }
      }

      if (season_num > 1) {
        const auto secondaryWithSeason =
            normalize(std::format("{} season {}", secondary, season_num));
        if (const auto data = cache()->find(secondaryWithSeason)) {
          constexpr float kSeasonBoost = 0.6f;
          for (const auto& [id, match] : data->matches) {
            mergeMatch(id, match.weight * kFolderTitleBaseWeight + kSeasonBoost);
          }
        }
      }
    }
  }

  // Deterministic and user-friendly ordering:
  // - Prefer anime that is actually on the user's list (entry exists) when multiple ids share a
  // title key.
  // - Then prefer higher weight.
  // - Then prefer smaller id for stability across runs (unordered_map iteration can otherwise
  // shuffle ties).
  std::ranges::sort(matches, [&](const Cache::Data::Match& a, const Cache::Data::Match& b) {
    const bool a_on_list = anime::db.entry(a.id) != nullptr;
    const bool b_on_list = anime::db.entry(b.id) != nullptr;
    if (a_on_list != b_on_list) return a_on_list > b_on_list;
    if (a.weight != b.weight) return a.weight > b.weight;
    return a.id < b.id;
  });

  for (const auto& match : matches) {
    if (isValidMatch(match.id, episode)) return match.id;
  }

  return anime::kUnknownId;
}

QString debugIdentifySummary(const Episode& episode) {
  cache()->init();

  const std::string title = episode.element(anitomy::ElementKind::Title);
  const std::string season_str = episode.element(anitomy::ElementKind::Season);
  const std::string ep_str = episode.element(anitomy::ElementKind::Episode);
  const int season_num = season_str.empty() ? 0 : QString::fromStdString(season_str).toInt();

  const std::string key0 = normalize(title);
  const bool hit0 = cache()->find(key0).has_value();

  bool hitS = false;
  std::string keyS;
  if (title.size() > 16) {
    const QString qt = QString::fromStdString(title).trimmed();
    if (!qt.endsWith(QLatin1Char('s'), Qt::CaseInsensitive)) {
      keyS = normalize(title + "s");
      hitS = cache()->find(keyS).has_value();
    }
  }

  bool hitSeason = false;
  std::string keySeason;
  if (season_num > 1) {
    keySeason = normalize(std::format("{} season {}", title, season_num));
    hitSeason = cache()->find(keySeason).has_value();
  }

  // Also show whether the title->matches we found map to an anime::db item yet.
  int matches0 = 0;
  int matches0_missing_item = 0;
  if (const auto data = cache()->find(key0)) {
    matches0 = static_cast<int>(data->matches.size());
    for (const auto& [id, _] : data->matches) {
      if (!anime::db.item(id)) ++matches0_missing_item;
    }
  }

  return QStringLiteral(
             "identify: title='%1' S='%2' E='%3' key0='%4' hit0=%5 m0=%6 missingDb=%7 hitS=%8 "
             "hitSeason=%9")
      .arg(QString::fromStdString(title).left(80))
      .arg(QString::fromStdString(season_str))
      .arg(QString::fromStdString(ep_str))
      .arg(QString::fromStdString(key0).left(80))
      .arg(hit0 ? 1 : 0)
      .arg(matches0)
      .arg(matches0_missing_item)
      .arg(hitS ? 1 : 0)
      .arg(hitSeason ? 1 : 0);
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
    if (!number.empty() && value < 1) return false;

    const auto season_str = episode.element(anitomy::ElementKind::Season);
    const bool is_s0 = season_str == "0" || season_str == "00";
    // Season 0 / specials: filenames often use global indices (e.g. S00E10) greater than the
    // listed episode count (e.g. three OVAs). Still treat as a valid match for identification.
    if (is_s0 && (item->episode_count < 1 || value > item->episode_count)) return true;

    if (value <= item->episode_count) return true;  // in range

    if (item->episode_count < 1) return true;  // episode count is unknown, so anything goes

    return false;  // out of range
  };

  if (!is_valid_episode_number()) return false;

  return true;
}

}  // namespace track::recognition
