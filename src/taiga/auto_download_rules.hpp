#pragma once

#include <cstdint>

namespace anime {
struct Details;
}

namespace taiga {

/// Best-effort upper bound for "episodes that have aired" for auto-download.
/// Conservative by design: avoids guessing that future episodes aired when schedule metadata is missing.
int computeLastAiredEpisodeForAutoDownload(const anime::Details& item, int watched_episodes,
                                          std::int64_t now_secs);

}  // namespace taiga

