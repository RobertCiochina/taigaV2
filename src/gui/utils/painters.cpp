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

#include "painters.hpp"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

#include "base/string.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/painter_state_saver.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_utils.hpp"
#include "media/anime_utils.hpp"
#include "taiga/settings.hpp"
#include "track/scanner.hpp"

namespace gui {

void paintEmptyListText(QAbstractScrollArea* area, const QString& text) {
  QPainter painter(area->viewport());

  painter.setFont([&painter]() {
    auto font = painter.font();
    font.setItalic(true);
    return font;
  }());

  painter.drawText(area->viewport()->rect(), Qt::AlignCenter, text);
}

void paintProgressBar(QPainter* painter, const QStyleOption& option, const Anime* anime,
                      const ListEntry* entry) {
  if (!anime || !entry) return;

  const QRect r = option.rect;
  const PainterStateSaver painterStateSaver(painter);

  painter->fillRect(r, option.palette.mid().color());

  float ratio_aired = 0.f;
  float ratio_watched = 0.f;
  anime::list::getProgressBarRatios(anime, entry, ratio_aired, ratio_watched);
  ratio_aired = std::min(ratio_aired, 1.f);
  ratio_watched = std::min(ratio_watched, 1.f);

  const int strip_h = std::clamp(r.height() / 5, 2, 4);

  const QColor watchedColor = theme.isDark() ? QColor{12, 164, 12, 140} : QColor{12, 164, 12, 220};
  const QColor airedColor = theme.isDark() ? QColor{80, 160, 255, 180} : QColor{33, 150, 243, 200};
  const QColor availableColor = theme.isDark() ? QColor{255, 200, 60, 200} : QColor{255, 180, 0, 220};

  if (taiga::settings.listProgressShowAired() && ratio_aired > 0.f) {
    QRect ar = r;
    ar.setTop(r.bottom() - strip_h + 1);
    ar.setRight(r.left() + std::max(1, static_cast<int>(std::lround(r.width() * ratio_aired))));
    painter->fillRect(ar, airedColor);
  }

  if (ratio_watched > 0.f) {
    QRect wr = r;
    wr.setWidth(std::max(1, static_cast<int>(std::lround(r.width() * ratio_watched))));
    painter->fillRect(wr, watchedColor);
  }

  if (taiga::settings.listProgressShowAvailable()) {
    const int eps_total = anime::estimateEpisodeCount(*anime, 0);
    const int eps_aired = anime::list::lastAiredEpisodeForProgress(*anime, entry);
    int eps_span = std::max(eps_total, eps_aired);
    if (eps_span < 1) eps_span = 1;
    const int bar_width = eps_total > 0 ? r.width() : static_cast<int>(std::lround(r.width() * 0.8f));
    const float ep_width = static_cast<float>(bar_width) / static_cast<float>(eps_span);
    const int watched_right = r.left() + std::max(0, static_cast<int>(std::lround(r.width() * ratio_watched)));

    QRect band = r;
    band.setTop(r.bottom() - strip_h + 1);
    for (int i = 1; i <= eps_span; ++i) {
      if (!track::libraryHasLocalEpisode(anime->id, i)) continue;
      int left = r.left() + static_cast<int>(std::floor(ep_width * static_cast<float>(i - 1)));
      int right = left + static_cast<int>(std::ceil(ep_width));
      if (i == entry->watched_episodes) {
        right = std::min(r.left() + bar_width, watched_right);
      }
      right = std::min(right, r.left() + bar_width);
      if (right <= left) continue;
      painter->fillRect(QRect{left, band.top(), right - left, band.height()}, availableColor);
    }
  }

  const int episodes = anime->episode_count;
  const int watched = std::clamp(entry->watched_episodes, 0,
                                 episodes > 0 ? episodes : std::numeric_limits<int>::max());
  const QString text = u"%1/%2"_s.arg(watched).arg(formatNumber(episodes, "?"));

  painter->setPen(option.palette.windowText().color());
  painter->drawText(r, Qt::AlignCenter, text);
}

}  // namespace gui
