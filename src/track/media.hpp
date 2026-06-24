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

#pragma once

#include <QApplication>
#include <QObject>
#include <QString>
#include <QTimer>
#include <anisthesia.hpp>
#include <optional>
#include <vector>

#include "track/episode.hpp"

namespace track::media {

class Detection final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Detection)

public:
  using media_t = anisthesia::Media;
  using player_t = anisthesia::Player;

  Detection(QObject* parent);

  const std::optional<Episode> getCurrentEpisode() const;
  const std::optional<media_t> getCurrentMedia() const;
  const std::optional<player_t> getCurrentPlayer() const;

  bool init();
  void poll();
  void setPollingEnabled(bool on);
  /// Re-read interval and on/off from settings (e.g. after Settings dialog OK).
  void refreshPollingFromSettings();

  /// Notify that Taiga itself started playback (anime id + episode known up front). Lets the
  /// list-update tracking proceed even when external player detection can't read the player.
  void notifyTaigaLaunched(int animeId, int episode, const QString& filePath);

signals:
  void currentEpisodeChanged(std::optional<Episode> media) const;
  /// Emitted by notifyTaigaLaunched() — playback initiated by Taiga's own Play action.
  void taigaLaunchedEpisode(int animeId, int episode, const QString& filePath) const;

private:
  std::optional<Episode> currentEpisode_;
  std::optional<media_t> currentMedia_;
  std::optional<player_t> currentPlayer_;
  std::vector<player_t> players_;

  QTimer* pollTimer_;
};

inline Detection* detection() {
  static auto detection = new Detection(qApp);
  return detection;
}

}  // namespace track::media
