/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <array>
#include <cstdint>

namespace taiga {

/// Aggregates derived from the local DB + list (same spirit as v1 `taiga::stats`, without torrent/uptime).
struct ListStatistics {
  int anime_on_list = 0;
  int db_items = 0;
  /// Sum of progress plus full runs for each rewatch (v1 episode count heuristic).
  int episode_equivalents = 0;
  int planned_watch_seconds = 0;
  int spent_watch_seconds = 0;
  float mean_score_0_100 = 0.f;
  float score_stddev_0_100 = 0.f;
  int scored_title_count = 0;
  std::array<int, 11> score_histogram{};
  /// Normalized 0..1 for UI bars (v1 divides by max bucket count).
  std::array<float, 11> score_bar_fraction{};
  int poster_file_count = 0;
  std::int64_t poster_bytes = 0;
};

[[nodiscard]] ListStatistics computeListStatistics();

}  // namespace taiga
