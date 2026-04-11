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

public slots:
  void addNewFolder();
  void displayWindow();
  void handleListSyncFinished(bool ok, QString message);
  void navigateTo(MainWindowPage page);
  void refreshLibraryRootsFromSettings();
  void runInteractiveLibraryScan();
  void showUserFeedback(QString message, bool error);
  /// Call after settings change the active list site or sync permissions (toolbar, search hint, nav).
  void refreshServiceDependentUi();
  void refreshAnimeListProgressDecorations();
  void refreshAnimeListNewEpisodeHighlight();
  /// Keeps View → Enable synchronization in sync with `taiga::settings` without emitting toggled.
  void applyListSynchronizationToggleFromSettings();
  /// Keeps View → Enable media detection in sync after changing recognition options in Settings.
  void applyMediaDetectionToggleFromSettings();
  void updateTitle();
  void startListSynchronization(bool queue_if_busy = false);
  void importAnimeListMalXml();
  /// Switches to Torrents, sets the toolbar query when non-empty, and runs an in-app RSS fetch.
  /// \p fallback is tried automatically if the primary search returns no results.
  void openTorrentSearchInApp(const QString& title, const QString& fallback = {});
  void postTrayMessage(const QString& title, const QString& message);
  void refreshTorrentCatalogAutocheckTimer();
  void resortTorrentRssTableFromSettings();
  /// Kick off the auto-download run (checks all watching anime for new episodes and downloads best match).
  void runAutoDownload(bool silent = false);
  void refreshHomeDashboard();
  void refreshListColors();
  void updateAutoDownloadCountdownLabel();
  void openDataFolder();
  /// Non-modal franchise / watch-order graph for one anime (from list context menu).
  void openWatchOrderGuideForAnime(int anime_id);
  /// Sync + auto-download after the embedded list-page watch-order panel or modeless guide edits the list.
  void applyWatchNextListSideEffects();

private slots:
  void about();
  void donate() const;
  void setPage(MainWindowPage page);
  void support() const;
  void profile();
  void statistics();
  void onTorrentCatalogAutocheckTimer();
  void onAutoDownloadTimer();
  void onWatchOrderGuideListCommitted();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent *event) override;
  void showEvent(QShowEvent* event) override;

private:
  void initActions();
  void initIcons();
  void initNavigation();
  void initNowPlaying();
  void initPage(MainWindowPage page);
  void initStatusbar();
  void initToolbar();
  void initTrayIcon();
  void maybeShowWelcomeSetup();
  void trySyncAfterFocusReturn();
  void updateToolbarSearchPlaceholder();
  void checkForUpdatesManually();
  enum class LibraryScanReason { StartupPreSync, StartupPostSync, Watcher, Manual };
  void runLibraryScan(bool startup_silent, LibraryScanReason reason);
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
  TrayIcon* m_trayIcon = nullptr;
  QTimer* m_catalog_autocheck_timer_ = nullptr;
  QTimer* m_auto_download_timer_ = nullptr;
  QTimer* m_home_countdown_timer_ = nullptr;  // 1-second tick to update the countdown label
  QTimer* m_home_qbit_poll_timer_ = nullptr;
  QLabel* m_toolbarCountdownLabel = nullptr;  // permanent toolbar countdown (all pages)
  QLabel* m_homeBodyLabel = nullptr;
  QWidget* m_homeUpNextContainer = nullptr;
  QLabel* m_homeUpNextHeader = nullptr;
  QWidget* m_homeRecentContainer = nullptr;
  QLabel* m_homeRecentHeader = nullptr;

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
  bool m_upcoming_release_sync_in_progress_ = false;
  bool m_upcoming_release_auto_download_pending_ = false;
  qint64 m_last_upcoming_release_sync_trigger_secs_ = 0;

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
