#include "list_title_color.hpp"

namespace gui {

std::optional<QColor> decideListTitleColor(const bool caught_up_or_done,
                                          const bool next_unwatched_episode_on_disk,
                                          const bool aired_but_not_downloaded) {
  if (next_unwatched_episode_on_disk) {
    return QColor(0x42, 0xa5, 0xf5);  // material blue
  }
  if (caught_up_or_done) {
    return QColor(0x4c, 0xaf, 0x50);  // material green
  }
  if (aired_but_not_downloaded) {
    return QColor(0x9e, 0x9e, 0x9e);  // material grey
  }
  return std::nullopt;
}

}  // namespace gui

