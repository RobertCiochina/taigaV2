/**
 * Taiga
 */

#include "gui/main/announced_releases_widget.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "gui/main/main_window.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/ui_title.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "media/announced_related_refresh.hpp"
#include "media/announced_releases.hpp"
#include "sync/service.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"

namespace gui {

namespace {

QString formatTypeLabel(const anime::Type t) {
  using anime::Type;
  switch (t) {
    case Type::Tv:
      return QApplication::translate("AnnouncedReleases", "TV");
    case Type::Movie:
      return QApplication::translate("AnnouncedReleases", "Movie");
    case Type::Ova:
      return QApplication::translate("AnnouncedReleases", "OVA");
    case Type::Ona:
      return QApplication::translate("AnnouncedReleases", "ONA");
    case Type::Special:
      return QApplication::translate("AnnouncedReleases", "Special");
    case Type::Music:
      return QApplication::translate("AnnouncedReleases", "Music");
    case Type::Unknown:
    default:
      return {};
  }
}

QString mediaStatusLabel(const anime::Status s) {
  using anime::Status;
  switch (s) {
    case Status::NotYetAired:
      return QApplication::translate("AnnouncedReleases", "Not yet aired");
    case Status::Airing:
      return QApplication::translate("AnnouncedReleases", "Airing");
    case Status::FinishedAiring:
      return QApplication::translate("AnnouncedReleases", "Finished");
    default:
      return {};
  }
}

}  // namespace

AnnouncedReleasesWidget::AnnouncedReleasesWidget(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(16, 16, 16, 16);
  outer->setSpacing(12);

  auto* hint = new QLabel(
      QApplication::translate(
          "AnnouncedReleases",
          "Shows anime that are <b>not yet aired</b> or <b>currently airing</b> when they are a "
          "<b>direct sequel</b> (on AniList) to something on your list as <b>Completed</b> or "
          "<b>Planning</b> or <b>Watching</b>. Keep your list synchronized; sequel links cached on "
          "those titles are "
          "read from disk, and missing sequel entries are fetched from AniList when you open this "
          "page."),
      this);
  hint->setWordWrap(true);
  hint->setTextFormat(Qt::RichText);
  outer->addWidget(hint);

  auto* top = new QHBoxLayout();
  auto* checkNow = new QPushButton(QApplication::translate("AnnouncedReleases", "Check now"), this);
  checkNow->setToolTip(QApplication::translate(
      "AnnouncedReleases",
      "Force-refreshes all list anchors and their sequel links from AniList now."));
  connect(checkNow, &QPushButton::clicked, this, []() {
    if (MainWindow* mw = mainWindow()) mw->runFullAnnouncedRelatedRefresh();
  });
  top->addWidget(checkNow);
  auto* addAll =
      new QPushButton(QApplication::translate("AnnouncedReleases", "Add all to Planning"), this);
  addAll->setToolTip(QApplication::translate("AnnouncedReleases",
                                             "Adds every visible title to Planning on your list."));
  connect(addAll, &QPushButton::clicked, this, [this]() {
    const auto cands = anime::computeAnnouncedReleaseCandidates(
        taiga::session.announcedReleasesDismissedAnimeIds());
    QVector<int> ids;
    for (const auto& c : cands) {
      const Anime* a = anime::db.item(c.anime_id);
      if (!a) continue;
      if (!taiga::settings.listShowMatureContent() && anime::isNsfw(*a)) continue;
      const QString t = uiTitle(*a);
      if (!m_filter.trimmed().isEmpty() && !t.contains(m_filter.trimmed(), Qt::CaseInsensitive)) {
        continue;
      }
      ids.push_back(c.anime_id);
    }
    if (ids.isEmpty()) {
      QMessageBox::information(this,
                               QApplication::translate("AnnouncedReleases", "Announced releases"),
                               QApplication::translate("AnnouncedReleases", "Nothing to add."));
      return;
    }
    const auto answer = QMessageBox::question(
        this, QApplication::translate("AnnouncedReleases", "Add all to Planning"),
        QApplication::translate("AnnouncedReleases", "Add %1 title(s) to Planning?")
            .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) return;

    const std::time_t now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
    for (const int aid : ids) {
      ListEntry e;
      e.anime_id = aid;
      e.status = anime::list::Status::PlanToWatch;
      e.watched_episodes = 0;
      e.last_updated = now;
      commitListEntryLocalAndMaybeRemote(e, this);
    }
    if (MainWindow* mw = mainWindow()) mw->refreshAnnouncedReleasesSurfaces();
  });
  top->addWidget(addAll);
  top->addStretch(1);
  outer->addLayout(top);

  m_scheduleLabel_ = new QLabel(this);
  m_scheduleLabel_->setWordWrap(true);
  m_scheduleLabel_->setTextFormat(Qt::RichText);
  m_scheduleLabel_->setStyleSheet(
      QStringLiteral("QLabel{color: palette(text); font-size:13px; font-weight:600;"
                     " background: palette(alternate-base); border: 1px solid palette(mid);"
                     " border-radius:6px; padding:8px 10px;}"));
  outer->addWidget(m_scheduleLabel_);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto* inner = new QWidget(scroll);
  m_rowsLayout = new QVBoxLayout(inner);
  m_rowsLayout->setContentsMargins(0, 0, 0, 0);
  m_rowsLayout->setSpacing(10);
  m_rowsLayout->addStretch(1);
  scroll->setWidget(inner);
  outer->addWidget(scroll, 1);

  m_dbRefreshDebounce_ = new QTimer(this);
  m_dbRefreshDebounce_->setSingleShot(true);
  connect(m_dbRefreshDebounce_, &QTimer::timeout, this, &AnnouncedReleasesWidget::refresh);
  connect(&anime::db, &anime::Database::itemUpdated, this, [this](int) {
    if (!isVisible()) return;
    m_dbRefreshDebounce_->start(400);
  });

  // Keeps the "next scan" countdown live without rebuilding the (potentially large) rows list.
  m_scheduleTick_ = new QTimer(this);
  m_scheduleTick_->setTimerType(Qt::VeryCoarseTimer);
  m_scheduleTick_->setInterval(60 * 1000);
  connect(m_scheduleTick_, &QTimer::timeout, this,
          &AnnouncedReleasesWidget::updateScanScheduleLabel);
}

void AnnouncedReleasesWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  updateScanScheduleLabel();
  if (m_scheduleTick_) m_scheduleTick_->start();
}

void AnnouncedReleasesWidget::hideEvent(QHideEvent* event) {
  QWidget::hideEvent(event);
  if (m_scheduleTick_) m_scheduleTick_->stop();
}

void AnnouncedReleasesWidget::applyToolbarTextFilter(const QString& text) {
  m_filter = text;
  rebuildRows();
}

void AnnouncedReleasesWidget::refresh() {
  rebuildRows();
}

namespace {

QString formatCountdown(qint64 secs) {
  if (secs < 60) return QApplication::translate("AnnouncedReleases", "under a minute");
  const qint64 days = secs / 86400;
  const qint64 hours = (secs % 86400) / 3600;
  const qint64 mins = (secs % 3600) / 60;
  if (days > 0) {
    return QApplication::translate("AnnouncedReleases", "%1d %2h").arg(days).arg(hours);
  }
  if (hours > 0) {
    return QApplication::translate("AnnouncedReleases", "%1h %2m").arg(hours).arg(mins);
  }
  return QApplication::translate("AnnouncedReleases", "%1m").arg(mins);
}

}  // namespace

void AnnouncedReleasesWidget::updateScanScheduleLabel() {
  if (!m_scheduleLabel_) return;

  if (sync::currentServiceId() != sync::ServiceId::AniList) {
    m_scheduleLabel_->hide();
    return;
  }

  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const auto s =
      anime::computeAnnouncedRelatedScanSchedule(now, anime::kAnnouncedRelatedStaleAfterSecs);

  if (s.total_count == 0) {
    m_scheduleLabel_->setText(QApplication::translate(
        "AnnouncedReleases",
        "No related titles are being tracked yet. Add sequels' roots to your list to start."));
    m_scheduleLabel_->show();
    return;
  }

  QString text;
  if (s.due_now_count > 0) {
    // A due title is refreshed on the next sync-triggered sweep (all due titles at once, ~3s each).
    text = QApplication::translate(
               "AnnouncedReleases",
               "<b>%1</b> of %2 related title(s) are due for a refresh now — they'll all be "
               "refreshed together (~3s each) on the next sync-triggered scan. "
               "Or use <b>Check now</b> for an immediate full find.")
               .arg(s.due_now_count)
               .arg(s.total_count);
  } else if (s.next_due_secs > 0) {
    const QDateTime when = QDateTime::fromSecsSinceEpoch(s.next_due_secs);
    text = QApplication::translate(
               "AnnouncedReleases",
               "All %1 related titles are up to date. Next one becomes due in <b>%2</b> (%3); "
               "each title refreshes on its own 30-day cadence. "
               "Use <b>Check now</b> to force-refresh all anchors immediately.")
               .arg(s.total_count)
               .arg(formatCountdown(s.next_due_secs - now),
                    when.toString(QStringLiteral("ddd, MMM d, HH:mm")));
  } else {
    text = QApplication::translate(
               "AnnouncedReleases",
               "Tracking %1 related title(s). Use <b>Check now</b> to force-refresh.")
               .arg(s.total_count);
  }

  m_scheduleLabel_->setText(text);
  m_scheduleLabel_->show();
}

void AnnouncedReleasesWidget::rebuildRows() {
  if (!m_rowsLayout) return;

  updateScanScheduleLabel();

  while (m_rowsLayout->count()) {
    QLayoutItem* it = m_rowsLayout->takeAt(0);
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }

  if (sync::currentServiceId() != sync::ServiceId::AniList) {
    auto* empty = new QLabel(
        QApplication::translate("AnnouncedReleases",
                                "Announced releases require AniList as the active list service."),
        this);
    empty->setWordWrap(true);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    m_rowsLayout->addWidget(empty);
    m_rowsLayout->addStretch(1);
    return;
  }

  anime::prefetchMissingAnnouncedSequelMediaFromAnchors();

  const auto cands =
      anime::computeAnnouncedReleaseCandidates(taiga::session.announcedReleasesDismissedAnimeIds());
  if (cands.isEmpty()) {
    const QString emptyText =
        anime::hasAnnouncedSequelAnchorsAwaitingMediaFetch()
            ? QApplication::translate("AnnouncedReleases", "Fetching related sequels from AniList…")
            : QApplication::translate("AnnouncedReleases", "No matching titles right now.");
    auto* empty = new QLabel(emptyText, this);
    empty->setWordWrap(true);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    m_rowsLayout->addWidget(empty);
    m_rowsLayout->addStretch(1);
    return;
  }

  int shown = 0;
  for (const auto& c : cands) {
    const Anime* a = anime::db.item(c.anime_id);
    const Anime* anchor = anime::db.item(c.anchor_anime_id);
    if (!a) continue;
    if (!taiga::settings.listShowMatureContent() && anime::isNsfw(*a)) continue;
    const QString title = uiTitle(*a);
    if (!m_filter.trimmed().isEmpty() && !title.contains(m_filter.trimmed(), Qt::CaseInsensitive)) {
      continue;
    }

    const QString anchorLine =
        anchor ? tr("From your list: %1").arg(uiTitle(*anchor)) : tr("From your list");

    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    auto* hl = new QHBoxLayout(frame);
    hl->setContentsMargins(10, 8, 10, 8);
    hl->setSpacing(10);

    constexpr int kPosterW = 56;
    constexpr int kPosterH = 80;
    auto* poster = new QLabel(frame);
    poster->setFixedSize(kPosterW, kPosterH);
    poster->setAlignment(Qt::AlignCenter);
    poster->setStyleSheet(
        QStringLiteral("QLabel{border-radius:4px; background: palette(alternate-base);}"));
    const int aid = c.anime_id;
    auto paintPoster = [poster, aid]() {
      if (const QPixmap* p = imageProvider.loadPoster(aid); p && !p->isNull()) {
        poster->setPixmap(p->scaled(poster->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        poster->setText(QString());
      } else {
        poster->clear();
        poster->setText(QStringLiteral("…"));
      }
    };
    paintPoster();
    connect(&imageProvider, &ImageProvider::posterChanged, frame, [poster, aid](const int id) {
      if (id != aid) return;
      if (const QPixmap* p = imageProvider.loadPoster(aid); p && !p->isNull()) {
        poster->setPixmap(p->scaled(poster->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        poster->setText(QString());
      }
    });
    hl->addWidget(poster);

    auto* vl = new QVBoxLayout();
    auto* t1 = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), frame);
    t1->setTextFormat(Qt::RichText);
    t1->setWordWrap(true);
    const QString meta =
        QStringLiteral("%1 · %2").arg(mediaStatusLabel(a->status), formatTypeLabel(a->type));
    auto* t2 = new QLabel(meta, frame);
    t2->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText); font-size:11px;}"));
    auto* t3 = new QLabel(anchorLine, frame);
    t3->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText); font-size:11px;}"));
    t3->setWordWrap(true);
    vl->addWidget(t1);
    vl->addWidget(t2);
    vl->addWidget(t3);
    hl->addLayout(vl, 1);

    auto* addBtn = new QPushButton(tr("Add to Planning"), frame);
    connect(addBtn, &QPushButton::clicked, this, [this, aid]() {
      ListEntry e;
      e.anime_id = aid;
      e.status = anime::list::Status::PlanToWatch;
      e.watched_episodes = 0;
      e.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
      commitListEntryLocalAndMaybeRemote(e, this);
      if (MainWindow* mw = mainWindow()) mw->refreshAnnouncedReleasesSurfaces();
    });

    auto* woBtn = new QPushButton(tr("Watch order…"), frame);
    connect(woBtn, &QPushButton::clicked, this, [aid]() {
      if (MainWindow* mw = mainWindow()) mw->openWatchOrderGuideForAnime(aid);
    });

    auto* dismissBtn = new QPushButton(tr("Dismiss"), frame);
    dismissBtn->setStyleSheet(QStringLiteral("QPushButton{color: palette(placeholderText);}"));
    connect(dismissBtn, &QPushButton::clicked, this, [this, aid]() {
      const auto answer =
          QMessageBox::question(this, tr("Dismiss this title?"),
                                tr("Hide “%1” from Announced releases until it changes again?\n\n"
                                   "This does not change your anime list.")
                                    .arg(uiTitle(*anime::db.item(aid))),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) return;
      taiga::session.addAnnouncedReleaseDismissedAnimeId(aid);
      if (MainWindow* mw = mainWindow()) mw->refreshAnnouncedReleasesSurfaces();
    });

    auto* btnCol = new QVBoxLayout();
    btnCol->addWidget(addBtn);
    btnCol->addWidget(woBtn);
    btnCol->addWidget(dismissBtn);
    hl->addLayout(btnCol);

    m_rowsLayout->addWidget(frame);
    ++shown;
  }

  if (shown == 0) {
    auto* empty = new QLabel(
        QApplication::translate("AnnouncedReleases", "No titles match the filter."), this);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    m_rowsLayout->addWidget(empty);
  }
  m_rowsLayout->addStretch(1);
}

}  // namespace gui
