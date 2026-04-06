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

#include "media.hpp"

#include "base/file.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/streaming_sites.hpp"

namespace track::media {

Detection::Detection(QObject* parent) : QObject(parent) {
  pollTimer_ = new QTimer(this);
  connect(pollTimer_, &QTimer::timeout, this, &Detection::poll);
}

const std::optional<Episode> Detection::getCurrentEpisode() const {
  return currentEpisode_;
}

const std::optional<Detection::media_t> Detection::getCurrentMedia() const {
  return currentMedia_;
}

const std::optional<Detection::player_t> Detection::getCurrentPlayer() const {
  return currentPlayer_;
}

bool Detection::init() {
  const auto file = base::readFile(":/players.anisthesia");

  if (file.isEmpty()) {
    return false;
  }

  if (!anisthesia::ParsePlayersData(file.toStdString(), players_)) {
    return false;
  }

#ifdef Q_OS_WINDOWS
  pollTimer_->setInterval(taiga::settings.mediaDetectionInterval());
  if (taiga::settings.mediaDetectionPollingActive()) {
    pollTimer_->start();
  }
#endif

  return true;
}

void Detection::setPollingEnabled(const bool on) {
#ifdef Q_OS_WINDOWS
  if (on) {
    pollTimer_->setInterval(taiga::settings.mediaDetectionInterval());
    pollTimer_->start();
  } else {
    pollTimer_->stop();
    currentPlayer_.reset();
    currentMedia_.reset();
    if (currentEpisode_) {
      currentEpisode_.reset();
      emit currentEpisodeChanged(std::nullopt);
    }
  }
#else
  (void)on;
#endif
}

void Detection::refreshPollingFromSettings() {
#ifdef Q_OS_WINDOWS
  pollTimer_->setInterval(taiga::settings.mediaDetectionInterval());
  if (taiga::settings.mediaDetectionPollingActive()) {
    pollTimer_->start();
  } else {
    pollTimer_->stop();
    currentPlayer_.reset();
    currentMedia_.reset();
    if (currentEpisode_) {
      currentEpisode_.reset();
      emit currentEpisodeChanged(std::nullopt);
    }
  }
#endif
}

void Detection::poll() {
#ifdef Q_OS_WINDOWS
  if (players_.empty()) return;

  const bool want_players = taiga::settings.mediaDetectionPlayersEnabled();
  const bool want_streaming = taiga::settings.mediaDetectionStreamingEnabled();
  std::vector<player_t> players;
  for (const auto& player : players_) {
    if (player.type == anisthesia::PlayerType::WebBrowser) {
      if (want_streaming) players.emplace_back(player);
    } else if (want_players) {
      players.emplace_back(player);
    }
  }
  if (players.empty()) {
    currentPlayer_.reset();
    currentMedia_.reset();
    if (currentEpisode_) {
      currentEpisode_.reset();
      emit currentEpisodeChanged(std::nullopt);
    }
    return;
  }

  static const auto media_proc = [](const anisthesia::MediaInfo&) {
    return true;  // Accept all media
  };

  std::vector<anisthesia::win::Result> results;
  if (!anisthesia::win::GetResults(players, media_proc, results)) {
    currentPlayer_.reset();
    currentMedia_.reset();
    if (currentEpisode_) {
      currentEpisode_.reset();
      emit currentEpisodeChanged(std::nullopt);
    }
    return;
  }

  const auto& res = results.front();
  currentPlayer_ = res.player;
  currentMedia_ = res.media.empty() ? std::nullopt : std::optional<media_t>{res.media.front()};

  std::string file;
  std::string url;
  std::string title;
  std::string tab;
  for (const auto& med : res.media) {
    for (const auto& info : med.information) {
      switch (info.type) {
        case anisthesia::MediaInfoType::File:
          file = info.value;
          break;
        case anisthesia::MediaInfoType::Url:
          url = info.value;
          break;
        case anisthesia::MediaInfoType::Title:
          title = info.value;
          break;
        case anisthesia::MediaInfoType::Tab:
          tab = info.value;
          break;
        default:
          break;
      }
    }
  }
  if (title.empty() && !tab.empty()) {
    title = tab;
  }

  track::Episode episode;
  if (!file.empty()) {
    const QFileInfo fileInfo{QString::fromStdString(file)};
    episode = track::recognition::parseFileInfo(
        fileInfo, {}, taiga::settings.libraryScanLookupParentDirectories());
    episode.setFilePath(file);
  } else {
    std::string parse_str = title;
    if (res.player.type == anisthesia::PlayerType::WebBrowser) {
      track::streaming::normalizeBrowserTitle(url, parse_str);
      if (const auto slug = track::streaming::matchProviderSlugByUrl(url)) {
        if (!taiga::settings.streamProviderEnabled(std::string(*slug))) {
          currentPlayer_.reset();
          currentMedia_.reset();
          if (currentEpisode_) {
            currentEpisode_.reset();
            emit currentEpisodeChanged(std::nullopt);
          }
          return;
        }
        (void)track::streaming::refineTitleForProvider(*slug, parse_str);
      }
    }
    if (parse_str.empty()) {
      currentPlayer_.reset();
      currentMedia_.reset();
      if (currentEpisode_) {
        currentEpisode_.reset();
        emit currentEpisodeChanged(std::nullopt);
      }
      return;
    }
    episode = track::recognition::parse(parse_str);
  }

  const auto animeId = track::recognition::identify(episode);
  episode.setAnimeId(animeId);

  if (!currentEpisode_ || currentEpisode_->animeId() != animeId) {
    currentEpisode_ = episode;
    emit currentEpisodeChanged(episode);
  }
#endif
}

}  // namespace track::media
