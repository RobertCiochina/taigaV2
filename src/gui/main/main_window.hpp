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

#include <QDate>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <optional>
#include <vector>

#include "media/anime_list.hpp"

class QLabel;
class QLineEdit;
class QAction;
class QEvent;
class QShowEvent;
class QTimer;
class QPushButton;

namespace Ui {
class MainWindow;
}

namespace track {
class Episode;
}

namespace gui {

class WatchNextDialog;
class AnnouncedReleasesWidget;

class HistoryWidget;
class LibraryWidget;
class ListWidget;
class NavigationWidget;
class NowPlayingWidget;
class SearchWidget;
class TorrentFeedWidget;
class TrayIcon;

enum class MainWindowPage {
  Home,
  Search,
  List,
  History,
  Library,
  Torrents,
  Profile,
  AnnouncedReleases,
};

class MainWindow final : public QMainWindow {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(MainWindow)

public:
  MainWindow();
  ~MainWindow() = default;

  NavigationWidget* navigation() const;
  NowPlayingWidget* nowPlaying() const;
  QLineEdit* searchBox() const;
  Ui::MainWindow* ui() const;

  void init();
  void initUi(bool startup_blocking);
  void scheduleStartupWork();
  bool startupBlockingActive() const;
  void setStartupBlockingMode(bool on);

  // Startup pipeline helpers (used to block before first show).
  void ensurePageInitialized(MainWindowPage page);
  void runStartupPreSyncScan();
  void runStartupPostSyncScan();
  void prepareForFirstShow();

public slots:
  void addNewFolder();
  void displayWindow();
  void handleListSyncFinished(bool ok, QString message);
  void navigateTo(MainWindowPage page);
  void refreshLibraryRootsFromSettings();
  void runInteractiveLibraryScan();
  void showUserFeedback(QString message, bool error);
  /// Call after settings change the active list site or sync permissions (toolbar, search hint,
  /// nav). Does not rebuild Home or the Announced releases page — call `refreshHomeDashboard` /
  /// `refreshAnnouncedReleasesPageAfterServiceChange` when those surfaces must update.
  void refreshServiceDependentUi();
  /// Rebuilds sidebar list counts without changing the active page (`m_activePage`). Plain
  /// `NavigationWidget::refresh()` clears the tree and can leave selection on Home, which emits
  /// `currentPageChanged` and navigates away from List/Search.
  void refreshNavigationSidebar();
  void refreshAnimeListProgressDecorations();
  void refreshAnimeListNewEpisodeHighlight();
  /// Re-applies the “show mature titles” setting across list/search proxies, history, Home, and
  /// Announced releases.
  void refreshMatureContentSurfaces();
  /// Shows or hides the “sync at startup is off” notice until the first successful list sync this
  /// session.
  void updateNoStartupSyncBanner();
  /// Keeps the Enable synchronization toggle in sync with `taiga::settings` without emitting
  /// toggled.
  void applyListSynchronizationToggleFromSettings();
  /// Keeps View → Enable media detection in sync after changing recognition options in Settings.
  void applyMediaDetectionToggleFromSettings();
  void updateTitle();
  void startListSynchronization(bool queue_if_busy = false);
  void importAnimeListMalXml();
  /// Switches to Torrents, sets the toolbar query when non-empty, and runs an in-app RSS fetch.
  /// \p fallback is tried automatically if the primary search returns no results.
  /// \p anime_id provides an optional context for manual torrent search enhancements.
  void openTorrentSearchInApp(const QString& title, const QString& fallback = {}, int anime_id = 0);
  void postTrayMessage(const QString& title, const QString& message);
  void refreshTorrentCatalogAutocheckTimer();
  void resortTorrentRssTableFromSettings();
  /// Kick off the auto-download run (checks all watching anime for new episodes and downloads best
  /// match).
  void runAutoDownload(bool silent = false);
  void refreshHomeDashboard();
  /// Refreshes the Announced releases sidebar page after the active list service changes (Home
  /// banner is handled by `refreshHomeDashboard` when needed).
  void refreshAnnouncedReleasesPageAfterServiceChange();
  void refreshListColors();
  void updateAutoDownloadCountdownLabel();
  void openDataFolder();
  /// Non-modal franchise / watch-order graph for one anime (from list context menu).
  void openWatchOrderGuideForAnime(int anime_id);
  /// Refreshes the Announced releases page and Home banner after list/dismiss changes.
  void refreshAnnouncedReleasesSurfaces();
  /// Sync + auto-download after the embedded list-page watch-order panel or modeless guide edits
  /// the list.
  void applyWatchNextListSideEffects();

signals:
  void listSyncFinished(bool ok, const QString& message);
  void libraryScanFinished(const QString& reason_label, const QString& message);
  void autoDownloadFinished(int torrents_sent, int anime_total);

private slots:
  void about();
  void donate() const;
  void setPage(MainWindowPage page);
  void support() const;
  void profile();
  void statistics();
  void onTorrentCatalogAutocheckTimer();
  void onWatchOrderGuideListCommitted();
  void writeLocalListBackup();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  struct StatusMessage {
    QString text;
    bool error = false;
  };

  void initActions();
  void initIcons();
  void initNavigation();
  void initNowPlaying();
  void initPage(MainWindowPage page);
  void initStatusbar();
  void initToolbar();
  void initTrayIcon();
  void enqueueStatusMessage(QString message, bool error = false);
  void showNextQueuedStatusMessage();
  void maybeShowWelcomeSetup();
  void trySyncAfterFocusReturn();
  void updateToolbarSearchPlaceholder();
  void checkForUpdatesManually();
  enum class LibraryScanReason {
    StartupPreSync,
    StartupPostSync,
    DelayedAutoDownload,
    Watcher,
    Manual
  };
  void runLibraryScan(bool startup_silent, LibraryScanReason reason);
  void scheduleDelayedAutoDownload(int delay_minutes);
  void cancelDelayedAutoDownload(const QString& reason);
  void beginDelayedAutoDownloadRun();
  void updateAutoDownloadActionLabel();
  void checkWatchingReleaseEvent();
  void initFeatureToggleActions();
  void applyMainPage(MainWindowPage page);
  void recordNavHistory(MainWindowPage page);
  void goBackNavigation();
  void goForwardNavigation();
  void updateNavHistoryActions();
  void refreshSyncActionState();
  void finalizeListSyncSession();
  std::optional<int> animeIdForPlaybackContext() const;
  void exportAnimeListMarkdown();
  void exportAnimeListXml();
  void exportAnimeListCsv();
  void playNextEpisodeFromMenu();
  void playRandomAnimeFromMenu();
  void routeToolbarSearchToActivePage();
  void showLibraryFoldersDialog();
  void restoreViewChromeFromSession();
  void updateTrayTooltip();
  void maybeNotifyMediaDetectionBalloon(const std::optional<track::Episode>& episode);
  void ensureWatchOrderGuideWindow();
  void refreshHomeQBitPlayButtons();
  void updateHomeAnnouncedBanner();
  void initNoStartupSyncBanner();
  void setStartupBlockingActive(bool on);
  void scheduleWelcomeSetupPrompt();
  void scheduleUpdateCheckStartup();
  void scheduleLibraryScanStartup();
  void scheduleListSyncStartup();
  void initAnnouncedRelatedRefresh();
  void pauseAnnouncedRelatedRefresh();
  void scheduleAnnouncedRelatedResumeAfterSync();
  void onAnnouncedRelatedResumeTimer();
  void tryRunAnnouncedRelatedAfterStartup();
  void maybeRunAnnouncedRelatedRefresh();
  void checkAnnouncedRelatedDiffAndNotify();
  void rescheduleAnnouncedRelatedDueCheck();
  void onAnnouncedRelatedDueTimer();

  Ui::MainWindow* ui_ = nullptr;

  HistoryWidget* m_historyWidget = nullptr;
  LibraryWidget* m_libraryWidget = nullptr;
  ListWidget* m_listWidget = nullptr;
  NavigationWidget* m_navigationWidget = nullptr;
  NowPlayingWidget* m_nowPlayingWidget = nullptr;
  QAction* m_searchBoxAction = nullptr;
  QLineEdit* m_searchBox = nullptr;
  SearchWidget* m_searchWidget = nullptr;
  TorrentFeedWidget* m_torrentFeedWidget = nullptr;
  AnnouncedReleasesWidget* m_announcedReleasesWidget = nullptr;
  TrayIcon* m_trayIcon = nullptr;
  QTimer* m_catalog_autocheck_timer_ = nullptr;
  QTimer* m_home_countdown_timer_ = nullptr;  // light periodic tick for toolbar label refresh
  QTimer* m_release_event_timer_ = nullptr;   // lightweight release-event detection
  QTimer* m_delayed_autodl_timer_ = nullptr;  // single-shot delayed auto-download
  QTimer* m_home_qbit_poll_timer_ = nullptr;
  QTimer* m_announced_related_resume_timer_ = nullptr;  // post-sync delay before refresh
  QTimer* m_announced_related_diff_timer_ = nullptr;    // debounce candidate diff + notify
  QTimer* m_announced_related_due_timer_ = nullptr;     // fires when a title's 30-day cache expires
  bool m_announced_related_paused_ = false;
  QTimer* m_status_message_timer_ = nullptr;
  QTimer* m_local_backup_timer_ = nullptr;  // debounced auto-write of local MAL XML backup
  QAction* m_autoDownloadAction = nullptr;  // permanent toolbar action (all pages)
  QLabel* m_homeBodyLabel = nullptr;
  QWidget* m_homeUpNextContainer = nullptr;
  QLabel* m_homeUpNextHeader = nullptr;
  QWidget* m_homeRecentContainer = nullptr;
  QLabel* m_homeRecentHeader = nullptr;
  QWidget* m_homeAnnouncedBannerHost = nullptr;
  QWidget* m_noStartupSyncBannerHost = nullptr;
  QLabel* m_noStartupSyncBannerMessage = nullptr;

  struct HomeUpNextButton {
    QPointer<QPushButton> btn;
    QString save_path;
    int anime_id = 0;
  };
  QList<HomeUpNextButton> m_home_upnext_play_buttons_;

  MainWindowPage m_activePage = MainWindowPage::Home;
  bool m_startup_sync_done_ = false;  // true after the first sync on this run
  bool m_startup_scan_done_ = false;  // true after first startup library scan (this run)
  bool m_library_scan_in_progress_ = false;
  bool m_list_sync_in_progress_ = false;
  bool m_list_sync_queued_ = false;
  bool m_post_sync_auto_download_ = false;
  bool m_startup_auto_download_pending_ = false;
  bool m_delayed_autodl_after_sync_pending_ = false;
  bool m_delayed_autodl_after_scan_pending_ = false;
  qint64 m_delayed_autodl_scheduled_at_secs_ = 0;
  qint64 m_last_release_event_trigger_secs_ = 0;
  qint64 m_last_announced_related_check_started_secs_ = 0;
  int m_last_announced_related_fetch_count_ = 0;
  QSet<int> m_announced_related_pending_ids_;  // ids queued by the current sweep (for diag logging)

  QQueue<StatusMessage> m_status_message_queue_;

  QDate m_auto_download_fail_day_;
  QHash<int, int> m_auto_download_fail_streak_today_;
  // Avoid "pop-in" on Home at startup: when scan-on-startup is enabled, we defer the first
  // Home dashboard render until the startup-pre-sync scan finishes.
  bool m_defer_home_refresh_until_startup_scan_ = false;
  // Per-page saved search-box text so switching pages doesn't bleed one page's
  // filter text into another page's query field.
  QHash<int, QString> m_pageSearchTexts_;
  bool m_welcomeCheckScheduled = false;
  qint64 m_lastDeactivateMs = 0;
  bool m_startup_blocking_active_ = false;
  bool m_welcome_prompt_deferred_ = false;

  std::vector<MainWindowPage> m_navStack;
  int m_navHistoryPos = -1;
  bool m_navHistorySuppress = false;
  QString m_last_media_balloon_sig_;
  /// While the modal "What to watch next" dialog runs, skip `NavigationWidget::refresh()` on DB
  /// updates — refresh rebuilds/clears the tree and can crash with a modal session active.
  bool m_watch_next_modal_open_ = false;

  WatchNextDialog* m_watchOrderGuide = nullptr;
};

MainWindow* mainWindow();

}  // namespace gui
