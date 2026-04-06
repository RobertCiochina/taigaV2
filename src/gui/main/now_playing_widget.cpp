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
#include <QLabel>
#include <optional>

#include "base/string.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/media.hpp"

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
              reset();
            }
          });
}

void NowPlayingWidget::reset() {
  m_countdown_timer_->stop();
  m_countdown_remaining_ = 0;
  m_update_committed_ = false;
  hide();
  m_anime.reset();
  m_episode.reset();
  refresh();
}

void NowPlayingWidget::setPlaying(track::Episode episode) {
  const int incoming_id = episode.animeId();
  const QString incoming_ep = QString::fromStdString(episode.element(anitomy::ElementKind::Episode));

  // Restart countdown only when the episode actually changes
  const bool same_episode =
      m_episode.has_value() &&
      m_episode->animeId() == incoming_id &&
      QString::fromStdString(m_episode->element(anitomy::ElementKind::Episode)) == incoming_ep;

  m_episode = episode;

  if (const auto item = anime::db.item(episode.animeId())) {
    m_anime = *item;
  } else {
    m_anime.reset();
  }

  if (!same_episode) {
    m_update_committed_ = false;
    m_countdown_timer_->stop();
    m_countdown_remaining_ = 0;

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
  if (!m_episode || m_update_committed_) return;
  const int anime_id = m_episode->animeId();
  if (anime_id <= 0) return;

  const auto* item = anime::db.item(anime_id);
  if (!item) return;

  const QString ep_str = QString::fromStdString(m_episode->element(anitomy::ElementKind::Episode));
  bool ok = false;
  const int ep_no = ep_str.toInt(&ok);
  if (!ok || ep_no <= 0) return;

  // out-of-range guard: skip update if episode exceeds known total
  if (taiga::settings.recognitionUpdateOutOfRange() && item->episode_count > 0 &&
      ep_no > item->episode_count) {
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
    gui::commitListEntryLocalAndMaybeRemote(new_entry, this);
    return;
  }

  // Only update if detected episode advances the counter
  if (ep_no <= entry->watched_episodes) return;

  ListEntry updated = *entry;
  updated.watched_episodes = ep_no;
  // Auto-set status to Watching if it was Plan to Watch
  if (updated.status == anime::list::Status::PlanToWatch) {
    updated.status = anime::list::Status::Watching;
  }
  m_update_committed_ = true;
  gui::commitListEntryLocalAndMaybeRemote(updated, this);
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

  const auto title =
      m_anime ? m_anime->titles.romaji : m_episode->element(anitomy::ElementKind::Title);
  const auto episodeNumber = m_episode->element(anitomy::ElementKind::Episode, "1");
  const auto episodeCount = formatNumber(m_anime ? m_anime->episode_count : 0, "?");

  m_mainLabel->setText(u"Watching <a href=\"#\" style=\"%3\">%1</a> – Episode %2"_s
                           .arg(QString::fromStdString(title))
                           .arg(u"%1/%2"_s.arg(episodeNumber).arg(episodeCount))
                           .arg("font-weight: 600; text-decoration: none;"));

  if (m_update_committed_) {
    m_timerLabel->setText(tr("List <b style=\"font-weight:600;\">updated</b>"));
  } else if (m_countdown_remaining_ > 0) {
    const int mins = m_countdown_remaining_ / 60;
    const int secs = m_countdown_remaining_ % 60;
    m_timerLabel->setText(tr("List update in <b style=\"font-weight:600;\">%1</b>")
                              .arg(u"%1:%2"_s.arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'))));
  } else {
    m_timerLabel->setText({});
  }
}

}  // namespace gui
