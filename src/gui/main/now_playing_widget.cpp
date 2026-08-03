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

#include "now_playing_widget.hpp"

#include <QBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <optional>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "media/anime_list.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/episode_offset.hpp"
#include "track/media.hpp"
#include "track/recognition.hpp"
#include "track/scanner.hpp"

namespace gui {

NowPlayingWidget::NowPlayingWidget(QWidget* parent) : QFrame(parent) {
  setObjectName("nowPlaying");
  setSizePolicy(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Maximum);

  const auto layout = new QHBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  setLayout(layout);

  // Icon
  m_iconLabel = new QLabel(this);
  m_iconLabel->setFixedWidth(16);
  m_iconLabel->setFixedHeight(16);
  m_iconLabel->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
  m_iconLabel->setPixmap(theme.getIcon("info").pixmap(QSize(16, 16)));
  layout->addWidget(m_iconLabel);

  // Main
  m_mainLabel = new QLabel(this);
  layout->addWidget(m_mainLabel);
  connect(m_mainLabel, &QLabel::linkActivated, this, [this]() {
    if (m_anime) MediaDialog::show(this, MediaDialogPage::Details, *m_anime, {});
  });

  // Timer
  m_timerLabel = new QLabel(this);
  m_timerLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(m_timerLabel);
  connect(m_timerLabel, &QLabel::linkActivated, this, [this]() { cancelPendingUpdate(); });

  // Countdown timer (1-second ticks)
  m_countdown_timer_ = new QTimer(this);
  m_countdown_timer_->setInterval(1000);
  connect(m_countdown_timer_, &QTimer::timeout, this, &NowPlayingWidget::onCountdownTick);

  refresh();

  connect(track::media::detection(), &track::media::Detection::currentEpisodeChanged, this,
          [this](std::optional<track::Episode> episode) {
            if (episode) {
              setPlaying(*episode);
            } else {
              // Detection emits "nothing playing" only when the player actually closes (not on
              // transient unreadable polls), so this reliably ends the session: hide + auto-delete.
              reset();
            }
          });

  connect(track::media::detection(), &track::media::Detection::taigaLaunchedEpisode, this,
          [this](int animeId, int episode, const QString& filePath) {
            armTaigaInitiatedUpdate(animeId, episode, filePath);
          });
}

void NowPlayingWidget::reset() {
  m_countdown_timer_->stop();

  const int anime_id = m_episode ? m_episode->animeId() : anime::kUnknownId;
  const QString ep_str =
      m_episode
          ? QString::fromStdString(m_episode->element(anitomy::ElementKind::Episode, std::string{}))
          : QString{};
  bool ep_ok = false;
  int ep_no = ep_str.toInt(&ep_ok);
  if (!ep_ok || ep_no < 1) ep_no = 1;
  if (anime_id > 0) {
    const int list_ep = track::toListEpisode(anime_id, ep_no);
    if (list_ep > 0) ep_no = list_ep;
  }

  // Auto-delete: if the list was updated and a local file path is known, delete it.
  if (m_update_committed_ && m_episode && !m_episode->filePath().empty() &&
      taiga::settings.recognitionDeleteAfterWatched()) {
    const QString path = QString::fromStdString(m_episode->filePath());
    track::deleteWatchedEpisodeFile(anime_id, ep_no, path);

    // Title subfolders are recreated on download; always prune empty parents after delete.
    track::cleanupEmptyLibraryDirectoriesFromPath(path);
  }

  m_countdown_remaining_ = 0;
  m_update_committed_ = false;
  m_update_canceled_ = false;
  hide();
  m_anime.reset();
  m_episode.reset();
  refresh();

  // Ensure Home "Up next" updates immediately after playback finishes (commit + optional delete).
  if (auto* mw = gui::mainWindow()) {
    mw->refreshHomeDashboard();
    mw->refreshAnimeListProgressDecorations();
    mw->refreshListColors();
  }
}

void NowPlayingWidget::setPlaying(track::Episode episode) {
  const int incoming_id = episode.animeId();

  // Preserve a known local file path across detections that can't report one. Window-title based
  // detection (and Taiga-initiated playback that detection later picks up) yields an episode with
  // no file handle; without this, auto-delete-after-watched would lose the path before reset().
  if (episode.filePath().empty() && m_episode && !m_episode->filePath().empty() &&
      m_episode->animeId() == incoming_id) {
    episode.setFilePath(m_episode->filePath());
  }

  // Preserve a known episode number when later detection for the same anime has none (common for
  // movies: AAC2.0 → spurious 0 dropped → empty). Without this, Taiga-armed ep 1 is wiped and
  // commitListUpdate skips the list update / delete-after-watched.
  if (episode.element(anitomy::ElementKind::Episode).empty() && m_episode &&
      m_episode->animeId() == incoming_id) {
    const auto prev_ep = m_episode->element(anitomy::ElementKind::Episode);
    if (!prev_ep.empty()) {
      episode.setElement(anitomy::ElementKind::Episode, prev_ep);
    }
  }

  // Single-episode titles (movies) often have no episode token in the filename. Map to 1 so
  // history, countdown identity, and list commit agree with library scan indexing.
  if (episode.element(anitomy::ElementKind::Episode).empty() && incoming_id > 0) {
    if (const auto* item = anime::db.item(incoming_id); item && item->episode_count == 1) {
      episode.setElement(anitomy::ElementKind::Episode, "1");
    }
  }

  const QString incoming_ep =
      QString::fromStdString(episode.element(anitomy::ElementKind::Episode));

  // Restart countdown only when the episode actually changes
  const bool same_episode =
      m_episode.has_value() && m_episode->animeId() == incoming_id &&
      QString::fromStdString(m_episode->element(anitomy::ElementKind::Episode)) == incoming_ep;

  m_episode = episode;

  if (const auto item = anime::db.item(episode.animeId())) {
    m_anime = *item;
  } else {
    m_anime.reset();
  }

  if (taiga::settings.cacheDiagnosticsEnabled()) {
    QString line =
        QStringLiteral("nowPlaying: setPlaying title='%1' S='%2' E='%3' animeId=%4 same=%5")
            .arg(QString::fromStdString(episode.element(anitomy::ElementKind::Title)).left(80))
            .arg(QString::fromStdString(episode.element(anitomy::ElementKind::Season, {})))
            .arg(incoming_ep)
            .arg(incoming_id)
            .arg(same_episode ? 1 : 0);
    if (incoming_id <= 0) {
      line += QStringLiteral(" | %1").arg(track::recognition::debugIdentifySummary(episode));
    }
    track::appendLibraryEpisodeIndexCacheDebugLine(line);
  }

  if (!same_episode) {
    m_update_committed_ = false;
    m_update_canceled_ = false;
    m_countdown_timer_->stop();
    m_countdown_remaining_ = 0;

    // Record to History as soon as media detection sees a (new) episode.
    bool ep_ok = false;
    int ep_no = incoming_ep.toInt(&ep_ok);
    if ((!ep_ok || ep_no <= 0) && incoming_id > 0) {
      const auto* item = anime::db.item(incoming_id);
      if (item && item->episode_count == 1) {
        ep_no = 1;
        ep_ok = true;
      }
    }
    if (ep_ok && incoming_id > 0 && ep_no > 0) {
      // Apply the same S00 / absolute-offset mapping as list updates.
      const auto* item = anime::db.item(incoming_id);
      const std::string season_str = episode.element(anitomy::ElementKind::Season, {});
      const bool is_s0 = season_str == "0" || season_str == "00";
      if (item && is_s0 && item->episode_count > 0 && item->episode_count <= 12 &&
          ep_no > item->episode_count) {
        ep_no = ((ep_no - 1) % item->episode_count) + 1;
      } else if (!is_s0) {
        const int list_ep = track::toListEpisode(incoming_id, ep_no);
        if (list_ep > 0) ep_no = list_ep;
      }
      anime::history().recordEpisode(incoming_id, ep_no);
    }

    if (taiga::settings.recognitionAutoUpdateList() && incoming_id > 0) {
      m_countdown_remaining_ = taiga::settings.recognitionUpdateDelaySeconds();
      m_countdown_timer_->start();
    }
  }

  refresh();
  if (taiga::session.mainWindowNowPlayingBarEnabled()) {
    show();
  } else {
    hide();
  }
}

void NowPlayingWidget::onCountdownTick() {
  if (m_countdown_remaining_ > 0) {
    --m_countdown_remaining_;
    refresh();
  }
  if (m_countdown_remaining_ <= 0) {
    m_countdown_timer_->stop();
    if (!m_update_committed_) {
      commitListUpdate();
    }
  }
}

void NowPlayingWidget::commitListUpdate() {
  const auto dbg = [](const QString& reason) {
    if (taiga::settings.cacheDiagnosticsEnabled()) {
      track::appendLibraryEpisodeIndexCacheDebugLine(
          QStringLiteral("nowPlaying: commit %1").arg(reason));
    }
  };

  if (!m_episode || m_update_committed_) {
    dbg(QStringLiteral("skip: no episode or already committed"));
    return;
  }
  const int anime_id = m_episode->animeId();
  if (anime_id <= 0) {
    dbg(QStringLiteral("skip: unrecognized (animeId<=0)"));
    return;
  }

  const auto* item = anime::db.item(anime_id);
  if (!item) {
    dbg(QStringLiteral("skip: no db item for id=%1").arg(anime_id));
    return;
  }

  const QString ep_str = QString::fromStdString(m_episode->element(anitomy::ElementKind::Episode));
  bool ok = false;
  int ep_no = ep_str.toInt(&ok);
  if (!ok || ep_no <= 0) {
    // Movies / single-episode OVAs often have no episode token after anitomy cleanup (e.g. AAC2.0
    // → spurious 0 dropped). Library scan already indexes these as episode 1.
    if (item->episode_count == 1) {
      ep_no = 1;
      ok = true;
      dbg(QStringLiteral("map: empty/invalid episode ('%1') → 1 for single-episode id=%2")
              .arg(ep_str)
              .arg(anime_id));
    } else {
      dbg(QStringLiteral("skip: no/invalid episode number ('%1')").arg(ep_str));
      return;
    }
  }

  // Season 0 / specials: filenames often use global indices (e.g. S00E10..E12) even when the
  // AniList entry is a short multi-episode OVA/movie. Map to 1..N so list progress advances.
  const std::string season_str = m_episode->element(anitomy::ElementKind::Season, {});
  const bool is_s0 = season_str == "0" || season_str == "00";
  if (is_s0 && item->episode_count > 0 && item->episode_count <= 12 &&
      ep_no > item->episode_count) {
    ep_no = ((ep_no - 1) % item->episode_count) + 1;
  } else if (!is_s0) {
    const int list_ep = track::toListEpisode(anime_id, ep_no);
    if (list_ep > 0) ep_no = list_ep;
  }

  // out-of-range guard: skip update if episode exceeds known total
  if (taiga::settings.recognitionUpdateOutOfRange() && item->episode_count > 0 &&
      ep_no > item->episode_count) {
    dbg(QStringLiteral("skip: out-of-range ep=%1 > count=%2").arg(ep_no).arg(item->episode_count));
    return;
  }

  const auto* entry = anime::db.entry(anime_id);
  if (!entry) {
    // Create a minimal in-progress entry if one doesn't exist
    ListEntry new_entry;
    new_entry.anime_id = anime_id;
    new_entry.status = anime::list::Status::Watching;
    new_entry.watched_episodes = ep_no;
    m_update_committed_ = true;
    dbg(QStringLiteral("create: new Watching entry id=%1 ep=%2").arg(anime_id).arg(ep_no));
    gui::commitListEntryLocalAndMaybeRemote(new_entry, this);
    return;
  }

  // Only update if detected episode advances the counter
  if (ep_no <= entry->watched_episodes) {
    dbg(QStringLiteral("skip: already counted ep=%1 <= watched=%2")
            .arg(ep_no)
            .arg(entry->watched_episodes));
    return;
  }

  ListEntry updated = *entry;
  updated.watched_episodes = ep_no;
  // Auto-set status to Watching if it was Plan to Watch
  if (updated.status == anime::list::Status::PlanToWatch) {
    updated.status = anime::list::Status::Watching;
  }
  m_update_committed_ = true;
  dbg(QStringLiteral("commit: id=%1 ep=%2 (was %3)")
          .arg(anime_id)
          .arg(ep_no)
          .arg(entry->watched_episodes));
  // Mark as Completed when the last episode is reached (silent; no popup).
  gui::maybePromptCompletion(this, *item, updated);
  gui::commitListEntryLocalAndMaybeRemote(updated, this);
}

void NowPlayingWidget::cancelPendingUpdate() {
  if (m_update_committed_ || m_countdown_remaining_ <= 0) return;

  m_countdown_timer_->stop();
  m_countdown_remaining_ = 0;
  m_update_canceled_ = true;

  if (taiga::settings.cacheDiagnosticsEnabled()) {
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("nowPlaying: update canceled by user"));
  }

  refresh();
}

void NowPlayingWidget::syncFromDetection() {
  if (!taiga::session.mainWindowNowPlayingBarEnabled()) {
    hide();
    return;
  }
  if (const auto ep = track::media::detection()->getCurrentEpisode()) {
    setPlaying(*ep);
  } else {
    reset();
  }
}

void NowPlayingWidget::armTaigaInitiatedUpdate(int animeId, int episode, const QString& filePath) {
  if (animeId <= 0) return;
  if (!taiga::session.mainWindowNowPlayingBarEnabled()) return;

  track::Episode ep;
  ep.setAnimeId(animeId);
  if (episode > 0) ep.setElement(anitomy::ElementKind::Episode, std::to_string(episode));
  if (!filePath.isEmpty()) ep.setFilePath(filePath.toStdString());

  setPlaying(std::move(ep));
}

void NowPlayingWidget::refresh() {
  if (!m_episode.has_value()) {
    m_iconLabel->setToolTip({});
    m_mainLabel->setText({});
    m_timerLabel->setText({});
    return;
  }

  QStringList lines;
  if (const auto player = track::media::detection()->getCurrentPlayer()) {
    lines += u"<b>Media player:</b> %1"_s.arg(QString::fromStdString(player->name));
  }
  if (m_episode->contains(anitomy::ElementKind::EpisodeTitle)) {
    const auto episodeTitle = m_episode->element(anitomy::ElementKind::EpisodeTitle);
    lines += u"<b>Episode title:</b> %1"_s.arg(episodeTitle);
  }
  if (m_episode->contains(anitomy::ElementKind::ReleaseGroup)) {
    const auto releaseGroup = m_episode->element(anitomy::ElementKind::ReleaseGroup);
    lines += u"<b>Group:</b> %1"_s.arg(releaseGroup);
  }
  m_iconLabel->setToolTip(lines.join("<br>"));

  const QString iconName = m_anime ? "check_circle" : "info";
  m_iconLabel->setPixmap(theme.getIcon(iconName).pixmap(QSize(16, 16)));

  const std::string title =
      m_anime ? anime::preferredListTitleString(*m_anime, anime::TitleLanguage::English)
              : m_episode->element(anitomy::ElementKind::Title);
  const auto episodeNumber = m_episode->element(anitomy::ElementKind::Episode, "1");
  const auto episodeCount = formatNumber(m_anime ? m_anime->episode_count : 0, "?");

  m_mainLabel->setText(u"Watching <a href=\"#\" style=\"%3\">%1</a> – Episode %2"_s
                           .arg(QString::fromStdString(title))
                           .arg(u"%1/%2"_s.arg(episodeNumber).arg(episodeCount))
                           .arg("font-weight: 600; text-decoration: none;"));

  if (m_update_committed_) {
    m_timerLabel->setText(tr("List <b style=\"font-weight:600;\">updated</b>"));
  } else if (m_update_canceled_) {
    m_timerLabel->setText(tr("Update <b style=\"font-weight:600;\">canceled</b>"));
  } else if (m_countdown_remaining_ > 0) {
    const int mins = m_countdown_remaining_ / 60;
    const int secs = m_countdown_remaining_ % 60;
    m_timerLabel->setText(
        tr("List update in <b style=\"font-weight:600;\">%1</b> · "
           "<a href=\"#cancel\" style=\"text-decoration:none;\">%2</a>")
            .arg(u"%1:%2"_s.arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0')))
            .arg(tr("Cancel")));
  } else {
    m_timerLabel->setText({});
  }
}

}  // namespace gui
