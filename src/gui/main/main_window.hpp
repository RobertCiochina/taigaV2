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

#include <QHash>
#include <QMainWindow>
#include <optional>
#include <vector>

#include "media/anime_list.hpp"

class QLabel;
class QLineEdit;
class QAction;
class QEvent;
class QShowEvent;
class QTimer;

namespace Ui {
class MainWindow;
}

namespace track {
class Episode;
}

namespace gui {

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
  void startListSynchronization();
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

private slots:
  void about();
  void donate() const;
  void setPage(MainWindowPage page);
  void support() const;
  void profile();
  void statistics();
  void onTorrentCatalogAutocheckTimer();
  void onAutoDownloadTimer();

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
  void runLibraryScan(bool startup_silent);
  void initFeatureToggleActions();
  void applyMainPage(MainWindowPage page);
  void recordNavHistory(MainWindowPage page);
  void goBackNavigation();
  void goForwardNavigation();
  void updateNavHistoryActions();
  void refreshSyncActionState();
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
  QLabel* m_toolbarCountdownLabel = nullptr;  // permanent toolbar countdown (all pages)
  QLabel* m_homeBodyLabel = nullptr;
  QWidget* m_homeUpNextContainer = nullptr;
  QLabel* m_homeUpNextHeader = nullptr;
  QWidget* m_homeRecentContainer = nullptr;
  QLabel* m_homeRecentHeader = nullptr;

  MainWindowPage m_activePage = MainWindowPage::Home;
  bool m_startup_sync_done_ = false;  // true after the first sync on this run
  // Per-page saved search-box text so switching pages doesn't bleed one page's
  // filter text into another page's query field.
  QHash<int, QString> m_pageSearchTexts_;
  bool m_welcomeCheckScheduled = false;
  qint64 m_lastDeactivateMs = 0;

  std::vector<MainWindowPage> m_navStack;
  int m_navHistoryPos = -1;
  bool m_navHistorySuppress = false;
  QString m_last_media_balloon_sig_;
};

MainWindow* mainWindow();

}  // namespace gui
