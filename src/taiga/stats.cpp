/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "taiga/stats.hpp"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileInfo>

#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "taiga/path.hpp"

namespace taiga {

namespace {

int episodeRunLength(const Anime& a) {
  if (a.episode_count > 0) return a.episode_count;
  return anime::estimateEpisodeCount(a, a.last_aired_episode);
}

void accumulatePosterCache(ListStatistics& out) {
  const QString root = QString::fromStdString(get_data_path());
  const QDir dir(QStringLiteral("%1/v1/db/image").arg(root));
  if (!dir.exists()) return;

  const QFileInfoList files =
      dir.entryInfoList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
  out.poster_file_count = static_cast<int>(files.size());
  for (const QFileInfo& fi : files) {
    out.poster_bytes += fi.size();
  }
}

}  // namespace

ListStatistics computeListStatistics() {
  ListStatistics out{};
  out.db_items = anime::db.items().size();
  const auto& entries = anime::db.entries();
  out.anime_on_list = entries.size();

  float score_sum = 0.f;

  for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
    const int anime_id = it.key();
    const ListEntry& entry = it.value();
    const Anime* anime = anime::db.item(anime_id);
    if (!anime) continue;

    const int run = episodeRunLength(*anime);
    const int len_min = std::max(1, anime::estimateEpisodeLength(*anime));
    const int sec_per_ep = len_min * 60;

    out.episode_equivalents += entry.watched_episodes + entry.rewatched_times * run;

    const int watched_total = entry.watched_episodes + entry.rewatched_times * run;
    out.spent_watch_seconds += sec_per_ep * watched_total;

    switch (entry.status) {
      case anime::list::Status::NotInList:
      case anime::list::Status::Completed:
      case anime::list::Status::Dropped:
        break;
      default: {
        const int remaining = std::max(0, run - entry.watched_episodes);
        out.planned_watch_seconds += sec_per_ep * remaining;
        break;
      }
    }

    if (entry.score > 0) {
      score_sum += static_cast<float>(entry.score);
      ++out.scored_title_count;
      const size_t idx = static_cast<size_t>(std::floor(static_cast<double>(entry.score) / 10.0));
      const size_t clamped = std::min(idx, out.score_histogram.size() - 1u);
      ++out.score_histogram[clamped];
    }
  }

  if (out.scored_title_count > 0) {
    out.mean_score_0_100 = score_sum / static_cast<float>(out.scored_title_count);
    float sum_sq = 0.f;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
      const ListEntry& entry = it.value();
      if (entry.score <= 0) continue;
      const float d = static_cast<float>(entry.score) - out.mean_score_0_100;
      sum_sq += d * d;
    }
    out.score_stddev_0_100 = std::sqrt(sum_sq / static_cast<float>(out.scored_title_count));
  }

  float max_bucket = 1.f;
  for (const int c : out.score_histogram) {
    max_bucket = std::max(max_bucket, static_cast<float>(c));
  }
  for (size_t i = 0; i < out.score_bar_fraction.size(); ++i) {
    out.score_bar_fraction[i] = static_cast<float>(out.score_histogram[i]) / max_bucket;
  }

  accumulatePosterCache(out);
  return out;
}

}  // namespace taiga
