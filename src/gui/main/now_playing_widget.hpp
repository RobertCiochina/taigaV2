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

#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <optional>

#include "media/anime.hpp"
#include "track/episode.hpp"

namespace gui {

class NowPlayingWidget final : public QFrame {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(NowPlayingWidget)

public:
  NowPlayingWidget(QWidget* parent);
  ~NowPlayingWidget() = default;

  void reset();
  void setPlaying(track::Episode episode);
  void syncFromDetection();

private:
  void refresh();
  void onCountdownTick();
  void commitListUpdate();

  QLabel* m_iconLabel = nullptr;
  QLabel* m_mainLabel = nullptr;
  QLabel* m_timerLabel = nullptr;

  QTimer* m_countdown_timer_ = nullptr;
  int m_countdown_remaining_ = 0;
  bool m_update_committed_ = false;

  std::optional<Anime> m_anime;
  std::optional<track::Episode> m_episode;
};

}  // namespace gui
