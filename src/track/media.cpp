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
#include "track/scanner.hpp"
#include "track/streaming_sites.hpp"

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <string>

namespace track::media {

namespace {

bool isHex32(const std::string& s) {
  if (s.size() != 32) return false;
  for (const unsigned char c : s) {
    const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!is_hex) return false;
  }
  return true;
}

QString normalizeWinPath(QString path) {
  // Anisthesia may return Win32 extended paths, e.g. \\?\C:\...
  static const QString kPrefix = QStringLiteral("\\\\?\\");
  if (path.startsWith(kPrefix)) {
    path.remove(0, kPrefix.size());
  }
  return path;
}

bool looksLikeVideoFile(const QString& path) {
  const QString p = normalizeWinPath(path);
  const QString ext = QFileInfo(p).suffix().toLower();
  // Keep this conservative: common containers only.
  static const QSet<QString> kVideoExt = {
      QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"), QStringLiteral("mov"),
      QStringLiteral("wmv"), QStringLiteral("m4v"), QStringLiteral("webm"), QStringLiteral("ts"),
  };
  return kVideoExt.contains(ext);
}

bool isLikelyCacheOrTemp(const QString& path) {
  const QString p = normalizeWinPath(path).toLower();
  // The observed failure: AMD DxCache .parc file being treated as the "playing file".
  if (p.contains(QStringLiteral("\\appdata\\local\\amd\\dxcache\\"))) return true;
  if (p.endsWith(QStringLiteral(".parc"))) return true;
  return false;
}

QString summarizeMediaInfo(const anisthesia::Media& med) {
  std::string file;
  std::string title;
  std::string tab;
  std::string url;
  for (const auto& info : med.information) {
    switch (info.type) {
      case anisthesia::MediaInfoType::File:
        if (file.empty()) file = info.value;
        break;
      case anisthesia::MediaInfoType::Title:
        if (title.empty()) title = info.value;
        break;
      case anisthesia::MediaInfoType::Tab:
        if (tab.empty()) tab = info.value;
        break;
      case anisthesia::MediaInfoType::Url:
        if (url.empty()) url = info.value;
        break;
      default:
        break;
    }
  }
  if (title.empty() && !tab.empty()) title = tab;

  auto clip = [](const std::string& s, const size_t n) -> QString {
    if (s.empty()) return {};
    if (s.size() <= n) return QString::fromStdString(s);
    return QString::fromStdString(s.substr(0, n)) + QStringLiteral("…");
  };

  QString out;
  if (!file.empty()) out += QStringLiteral("file=\"%1\" ").arg(clip(file, 160));
  if (!title.empty()) out += QStringLiteral("title=\"%1\" ").arg(clip(title, 120));
  if (!url.empty()) out += QStringLiteral("url=\"%1\" ").arg(clip(url, 120));
  return out.trimmed();
}

}  // namespace

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

  const bool diag = taiga::settings.cacheDiagnosticsEnabled();
  if (diag && !results.empty()) {
    const auto& r0 = results.front();
    const std::string player_name = r0.player.name;
    // mpv-specific investigation: we sometimes get a 32-hex ID as "title" instead of a path/title.
    const auto is_mpv = [](const std::string& n) {
      if (n.size() != 3) return false;
      return (n[0] == 'm' || n[0] == 'M') && (n[1] == 'p' || n[1] == 'P') && (n[2] == 'v' || n[2] == 'V');
    };
    if (is_mpv(player_name)) {
      // Rate-limit: log only when the first result's apparent identity changes.
      std::string first_file;
      std::string first_title;
      std::string first_tab;
      for (const auto& med : r0.media) {
        for (const auto& info : med.information) {
          switch (info.type) {
            case anisthesia::MediaInfoType::File:
              if (first_file.empty()) first_file = info.value;
              break;
            case anisthesia::MediaInfoType::Title:
              if (first_title.empty()) first_title = info.value;
              break;
            case anisthesia::MediaInfoType::Tab:
              if (first_tab.empty()) first_tab = info.value;
              break;
            default:
              break;
          }
        }
      }
      if (first_title.empty() && !first_tab.empty()) first_title = first_tab;

      const std::string identity = !first_file.empty() ? ("file:" + first_file)
                                                       : (!first_title.empty() ? ("title:" + first_title) : "none");
      static std::string last_identity;
      if (identity != last_identity) {
        last_identity = identity;

        QStringList parts;
        parts << QStringLiteral("mediaDetect: mpv results=%1 chosen=0 media0=%2%3")
                     .arg(static_cast<int>(results.size()))
                     .arg(static_cast<int>(r0.media.size()))
                     .arg(isHex32(first_title) ? QStringLiteral(" (title=hex32)") : QString{});

        // Show the first few candidates to understand ordering.
        const int kMaxResults = 4;
        const int kMaxMediaPerResult = 2;
        for (int i = 0; i < static_cast<int>(results.size()) && i < kMaxResults; ++i) {
          const auto& rr = results[static_cast<size_t>(i)];
          parts << QStringLiteral("  [%1] player=\"%2\" media=%3")
                       .arg(i)
                       .arg(QString::fromStdString(rr.player.name))
                       .arg(static_cast<int>(rr.media.size()));
          for (int m = 0; m < static_cast<int>(rr.media.size()) && m < kMaxMediaPerResult; ++m) {
            parts << QStringLiteral("    - %1").arg(summarizeMediaInfo(rr.media[static_cast<size_t>(m)]));
          }
        }

        track::appendLibraryEpisodeIndexCacheDebugLine(parts.join('\n'));
      }
    }
  }

  const auto& res = results.front();
  currentPlayer_ = res.player;
  currentMedia_ = res.media.empty() ? std::nullopt : std::optional<media_t>{res.media.front()};

  std::string file;
  std::string url;
  std::string title;
  std::string tab;
  QString best_file_qt;
  for (const auto& med : res.media) {
    for (const auto& info : med.information) {
      switch (info.type) {
        case anisthesia::MediaInfoType::File:
          // mpv (via open_files) may report multiple open files, including driver caches
          // (e.g. AMD DxCache *.parc). Prefer a real video file deterministically.
          if (best_file_qt.isEmpty()) {
            best_file_qt = QString::fromStdString(info.value);
          } else {
            const QString cand = QString::fromStdString(info.value);
            const bool best_is_video = looksLikeVideoFile(best_file_qt);
            const bool cand_is_video = looksLikeVideoFile(cand);
            const bool best_is_bad = isLikelyCacheOrTemp(best_file_qt) && !best_is_video;
            const bool cand_is_bad = isLikelyCacheOrTemp(cand) && !cand_is_video;

            // Priority:
            // 1) any video file beats non-video
            // 2) among non-video, avoid cache/temp paths
            // 3) otherwise keep first (stable)
            if (!best_is_video && cand_is_video) {
              best_file_qt = cand;
            } else if (!cand_is_video && best_is_video) {
              // keep best
            } else if (best_is_bad && !cand_is_bad) {
              best_file_qt = cand;
            }
          }
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
  if (!best_file_qt.isEmpty()) file = best_file_qt.toStdString();
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
