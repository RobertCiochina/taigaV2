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

#include "main_window.hpp"

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRunnable>
#include <QScreen>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtWidgets>
#include <algorithm>
#include <anitomy.hpp>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>

#include "base/log.hpp"
#include "base/string.hpp"
#include "gui/history/history_widget.hpp"
#include "gui/library/library_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/list/watch_next_dialog.hpp"
#include "gui/main/about_dialog.hpp"
#include "gui/main/announced_releases_widget.hpp"
#include "gui/main/navigation_sidebar_refresh.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/main/stats_dialog.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/torrent/torrent_auto_cleanup.hpp"
#include "gui/torrent/torrent_feed_widget.hpp"
#include "gui/utils/table_view_defaults.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/tray_icon.hpp"
#include "gui/utils/ui_strings.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_list_import.hpp"
#include "media/anime_utils.hpp"
#include "media/announced_related_refresh.hpp"
#include "media/announced_releases.hpp"
#include "sync/anilist.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
#include "taiga/auto_download_rules.hpp"
#include "taiga/network.hpp"
#include "taiga/path.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "taiga/stats.hpp"
#include "taiga/tray_balloon_format.hpp"
#include "taiga/update_check.hpp"
#include "taiga/user_feedback.hpp"
#include "track/episode.hpp"
#include "track/library_watcher.hpp"
#include "track/media.hpp"
#include "track/play.hpp"
#include "track/recognition_cache.hpp"
#include "track/scanner.hpp"
#include "ui_main_window.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

namespace {

QString announcedRelatedDiagTitle(const Anime& a) {
  return QString::fromStdString(anime::preferredListTitleString(a, anime::TitleLanguage::English));
}

}  // namespace

MainWindow::MainWindow() : QMainWindow(), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

  ui_->menubar->hide();

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  if (const auto geometry = taiga::session.mainWindowGeometry(); !geometry.isEmpty()) {
    restoreGeometry(geometry);
  } else {
    // First run (or no session): center for a good default placement.
    centerWidgetToScreen(this);
  }

  // Legacy sessions can store a very small window; grow to a comfortable floor
  // without exceeding the current screen's work area.
  if (QScreen* sc = screen() ? screen() : QGuiApplication::primaryScreen()) {
    const QRect avail = sc->availableGeometry();
    constexpr int kFloorW = 1320;
    constexpr int kFloorH = 780;
    if (width() < kFloorW || height() < kFloorH) {
      const int targetW = qMin(qMax(width(), kFloorW), avail.width() - 24);
      const int targetH = qMin(qMax(height(), kFloorH), avail.height() - 24);
      if (targetW > 0 && targetH > 0 && (targetW != width() || targetH != height())) {
        resize(targetW, targetH);
      }
    }
  }

  // Do not call `init()` here, as it relies on the main window pointer being
  // available through the application instance.
}

MainWindow* mainWindow() {
  return taiga::app()->mainWindow();
}

NavigationWidget* MainWindow::navigation() const {
  return m_navigationWidget;
}

NowPlayingWidget* MainWindow::nowPlaying() const {
  return m_nowPlayingWidget;
}

QLineEdit* MainWindow::searchBox() const {
  return m_searchBox;
}

Ui::MainWindow* MainWindow::ui() const {
  return ui_;
}

void MainWindow::init() {
  initUi(/*startup_blocking=*/false);
  scheduleStartupWork();
}

bool MainWindow::startupBlockingActive() const {
  return m_startup_blocking_active_;
}

void MainWindow::setStartupBlockingMode(const bool on) {
  setStartupBlockingActive(on);
}

void MainWindow::ensurePageInitialized(const MainWindowPage page) {
  initPage(page);
}

void MainWindow::setStartupBlockingActive(const bool on) {
  const bool was_blocking = m_startup_blocking_active_;
  m_startup_blocking_active_ = on;
  if (was_blocking && !on) {
    tryRunAnnouncedRelatedAfterStartup();
  }
  if (!on && m_welcome_prompt_deferred_) {
    m_welcome_prompt_deferred_ = false;
    scheduleWelcomeSetupPrompt();
  }
}

void MainWindow::scheduleWelcomeSetupPrompt() {
  if (m_welcomeCheckScheduled) return;
  m_welcomeCheckScheduled = true;
  QTimer::singleShot(400, this, &MainWindow::maybeShowWelcomeSetup);
}

void MainWindow::scheduleListSyncStartup() {
  if (taiga::settings.syncAutoOnStart() && taiga::settings.listSynchronizationEnabled()) {
    QTimer::singleShot(0, this, [this]() { startListSynchronization(false); });
  }
}

void MainWindow::scheduleUpdateCheckStartup() {
  if (taiga::settings.checkForUpdatesOnStartup()) {
    QTimer::singleShot(2200, this, [this]() { taiga::checkForUpdates(this, true); });
  }
}

void MainWindow::scheduleLibraryScanStartup() {
  if (!taiga::settings.scanLibraryOnStartup()) return;
  // Scan-on-startup now means: run ONE scan after startup sync (if it runs),
  // otherwise run one scan immediately.
  if (taiga::settings.syncAutoOnStart() && taiga::settings.listSynchronizationEnabled()) return;
  QTimer::singleShot(0, this, [this]() { runStartupPostSyncScan(); });
}

void MainWindow::scheduleStartupWork() {
  if (startupBlockingActive()) return;
  const bool will_sync =
      taiga::settings.syncAutoOnStart() && taiga::settings.listSynchronizationEnabled();
  scheduleListSyncStartup();
  scheduleUpdateCheckStartup();
  scheduleLibraryScanStartup();
  if (!will_sync) {
    tryRunAnnouncedRelatedAfterStartup();
  }
}

void MainWindow::runStartupPreSyncScan() {
  if (m_startup_scan_done_) return;

  const auto startPreSyncScan = [this]() {
    if (m_startup_scan_done_) return;
    m_startup_scan_done_ = true;
    // Ensure recognition cache incorporates any newly-fetched Media entries.
    track::recognition::cache()->clear();
    runLibraryScan(true, LibraryScanReason::StartupPreSync);
  };

  // If the cached library episode index references anime ids that are not present in the local
  // anime DB yet (common for specials), prefetch them so recognition can resolve those titles even
  // before the full startup sync completes.
  QSet<int> missing;
  for (const auto& [aid, _] : track::libraryEpisodeAvailability()) {
    if (!anime::db.item(aid)) missing.insert(aid);
  }

  if (missing.isEmpty() || sync::currentServiceId() != sync::ServiceId::AniList) {
    startPreSyncScan();
    return;
  }

  // Limit prefetch fanout.
  constexpr int kMaxPrefetch = 40;
  QList<int> ids = missing.values();
  if (ids.size() > kMaxPrefetch) ids = ids.mid(0, kMaxPrefetch);

  track::appendLibraryEpisodeIndexCacheDebugLine(
      QStringLiteral("startup: prefetch missing media ids (%1) before pre-sync scan")
          .arg(ids.size()));
  for (int id : ids) {
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("startup: prefetch media id=%1").arg(id));
  }

  auto* svc = sync::anilist::Service::instance();
  auto remaining = std::make_shared<int>(ids.size());

  const auto tryFinish = [this, remaining, startPreSyncScan]() {
    if (*remaining <= 0) startPreSyncScan();
  };

  // If any fetch completes, decrement. Also guard with a timeout so startup isn't blocked.
  QMetaObject::Connection conn;
  conn = connect(svc, &sync::anilist::Service::mediaFetchFinished, this,
                 [remaining, &conn, tryFinish](int /*id*/, bool /*success*/) {
                   if (*remaining > 0) --(*remaining);
                   if (*remaining <= 0) {
                     disconnect(conn);
                   }
                   tryFinish();
                 });

  for (int id : ids) {
    svc->fetchAnime(id);
  }
}

void MainWindow::runStartupPostSyncScan() {
  runLibraryScan(true, LibraryScanReason::StartupPostSync);
}

void MainWindow::prepareForFirstShow() {
  // Ensure the initial visible page is fully constructed and laid out before the window is mapped.
  initPage(MainWindowPage::Home);
  applyMainPage(MainWindowPage::Home);
  refreshNavigationSidebar();
  refreshHomeDashboard();
}

void MainWindow::initUi(const bool startup_blocking) {
  setStartupBlockingActive(startup_blocking);
  taiga::setUserFeedbackHandler([](const QString& msg, const bool err) {
    if (auto* w = mainWindow()) {
      QMetaObject::invokeMethod(w, "showUserFeedback", Qt::QueuedConnection, Q_ARG(QString, msg),
                                Q_ARG(bool, err));
    }
  });

  m_status_message_timer_ = new QTimer(this);
  m_status_message_timer_->setSingleShot(true);
  m_status_message_timer_->setTimerType(Qt::CoarseTimer);
  connect(m_status_message_timer_, &QTimer::timeout, this,
          &MainWindow::showNextQueuedStatusMessage);

  // Load last-known library availability index as early as possible so Home "Up next" can populate
  // immediately when the first page is initialized (before any startup scan runs).
  if (taiga::settings.scanLibraryOnStartup()) {
    const bool ok = track::loadLibraryEpisodeIndexCache();
    // We can't use the status bar yet (initStatusbar runs later). Stash a short note now and show
    // it once the UI is ready.
    const QString note =
        ok ? track::libraryEpisodeIndexCacheLastInfo()
           : tr("Library cache: %1").arg(track::libraryEpisodeIndexCacheLastError());
    QTimer::singleShot(0, this, [this, note]() { enqueueStatusMessage(note, false); });
  }

  initActions();
  initIcons();
  initTrayIcon();
  initToolbar();
  initNavigation();

  // Keep sidebar counts and Home dashboard in sync after list edits (Media dialog, menus, etc.).
  connect(&anime::db, &anime::Database::entryUpdated, this, [this](int) {
    if (m_watch_next_modal_open_) return;
    refreshNavigationSidebar();
    refreshHomeDashboard();
    if (taiga::settings.localListBackupEnabled() &&
        !taiga::settings.localListBackupPath().isEmpty()) {
      m_local_backup_timer_->start();
    }
  });

  m_local_backup_timer_ = new QTimer(this);
  m_local_backup_timer_->setSingleShot(true);
  m_local_backup_timer_->setInterval(2000);
  connect(m_local_backup_timer_, &QTimer::timeout, this, &MainWindow::writeLocalListBackup);
  connect(&anime::db, &anime::Database::itemUpdated, this, [this](int id) {
    // Keep recognition in sync with title/episode-count changes from any source (fetchAnime,
    // media dialog, search, season browse) — not just full list syncs. Only update incrementally
    // when the cache is already built; an empty cache is (re)built lazily by identify().
    if (auto* c = track::recognition::cache(); !c->empty()) {
      if (const auto* it = anime::db.item(id)) c->update(*it);
    }
    if (m_watch_next_modal_open_) return;
    refreshNavigationSidebar();
    refreshHomeDashboard();
  });

  // Bulk updates (list sync) suppress per-item signals; invalidate the recognition cache once so
  // newly-synced titles and corrected episode counts are indexed on the next lookup.
  connect(&anime::db, &anime::Database::batchFinished, this,
          []() { track::recognition::cache()->clear(); });

  if (const QByteArray splitter_state = taiga::session.mainWindowSplitterState();
      !splitter_state.isEmpty()) {
    ui_->splitter->restoreState(splitter_state);
  }
  initStatusbar();
  initNowPlaying();
  initNoStartupSyncBanner();
  restoreViewChromeFromSession();
  updateTitle();
  updateToolbarSearchPlaceholder();

  connect(track::media::detection(), &track::media::Detection::currentEpisodeChanged, this,
          [this](const std::optional<track::Episode>& ep) {
            updateTrayTooltip();
            maybeNotifyMediaDetectionBalloon(ep);
          });

  connect(
      track::libraryFolderWatcher(), &track::LibraryFolderWatcher::debouncedRescanTriggered, this,
      [this]() {
        enqueueStatusMessage(tr("Library folders changed — rescanning…"), false);
        runLibraryScan(true, LibraryScanReason::Watcher);
      },
      Qt::QueuedConnection);

  initFeatureToggleActions();

  m_catalog_autocheck_timer_ = new QTimer(this);
  m_catalog_autocheck_timer_->setTimerType(Qt::VeryCoarseTimer);
  connect(m_catalog_autocheck_timer_, &QTimer::timeout, this,
          &MainWindow::onTorrentCatalogAutocheckTimer);
  refreshTorrentCatalogAutocheckTimer();

  initAnnouncedRelatedRefresh();

  {
    auto* shortcut_find = new QShortcut(QKeySequence::Find, this);
    shortcut_find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut_find, &QShortcut::activated, this, [this]() {
      if (!m_searchBox) return;
      m_searchBox->setFocus(Qt::ShortcutFocusReason);
      m_searchBox->selectAll();
    });
    auto* shortcut_settings = new QShortcut(QKeySequence::Preferences, this);
    shortcut_settings->setContext(Qt::ApplicationShortcut);
    connect(shortcut_settings, &QShortcut::activated, this,
            [this]() { SettingsDialog::show(this); });
  }

  updateTrayTooltip();
}

void MainWindow::initAnnouncedRelatedRefresh() {
  m_announced_related_resume_timer_ = new QTimer(this);
  m_announced_related_resume_timer_->setSingleShot(true);
  m_announced_related_resume_timer_->setTimerType(Qt::CoarseTimer);
  connect(m_announced_related_resume_timer_, &QTimer::timeout, this,
          &MainWindow::onAnnouncedRelatedResumeTimer);

  m_announced_related_diff_timer_ = new QTimer(this);
  m_announced_related_diff_timer_->setSingleShot(true);
  m_announced_related_diff_timer_->setTimerType(Qt::CoarseTimer);
  connect(m_announced_related_diff_timer_, &QTimer::timeout, this,
          &MainWindow::checkAnnouncedRelatedDiffAndNotify);

  m_announced_related_due_timer_ = new QTimer(this);
  m_announced_related_due_timer_->setSingleShot(true);
  m_announced_related_due_timer_->setTimerType(Qt::CoarseTimer);
  connect(m_announced_related_due_timer_, &QTimer::timeout, this,
          &MainWindow::onAnnouncedRelatedDueTimer);

  if (sync::currentServiceId() == sync::ServiceId::AniList) {
    auto* svc = sync::anilist::Service::instance();
    connect(svc, &sync::anilist::Service::mediaFetchFinished, this, [this](int id, bool success) {
      // Log completion of items the current announced-related sweep queued.
      if (m_announced_related_pending_ids_.remove(id)) {
        const Anime* a = anime::db.item(id);
        track::appendLibraryEpisodeIndexCacheDebugLine(
            QStringLiteral("announced_related: refreshed aid=%1 success=%2 remaining=%3 "
                           "title='%4'")
                .arg(id)
                .arg(success ? 1 : 0)
                .arg(m_announced_related_pending_ids_.size())
                .arg(a ? announcedRelatedDiagTitle(*a) : QStringLiteral("?")));
        // Sweep finished: re-arm the due-check for the next title to expire.
        if (m_announced_related_pending_ids_.isEmpty()) rescheduleAnnouncedRelatedDueCheck();
      }
      // Any media refresh may reveal new sequels; debounce to avoid spam.
      if (m_announced_related_diff_timer_) m_announced_related_diff_timer_->start(2500);
    });
  }

  rescheduleAnnouncedRelatedDueCheck();
}

void MainWindow::pauseAnnouncedRelatedRefresh() {
  m_announced_related_paused_ = true;
  if (m_announced_related_resume_timer_) {
    m_announced_related_resume_timer_->stop();
  }
  if (m_announced_related_due_timer_) {
    m_announced_related_due_timer_->stop();
  }
}

void MainWindow::scheduleAnnouncedRelatedResumeAfterSync() {
  if (sync::currentServiceId() != sync::ServiceId::AniList) return;
  if (m_list_sync_in_progress_ || m_list_sync_queued_) return;
  if (!m_announced_related_resume_timer_) return;

  m_announced_related_paused_ = true;
  if (m_announced_related_due_timer_) m_announced_related_due_timer_->stop();
  m_announced_related_resume_timer_->start(10 * 60 * 1000);
  LOGW("announced_related: resume scheduled in 10 min");
  track::appendLibraryEpisodeIndexCacheDebugLine(
      QStringLiteral("announced_related: resume scheduled in 10 min"));
}

void MainWindow::onAnnouncedRelatedResumeTimer() {
  m_announced_related_paused_ = false;
  maybeRunAnnouncedRelatedRefresh();
}

void MainWindow::rescheduleAnnouncedRelatedDueCheck() {
  if (!m_announced_related_due_timer_) return;
  m_announced_related_due_timer_->stop();
  if (sync::currentServiceId() != sync::ServiceId::AniList) return;
  if (m_announced_related_paused_) return;                  // resume path will re-arm
  if (startupBlockingActive()) return;                      // unblock path will re-arm
  if (!m_announced_related_pending_ids_.isEmpty()) return;  // sweep in flight; re-armed on finish

  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const auto schedule =
      anime::computeAnnouncedRelatedScanSchedule(now, anime::kAnnouncedRelatedStaleAfterSecs);

  qint64 delay_ms = -1;
  if (schedule.due_now_count > 0) {
    // A title is already due. Auto-trigger, but keep a minimum spacing between sweeps so a title
    // whose fetch can't clear its stale state (e.g. transient failures) can't spin the loop.
    constexpr qint64 kMinAutoRescanSecs = 6LL * 60 * 60;  // 6 hours
    const qint64 last = taiga::session.announcedReleasesRelatedRefreshAtSecs();
    const qint64 since = last > 0 ? (now - last) : kMinAutoRescanSecs;
    delay_ms = since >= kMinAutoRescanSecs ? 0 : (kMinAutoRescanSecs - since) * 1000;
  } else if (schedule.next_due_secs > 0) {
    delay_ms = (schedule.next_due_secs - now) * 1000;
    if (delay_ms < 1000) delay_ms = 1000;
    // Cap so the list/DB is re-evaluated periodically even for far-future due times.
    constexpr qint64 kMaxDelayMs = 6LL * 60 * 60 * 1000;  // 6 hours
    if (delay_ms > kMaxDelayMs) delay_ms = kMaxDelayMs;
  }

  if (delay_ms >= 0) m_announced_related_due_timer_->start(static_cast<int>(delay_ms));
}

void MainWindow::onAnnouncedRelatedDueTimer() {
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const auto schedule =
      anime::computeAnnouncedRelatedScanSchedule(now, anime::kAnnouncedRelatedStaleAfterSecs);
  if (schedule.due_now_count > 0) {
    // maybeRun guards against pause/startup-blocking/AniList/in-flight overlap.
    maybeRunAnnouncedRelatedRefresh();
  }
  rescheduleAnnouncedRelatedDueCheck();
}

void MainWindow::tryRunAnnouncedRelatedAfterStartup() {
  if (sync::currentServiceId() != sync::ServiceId::AniList) return;
  if (m_announced_related_paused_) return;
  if (m_list_sync_in_progress_ || m_list_sync_queued_) return;
  maybeRunAnnouncedRelatedRefresh();
}

void MainWindow::maybeRunAnnouncedRelatedRefresh() {
  if (startupBlockingActive()) return;
  if (m_announced_related_paused_) return;
  if (sync::currentServiceId() != sync::ServiceId::AniList) return;
  // Don't start an overlapping sweep while one is still fetching.
  if (!m_announced_related_pending_ids_.isEmpty()) return;

  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const qint64 last = taiga::session.announcedReleasesRelatedRefreshAtSecs();

  LOGW("announced_related: start now={} last={} delta={}", static_cast<long long>(now),
       static_cast<long long>(last), static_cast<long long>(now - last));
  track::appendLibraryEpisodeIndexCacheDebugLine(
      QStringLiteral("announced_related: start now=%1 last=%2 delta=%3")
          .arg(now)
          .arg(last)
          .arg(now - last));

  taiga::session.setAnnouncedReleasesRelatedRefreshAtSecs(now);

  // Always prefetch sequel media ids referenced by cached relations.
  anime::prefetchMissingAnnouncedSequelMediaFromAnchors();

  // Full sweep of all stale ids (>30 days). Paced at 3s/req so rate-limit risk is minimal.
  constexpr qint64 kStaleAfter = anime::kAnnouncedRelatedStaleAfterSecs;
  const auto ids = anime::computeAnnouncedRelatedRefreshAnimeIds(std::numeric_limits<int>::max(),
                                                                 now, kStaleAfter);
  m_last_announced_related_check_started_secs_ = now;
  m_last_announced_related_fetch_count_ = ids.size();
  LOGW("announced_related: queued_refresh_ids={} stale_after_secs={}", static_cast<int>(ids.size()),
       static_cast<long long>(kStaleAfter));
  track::appendLibraryEpisodeIndexCacheDebugLine(
      QStringLiteral("announced_related: queued_refresh_ids=%1 stale_after_secs=%2")
          .arg(ids.size())
          .arg(kStaleAfter));
  m_announced_related_pending_ids_.clear();
  for (const int id : ids) {
    const Anime* a = anime::db.item(id);
    const QString title = a ? announcedRelatedDiagTitle(*a) : QStringLiteral("?");
    const qint64 age_days =
        (a && a->relations_fetched_at > 0) ? (now - a->relations_fetched_at) / 86400 : -1;
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("announced_related: queue aid=%1 last_fetch_age_days=%2 title='%3'")
            .arg(id)
            .arg(age_days)
            .arg(title));
    m_announced_related_pending_ids_.insert(id);
    sync::fetchAnime(id);
  }

  if (!ids.isEmpty()) {
    enqueueStatusMessage(tr("New seasons check: queued %1 refresh(es)…").arg(ids.size()), false);
  }
  if (m_announced_related_diff_timer_) m_announced_related_diff_timer_->start(6000);

  // If nothing was queued (all up to date), arm the timer for the next title to expire.
  // Otherwise the pending-empty handler re-arms once fetches complete.
  if (ids.isEmpty()) rescheduleAnnouncedRelatedDueCheck();
}

void MainWindow::checkAnnouncedRelatedDiffAndNotify() {
  if (sync::currentServiceId() != sync::ServiceId::AniList) return;

  const QSet<int> dismissed = taiga::session.announcedReleasesDismissedAnimeIds();
  const bool show_mature = taiga::settings.listShowMatureContent();
  const QSet<int> current =
      anime::computeVisibleAnnouncedReleaseCandidateIds(dismissed, show_mature);
  const QSet<int> known = taiga::session.announcedReleasesKnownCandidateAnimeIds();

  QSet<int> added = current;
  for (const int id : known) added.remove(id);

  taiga::session.setAnnouncedReleasesKnownCandidateAnimeIds(current);

  if (!added.isEmpty()) {
    const int n = added.size();
    const QString msg = (n == 1)
                            ? tr("New related anime found. Check Announced releases.")
                            : tr("New related anime found (%1). Check Announced releases.").arg(n);
    LOGW("announced_related: diff added={} current={} known={}", n, current.size(), known.size());
    track::appendLibraryEpisodeIndexCacheDebugLine(
        QStringLiteral("announced_related: diff added=%1 current=%2 known=%3")
            .arg(n)
            .arg(current.size())
            .arg(known.size()));
    taiga::userFeedback(msg, false);
    enqueueStatusMessage(msg, false);
    postTrayMessage(tr("Taiga"), msg);
    refreshAnnouncedReleasesSurfaces();
  } else {
    // Only emit a "no new" message when a daily check just ran, to avoid noise from unrelated
    // fetches.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_last_announced_related_fetch_count_ > 0 &&
        m_last_announced_related_check_started_secs_ > 0 &&
        now - m_last_announced_related_check_started_secs_ <= 10 * 60) {
      LOGW("announced_related: diff none current={} known={}", current.size(), known.size());
      track::appendLibraryEpisodeIndexCacheDebugLine(
          QStringLiteral("announced_related: diff none current=%1 known=%2")
              .arg(current.size())
              .arg(known.size()));
      enqueueStatusMessage(tr("New seasons check: no new related anime found."), false);
    }
  }
}

void MainWindow::initActions() {
  ui_->actionProfile->setToolTip(tr("Profile"));
  ui_->actionSynchronize->setToolTip(
      synchronizeWithServiceToolTip(sync::serviceName(sync::currentServiceId())));

  connect(ui_->actionAddNewFolder, &QAction::triggered, this, &MainWindow::addNewFolder);
  // Use window-close path so close-to-tray and "save on close" behavior runs consistently.
  connect(ui_->actionExit, &QAction::triggered, this, [this]() { close(); }, Qt::QueuedConnection);
  connect(ui_->actionOpenDataFolder, &QAction::triggered, this, &MainWindow::openDataFolder);
  connect(ui_->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog::show(this); });
  ui_->actionSettings->setToolTip(settingsActionToolTipWithShortcut(
      QKeySequence(QKeySequence::Preferences).toString(QKeySequence::NativeText)));
  connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::about);
  connect(ui_->actionDonate, &QAction::triggered, this, &MainWindow::donate);
  connect(ui_->actionSupport, &QAction::triggered, this, &MainWindow::support);
  connect(ui_->actionProfile, &QAction::triggered, this, &MainWindow::profile);
  connect(ui_->actionStatistics, &QAction::triggered, this, &MainWindow::statistics);
  connect(ui_->actionDisplayWindow, &QAction::triggered, this, &MainWindow::displayWindow);

  connect(ui_->actionSynchronize, &QAction::triggered, this, [this]() {
    cancelDelayedAutoDownload(tr("Manual sync"));
    startListSynchronization(true);
  });
  ui_->actionSynchronize->setShortcuts(
      {QKeySequence{QKeySequence::Refresh}, QKeySequence{Qt::CTRL | Qt::Key_S}});
  ui_->actionSynchronize->setShortcutContext(Qt::ApplicationShortcut);
  ui_->actionSynchronize->setStatusTip(
      synchronizeDownloadListStatusTip(sync::serviceName(sync::currentServiceId())));
  connect(ui_->actionCheckForUpdates, &QAction::triggered, this,
          &MainWindow::checkForUpdatesManually);
  connect(ui_->actionScanAvailableEpisodes, &QAction::triggered, this,
          [this]() { runLibraryScan(false, LibraryScanReason::Manual); });

  connect(ui_->actionExportListAsMarkdown, &QAction::triggered, this,
          &MainWindow::exportAnimeListMarkdown);
  connect(ui_->actionExportListAsMyAnimeListXML, &QAction::triggered, this,
          &MainWindow::exportAnimeListXml);
  connect(ui_->actionExportListAsCsv, &QAction::triggered, this, &MainWindow::exportAnimeListCsv);
  connect(ui_->actionImportListFromMalXml, &QAction::triggered, this,
          &MainWindow::importAnimeListMalXml);

  connect(ui_->actionPlayNextEpisode, &QAction::triggered, this,
          &MainWindow::playNextEpisodeFromMenu);
  connect(ui_->actionPlayRandomAnime, &QAction::triggered, this,
          &MainWindow::playRandomAnimeFromMenu);

  connect(ui_->actionBack, &QAction::triggered, this, &MainWindow::goBackNavigation);
  connect(ui_->actionForward, &QAction::triggered, this, &MainWindow::goForwardNavigation);

  connect(ui_->toolbar, &QToolBar::visibilityChanged, this, [this](const bool visible) {
    const QSignalBlocker b(ui_->actionToggleToolbar);
    ui_->actionToggleToolbar->setChecked(visible);
    taiga::session.setMainWindowToolbarVisible(visible);
    ui_->menubar->setVisible(!visible);
  });
  connect(ui_->actionToggleToolbar, &QAction::toggled, this, [this](const bool on) {
    ui_->toolbar->setVisible(on);
    // visibilityChanged handles persistence and menu bar sync
  });
  connect(ui_->actionToggleStatusbar, &QAction::toggled, this, [this](const bool on) {
    ui_->statusbar->setVisible(on);
    taiga::session.setMainWindowStatusBarVisible(on);
  });
  connect(ui_->actionToggleNowPlaying, &QAction::toggled, this, [this](const bool on) {
    taiga::session.setMainWindowNowPlayingBarEnabled(on);
    if (!m_nowPlayingWidget) return;
    if (on) {
      m_nowPlayingWidget->syncFromDetection();
    } else {
      m_nowPlayingWidget->hide();
    }
  });
  ui_->actionToggleNavigationSidebar->setShortcutContext(Qt::ApplicationShortcut);
  ui_->actionToggleNavigationSidebar->setStatusTip(
      tr("Show or hide the left navigation pane (Ctrl+B)."));
  connect(ui_->actionToggleNavigationSidebar, &QAction::toggled, this, [this](const bool on) {
    Q_UNUSED(on);
    // Sidebar is always-on.
    if (m_navigationWidget) m_navigationWidget->setVisible(true);
    taiga::settings.setNavigationSidebarVisible(true);
    const QSignalBlocker b(ui_->actionToggleNavigationSidebar);
    ui_->actionToggleNavigationSidebar->setChecked(true);
  });
  ui_->actionToggleNavigationSidebar->setEnabled(false);
  ui_->actionToggleNavigationSidebar->setChecked(true);
  ui_->actionToggleNavigationSidebar->setShortcuts({});
  connect(ui_->actionLibraryFolders, &QAction::triggered, this,
          &MainWindow::showLibraryFoldersDialog);
}

void MainWindow::initIcons() {
  ui_->menuLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->menuImport->setIcon(theme.getIcon("add_box"));
  ui_->menuExport->setIcon(theme.getIcon("export_notes"));

  ui_->actionAddNewFolder->setIcon(theme.getIcon("create_new_folder"));
  ui_->actionAbout->setIcon(theme.getIcon("info"));
  ui_->actionBack->setIcon(theme.getIcon("arrow_back"));
  ui_->actionCheckForUpdates->setIcon(theme.getIcon("cloud_download"));
  ui_->actionDonate->setIcon(theme.getIcon("favorite"));
  ui_->actionOpenDataFolder->setIcon(theme.getIcon("folder"));
  ui_->actionOpenDataFolder->setText(openPrimaryLibraryOrDataFolderActionLabel());
  ui_->actionOpenDataFolder->setToolTip(openPrimaryLibraryOrDataFolderToolTip());
  ui_->actionExit->setIcon(theme.getIcon("logout"));
  ui_->actionForward->setIcon(theme.getIcon("arrow_forward"));
  ui_->actionLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->actionMenu->setIcon(theme.getIcon("menu"));
  ui_->actionPlayNextEpisode->setIcon(theme.getIcon("skip_next"));
  ui_->actionPlayNextEpisode->setText(playNextEpisodeActionLabel());
  ui_->actionSynchronize->setText(synchronizeActionLabel());
  ui_->actionSettings->setText(settingsActionLabel());
  ui_->actionPlayRandomAnime->setIcon(theme.getIcon("shuffle"));
  ui_->actionProfile->setIcon(theme.getIcon("account_circle"));
  ui_->actionScanAvailableEpisodes->setIcon(theme.getIcon("pageview"));
  ui_->actionSettings->setIcon(theme.getIcon("settings"));
  ui_->actionSupport->setIcon(theme.getIcon("help"));
  ui_->actionSynchronize->setIcon(theme.getIcon("sync"));
  ui_->actionStatistics->setIcon(theme.getIcon("bar_chart"));
  ui_->actionToggleNavigationSidebar->setIcon(theme.getIcon("lists"));
}

void MainWindow::initNavigation() {
  m_navigationWidget = new NavigationWidget(this);

  connect(m_navigationWidget, &NavigationWidget::currentPageChanged, this, &MainWindow::setPage);
  connect(m_navigationWidget, &NavigationWidget::currentListStatusChanged, this,
          [this](anime::list::Status) { updateToolbarSearchPlaceholder(); });
  connect(m_navigationWidget, &NavigationWidget::watchNextRequested, this, [this]() {
    // Defer so we are not inside `QTreeWidget::currentItemChanged` while showing the modal, and so
    // AniList-driven DB updates can safely skip sidebar refresh until the modal closes.
    QTimer::singleShot(0, this, [this]() {
      const MainWindowPage saved_page = m_activePage;
      std::optional<anime::list::Status> saved_list_status;
      if (saved_page == MainWindowPage::List && m_listWidget) {
        const auto lf = m_listWidget->currentListSidebarFilter();
        if (!lf.anyStatus && lf.status.has_value()) {
          saved_list_status = static_cast<anime::list::Status>(*lf.status);
        }
      }

      const auto restore_watch_next_nav = [this, saved_page, saved_list_status]() {
        if (!m_navigationWidget) return;
        m_navHistorySuppress = true;
        {
          const QSignalBlocker blocker(m_navigationWidget);
          m_navigationWidget->setCurrentNavigationPage(saved_page, saved_list_status);
        }
        applyMainPage(saved_page);
        m_navHistorySuppress = false;
        updateNavHistoryActions();
      };

      const auto run_watch_next_modal = [this]() -> bool {
        m_watch_next_modal_open_ = true;
        WatchNextDialog dlg(nullptr);
        dlg.setWindowModality(Qt::ApplicationModal);
        dlg.runModalRandomPlanningSession();
        if (MainWindow* mw = mainWindow()) {
          dlg.adjustSize();
          const QRect ag = mw->frameGeometry();
          dlg.move(ag.center() - QPoint(dlg.width() / 2, dlg.height() / 2));
        }
        dlg.exec();
        m_watch_next_modal_open_ = false;
        return dlg.didChangeList();
      };

      // First visit to the anime list: do not call `navigateTo(List)` before the List page exists.
      // That would highlight List in the sidebar while the stacked widget is still on Home, which
      // can desync navigation state and crash. Run the modal first, then `setPage(List)` to create
      // the list page, then restore the sidebar/content to where the user was (often Home).
      if (!m_listWidget) {
        m_navHistorySuppress = true;
        const bool list_changed = run_watch_next_modal();
        refreshNavigationSidebar();
        refreshHomeDashboard();
        applyMainPage(MainWindowPage::List);
        {
          const QSignalBlocker blocker(m_navigationWidget);
          m_navigationWidget->setCurrentNavigationPage(saved_page, std::nullopt);
        }
        applyMainPage(saved_page);
        m_navHistorySuppress = false;
        updateNavHistoryActions();
        if (!list_changed) return;
        applyWatchNextListSideEffects();
        return;
      }

      const bool list_changed = run_watch_next_modal();
      refreshNavigationSidebar();
      refreshHomeDashboard();
      restore_watch_next_nav();
      if (!list_changed) return;
      applyWatchNextListSideEffects();
    });
  });

  navigateTo(MainWindowPage::Home);

  ui_->splitter->insertWidget(0, m_navigationWidget);
}

void MainWindow::initNowPlaying() {
  m_nowPlayingWidget = new NowPlayingWidget(ui_->centralWidget);

  ui_->centralWidget->layout()->addWidget(m_nowPlayingWidget);
  m_nowPlayingWidget->hide();
}

void MainWindow::initPage(MainWindowPage page) {
  static QSet<MainWindowPage> initializedPages;

  if (initializedPages.contains(page)) return;

  static const auto init_page = [](QWidget* page, QWidget* widget) {
    const auto layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(widget);
    page->setLayout(layout);
  };

  switch (page) {
    case MainWindowPage::Home: {
      if (auto* outerLayout = qobject_cast<QVBoxLayout*>(ui_->homePage->layout())) {
        // Clear any existing home content
        while (outerLayout->count()) {
          QLayoutItem* it = outerLayout->takeAt(0);
          if (it->widget()) delete it->widget();
          delete it;
        }

        // ── Scrollable dashboard ──────────────────────────────────────────────
        auto* scroll = new QScrollArea(ui_->homePage);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget();
        auto* lay = new QVBoxLayout(body);
        lay->setContentsMargins(24, 16, 24, 16);
        lay->setSpacing(4);

        m_homeAnnouncedBannerHost = new QWidget(body);
        m_homeAnnouncedBannerHost->setVisible(false);
        auto* announcedLay = new QVBoxLayout(m_homeAnnouncedBannerHost);
        announcedLay->setContentsMargins(0, 0, 0, 12);
        announcedLay->setSpacing(0);
        lay->addWidget(m_homeAnnouncedBannerHost);

        // Stats summary (replaces the old m_homeBodyLabel text blob)
        auto* statsLabel = new QLabel(body);
        statsLabel->setWordWrap(true);
        statsLabel->setTextFormat(Qt::RichText);
        statsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_homeBodyLabel = statsLabel;
        lay->addWidget(statsLabel);

        // ── Section: Up next ─────────────────────────────────────────────────
        lay->addSpacing(12);
        auto* upNextHeader = new QLabel(
            tr("<span style=\"font-size:large\"><b>▶ Up next — episodes ready to watch</b></span>"),
            body);
        upNextHeader->setTextFormat(Qt::RichText);
        m_homeUpNextHeader = upNextHeader;
        lay->addWidget(upNextHeader);

        auto* upNextContainer = new QWidget(body);
        upNextContainer->setLayout(new QVBoxLayout());
        upNextContainer->layout()->setContentsMargins(0, 4, 0, 0);
        upNextContainer->layout()->setSpacing(2);
        m_homeUpNextContainer = upNextContainer;
        lay->addWidget(upNextContainer);

        // ── Section: Upcoming & recently aired ──────────────────────────────
        lay->addSpacing(12);
        auto* recentHeader = new QLabel(tr("<b>Upcoming &amp; recently aired</b>"), body);
        recentHeader->setTextFormat(Qt::RichText);
        m_homeRecentHeader = recentHeader;
        lay->addWidget(recentHeader);

        auto* recentContainer = new QWidget(body);
        recentContainer->setLayout(new QVBoxLayout());
        recentContainer->layout()->setContentsMargins(0, 4, 0, 0);
        recentContainer->layout()->setSpacing(2);
        m_homeRecentContainer = recentContainer;
        lay->addWidget(recentContainer);

        lay->addStretch(1);
        scroll->setWidget(body);
        outerLayout->addWidget(scroll);

        // If scan-on-startup is enabled, defer the first dashboard render until the startup scan
        // completes to avoid individual titles popping in later (prefetch + scan updates).
        if (taiga::settings.scanLibraryOnStartup() && !m_startup_scan_done_) {
          m_defer_home_refresh_until_startup_scan_ = true;
          m_homeBodyLabel->setText(tr("<span style=\"color:#888\">Preparing library…</span>"));
          if (auto* vl = qobject_cast<QVBoxLayout*>(m_homeUpNextContainer->layout())) {
            auto* pending = new QLabel(tr("<span style=\"color:#888\">Preparing library…</span>"),
                                       m_homeUpNextContainer);
            pending->setTextFormat(Qt::RichText);
            vl->addWidget(pending);
          }
        } else {
          refreshHomeDashboard();
        }
      }
      break;
    }

    case MainWindowPage::Search:
      m_searchWidget = new SearchWidget(ui_->searchPage);
      init_page(ui_->searchPage, m_searchWidget);
      break;

    case MainWindowPage::List:
      m_listWidget = new ListWidget(ui_->listPage);
      init_page(ui_->listPage, m_listWidget);
      break;

    case MainWindowPage::History:
      m_historyWidget = new HistoryWidget(ui_->historyPage);
      init_page(ui_->historyPage, m_historyWidget);
      break;

    case MainWindowPage::Library:
      m_libraryWidget = new LibraryWidget(ui_->libraryPage);
      init_page(ui_->libraryPage, m_libraryWidget);
      break;

    case MainWindowPage::Torrents: {
      if (auto* l = qobject_cast<QVBoxLayout*>(ui_->torrentsPage->layout())) {
        while (l->count()) {
          QLayoutItem* it = l->takeAt(0);
          if (it->widget()) delete it->widget();
          delete it;
        }
        auto* title = new QLabel(tr("Torrents"), ui_->torrentsPage);
        QFont tf = title->font();
        tf.setBold(true);
        tf.setPointSizeF(tf.pointSizeF() + 4);
        title->setFont(tf);
        title->setAlignment(Qt::AlignHCenter);
        m_torrentFeedWidget = new TorrentFeedWidget(m_searchBox, ui_->torrentsPage);
        l->addWidget(title);
        l->addWidget(m_torrentFeedWidget, 1);
      }
      break;
    }

    case MainWindowPage::AnnouncedReleases:
      m_announcedReleasesWidget = new AnnouncedReleasesWidget(ui_->announcedPage);
      init_page(ui_->announcedPage, m_announcedReleasesWidget);
      break;

    case MainWindowPage::Profile: {
      if (auto* l = qobject_cast<QVBoxLayout*>(ui_->profilePage->layout())) {
        while (l->count()) {
          QLayoutItem* it = l->takeAt(0);
          if (it->widget()) delete it->widget();
          delete it;
        }
        const QString user =
            QString::fromStdString(taiga::accounts.serviceUsername(taiga::settings.service()));
        auto* title = new QLabel(tr("Profile"), ui_->profilePage);
        QFont tf = title->font();
        tf.setBold(true);
        tf.setPointSizeF(tf.pointSizeF() + 4);
        title->setFont(tf);
        title->setAlignment(Qt::AlignHCenter);
        auto* info = new QLabel(
            tr("<p><b>Service:</b> %1<br/><b>Username:</b> %2</p>")
                .arg(sync::serviceName(sync::currentServiceId()).toHtmlEscaped())
                .arg(user.isEmpty() ? tr("(not set)").toHtmlEscaped() : user.toHtmlEscaped()),
            ui_->profilePage);
        info->setWordWrap(true);
        info->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        info->setTextFormat(Qt::RichText);
        auto* btn = new QPushButton(tr("Open web profile"), ui_->profilePage);
        connect(btn, &QPushButton::clicked, [user]() {
          if (user.isEmpty()) return;
          QUrl url;
          switch (sync::currentServiceId()) {
            case sync::ServiceId::AniList:
              url = QUrl(u"https://anilist.co/user/%1"_s.arg(user));
              break;
            case sync::ServiceId::MyAnimeList:
              url = QUrl(u"https://myanimelist.net/profile/%1"_s.arg(user));
              break;
            case sync::ServiceId::Kitsu:
              url = QUrl(u"https://kitsu.app/users/%1"_s.arg(user));
              break;
            default:
              return;
          }
          QDesktopServices::openUrl(url);
        });
        auto* acct_btn = new QPushButton(tr("Account && credentials…"), ui_->profilePage);
        connect(acct_btn, &QPushButton::clicked, this,
                [this]() { SettingsDialog::showAccounts(this); });
        l->addStretch(1);
        l->addWidget(title);
        l->addWidget(info);
        l->addWidget(btn, 0, Qt::AlignHCenter);
        l->addWidget(acct_btn, 0, Qt::AlignHCenter);
        l->addStretch(2);
      }
      break;
    }
  }

  initializedPages.insert(page);
}

void MainWindow::initStatusbar() {
  ui_->statusbar->setContentsMargins(0, 8, 0, 0);
}

void MainWindow::initToolbar() {
  ui_->toolbar->setIconSize(QSize{24, 24});

  // Menu
  {
    const auto button = static_cast<QToolButton*>(ui_->toolbar->widgetForAction(ui_->actionMenu));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu([this]() {
      auto menu = new QMenu(this);
      auto* view_menu = menu->addMenu(tr("View"));
      view_menu->addAction(ui_->actionToggleToolbar);
      view_menu->addAction(ui_->actionToggleStatusbar);
      view_menu->addAction(ui_->actionToggleNowPlaying);
      menu->addSeparator();
      menu->addMenu(ui_->menuHelp);
      menu->addSeparator();
      menu->addAction(ui_->actionOpenDataFolder);
      menu->addSeparator();
      menu->addAction(ui_->actionExit);
      return menu;
    }());
  }

  // Search box
  {
    m_searchBox = new QLineEdit();
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setFixedWidth(320);

    const auto before = ui_->actionSettings;
    const auto insertSpacer = [this](QAction* before) {
      ui_->toolbar->insertWidget(before, [this]() {
        auto spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return spacer;
      }());
    };

    insertSpacer(before);
    m_searchBoxAction = ui_->toolbar->insertWidget(before, m_searchBox);
    insertSpacer(before);

    connect(m_searchBox, &QLineEdit::textChanged, this,
            &MainWindow::routeToolbarSearchToActivePage);
    connect(m_searchBox, &QLineEdit::returnPressed, this, [this]() {
      if (m_activePage != MainWindowPage::Torrents) return;
      if (m_torrentFeedWidget) m_torrentFeedWidget->runSearch();
    });
  }

  // Auto-download action — visible on every page, inline with toolbar actions.
  {
    if (!m_autoDownloadAction) {
      m_autoDownloadAction = new QAction(tr("Auto-download"), this);
      m_autoDownloadAction->setToolTip(tr("Download new episodes for Watching titles"));
      // Use a known theme key (avoid blank icon if key is missing).
      m_autoDownloadAction->setIcon(theme.getIcon("cloud_download"));
      connect(m_autoDownloadAction, &QAction::triggered, this, [this]() {
        cancelDelayedAutoDownload(tr("Manual auto-download"));
        runAutoDownload(false);
      });
    }
    // Place it between Sync and Scan.
    if (ui_->actionScanAvailableEpisodes) {
      ui_->toolbar->insertAction(ui_->actionScanAvailableEpisodes, m_autoDownloadAction);
    } else {
      ui_->toolbar->addAction(m_autoDownloadAction);
    }
    if (auto* btn =
            qobject_cast<QToolButton*>(ui_->toolbar->widgetForAction(m_autoDownloadAction))) {
      btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }

    // Lightweight timer to refresh button text/countdown.
    if (!m_home_countdown_timer_) {
      m_home_countdown_timer_ = new QTimer(this);
      m_home_countdown_timer_->setInterval(10 * 1000);
      connect(m_home_countdown_timer_, &QTimer::timeout, this,
              &MainWindow::updateAutoDownloadCountdownLabel);
      m_home_countdown_timer_->start();
    }
    updateAutoDownloadCountdownLabel();

    if (!m_delayed_autodl_timer_) {
      m_delayed_autodl_timer_ = new QTimer(this);
      m_delayed_autodl_timer_->setSingleShot(true);
      connect(m_delayed_autodl_timer_, &QTimer::timeout, this,
              &MainWindow::beginDelayedAutoDownloadRun);
    }
    if (!m_release_event_timer_) {
      m_release_event_timer_ = new QTimer(this);
      m_release_event_timer_->setTimerType(Qt::VeryCoarseTimer);
      m_release_event_timer_->setInterval(20 * 1000);
      connect(m_release_event_timer_, &QTimer::timeout, this,
              &MainWindow::checkWatchingReleaseEvent);
      m_release_event_timer_->start();
    }
  }
}

void MainWindow::initTrayIcon() {
  auto menu = new QMenu(this);
  menu->addAction(ui_->actionDisplayWindow);
  menu->setDefaultAction(ui_->actionDisplayWindow);
  menu->addSeparator();
  menu->addAction(ui_->actionSynchronize);
  menu->addAction(ui_->actionScanAvailableEpisodes);
  menu->addSeparator();
  menu->addAction(ui_->actionSettings);
  menu->addAction(ui_->actionOpenDataFolder);
  menu->addSeparator();
  menu->addAction(ui_->actionExit);

  m_trayIcon = new TrayIcon(this, windowIcon(), menu);

  connect(m_trayIcon, &TrayIcon::activated, this, &MainWindow::displayWindow);
  connect(m_trayIcon, &TrayIcon::messageClicked, this, &MainWindow::displayWindow);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (startupBlockingActive()) {
    m_welcome_prompt_deferred_ = true;
    return;
  }
  if (m_welcomeCheckScheduled) return;
  m_welcomeCheckScheduled = true;
  QTimer::singleShot(400, this, &MainWindow::maybeShowWelcomeSetup);
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::WindowStateChange) {
    if (isMinimized() && taiga::settings.minimizeToTray() &&
        QSystemTrayIcon::isSystemTrayAvailable()) {
      QTimer::singleShot(0, this, [this] {
        if (!taiga::settings.minimizeToTray() || !QSystemTrayIcon::isSystemTrayAvailable()) return;
        hide();
      });
    }
  }

  QMainWindow::changeEvent(event);

  if (event->type() == QEvent::ActivationChange) {
    if (!isActiveWindow()) {
      m_lastDeactivateMs = QDateTime::currentMSecsSinceEpoch();
      return;
    }
    trySyncAfterFocusReturn();
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  taiga::session.setMainWindowGeometry(saveGeometry());
  taiga::session.setMainWindowSplitterState(ui_->splitter->saveState());
  if (m_listWidget) m_listWidget->saveState();
  if (m_searchWidget) m_searchWidget->saveState();
  if (m_torrentFeedWidget) m_torrentFeedWidget->saveSessionState();
  sync::flushPendingListSaves();
  // Persist last-known library index so next startup can load it instantly.
  track::saveLibraryEpisodeIndexCache();

  if (event->spontaneous() && taiga::settings.closeToTray() &&
      QSystemTrayIcon::isSystemTrayAvailable()) {
    event->ignore();
    hide();
    return;
  }
  event->accept();
}

void MainWindow::refreshLibraryRootsFromSettings() {
  if (m_libraryWidget) m_libraryWidget->refreshRootsFromSettings();
  track::libraryFolderWatcher()->refreshFromSettings();
}

void MainWindow::runInteractiveLibraryScan() {
  runLibraryScan(false, LibraryScanReason::Manual);
}

void MainWindow::addNewFolder() {
  constexpr auto options =
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;

  const auto directory = QFileDialog::getExistingDirectory(this, tr("Add New Folder"), "", options);

  if (directory.isEmpty()) return;

  auto folders = taiga::settings.libraryFolders();
  const auto path = directory.toStdString();
  if (std::ranges::find(folders, path) != folders.end()) return;

  folders.push_back(path);
  taiga::settings.setLibraryFolders(std::move(folders));
  refreshLibraryRootsFromSettings();
}

void MainWindow::navigateTo(MainWindowPage page) {
  if (const auto item = m_navigationWidget->findItemByPage(page)) {
    m_navigationWidget->setCurrentItem(item);
  }
}

void MainWindow::openTorrentSearchInApp(const QString& title, const QString& fallback,
                                        const int anime_id) {
  navigateTo(MainWindowPage::Torrents);
  auto stripSubtitle = [](QString q) -> QString {
    // Strip episode-title suffix separated by " - " (e.g. "Anime (Special) - Arc Title").
    // RSS sites don't index episode subtitles, so searching the full string gives 0 results.
    const int dashIdx = q.indexOf(QStringLiteral(" - "));
    if (dashIdx > 0) q = q.left(dashIdx).trimmed();
    return q;
  };
  if (m_searchBox && !title.trimmed().isEmpty()) {
    m_searchBox->setText(stripSubtitle(title.trimmed()));
  }
  if (m_torrentFeedWidget) {
    m_torrentFeedWidget->setManualSearchAnimeContext(anime_id);
    // Register fallback before search so onFetchFinished can auto-retry.
    const QString fb = fallback.trimmed();
    m_torrentFeedWidget->setSearchFallback(fb.isEmpty() ? QString{} : stripSubtitle(fb));
    m_torrentFeedWidget->runSearch();
  }
  // Persist the torrent query so it's restored when switching back to this page.
  m_pageSearchTexts_[static_cast<int>(MainWindowPage::Torrents)] =
      m_searchBox ? m_searchBox->text() : QString{};
}

void MainWindow::applyMainPage(const MainWindowPage page) {
  // Save the current search-box text for the page we're leaving, so navigating
  // back restores it exactly as the user left it.
  if (m_searchBox) {
    m_pageSearchTexts_[static_cast<int>(m_activePage)] = m_searchBox->text();
  }

  m_activePage = page;
  initPage(page);

  // Home page doesn't use the toolbar search (it doesn't route anywhere),
  // so hide it there to avoid confusing "does nothing" behavior.
  // Profile page currently has no filterable content either, so hide it there too.
  if (m_searchBox) {
    m_searchBox->setVisible(page != MainWindowPage::Home && page != MainWindowPage::Profile);
  }
  if (m_searchBoxAction) {
    m_searchBoxAction->setVisible(page != MainWindowPage::Home && page != MainWindowPage::Profile);
  }

  // Restore the saved text for the new page.  For Torrents, fall back to the
  // persisted last-query session value if no text was ever saved this session.
  if (m_searchBox) {
    const QString saved = m_pageSearchTexts_.value(static_cast<int>(page));
    if (!saved.isEmpty()) {
      m_searchBox->blockSignals(true);
      m_searchBox->setText(saved);
      m_searchBox->blockSignals(false);
    } else if (page == MainWindowPage::Torrents) {
      const QString last = taiga::session.torrentPanelLastQuery();
      if (!last.isEmpty()) {
        m_searchBox->blockSignals(true);
        m_searchBox->setText(last);
        m_searchBox->blockSignals(false);
      } else {
        m_searchBox->blockSignals(true);
        m_searchBox->clear();
        m_searchBox->blockSignals(false);
      }
    } else {
      m_searchBox->blockSignals(true);
      m_searchBox->clear();
      m_searchBox->blockSignals(false);
    }
  }

  ui_->statusbar->clearMessage();
  ui_->stackedWidget->setCurrentIndex(static_cast<int>(page));
  updateToolbarSearchPlaceholder();
  routeToolbarSearchToActivePage();
  if (page == MainWindowPage::Home) {
    refreshHomeDashboard();
  }
  if (page == MainWindowPage::AnnouncedReleases && m_announcedReleasesWidget) {
    m_announcedReleasesWidget->refresh();
  }
}

void MainWindow::recordNavHistory(const MainWindowPage page) {
  if (m_navHistoryPos >= 0 && m_navHistoryPos < static_cast<int>(m_navStack.size()) &&
      m_navStack[m_navHistoryPos] == page) {
    return;
  }
  if (m_navHistoryPos + 1 < static_cast<int>(m_navStack.size())) {
    m_navStack.resize(m_navHistoryPos + 1);
  }
  m_navStack.push_back(page);
  m_navHistoryPos = static_cast<int>(m_navStack.size()) - 1;
}

void MainWindow::setPage(const MainWindowPage page) {
  if (!m_navHistorySuppress) {
    recordNavHistory(page);
  }
  applyMainPage(page);
  updateNavHistoryActions();
}

void MainWindow::goBackNavigation() {
  if (m_navHistoryPos <= 0) return;
  --m_navHistoryPos;
  const MainWindowPage page = m_navStack[m_navHistoryPos];
  m_navHistorySuppress = true;
  QSignalBlocker blocker(m_navigationWidget);
  navigateTo(page);
  applyMainPage(page);
  m_navHistorySuppress = false;
  updateNavHistoryActions();
}

void MainWindow::goForwardNavigation() {
  if (m_navHistoryPos + 1 >= static_cast<int>(m_navStack.size())) return;
  ++m_navHistoryPos;
  const MainWindowPage page = m_navStack[m_navHistoryPos];
  m_navHistorySuppress = true;
  QSignalBlocker blocker(m_navigationWidget);
  navigateTo(page);
  applyMainPage(page);
  m_navHistorySuppress = false;
  updateNavHistoryActions();
}

void MainWindow::updateNavHistoryActions() {
  ui_->actionBack->setEnabled(m_navHistoryPos > 0);
  ui_->actionForward->setEnabled(m_navHistoryPos + 1 < static_cast<int>(m_navStack.size()));
}

void MainWindow::initFeatureToggleActions() {
  {
    const QSignalBlocker b1(ui_->actionToggleDetection);
    const QSignalBlocker b3(ui_->actionToggleSynchronization);
    ui_->actionToggleDetection->setChecked(taiga::settings.mediaDetectionEnabled());
    ui_->actionToggleSynchronization->setChecked(taiga::settings.listSynchronizationEnabled());
  }

  connect(ui_->actionToggleDetection, &QAction::toggled, this, [](const bool on) {
    taiga::settings.setMediaDetectionEnabled(on);
    track::media::detection()->setPollingEnabled(taiga::settings.mediaDetectionPollingActive());
  });
  connect(ui_->actionToggleSynchronization, &QAction::toggled, this, [this](const bool on) {
    taiga::settings.setListSynchronizationEnabled(on);
    refreshSyncActionState();
    updateNoStartupSyncBanner();
  });

  refreshSyncActionState();
  updateNavHistoryActions();
}

void MainWindow::refreshSyncActionState() {
  const bool can_sync = sync::currentServiceId() != sync::ServiceId::Unknown &&
                        taiga::settings.listSynchronizationEnabled();
  ui_->actionSynchronize->setEnabled(can_sync && !m_list_sync_in_progress_);
}

void MainWindow::refreshServiceDependentUi() {
  const QString svc = sync::serviceName(sync::currentServiceId());
  ui_->actionSynchronize->setToolTip(synchronizeWithServiceToolTip(svc));
  ui_->actionSynchronize->setStatusTip(synchronizeDownloadListStatusTip(svc));
  refreshSyncActionState();
  updateToolbarSearchPlaceholder();
  refreshNavigationSidebar();
  // Home / Announced releases refresh only when their data can change (e.g. service or list
  // metadata), not on every Settings OK — callers invoke refreshHomeDashboard() / announced
  // refresh explicitly when needed.
  updateTrayTooltip();
  if (m_listWidget) m_listWidget->refreshListTitleDisplay();
  if (m_searchWidget) m_searchWidget->refreshListTitleDisplay();
  updateNoStartupSyncBanner();
}

void MainWindow::refreshNavigationSidebar() {
  if (!m_navigationWidget || m_watch_next_modal_open_) return;

  const MainWindowPage page = m_activePage;
  std::optional<anime::list::Status> list_status;
  if (page == MainWindowPage::List && m_listWidget) {
    const AnimeListStatusFilter lf = m_listWidget->currentListSidebarFilter();
    if (!lf.anyStatus && lf.status.has_value()) {
      list_status = static_cast<anime::list::Status>(*lf.status);
    }
  }

  refreshNavigationSidebarPreserving(m_navigationWidget, page, list_status);
}

void MainWindow::refreshAnimeListProgressDecorations() {
  if (m_listWidget) m_listWidget->refreshProgressColumnDisplay();
  if (m_searchWidget) m_searchWidget->refreshProgressColumnDisplay();
}

void MainWindow::refreshAnimeListNewEpisodeHighlight() {
  if (m_listWidget) m_listWidget->refreshNewEpisodeHighlightDisplay();
  if (m_searchWidget) m_searchWidget->refreshNewEpisodeHighlightDisplay();
}

void MainWindow::refreshMatureContentSurfaces() {
  if (m_listWidget) m_listWidget->refreshMatureContentRowFilter();
  if (m_searchWidget) m_searchWidget->refreshMatureContentRowFilter();
  if (m_historyWidget) m_historyWidget->refreshMatureContentRowFilter();
  if (m_announcedReleasesWidget) m_announcedReleasesWidget->refresh();
  refreshHomeDashboard();
}

void MainWindow::initNoStartupSyncBanner() {
  if (m_noStartupSyncBannerHost) return;
  auto* vl = qobject_cast<QVBoxLayout*>(ui_->centralWidget->layout());
  if (!vl) return;

  // Single frame (no extra wrapper): wrapped QLabel + QHBoxLayout otherwise report a huge
  // minimum height before width is known, which steals vertical space from the splitter.
  auto* frame = new QFrame(ui_->centralWidget);
  m_noStartupSyncBannerHost = frame;
  frame->setObjectName(QStringLiteral("noStartupSyncBanner"));
  frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  frame->setStyleSheet(QStringLiteral(
      "QFrame#noStartupSyncBanner{border-bottom:1px solid palette(mid); padding:8px 12px; "
      "background: palette(toolTipBase); color: palette(toolTipText);}"));
  auto* hl = new QHBoxLayout(frame);
  hl->setContentsMargins(10, 8, 10, 8);
  hl->setSpacing(12);

  m_noStartupSyncBannerMessage = new QLabel(frame);
  m_noStartupSyncBannerMessage->setWordWrap(true);
  m_noStartupSyncBannerMessage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  hl->addWidget(m_noStartupSyncBannerMessage, 1);

  auto* btn = new QPushButton(tr("Synchronize now"), frame);
  btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  btn->setToolTip(synchronizeWithServiceToolTip(sync::serviceName(sync::currentServiceId())));
  connect(btn, &QPushButton::clicked, this, [this]() { startListSynchronization(true); });
  hl->addWidget(btn, 0, Qt::AlignVCenter);

  vl->insertWidget(0, frame);
  if (const int splitterIdx = vl->indexOf(ui_->splitter); splitterIdx >= 0) {
    vl->setStretch(splitterIdx, 1);
  }
  for (int i = 0; i < vl->count(); ++i) {
    if (QWidget* w = vl->itemAt(i)->widget(); w && w != ui_->splitter) {
      vl->setStretch(i, 0);
    }
  }
  updateNoStartupSyncBanner();
}

void MainWindow::updateNoStartupSyncBanner() {
  if (!m_noStartupSyncBannerHost || !m_noStartupSyncBannerMessage) return;
  const bool show = taiga::settings.listSynchronizationEnabled() &&
                    !taiga::settings.syncAutoOnStart() && !m_startup_sync_done_;
  if (show) {
    m_noStartupSyncBannerMessage->setText(
        tr("Synchronizing the anime list when Taiga starts is turned off in Settings. Your list, "
           "search, and related data may be incomplete until you synchronize with %1.")
            .arg(sync::serviceName(sync::currentServiceId())));
  }
  m_noStartupSyncBannerHost->setVisible(show);
}

void MainWindow::applyListSynchronizationToggleFromSettings() {
  const QSignalBlocker b(ui_->actionToggleSynchronization);
  ui_->actionToggleSynchronization->setChecked(taiga::settings.listSynchronizationEnabled());
  updateNoStartupSyncBanner();
}

void MainWindow::applyMediaDetectionToggleFromSettings() {
  const QSignalBlocker b(ui_->actionToggleDetection);
  ui_->actionToggleDetection->setChecked(taiga::settings.mediaDetectionEnabled());
}

std::optional<int> MainWindow::animeIdForPlaybackContext() const {
  if (m_activePage == MainWindowPage::Library && m_libraryWidget) {
    if (const auto id = m_libraryWidget->selectedRecognizedAnimeId()) {
      return id;
    }
  }
  if (m_activePage == MainWindowPage::List && m_listWidget) {
    if (const auto id = m_listWidget->selectedAnimeId()) {
      return id;
    }
  }
  if (const auto ep = track::media::detection()->getCurrentEpisode()) {
    const int id = ep->animeId();
    if (id != anime::kUnknownId) return id;
  }
  return std::nullopt;
}

void MainWindow::exportAnimeListMarkdown() {
  const QString def =
      QDir::home().filePath(u"animelist_%1.md"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
  const QString path = QFileDialog::getSaveFileName(this, tr("Export anime list as Markdown"), def,
                                                    tr("Markdown (*.md);;All files (*)"));
  if (path.isEmpty()) return;
  if (anime::list::exportAsMarkdown(path.toStdString())) {
    enqueueStatusMessage(listExportSucceededMessage(path), false);
  } else {
    QMessageBox::warning(this, tr("Taiga"), listExportWriteFailedMessage());
  }
}

void MainWindow::exportAnimeListXml() {
  const QString def =
      QDir::home().filePath(u"animelist_%1.xml"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export anime list as MyAnimeList XML"), def, tr("XML (*.xml);;All files (*)"));
  if (path.isEmpty()) return;
  if (anime::list::exportAsXml(path.toStdString())) {
    enqueueStatusMessage(listExportSucceededMessage(path), false);
  } else {
    QMessageBox::warning(this, tr("Taiga"), listExportWriteFailedMessage());
  }
}

void MainWindow::exportAnimeListCsv() {
  const QString def =
      QDir::home().filePath(u"animelist_%1.csv"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
  const QString path = QFileDialog::getSaveFileName(this, tr("Export anime list as CSV"), def,
                                                    tr("CSV (*.csv);;All files (*)"));
  if (path.isEmpty()) return;
  if (anime::list::exportAsCsv(path.toStdString())) {
    enqueueStatusMessage(listExportSucceededMessage(path), false);
  } else {
    QMessageBox::warning(this, tr("Taiga"), listExportWriteFailedMessage());
  }
}

void MainWindow::importAnimeListMalXml() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Import MyAnimeList XML"), {},
                                                    tr("XML (*.xml);;All files (*)"));
  if (path.isEmpty()) return;

  const auto r = anime::list::importFromMyAnimeListXml(path.toStdString());
  if (!r.ok()) {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not import: %1").arg(r.error));
    return;
  }

  refreshNavigationSidebar();
  if (m_listWidget) m_listWidget->reloadAnimeList();
  if (m_searchWidget) m_searchWidget->reloadAnimeList();
  refreshHomeDashboard();

  QString msg = tr("Imported %1 anime list row(s).").arg(r.updated);
  if (r.skipped_unknown_anime > 0) {
    msg += u" "_s + tr("Skipped %1 (no local anime with that id).").arg(r.skipped_unknown_anime);
  }
  if (r.skipped_invalid_row > 0) {
    msg += u" "_s + tr("Skipped %1 invalid row(s).").arg(r.skipped_invalid_row);
  }
  if (sync::currentServiceId() != sync::ServiceId::MyAnimeList && r.skipped_unknown_anime > 0) {
    msg += u" "_s + tr("(AniList/Kitsu use different media IDs than MAL exports.)");
  }
  enqueueStatusMessage(msg, false);
}

void MainWindow::playNextEpisodeFromMenu() {
  if (const auto id = animeIdForPlaybackContext()) {
    if (track::playNextEpisode(*id)) {
      enqueueStatusMessage(playingNextEpisodeStatusMessage(), false);
      return;
    }
    QMessageBox::information(this, tr("Taiga"), playNextEpisodeNotFoundMessage());
    return;
  }
  QMessageBox::information(
      this, tr("Taiga"),
      tr("Select a title on your anime list, a recognized file in the Library, or start playback "
         "with media detection enabled so Taiga knows which title to use."));
}

void MainWindow::playRandomAnimeFromMenu() {
  if (track::playRandomFromListing()) {
    enqueueStatusMessage(tr("Playing a random title from your list…"), false);
    return;
  }
  QMessageBox::information(
      this, tr("Taiga"),
      tr("Could not play a random episode (empty list or no matching video files in library "
         "folders)."));
}

void MainWindow::updateTitle() {
  auto title = u"Taiga"_s;

  if (taiga::app()->isDebug()) {
    title += u" [debug]"_s;
  }

  setWindowTitle(title);
}

void MainWindow::displayWindow() {
  if (isHidden()) {
    show();
  }
  raise();
  setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  activateWindow();
}

void MainWindow::about() {
  displayAboutDialog(this);
}

void MainWindow::donate() const {
  QDesktopServices::openUrl(QUrl("https://taiga.moe/#donate"));
}

void MainWindow::support() const {
  QDesktopServices::openUrl(QUrl("https://taiga.moe/#support"));
}

void MainWindow::profile() {
  setPage(MainWindowPage::Profile);
  m_navigationWidget->setCurrentIndex({});
}

void MainWindow::statistics() {
  StatsDialog::show(this);
}

void MainWindow::startListSynchronization(const bool queue_if_busy) {
  if (sync::currentServiceId() == sync::ServiceId::Unknown) {
    return;
  }
  if (!taiga::settings.listSynchronizationEnabled()) {
    enqueueStatusMessage(synchronizationDisabledStatusHint(), false);
    return;
  }
  if (m_list_sync_in_progress_) {
    if (queue_if_busy) m_list_sync_queued_ = true;
    return;
  }
  m_list_sync_in_progress_ = true;
  refreshSyncActionState();
  pauseAnnouncedRelatedRefresh();

  QPointer<MainWindow> guard(this);
  enqueueStatusMessage(synchronizingWithServiceStatus(sync::serviceName(sync::currentServiceId())),
                       false);

  sync::fetchListEntries([guard](const bool ok, const QString& message) {
    if (!guard) return;
    QMetaObject::invokeMethod(guard.data(), "handleListSyncFinished", Qt::QueuedConnection,
                              Q_ARG(bool, ok), Q_ARG(QString, message));
  });
}

void MainWindow::handleListSyncFinished(bool ok, QString message) {
  m_list_sync_in_progress_ = false;
  refreshSyncActionState();
  if (m_post_sync_auto_download_) {
    m_post_sync_auto_download_ = false;
    if (ok) {
      QTimer::singleShot(0, this, [this]() { runAutoDownload(true); });
    }
  }
  if (ok) {
    // Invalidate the recognition cache so newly-synced titles are indexed
    // on the next library scan / media-detection lookup.
    track::recognition::cache()->clear();
    refreshNavigationSidebar();
    if (m_listWidget) m_listWidget->reloadAnimeList();
    if (m_searchWidget) m_searchWidget->reloadAnimeList();
    refreshHomeDashboard();
    if (m_announcedReleasesWidget) m_announcedReleasesWidget->refresh();
    enqueueStatusMessage(message.isEmpty() ? synchronizedDoneStatus() : message, false);

    // On the very first sync after startup, trigger a silent auto-download
    // so new episodes are picked up right away.
    if (!m_startup_sync_done_) {
      m_startup_sync_done_ = true;
      if (startupBlockingActive()) {
        // Startup pipeline is handled externally (Application splash). Avoid kicking off
        // additional chained startup tasks here so the pipeline can run deterministically.
        updateNoStartupSyncBanner();
        emit listSyncFinished(ok, ok ? (message.isEmpty() ? synchronizedDoneStatus() : message)
                                     : synchronizationFailedStatus(message));
        finalizeListSyncSession();
        return;
      }
      // Startup order (when enabled): scan → sync → scan → auto-download.
      // We do an immediate scan in init() for Home uptime, then scan again after sync so
      // recognition uses the updated title DB before auto-download runs.
      if (taiga::settings.scanLibraryOnStartup()) {
        // Don't start auto-download until the scan completes, otherwise the library index can be
        // stale and we'd re-download episodes that are already on disk.
        m_startup_auto_download_pending_ = true;
        runLibraryScan(true, LibraryScanReason::StartupPostSync);
        finalizeListSyncSession();
        updateNoStartupSyncBanner();
        return;
      }
      QTimer::singleShot(0, this, [this]() { runAutoDownload(true); });
    }

    if (m_delayed_autodl_after_sync_pending_) {
      m_delayed_autodl_after_sync_pending_ = false;
      // Always scan before a background auto-download so the episode index isn't stale.
      m_delayed_autodl_after_scan_pending_ = true;
      runLibraryScan(true, LibraryScanReason::DelayedAutoDownload);
    }
  } else {
    enqueueStatusMessage(synchronizationFailedStatus(message), true);
    if (m_delayed_autodl_after_sync_pending_) {
      m_delayed_autodl_after_sync_pending_ = false;
      // If sync fails, still try scan→auto-download to keep behavior resilient.
      m_delayed_autodl_after_scan_pending_ = true;
      runLibraryScan(true, LibraryScanReason::DelayedAutoDownload);
    }
  }

  updateNoStartupSyncBanner();
  emit listSyncFinished(ok, ok ? (message.isEmpty() ? synchronizedDoneStatus() : message)
                               : synchronizationFailedStatus(message));
  finalizeListSyncSession();
}

void MainWindow::finalizeListSyncSession() {
  if (m_list_sync_queued_) {
    m_list_sync_queued_ = false;
    QTimer::singleShot(0, this, [this]() { startListSynchronization(false); });
    return;
  }
  scheduleAnnouncedRelatedResumeAfterSync();
}

void MainWindow::showUserFeedback(QString message, bool error) {
  enqueueStatusMessage(message, error);
  if (error && !startupBlockingActive()) QMessageBox::warning(this, tr("Taiga"), message);
}

void MainWindow::enqueueStatusMessage(QString message, const bool error) {
  if (message.trimmed().isEmpty()) return;
  m_status_message_queue_.enqueue(StatusMessage{.text = std::move(message), .error = error});
  if (m_status_message_timer_ && !m_status_message_timer_->isActive()) {
    showNextQueuedStatusMessage();
  }
}

void MainWindow::showNextQueuedStatusMessage() {
  if (!statusBar()) return;
  if (m_status_message_queue_.isEmpty()) return;
  const StatusMessage m = m_status_message_queue_.dequeue();
  statusBar()->showMessage(m.text, 4000);
  if (m_status_message_timer_) m_status_message_timer_->start(4000);
}

void MainWindow::updateToolbarSearchPlaceholder() {
  if (!m_searchBox) return;
  const QString svc = sync::serviceName(sync::currentServiceId());
  switch (m_activePage) {
    case MainWindowPage::List:
      m_searchBox->setPlaceholderText(tr("Filter list…"));
      break;
    case MainWindowPage::Search:
      m_searchBox->setPlaceholderText(tr("Filter search results…"));
      break;
    case MainWindowPage::Torrents:
      m_searchBox->setPlaceholderText(tr("Anime title — Enter or F5 fetches RSS…"));
      break;
    case MainWindowPage::AnnouncedReleases:
      m_searchBox->setPlaceholderText(tr("Filter announced titles…"));
      break;
    case MainWindowPage::Home:
      m_searchBox->setPlaceholderText(tr("Filter list (open Anime list from the sidebar)…"));
      break;
    default:
      m_searchBox->setPlaceholderText(tr("Search or filter… (%1)").arg(svc));
      break;
  }
}

void MainWindow::maybeShowWelcomeSetup() {
  if (taiga::settings.welcomeSetupPromptDismissed()) return;
  if (sync::remoteListAccessConfigured()) {
    taiga::settings.setWelcomeSetupPromptDismissed(true);
    return;
  }
  const auto answer =
      QMessageBox::question(this, tr("Welcome to Taiga"),
                            tr("No sync account is configured for the active service yet. Open "
                               "Settings to add credentials?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  taiga::settings.setWelcomeSetupPromptDismissed(true);
  if (answer == QMessageBox::Yes) SettingsDialog::showAccounts(this);
}

void MainWindow::trySyncAfterFocusReturn() {
  if (!taiga::settings.syncOnWindowFocus()) return;
  if (!taiga::settings.listSynchronizationEnabled()) return;
  if (sync::currentServiceId() == sync::ServiceId::Unknown) return;
  if (m_lastDeactivateMs <= 0) return;
  const qint64 idle_ms = QDateTime::currentMSecsSinceEpoch() - m_lastDeactivateMs;
  const qint64 need_ms =
      static_cast<qint64>(taiga::settings.syncOnWindowFocusMinutes()) * 60LL * 1000LL;
  if (idle_ms < need_ms) return;
  m_lastDeactivateMs = 0;
  startListSynchronization();
}

void MainWindow::checkForUpdatesManually() {
  taiga::checkForUpdates(this, false);
}

void MainWindow::runLibraryScan(const bool startup_silent, const LibraryScanReason reason) {
  if (m_library_scan_in_progress_) return;
  constexpr int kMaxEntries = 50'000;
  const auto reasonLabel = [reason]() -> QString {
    switch (reason) {
      case LibraryScanReason::StartupPreSync:
        return QStringLiteral("startup-pre-sync");
      case LibraryScanReason::StartupPostSync:
        return QStringLiteral("startup-post-sync");
      case LibraryScanReason::DelayedAutoDownload:
        return QStringLiteral("autodl-delayed");
      case LibraryScanReason::Watcher:
        return QStringLiteral("watcher");
      case LibraryScanReason::Manual:
      default:
        return QStringLiteral("manual");
    }
  }();
  const auto folders = taiga::settings.libraryFolders();
  if (folders.empty()) {
    if (!startup_silent) {
      QMessageBox::information(this, tr("Taiga"), tr("No library folders are configured."));
    }
    emit libraryScanFinished(reasonLabel, tr("Library scan: no library folders are configured."));
    return;
  }
  enqueueStatusMessage(tr("Scanning library folders…"), false);

  // Library scans can take seconds+; always run them off the UI thread.
  m_library_scan_in_progress_ = true;
  QPointer<MainWindow> guard(this);

  struct ScanJob final : public QRunnable {
    QPointer<MainWindow> w;
    std::vector<std::string> folders;
    QString reasonLabel;
    void run() override {
      constexpr int kMaxEntriesLocal = 50'000;
      const bool allowApply = (reasonLabel != QStringLiteral("startup-pre-sync"));
      const track::LibraryScanSummary sum =
          track::scanLibraryFolders(folders, kMaxEntriesLocal, /*allow_regress_apply=*/allowApply);
      if (!w) return;
      QMetaObject::invokeMethod(
          w.data(),
          [w = w, sum, reasonLabel = reasonLabel]() {
            if (!w) return;
            w->m_library_scan_in_progress_ = false;
            QString msg = QObject::tr(
                              "Library scan: %1 video file(s), %2 recognized, %3 series with "
                              "local episodes "
                              "(visited %4 paths).")
                              .arg(sum.video_files)
                              .arg(sum.recognized)
                              .arg(sum.series_with_local_episodes)
                              .arg(sum.entries_visited);
            if (sum.entries_visited >= kMaxEntriesLocal) {
              msg += QObject::tr(" Scan stopped at safety limit.");
            }
            w->enqueueStatusMessage(msg, false);
            emit w->libraryScanFinished(reasonLabel, msg);

            // Reconcile "delete after watched": remove files for already-watched episodes still on
            // disk. Skip the startup-pre-sync scan, where list progress may not be loaded yet.
            if (reasonLabel != QStringLiteral("startup-pre-sync")) {
              const int auto_deleted = track::deleteAlreadyWatchedEpisodesOnDisk();
              if (auto_deleted > 0) {
                track::appendLibraryEpisodeIndexCacheDebugLine(
                    QStringLiteral("auto-delete: removed %1 watched file(s) still on disk")
                        .arg(auto_deleted));
              }
            }

            w->refreshAnimeListProgressDecorations();
            w->refreshAnimeListNewEpisodeHighlight();
            if (w->m_defer_home_refresh_until_startup_scan_) {
              w->m_defer_home_refresh_until_startup_scan_ = false;
            }
            w->refreshHomeDashboard();

            track::appendLibraryEpisodeIndexCacheDebugLine(
                QStringLiteral("scan: end (%1) => %2 series, %3 recognized")
                    .arg(reasonLabel)
                    .arg(sum.series_with_local_episodes)
                    .arg(sum.recognized));
            const bool allowRegress = (reasonLabel != QStringLiteral("startup-pre-sync"));
            track::saveLibraryEpisodeIndexCacheAfterScan(reasonLabel, allowRegress);
            // Torrent auto-cleanup runs after scans (safe only under torrent download root).
            gui::torrentAutoCleanup()->runCleanupAfterLibraryScan(reasonLabel);
            if (w->m_startup_auto_download_pending_) {
              w->m_startup_auto_download_pending_ = false;
              QTimer::singleShot(0, w.data(), [w]() {
                if (w) w->runAutoDownload(true);
              });
            }
            if (w->m_delayed_autodl_after_scan_pending_) {
              w->m_delayed_autodl_after_scan_pending_ = false;
              QTimer::singleShot(0, w.data(), [w]() {
                if (w) w->runAutoDownload(true);
              });
            }
          },
          Qt::QueuedConnection);
    }
  };

  auto* job = new ScanJob();
  job->setAutoDelete(true);
  job->w = guard;
  job->folders = folders;
  job->reasonLabel = reasonLabel;
  QThreadPool::globalInstance()->start(job);
}

void MainWindow::maybeNotifyMediaDetectionBalloon(const std::optional<track::Episode>& episode) {
  if (!m_trayIcon || !QSystemTrayIcon::supportsMessages()) return;
  if (!taiga::settings.mediaDetectionPollingActive()) return;

  QString sig;
  QString title;
  QString body;
  if (episode && episode->animeId() > 0) {
    if (!taiga::settings.mediaNotifyRecognizedBalloon()) return;
    if (const auto item = anime::db.item(episode->animeId())) {
      const QString epn =
          QString::fromStdString(episode->element(anitomy::ElementKind::Episode, std::string{"?"}));
      sig = QStringLiteral("ok:%1:%2").arg(episode->animeId()).arg(epn);
      title = tr("Now playing");
      body = taiga::tray_balloon::formatTemplate(
          QString::fromStdString(taiga::settings.mediaNotifyBalloonFormatRecognized()), *episode,
          item);
      if (body.trimmed().isEmpty()) {
        const QString displayTitle = QString::fromStdString(
            anime::preferredListTitleString(*item, anime::TitleLanguage::English));
        body =
            item->episode_count > 1 ? tr("%1 — episode %2").arg(displayTitle, epn) : displayTitle;
      }
    } else {
      return;
    }
  } else if (episode) {
    if (!taiga::settings.mediaNotifyUnrecognizedBalloon()) return;
    const QString raw = QString::fromStdString(episode->element(anitomy::ElementKind::Title));
    if (raw.isEmpty()) return;
    sig = QStringLiteral("bad:%1").arg(raw);
    title = tr("Unrecognized media");
    body = taiga::tray_balloon::formatTemplate(
        QString::fromStdString(taiga::settings.mediaNotifyBalloonFormatUnrecognized()), *episode,
        nullptr);
    if (body.trimmed().isEmpty()) {
      body = tr("Could not match: %1").arg(raw);
    }
    if (taiga::settings.mediaNotifyBalloonUnrecognizedAppendHint()) {
      body += QLatin1Char('\n');
      body += tr("Click here to view similar titles for this anime.");
    }
  } else {
    m_last_media_balloon_sig_.clear();
    return;
  }
  if (sig == m_last_media_balloon_sig_) return;
  m_last_media_balloon_sig_ = sig;
  postTrayMessage(title, body);
}

void MainWindow::updateTrayTooltip() {
  if (!m_trayIcon) return;

  QString line1;
  const auto svc_id = sync::currentServiceId();
  const QString svc = sync::serviceName(svc_id);
  if (svc_id != sync::ServiceId::Unknown && !svc.isEmpty()) {
    line1 = tr("Taiga — %1").arg(svc);
  } else {
    line1 = tr("Taiga");
  }

  QString line2;
  if (const auto ep = track::media::detection()->getCurrentEpisode()) {
    if (const auto item = anime::db.item(ep->animeId())) {
      const QString wtitle = QString::fromStdString(
          anime::preferredListTitleString(*item, anime::TitleLanguage::English));
      if (item->episode_count > 1) {
        const QString ep_num =
            QString::fromStdString(ep->element(anitomy::ElementKind::Episode, "1"));
        line2 = tr("Watching: %1 #%2").arg(wtitle, ep_num);
      } else {
        line2 = tr("Watching: %1").arg(wtitle);
      }
    } else {
      const QString raw = QString::fromStdString(ep->element(anitomy::ElementKind::Title));
      if (!raw.isEmpty()) line2 = tr("Playing: %1").arg(raw);
    }
  }

  QString tip = line2.isEmpty() ? line1 : (line1 + "\n" + line2);
  // Windows shells often cap tray icon tooltips (~128 characters).
  constexpr int kMaxLen = 127;
  if (tip.size() > kMaxLen) {
    tip = tip.left(kMaxLen - 1) + QChar(0x2026);
  }
  m_trayIcon->setToolTip(tip);
}

void MainWindow::postTrayMessage(const QString& title, const QString& message) {
  if (m_trayIcon) {
    m_trayIcon->showMessage(title, message);
  }
}

void MainWindow::refreshTorrentCatalogAutocheckTimer() {
  if (!m_catalog_autocheck_timer_) return;
  m_catalog_autocheck_timer_->stop();
  if (!taiga::settings.torrentDiscoveryAutoCheckEnabled()) return;
  const int min = taiga::settings.torrentDiscoveryAutoCheckIntervalMinutes();
  m_catalog_autocheck_timer_->start(min * 60 * 1000);
}

void MainWindow::resortTorrentRssTableFromSettings() {
  if (m_torrentFeedWidget) m_torrentFeedWidget->resortRssTableFromSettings();
}

void MainWindow::onTorrentCatalogAutocheckTimer() {
  initPage(MainWindowPage::Torrents);
  if (m_torrentFeedWidget) {
    m_torrentFeedWidget->runCatalogAutocheckFetch();
  }
}

void MainWindow::refreshHomeQBitPlayButtons() {
  if (m_activePage != MainWindowPage::Home) return;
  if (!taiga::settings.torrentQBitApiEnabled()) return;
  if (m_home_upnext_play_buttons_.isEmpty()) return;

  // Snapshot: buttons can be re-created by refreshHomeDashboard().
  const auto playButtons = m_home_upnext_play_buttons_;

  const QString base_url = QString::fromStdString(taiga::settings.torrentQBitApiUrl()).trimmed();
  if (base_url.isEmpty()) return;
  const QString username =
      QString::fromStdString(taiga::settings.torrentQBitApiUsername()).trimmed();
  const QString password = QString::fromStdString(taiga::settings.torrentQBitApiPassword());

  const auto applyEnabled = [playButtons](const QSet<QString>& downloading_paths) {
    for (const auto& pb : playButtons) {
      if (!pb.btn) continue;
      const bool downloading =
          !pb.save_path.isEmpty() && downloading_paths.contains(QDir::cleanPath(pb.save_path));
      pb.btn->setEnabled(!downloading);
      pb.btn->setCursor(downloading ? Qt::ArrowCursor : Qt::PointingHandCursor);
      pb.btn->setToolTip(downloading ? QObject::tr("Downloading in qBittorrent…") : QString{});
    }
  };

  const auto fetchInfo = [=](const QString& cookie) {
    QNetworkRequest req(QUrl(base_url + QStringLiteral("/api/v2/torrents/info")));
    taiga::applyCommonHeaders(req);
    if (!cookie.isEmpty()) req.setRawHeader("Cookie", cookie.toUtf8());
    auto* reply = taiga::network()->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, applyEnabled]() mutable {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        applyEnabled({});  // non-blocking UX
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
      if (!doc.isArray()) {
        applyEnabled({});
        return;
      }
      QSet<QString> downloading;
      for (const QJsonValue& v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const double progress = o.value(QStringLiteral("progress")).toDouble(1.0);
        if (progress >= 1.0) continue;
        const QString save_path = QDir::cleanPath(o.value(QStringLiteral("save_path")).toString());
        if (!save_path.isEmpty()) downloading.insert(save_path);
      }
      applyEnabled(downloading);
    });
  };

  // Default: enabled; will be disabled when we confirm active downloads for that folder.
  applyEnabled({});

  if (username.isEmpty()) {
    fetchInfo({});
  } else {
    QNetworkRequest login_req(QUrl(base_url + QStringLiteral("/api/v2/auth/login")));
    taiga::applyCommonHeaders(login_req);
    login_req.setHeader(QNetworkRequest::ContentTypeHeader,
                        QStringLiteral("application/x-www-form-urlencoded"));
    const QByteArray login_body = QByteArrayLiteral("username=") + username.toUtf8() +
                                  QByteArrayLiteral("&password=") + password.toUtf8();
    auto* login_reply = taiga::network()->post(login_req, login_body);
    connect(login_reply, &QNetworkReply::finished, this, [login_reply, fetchInfo]() mutable {
      login_reply->deleteLater();
      QString cookie_str;
      const QVariant cv = login_reply->header(QNetworkRequest::SetCookieHeader);
      if (cv.isValid()) {
        for (const QNetworkCookie& c : cv.value<QList<QNetworkCookie>>()) {
          if (!cookie_str.isEmpty()) cookie_str += QStringLiteral("; ");
          cookie_str +=
              QString::fromUtf8(c.name()) + QStringLiteral("=") + QString::fromUtf8(c.value());
        }
      }
      fetchInfo(cookie_str);
    });
  }
}

void MainWindow::runAutoDownload(const bool silent) {
  if (!m_torrentFeedWidget) {
    // Auto-download relies on the TorrentFeedWidget backend; initialize it on-demand so the
    // toolbar countdown action works even if the Torrents page has never been opened.
    initPage(MainWindowPage::Torrents);
  }
  if (!m_torrentFeedWidget) {
    emit autoDownloadFinished(0, 0);
    return;
  }

  if (!track::libraryScanHasResults()) {
    if (taiga::settings.cacheDiagnosticsEnabled()) {
      track::appendLibraryEpisodeIndexCacheDebugLine(
          QStringLiteral("autodl: skipped (library scan/index not ready yet)"));
    }
    if (!silent) {
      QMessageBox::information(this, tr("Auto-download"),
                               tr("The library episode index is not ready yet.\n\n"
                                  "Run a library scan (or enable scan-on-startup) and try again."));
    }
    emit autoDownloadFinished(0, 0);
    return;
  }

  // Collect anime on the Watching list where an episode has aired but is not yet on disk.
  struct Candidate {
    int anime_id;
    QString english_title;
    QString romaji_title;
    QString folder_name;  // always English title (or romaji if no English)
  };
  QList<Candidate> candidates;
  QStringList skipped_twice_today_labels;
  // The "skip after two failures today" throttle is only meant to reduce background noise.
  // Manual auto-download should always attempt every candidate.
  const bool skip_failed = silent && taiga::settings.torrentAutoDownloadSkipAfterTwoFailuresToday();
  const QDate today = QDate::currentDate();
  if (skip_failed) {
    if (!m_auto_download_fail_day_.isValid() || m_auto_download_fail_day_ != today) {
      m_auto_download_fail_day_ = today;
      m_auto_download_fail_streak_today_.clear();
    }
  }
  for (const auto& [anime_id, entry] : anime::db.entries().asKeyValueRange()) {
    if (entry.status != anime::list::Status::Watching) continue;
    const auto* item = anime::db.item(anime_id);
    if (!item) continue;
    // Movies are excluded from auto-download RSS (manual torrent search only).
    if (item->type == anime::Type::Movie) continue;
    const qint64 now_secs = QDateTime::currentSecsSinceEpoch();
    const int watched =
        std::max(entry.watched_episodes, anime::history().maxRecordedEpisodeForAnime(anime_id));
    const int last_aired = taiga::computeLastAiredEpisodeForAutoDownload(*item, watched, now_secs);
    if (last_aired <= watched) continue;
    // Skip only if every episode in [watched+1 .. last_aired] is already on disk.
    // This correctly handles "episodes downloaded but not yet watched" —
    // e.g. eps 1-3 on disk (unwatched), ep 4 not on disk → ep 4 should still be queued.
    bool has_missing = false;
    for (int ep = watched + 1; ep <= last_aired; ++ep) {
      if (!track::libraryHasLocalEpisode(anime_id, ep)) {
        has_missing = true;
        break;
      }
    }
    if (!has_missing) continue;

    const QString en = QString::fromStdString(item->titles.english);
    const QString romaji = QString::fromStdString(item->titles.romaji);
    const QString folder = en.isEmpty() ? romaji : en;
    if (!en.isEmpty() || !romaji.isEmpty()) {
      if (taiga::settings.cacheDiagnosticsEnabled()) {
        track::appendLibraryEpisodeIndexCacheDebugLine(
            QStringLiteral("autodl: queued aid=%1 watched=%2 lastAired=%3 nextTime=%4 epCount=%5 "
                           "status=%6 title='%7'")
                .arg(anime_id)
                .arg(watched)
                .arg(last_aired)
                .arg(static_cast<qint64>(item->next_episode_time))
                .arg(item->episode_count)
                .arg(static_cast<int>(item->status))
                .arg(folder.left(120)));
      }
      if (skip_failed) {
        const int streak = m_auto_download_fail_streak_today_.value(anime_id, 0);
        if (streak >= 2) {
          skipped_twice_today_labels.push_back(en.isEmpty() ? romaji : en);
          continue;
        }
      }
      candidates.push_back(Candidate{anime_id, en, romaji, folder});
    }
  }

  if (candidates.isEmpty()) {
    if (!silent) {
      QString msg =
          tr("No anime require episode downloads right now.\n"
             "All watching entries are either caught up or the next episode is already on disk.");
      if (skip_failed && !skipped_twice_today_labels.isEmpty()) {
        skipped_twice_today_labels.removeDuplicates();
        std::sort(skipped_twice_today_labels.begin(), skipped_twice_today_labels.end(),
                  [](const QString& a, const QString& b) {
                    return a.compare(b, Qt::CaseInsensitive) < 0;
                  });
        constexpr int kMaxShown = 10;
        const int total = skipped_twice_today_labels.size();
        if (skipped_twice_today_labels.size() > kMaxShown) {
          skipped_twice_today_labels = skipped_twice_today_labels.mid(0, kMaxShown);
        }
        msg += tr("\n\nSkipped today (failed twice in a row):\n");
        for (const QString& label : skipped_twice_today_labels) {
          msg += u"  • %1\n"_s.arg(label);
        }
        if (total > skipped_twice_today_labels.size()) {
          msg += tr("  …and %1 more").arg(total - skipped_twice_today_labels.size());
        }
      }
      QMessageBox::information(this, tr("Auto-download"), msg);
    }
    emit autoDownloadFinished(0, 0);
    return;
  }

  if (!silent) {
    QString msg =
        tr("The following anime have aired episodes that are not yet downloaded. "
           "The app will try multiple title variants and download the best matching "
           "torrent for each:\n\n");
    for (const auto& c : candidates) {
      const QString label = c.english_title.isEmpty() ? c.romaji_title : c.english_title;
      msg += u"  • %1\n"_s.arg(label);
    }
    msg += u"\n%1 anime total.\n\nProceed?"_s.arg(candidates.size());
    if (QMessageBox::question(this, tr("Auto-download"), msg, QMessageBox::Yes | QMessageBox::No) !=
        QMessageBox::Yes) {
      return;
    }
  }

  struct State {
    QList<Candidate> queue;
    int total = 0;
    int found = 0;
  };
  auto state = std::make_shared<State>();
  state->queue = candidates;
  state->total = candidates.size();

  auto step_fn = std::make_shared<std::function<void()>>();
  *step_fn = [this, state, step_fn]() {
    if (state->queue.isEmpty()) {
      const QString summary =
          tr("Auto-download: %1 torrent(s) sent for %2 anime.").arg(state->found).arg(state->total);
      enqueueStatusMessage(summary, false);
      // Tray notification so both manual and timer-triggered runs are visible.
      if (state->found > 0) postTrayMessage(tr("Auto-download"), summary);
      emit autoDownloadFinished(state->found, state->total);
      return;
    }
    const auto c = state->queue.takeFirst();
    const QString label = c.english_title.isEmpty() ? c.romaji_title : c.english_title;
    enqueueStatusMessage(tr("Auto-download: fetching RSS for %1 (%2 remaining)…")
                             .arg(label)
                             .arg(state->queue.size() + 1),
                         false);
    initPage(MainWindowPage::Torrents);  // ensure widget is initialized
    // Download ALL missing episodes for this anime (not just the newest).
    m_torrentFeedWidget->downloadAllEpisodesForAnime(
        c.anime_id, c.english_title, c.romaji_title, c.folder_name,
        [this, state, step_fn, label, anime_id = c.anime_id](int count) {
          state->found += count;
          if (taiga::settings.torrentAutoDownloadSkipAfterTwoFailuresToday()) {
            const QDate cur = QDate::currentDate();
            if (!m_auto_download_fail_day_.isValid() || m_auto_download_fail_day_ != cur) {
              m_auto_download_fail_day_ = cur;
              m_auto_download_fail_streak_today_.clear();
            }
            if (count > 0) {
              m_auto_download_fail_streak_today_.remove(anime_id);
            } else {
              m_auto_download_fail_streak_today_.insert(
                  anime_id, m_auto_download_fail_streak_today_.value(anime_id, 0) + 1);
            }
          }
          if (count > 0) {
            // Per-anime status feedback.
            enqueueStatusMessage(tr("Sent %1 episode(s) for %2.").arg(count).arg(label), false);
          }
          // Small delay to avoid hammering the RSS server.
          QTimer::singleShot(2000, this, [step_fn]() { (*step_fn)(); });
        });
  };

  (*step_fn)();
}

void MainWindow::refreshHomeDashboard() {
  if (!m_homeBodyLabel) return;
  if (m_defer_home_refresh_until_startup_scan_) return;

  // ── Stats summary ─────────────────────────────────────────────────────────
  const taiga::ListStatistics st = taiga::computeListStatistics();
  const QString spent = [&] {
    if (st.spent_watch_seconds <= 0) return tr("—");
    const int d = st.spent_watch_seconds / 86400;
    const int h = (st.spent_watch_seconds % 86400) / 3600;
    if (d > 0) return tr("%1 d %2 h").arg(d).arg(h);
    if (h > 0) return tr("%1 h").arg(h);
    const int m = (st.spent_watch_seconds % 3600) / 60;
    return tr("%1 min").arg(std::max(1, m));
  }();
  const QString mean = st.scored_title_count > 0
                           ? tr("%1 / 10").arg(QString::number(
                                 static_cast<double>(st.mean_score_0_100) / 10.0, 'f', 1))
                           : tr("—");
  m_homeBodyLabel->setText(tr("<span style=\"font-size:large; font-weight:600\">"
                              "%1 titles on list"
                              "</span>"
                              "<span style=\"color:#888; font-size:medium\">"
                              " &nbsp;·&nbsp; %2 in database"
                              " &nbsp;·&nbsp; Time spent: <b>%3</b>"
                              " &nbsp;·&nbsp; Mean score: <b>%4</b>"
                              "</span>")
                               .arg(anime::db.entries().size())
                               .arg(anime::db.items().size())
                               .arg(spent)
                               .arg(mean));

  // Helper: clear all children from a container widget
  const auto clearContainer = [](QWidget* w) {
    if (!w || !w->layout()) return;
    while (w->layout()->count()) {
      QLayoutItem* it = w->layout()->takeAt(0);
      if (it->widget()) it->widget()->deleteLater();
      delete it;
    }
  };

  const auto applyHomeRowChrome = [&](QWidget* row, QHBoxLayout* rl) {
    if (!row || !rl) return;
    rl->setContentsMargins(8, 4, 8, 4);
    row->setAttribute(Qt::WA_Hover, true);
    row->setMouseTracking(true);
    row->setObjectName(QStringLiteral("homeRow"));
    row->setStyleSheet(
        theme.isDark()
            ? QStringLiteral("QWidget#homeRow{background: transparent; border-radius: 6px;}"
                             "QWidget#homeRow:hover{background-color: rgba(255,255,255,18);}")
            : QStringLiteral("QWidget#homeRow{background: transparent; border-radius: 6px;}"
                             "QWidget#homeRow:hover{background-color: rgba(0,0,0,10);}"));
  };

  const auto addHomeDivider = [&](QVBoxLayout* vl, QWidget* parent) {
    if (!vl || !parent) return;
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    line->setStyleSheet(theme.isDark()
                            ? QStringLiteral("QFrame{background-color: rgba(255,255,255,28);}")
                            : QStringLiteral("QFrame{background-color: rgba(0,0,0,28);}"));
    vl->addWidget(line);
  };

  const auto hideMatureCatalogRow = [](const Anime* item) -> bool {
    return item && !taiga::settings.listShowMatureContent() && anime::isNsfw(*item);
  };

  // ── "Up next" section ────────────────────────────────────────────────────
  if (m_homeUpNextContainer) {
    clearContainer(m_homeUpNextContainer);
    auto* vl = qobject_cast<QVBoxLayout*>(m_homeUpNextContainer->layout());

    m_home_upnext_play_buttons_.clear();

    // Avoid showing misleading empty/partial results at startup: the on-disk episode index is
    // populated by scans/watcher events, and the startup scan runs on a timer.
    if (taiga::settings.scanLibraryOnStartup() && !track::libraryScanHasResults()) {
      auto* pending = new QLabel(tr("<span style=\"color:#888\">Library scan pending…</span>"),
                                 m_homeUpNextContainer);
      pending->setTextFormat(Qt::RichText);
      vl->addWidget(pending);
      return;
    }

    // Collect Watching entries that have the next episode on disk, sorted by title
    struct UpNextEntry {
      QString title;
      int anime_id;
      int next_ep;
    };
    QList<UpNextEntry> upNext;
    for (const auto& entry : anime::db.entries()) {
      if (entry.status != anime::list::Status::Watching) continue;
      const int next_ep = entry.watched_episodes + 1;
      if (!track::libraryHasLocalEpisode(entry.anime_id, next_ep)) continue;
      const auto* item = anime::db.item(entry.anime_id);
      if (!item) continue;
      if (hideMatureCatalogRow(item)) continue;
      upNext.push_back({QString::fromStdString(
                            anime::preferredListTitleString(*item, anime::TitleLanguage::English)),
                        entry.anime_id, next_ep});
    }
    std::sort(upNext.begin(), upNext.end(), [](const UpNextEntry& a, const UpNextEntry& b) {
      return a.title.toLower() < b.title.toLower();
    });

    if (upNext.isEmpty()) {
      auto* empty = new QLabel(tr("<span style=\"color:#888\">None — scan your library or "
                                  "add a folder in Settings → Library.</span>"),
                               m_homeUpNextContainer);
      empty->setTextFormat(Qt::RichText);
      vl->addWidget(empty);
    } else {
      // Helper: compute the same qBittorrent save-path Taiga uses when sending torrents.
      // IMPORTANT: must NOT create any directories here (Home should never create folders).
      const auto resolvedTorrentDownloadDir = [](const QString& folder_name) -> QString {
        QString base =
            QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed();
        if (base.isEmpty()) return {};
        if (!QDir(base).exists()) return {};
        if (taiga::settings.torrentDownloadCreateSubfolder()) {
          QString sub = folder_name.trimmed();
          for (const QChar c : QStringLiteral("\\/:*?\"<>|")) sub.replace(c, u'_');
          if (sub.isEmpty()) return QDir(base).absolutePath();
          sub = sub.left(120);
          QDir d(base);
          return d.filePath(sub);
        }
        return QDir(base).absolutePath();
      };

      for (int i = 0; i < upNext.size(); ++i) {
        const auto& ue = upNext[i];
        auto* row = new QWidget(m_homeUpNextContainer);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);
        applyHomeRowChrome(row, rl);

        // Eye-catching accent: teal for dark, deep-teal for light.
        const QString accentColor = theme.isDark() ? u"#2dd4bf"_s : u"#0d9488"_s;
        auto* titleLbl = new QLabel(u"<span style=\"color:%1; font-weight:600\">%2</span>"_s.arg(
                                        accentColor, ue.title.toHtmlEscaped()),
                                    row);
        titleLbl->setTextFormat(Qt::RichText);
        titleLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        const QString epLabel = tr("Ep %1").arg(ue.next_ep);
        auto* epLbl = new QLabel(u"<span style=\"color:%1; font-weight:600\">%2</span>"_s.arg(
                                     accentColor, epLabel.toHtmlEscaped()),
                                 row);
        epLbl->setTextFormat(Qt::RichText);
        epLbl->setFixedWidth(54);

        auto* playBtn = new QPushButton(tr("▶ Play"), row);
        playBtn->setFixedWidth(72);
        playBtn->setCursor(Qt::PointingHandCursor);
        // Make it look more like an action button.
        playBtn->setStyleSheet(
            QStringLiteral("QPushButton{padding:4px 10px; font-weight:600;}"
                           "QPushButton:disabled{color:palette(placeholderText);}"));
        const int aid = ue.anime_id;
        connect(playBtn, &QPushButton::clicked, this, [this, aid]() {
          if (!track::playNextEpisode(aid)) {
            enqueueStatusMessage(playNextEpisodeNotFoundMessage(), false);
          }
        });

        rl->addWidget(titleLbl);
        rl->addWidget(epLbl);
        rl->addWidget(playBtn);
        vl->addWidget(row);
        if (i != upNext.size() - 1) addHomeDivider(vl, m_homeUpNextContainer);

        // Capture button for qBittorrent progress gating.
        if (taiga::settings.torrentQBitApiEnabled()) {
          const auto* item = anime::db.item(aid);
          const QString folder =
              item ? QString::fromStdString(item->titles.english).trimmed() : QString{};
          const QString folder_name = folder.isEmpty() ? ue.title : folder;
          const QString save_path = resolvedTorrentDownloadDir(folder_name);
          HomeUpNextButton hb;
          hb.btn = playBtn;
          hb.save_path = save_path;
          hb.anime_id = aid;
          m_home_upnext_play_buttons_.append(hb);
        }
      }
    }

    // Gray out Play while qBittorrent reports active downloads in that save path (best-effort).
    if (taiga::settings.torrentQBitApiEnabled() && !m_home_upnext_play_buttons_.isEmpty()) {
      // First pass now, then keep it fresh while Home is visible.
      refreshHomeQBitPlayButtons();
      if (!m_home_qbit_poll_timer_) {
        m_home_qbit_poll_timer_ = new QTimer(this);
        m_home_qbit_poll_timer_->setInterval(8000);
        connect(m_home_qbit_poll_timer_, &QTimer::timeout, this,
                [this]() { refreshHomeQBitPlayButtons(); });
      }
      if (!m_home_qbit_poll_timer_->isActive()) m_home_qbit_poll_timer_->start();
    } else {
      if (m_home_qbit_poll_timer_) m_home_qbit_poll_timer_->stop();
    }

    if (m_homeUpNextHeader) {
      m_homeUpNextHeader->setVisible(!upNext.isEmpty() || true);  // always show header
    }
  }

  // ── "Upcoming & recently aired" section ─────────────────────────────────
  if (m_homeRecentContainer) {
    clearContainer(m_homeRecentContainer);
    auto* vl = qobject_cast<QVBoxLayout*>(m_homeRecentContainer->layout());

    const qint64 now_secs = QDateTime::currentSecsSinceEpoch();
    const QDate today = QDate::currentDate();

    // Helper: preferred display title
    auto preferredTitle = [](const Anime& item) -> QString {
      return QString::fromStdString(
          anime::preferredListTitleString(item, anime::TitleLanguage::English));
    };

    // ---- Sub-section: Upcoming episodes this week -------------------------
    struct UpcomingEntry {
      QString title;
      int anime_id;
      int episode;
      qint64 air_time;
    };
    QList<UpcomingEntry> upcomingWatching;
    QList<UpcomingEntry> upcomingOther;
    for (const auto& entry : anime::db.entries()) {
      if (entry.status != anime::list::Status::Watching &&
          entry.status != anime::list::Status::PlanToWatch)
        continue;
      const auto* item = anime::db.item(entry.anime_id);
      if (!item || item->next_episode_time <= now_secs) continue;
      if (hideMatureCatalogRow(item)) continue;
      const qint64 secs_until = item->next_episode_time - now_secs;
      if (secs_until > 7LL * 86400) continue;
      const int next_ep = item->last_aired_episode + 1;
      (entry.status == anime::list::Status::Watching ? upcomingWatching : upcomingOther)
          .push_back({preferredTitle(*item), entry.anime_id, next_ep, item->next_episode_time});
    }
    const auto byAirTime = [](const UpcomingEntry& a, const UpcomingEntry& b) {
      return a.air_time < b.air_time;
    };
    std::sort(upcomingWatching.begin(), upcomingWatching.end(), byAirTime);
    std::sort(upcomingOther.begin(), upcomingOther.end(), byAirTime);

    const auto addGroupLabel = [&](const QString& text) {
      auto* lbl = new QLabel(u"<span style=\"color:#888;font-size:small\"><b>%1</b></span>"_s.arg(
                                 text.toHtmlEscaped()),
                             m_homeRecentContainer);
      lbl->setTextFormat(Qt::RichText);
      vl->addWidget(lbl);
    };

    const auto addUpcomingRows = [&](const QList<UpcomingEntry>& upcoming) {
      for (int i = 0; i < upcoming.size(); ++i) {
        const auto& ue = upcoming[i];
        const qint64 secs_until = ue.air_time - now_secs;
        const int days = static_cast<int>(secs_until / 86400);
        const int hours = static_cast<int>((secs_until % 86400) / 3600);
        const int minutes = static_cast<int>((secs_until % 3600) / 60);
        QString when;
        if (days == 0) {
          if (secs_until < 60) {
            when = tr("soon");
          } else if (hours <= 0) {
            when = tr("in %1 min").arg(std::max(1, minutes));
          } else if (minutes > 0) {
            when = tr("in %1 h %2 min").arg(hours).arg(minutes);
          } else {
            when = tr("in %1 h").arg(hours);
          }
        } else if (days == 1)
          when = tr("tomorrow");
        else
          when = tr("in %1 d").arg(days);

        auto* row = new QWidget(m_homeRecentContainer);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);
        applyHomeRowChrome(row, rl);

        auto* titleBtn = new QPushButton(ue.title, row);
        titleBtn->setFlat(true);
        titleBtn->setCursor(Qt::PointingHandCursor);
        titleBtn->setStyleSheet(
            u"text-align:left; color: palette(link); text-decoration: underline; border: none; padding: 0;"_s);
        titleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(titleBtn, &QPushButton::clicked, this, [this, anime_id = ue.anime_id]() {
          const auto* item = anime::db.item(anime_id);
          if (!item) return;
          const auto* e = anime::db.entry(anime_id);
          std::optional<ListEntry> entry;
          if (e) entry = *e;
          gui::MediaDialog::show(this, gui::MediaDialogPage::Details, *item, entry);
        });

        auto* epLbl = new QLabel(u"<span style=\"color:#888\">Ep %1</span>"_s.arg(ue.episode), row);
        epLbl->setTextFormat(Qt::RichText);
        epLbl->setFixedWidth(54);

        auto* whenLbl = new QLabel(
            u"<span style=\"color:#888;font-size:medium\">%1</span>"_s.arg(when.toHtmlEscaped()),
            row);
        whenLbl->setTextFormat(Qt::RichText);
        whenLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        whenLbl->setFixedWidth(120);

        rl->addWidget(titleBtn);
        rl->addWidget(epLbl);
        rl->addWidget(whenLbl);
        vl->addWidget(row);
        if (i != upcoming.size() - 1) addHomeDivider(vl, m_homeRecentContainer);
      }
    };

    const bool haveUpcoming = !upcomingWatching.isEmpty() || !upcomingOther.isEmpty();
    if (haveUpcoming) {
      auto* subHdr =
          new QLabel(u"<span style=\"color:#aaa;font-size:medium\"><b>%1</b></span>"_s.arg(
                         tr("UPCOMING THIS WEEK")),
                     m_homeRecentContainer);
      subHdr->setTextFormat(Qt::RichText);
      vl->addWidget(subHdr);

      if (!upcomingWatching.isEmpty() && !upcomingOther.isEmpty()) {
        addGroupLabel(tr("Watching"));
      }
      if (!upcomingWatching.isEmpty()) {
        addUpcomingRows(upcomingWatching);
      }
      if (!upcomingWatching.isEmpty() && !upcomingOther.isEmpty()) {
        vl->addSpacing(6);
        addGroupLabel(tr("Not watching yet"));
      }
      if (!upcomingOther.isEmpty()) {
        if (!upcomingWatching.isEmpty()) addHomeDivider(vl, m_homeRecentContainer);
        addUpcomingRows(upcomingOther);
      }
      vl->addSpacing(8);
    }

    // ---- Sub-section: Finished airing in the last 7 days -----------------
    struct RecentlyAiredEntry {
      QString title;
      int anime_id;
      QDate finished;
    };
    QList<RecentlyAiredEntry> recentlyAiredWatching;
    QList<RecentlyAiredEntry> recentlyAiredOther;
    for (const auto& entry : anime::db.entries()) {
      if (entry.status == anime::list::Status::Completed) continue;
      const auto* item = anime::db.item(entry.anime_id);
      if (!item || item->date_finished.empty()) continue;
      if (hideMatureCatalogRow(item)) continue;
      const int yr = item->date_finished.year();
      const int mo = item->date_finished.month();
      const int dy = item->date_finished.day();
      if (yr == 0 || mo == 0 || dy == 0) continue;
      const QDate finishedDate(yr, mo, dy);
      if (!finishedDate.isValid()) continue;
      const int daysAgo = finishedDate.daysTo(today);
      if (daysAgo < 0 || daysAgo > 7) continue;
      // Avoid duplicates (multiple list entries for same anime)
      const auto isSame = [&](const RecentlyAiredEntry& r) { return r.anime_id == entry.anime_id; };
      const bool already =
          std::any_of(recentlyAiredWatching.cbegin(), recentlyAiredWatching.cend(), isSame) ||
          std::any_of(recentlyAiredOther.cbegin(), recentlyAiredOther.cend(), isSame);
      if (already) continue;
      (entry.status == anime::list::Status::Watching ? recentlyAiredWatching : recentlyAiredOther)
          .push_back({preferredTitle(*item), entry.anime_id, finishedDate});
    }
    const auto byFinishedDesc = [](const RecentlyAiredEntry& a, const RecentlyAiredEntry& b) {
      return b.finished < a.finished;  // most recently finished first
    };
    std::sort(recentlyAiredWatching.begin(), recentlyAiredWatching.end(), byFinishedDesc);
    std::sort(recentlyAiredOther.begin(), recentlyAiredOther.end(), byFinishedDesc);

    const bool haveFinished = !recentlyAiredWatching.isEmpty() || !recentlyAiredOther.isEmpty();
    if (haveFinished) {
      auto* subHdr =
          new QLabel(u"<span style=\"color:#aaa;font-size:medium\"><b>%1</b></span>"_s.arg(
                         tr("FINISHED AIRING THIS WEEK")),
                     m_homeRecentContainer);
      subHdr->setTextFormat(Qt::RichText);
      vl->addWidget(subHdr);

      const auto addFinishedRows = [&](const QList<RecentlyAiredEntry>& recentlyAired) {
        for (int i = 0; i < recentlyAired.size(); ++i) {
          const auto& ra = recentlyAired[i];
          const int daysAgo = ra.finished.daysTo(today);
          QString when;
          if (daysAgo == 0)
            when = tr("today");
          else if (daysAgo == 1)
            when = tr("yesterday");
          else
            when = tr("%1 days ago").arg(daysAgo);

          auto* row = new QWidget(m_homeRecentContainer);
          auto* rl = new QHBoxLayout(row);
          rl->setContentsMargins(0, 0, 0, 0);
          rl->setSpacing(8);
          applyHomeRowChrome(row, rl);

          auto* titleBtn = new QPushButton(ra.title, row);
          titleBtn->setFlat(true);
          titleBtn->setCursor(Qt::PointingHandCursor);
          titleBtn->setStyleSheet(
              u"text-align:left; color: palette(link); text-decoration: underline; border: none; padding: 0;"_s);
          titleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
          connect(titleBtn, &QPushButton::clicked, this, [this, anime_id = ra.anime_id]() {
            const auto* item = anime::db.item(anime_id);
            if (!item) return;
            const auto* e = anime::db.entry(anime_id);
            std::optional<ListEntry> entry;
            if (e) entry = *e;
            gui::MediaDialog::show(this, gui::MediaDialogPage::Details, *item, entry);
          });

          auto* whenLbl = new QLabel(
              u"<span style=\"color:#888;font-size:medium\">%1</span>"_s.arg(when.toHtmlEscaped()),
              row);
          whenLbl->setTextFormat(Qt::RichText);
          whenLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
          whenLbl->setFixedWidth(96);

          rl->addWidget(titleBtn);
          rl->addWidget(whenLbl);
          vl->addWidget(row);
          if (i != recentlyAired.size() - 1) addHomeDivider(vl, m_homeRecentContainer);
        }
      };
      if (!recentlyAiredWatching.isEmpty() && !recentlyAiredOther.isEmpty()) {
        addGroupLabel(tr("Watching"));
      }
      if (!recentlyAiredWatching.isEmpty()) {
        addFinishedRows(recentlyAiredWatching);
      }
      if (!recentlyAiredWatching.isEmpty() && !recentlyAiredOther.isEmpty()) {
        vl->addSpacing(6);
        addGroupLabel(tr("Not watching yet"));
      }
      if (!recentlyAiredOther.isEmpty()) {
        if (!recentlyAiredWatching.isEmpty()) addHomeDivider(vl, m_homeRecentContainer);
        addFinishedRows(recentlyAiredOther);
      }
    }

    const bool anyContent = haveUpcoming || haveFinished;
    if (m_homeRecentHeader) m_homeRecentHeader->setVisible(anyContent);
    if (m_homeRecentContainer) m_homeRecentContainer->setVisible(anyContent);
  }

  updateHomeAnnouncedBanner();
}

void MainWindow::refreshAnnouncedReleasesPageAfterServiceChange() {
  if (m_announcedReleasesWidget) m_announcedReleasesWidget->refresh();
}

void MainWindow::refreshAnnouncedReleasesSurfaces() {
  if (m_announcedReleasesWidget) m_announcedReleasesWidget->refresh();
  updateHomeAnnouncedBanner();
  refreshNavigationSidebar();
}

void MainWindow::updateHomeAnnouncedBanner() {
  if (!m_homeAnnouncedBannerHost) return;
  QLayout* outerLay = m_homeAnnouncedBannerHost->layout();
  if (!outerLay) return;
  while (QLayoutItem* it = outerLay->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }
  const int visible_announce = anime::countVisibleAnnouncedReleaseCandidates(
      taiga::session.announcedReleasesDismissedAnimeIds(), taiga::settings.listShowMatureContent());
  if (visible_announce == 0) {
    m_homeAnnouncedBannerHost->setVisible(false);
    return;
  }
  m_homeAnnouncedBannerHost->setVisible(true);
  auto* frame = new QFrame(m_homeAnnouncedBannerHost);
  frame->setObjectName(QStringLiteral("homeAnnouncedBanner"));
  frame->setStyleSheet(QStringLiteral(
      "QFrame#homeAnnouncedBanner{border:1px solid palette(mid); border-radius:8px; padding:8px; "
      "background: palette(alternate-base);}"));
  auto* hl = new QHBoxLayout(frame);
  hl->setContentsMargins(10, 8, 10, 8);
  hl->setSpacing(12);
  auto* msg = new QLabel(
      tr("<b>New seasons</b> — %1 announced sequel(s) matched your list. Open the tab to add them "
         "to Planning or explore watch order.")
          .arg(visible_announce),
      frame);
  msg->setWordWrap(true);
  msg->setTextFormat(Qt::RichText);
  hl->addWidget(msg, 1);
  auto* btn = new QPushButton(tr("Announced releases"), frame);
  connect(btn, &QPushButton::clicked, this,
          [this]() { setPage(MainWindowPage::AnnouncedReleases); });
  hl->addWidget(btn, 0, Qt::AlignVCenter);
  outerLay->addWidget(frame);
}

void MainWindow::refreshListColors() {
  if (m_listWidget) {
    m_listWidget->refreshNewEpisodeHighlightDisplay();
  }
  if (m_searchWidget) m_searchWidget->refreshNewEpisodeHighlightDisplay();
}

void MainWindow::updateAutoDownloadCountdownLabel() {
  updateAutoDownloadActionLabel();
}

void MainWindow::updateAutoDownloadActionLabel() {
  if (!m_autoDownloadAction) return;
  if (m_delayed_autodl_timer_ && m_delayed_autodl_timer_->isActive()) {
    const int secs = std::max(0, m_delayed_autodl_timer_->remainingTime() / 1000);
    const int min = (secs + 59) / 60;
    const QString text =
        (min <= 0) ? tr("Auto-download (scheduled…)") : tr("Auto-download (in %1m)").arg(min);
    m_autoDownloadAction->setText(text);
    return;
  }
  m_autoDownloadAction->setText(tr("Auto-download"));
}

void MainWindow::scheduleDelayedAutoDownload(const int delay_minutes) {
  if (!m_delayed_autodl_timer_) return;
  const int clamped_min = std::max(1, delay_minutes);
  // If already scheduled, keep the earliest fire time.
  if (m_delayed_autodl_timer_->isActive()) {
    const int remaining_ms = m_delayed_autodl_timer_->remainingTime();
    const int requested_ms = clamped_min * 60 * 1000;
    if (remaining_ms > 0 && remaining_ms <= requested_ms) {
      updateAutoDownloadActionLabel();
      return;
    }
    m_delayed_autodl_timer_->stop();
  }
  m_delayed_autodl_scheduled_at_secs_ = QDateTime::currentSecsSinceEpoch();
  m_delayed_autodl_timer_->start(clamped_min * 60 * 1000);
  updateAutoDownloadActionLabel();
  enqueueStatusMessage(tr("Auto-download scheduled."), false);
}

void MainWindow::cancelDelayedAutoDownload(const QString& reason) {
  if (!m_delayed_autodl_timer_) return;
  if (!m_delayed_autodl_timer_->isActive()) return;
  m_delayed_autodl_timer_->stop();
  m_delayed_autodl_scheduled_at_secs_ = 0;
  updateAutoDownloadActionLabel();
  if (!reason.isEmpty()) {
    enqueueStatusMessage(tr("Canceled scheduled auto-download (%1).").arg(reason), false);
  } else {
    enqueueStatusMessage(tr("Canceled scheduled auto-download."), false);
  }
}

void MainWindow::beginDelayedAutoDownloadRun() {
  m_delayed_autodl_scheduled_at_secs_ = 0;
  updateAutoDownloadActionLabel();

  // Ordering: sync (if configured) → scan → auto-download.
  const bool can_sync = taiga::settings.listSynchronizationEnabled() &&
                        sync::currentServiceId() != sync::ServiceId::Unknown &&
                        sync::remoteListAccessConfigured();
  if (can_sync) {
    m_delayed_autodl_after_sync_pending_ = true;
    enqueueStatusMessage(tr("Auto-download: synchronizing…"), false);
    startListSynchronization(/*queue_if_busy=*/false);
    return;
  }
  m_delayed_autodl_after_scan_pending_ = true;
  enqueueStatusMessage(tr("Auto-download: scanning library…"), false);
  runLibraryScan(true, LibraryScanReason::DelayedAutoDownload);
}

void MainWindow::checkWatchingReleaseEvent() {
  // Watch only: detect if the soonest upcoming episode just crossed "now".
  // Debounced so it fires at most once per minute.
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (m_last_release_event_trigger_secs_ == 0) {
    m_last_release_event_trigger_secs_ = now;
    return;
  }
  if (now - m_last_release_event_trigger_secs_ < 60) return;
  m_last_release_event_trigger_secs_ = now;

  if (m_delayed_autodl_timer_ && m_delayed_autodl_timer_->isActive()) return;
  if (m_list_sync_in_progress_ || m_library_scan_in_progress_) return;

  qint64 soonest = 0;
  for (const auto& entry : anime::db.entries()) {
    if (entry.status != anime::list::Status::Watching) continue;
    const Anime* item = anime::db.item(entry.anime_id);
    if (!item) continue;
    if (item->next_episode_time <= 0) continue;
    const qint64 t = static_cast<qint64>(item->next_episode_time);
    if (soonest == 0 || t < soonest) soonest = t;
  }

  // If "soonest" is in the recent past (within 5 minutes), treat as a release event.
  if (soonest > 0 && now >= soonest && now - soonest <= 5 * 60) {
    scheduleDelayedAutoDownload(taiga::settings.torrentAutoDownloadReleaseEventDelayMinutes());
  }
}

void MainWindow::restoreViewChromeFromSession() {
  // Sidebar is always-on.
  if (m_navigationWidget) m_navigationWidget->setVisible(true);
  {
    const QSignalBlocker b(ui_->actionToggleNavigationSidebar);
    ui_->actionToggleNavigationSidebar->setChecked(true);
  }

  {
    const bool toolbarVisible = taiga::session.mainWindowToolbarVisible();
    const QSignalBlocker b(ui_->actionToggleToolbar);
    ui_->toolbar->setVisible(toolbarVisible);
    ui_->actionToggleToolbar->setChecked(toolbarVisible);
    ui_->menubar->setVisible(!toolbarVisible);
  }

  ui_->statusbar->setVisible(taiga::session.mainWindowStatusBarVisible());
  {
    const QSignalBlocker b(ui_->actionToggleStatusbar);
    ui_->actionToggleStatusbar->setChecked(taiga::session.mainWindowStatusBarVisible());
  }
  {
    const QSignalBlocker b(ui_->actionToggleNowPlaying);
    ui_->actionToggleNowPlaying->setChecked(taiga::session.mainWindowNowPlayingBarEnabled());
  }
  if (!taiga::session.mainWindowNowPlayingBarEnabled() && m_nowPlayingWidget) {
    m_nowPlayingWidget->hide();
  }
}

void MainWindow::routeToolbarSearchToActivePage() {
  if (!m_searchBox) return;
  const QString text = m_searchBox->text();
  switch (m_activePage) {
    case MainWindowPage::List:
      if (m_listWidget) m_listWidget->applyToolbarTextFilter(text);
      break;
    case MainWindowPage::Search:
      if (m_searchWidget) m_searchWidget->applyToolbarTextFilter(text);
      break;
    case MainWindowPage::History:
      if (m_historyWidget) m_historyWidget->applyToolbarTextFilter(text);
      break;
    case MainWindowPage::AnnouncedReleases:
      if (m_announcedReleasesWidget) m_announcedReleasesWidget->applyToolbarTextFilter(text);
      break;
    default:
      break;
  }
}

void MainWindow::openDataFolder() {
  // Prefer the configured library folder; fall back to the app data folder.
  const auto folders = taiga::settings.libraryFolders();
  const QString path =
      !folders.empty() ? QString::fromStdString(folders.front())
                       : QDir::fromNativeSeparators(QString::fromStdString(taiga::get_data_path()));
  if (path.isEmpty()) {
    QMessageBox::information(this, tr("Taiga"), noLibraryFolderConfiguredBody());
    return;
  }
  if (!QDir{}.mkpath(path)) {
    QMessageBox::warning(this, tr("Taiga"), openPrimaryFolderCreateFailedMessage());
    return;
  }
  const QUrl url = QUrl::fromLocalFile(path.endsWith(u'/') ? path : path + u'/');
  if (!QDesktopServices::openUrl(url)) {
    QMessageBox::warning(this, tr("Taiga"), openPrimaryFolderLaunchFailedMessage());
    return;
  }
  enqueueStatusMessage(openPrimaryFolderOpenedStatus(path), false);
}

void MainWindow::showLibraryFoldersDialog() {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Library folders"));
  dlg.resize(520, 360);

  auto* layout = new QVBoxLayout(&dlg);
  auto* list = new QListWidget(&dlg);
  for (const auto& folder : taiga::settings.libraryFolders()) {
    list->addItem(QString::fromStdString(folder));
  }
  layout->addWidget(list);

  auto* row = new QHBoxLayout();
  auto* add_btn = new QPushButton(tr("Add folder…"), &dlg);
  auto* remove_btn = new QPushButton(tr("Remove"), &dlg);
  row->addWidget(add_btn);
  row->addWidget(remove_btn);
  row->addStretch();
  layout->addLayout(row);

  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  layout->addWidget(box);
  connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  connect(add_btn, &QPushButton::clicked, &dlg, [&dlg, list]() {
    constexpr auto options =
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;
    const QString directory =
        QFileDialog::getExistingDirectory(&dlg, tr("Add library folder"), {}, options);
    if (directory.isEmpty()) return;
    const auto matches = list->findItems(directory, Qt::MatchFixedString);
    if (!matches.isEmpty()) return;
    list->addItem(directory);
  });
  connect(remove_btn, &QPushButton::clicked, &dlg,
          [list]() { delete list->takeItem(list->currentRow()); });

  if (dlg.exec() != QDialog::Accepted) return;

  std::vector<std::string> folders;
  folders.reserve(static_cast<size_t>(list->count()));
  for (int i = 0; i < list->count(); ++i) {
    folders.push_back(list->item(i)->text().toStdString());
  }
  taiga::settings.setLibraryFolders(std::move(folders));
  refreshLibraryRootsFromSettings();
  enqueueStatusMessage(tr("Library folders updated."), false);
}

void MainWindow::applyWatchNextListSideEffects() {
  if (sync::currentServiceId() != sync::ServiceId::Unknown &&
      taiga::settings.listSynchronizationEnabled()) {
    m_post_sync_auto_download_ = true;
    startListSynchronization();
  } else {
    runAutoDownload(true);
  }
}

void MainWindow::ensureWatchOrderGuideWindow() {
  if (m_watchOrderGuide) return;
  m_watchOrderGuide = new WatchNextDialog(this);
  m_watchOrderGuide->setAttribute(Qt::WA_DeleteOnClose, false);
  connect(m_watchOrderGuide, &WatchNextDialog::listChangeCommitted, this,
          &MainWindow::onWatchOrderGuideListCommitted);
}

void MainWindow::openWatchOrderGuideForAnime(const int anime_id) {
  if (anime_id <= 0) return;
  ensureWatchOrderGuideWindow();
  m_watchOrderGuide->presentModelessGuideForAnime(anime_id);
  m_watchOrderGuide->show();
  m_watchOrderGuide->raise();
  m_watchOrderGuide->activateWindow();
}

void MainWindow::onWatchOrderGuideListCommitted() {
  applyWatchNextListSideEffects();
}

void MainWindow::writeLocalListBackup() {
  if (!taiga::settings.localListBackupEnabled()) return;
  const QString path = taiga::settings.localListBackupPath();
  if (path.isEmpty()) return;
  anime::list::exportAsXml(path.toStdString());
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  return QMainWindow::eventFilter(watched, event);
}

}  // namespace gui
