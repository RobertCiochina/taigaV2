#pragma once

#include <optional>

#include <QColor>

namespace gui {

// Title color rules:
// - Blue: next unwatched episode is on disk (ready to watch)
// - Green: caught up / fully done
// - Grey: aired but not on disk
// - nullopt: default palette text
std::optional<QColor> decideListTitleColor(bool caught_up_or_done,
                                          bool next_unwatched_episode_on_disk,
                                          bool aired_but_not_downloaded);

}  // namespace gui

