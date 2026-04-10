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
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtWidgets>
#include <algorithm>
#include <anitomy.hpp>
#include <functional>
#include <memory>
#include <ranges>

#include "base/string.hpp"
#include "gui/history/history_widget.hpp"
#include "gui/library/library_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/list/watch_next_dialog.hpp"
#include "gui/main/about_dialog.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/main/stats_dialog.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/torrent/torrent_feed_widget.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/tray_icon.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_list_import.hpp"
#include "sync/anilist.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
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
  taiga::setUserFeedbackHandler([](const QString& msg, const bool err) {
    if (auto* w = mainWindow()) {
      QMetaObject::invokeMethod(w, "showUserFeedback", Qt::QueuedConnection, Q_ARG(QString, msg),
                                Q_ARG(bool, err));
    }
  });

  // Load last-known library availability index as early as possible so Home "Up next" can populate
  // immediately when the first page is initialized (before any startup scan runs).
  if (taiga::settings.scanLibraryOnStartup()) {
    const bool ok = track::loadLibraryEpisodeIndexCache();
    // We can't use the status bar yet (initStatusbar runs later). Stash a short note now and show
    // it once the UI is ready.
    const QString note =
        ok ? track::libraryEpisodeIndexCacheLastInfo()
           : tr("Library cache: %1").arg(track::libraryEpisodeIndexCacheLastError());
    QTimer::singleShot(0, this, [this, note]() {
      if (statusBar()) statusBar()->showMessage(note, 5000);
    });
  }

  initActions();
  initIcons();
  initTrayIcon();
  initToolbar();
  initNavigation();

  // Keep sidebar counts and Home dashboard in sync after list edits (Media dialog, menus, etc.).
  connect(&anime::db, &anime::Database::entryUpdated, this, [this](int) {
    if (m_navigationWidget) m_navigationWidget->refresh();
    refreshHomeDashboard();
  });
  connect(&anime::db, &anime::Database::itemUpdated, this, [this](int) {
    if (m_navigationWidget) m_navigationWidget->refresh();
    refreshHomeDashboard();
  });

  if (const QByteArray splitter_state = taiga::session.mainWindowSplitterState();
      !splitter_state.isEmpty()) {
    ui_->splitter->restoreState(splitter_state);
  }
  initStatusbar();
  initNowPlaying();
  restoreViewChromeFromSession();
  updateTitle();
  updateToolbarSearchPlaceholder();

  if (taiga::settings.syncAutoOnStart() && taiga::settings.listSynchronizationEnabled()) {
    QTimer::singleShot(0, this, &MainWindow::startListSynchronization);
  }
  if (taiga::settings.checkForUpdatesOnStartup()) {
    QTimer::singleShot(2200, this, [this]() { taiga::checkForUpdates(this, true); });
  }
  if (taiga::settings.scanLibraryOnStartup()) {
    // Run a quick scan immediately so Home "Up next" has minimal downtime on startup.
    // If a sync also runs, we will do a second scan after sync finishes (so recognition uses the
    // updated title DB) before auto-download.
    QTimer::singleShot(0, this, [this]() {
      if (m_startup_scan_done_) return;

      const auto startPreSyncScan = [this]() {
        if (m_startup_scan_done_) return;
        m_startup_scan_done_ = true;
        // Ensure recognition cache incorporates any newly-fetched Media entries.
        track::recognition::cache()->clear();
        runLibraryScan(true, LibraryScanReason::StartupPreSync);
      };

      // If the cached library episode index references anime ids that are not present in the local
      // anime DB yet (common for specials), prefetch them so recognition can resolve those titles
      // even before the full startup sync completes.
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

      QPointer<MainWindow> guard(this);
      auto* svc = sync::anilist::Service::instance();
      auto remaining = std::make_shared<int>(ids.size());

      const auto tryFinish = [guard, remaining, startPreSyncScan]() {
        if (!guard) return;
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

      QTimer::singleShot(2500, this, [remaining, conn, tryFinish]() mutable {
        // Timeout: stop waiting and proceed with scan.
        disconnect(conn);
        *remaining = 0;
        tryFinish();
      });

      for (int id : ids) {
        svc->fetchAnime(id);
      }
    });
  }

  connect(track::media::detection(), &track::media::Detection::currentEpisodeChanged, this,
          [this](const std::optional<track::Episode>& ep) {
            updateTrayTooltip();
            maybeNotifyMediaDetectionBalloon(ep);
          });

  connect(
      track::libraryFolderWatcher(), &track::LibraryFolderWatcher::debouncedRescanTriggered, this,
      [this]() {
        statusBar()->showMessage(tr("Library folders changed — rescanning…"), 4000);
        runLibraryScan(true, LibraryScanReason::Watcher);
      },
      Qt::QueuedConnection);

  initFeatureToggleActions();

  m_catalog_autocheck_timer_ = new QTimer(this);
  m_catalog_autocheck_timer_->setTimerType(Qt::VeryCoarseTimer);
  connect(m_catalog_autocheck_timer_, &QTimer::timeout, this,
          &MainWindow::onTorrentCatalogAutocheckTimer);
  refreshTorrentCatalogAutocheckTimer();

  // Auto-download every 2 hours (silent, no confirmation when timer fires).
  m_auto_download_timer_ = new QTimer(this);
  m_auto_download_timer_->setTimerType(Qt::VeryCoarseTimer);
  m_auto_download_timer_->setInterval(2 * 60 * 60 * 1000);  // 2 hours
  connect(m_auto_download_timer_, &QTimer::timeout, this, &MainWindow::onAutoDownloadTimer);
  m_auto_download_timer_->start();

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

void MainWindow::initActions() {
  ui_->actionProfile->setToolTip(tr("Profile"));
  ui_->actionSynchronize->setToolTip(
      tr("Synchronize with %1").arg(sync::serviceName(sync::currentServiceId())));

  connect(ui_->actionAddNewFolder, &QAction::triggered, this, &MainWindow::addNewFolder);
  // Use window-close path so close-to-tray and "save on close" behavior runs consistently.
  connect(ui_->actionExit, &QAction::triggered, this, [this]() { close(); }, Qt::QueuedConnection);
  connect(ui_->actionOpenDataFolder, &QAction::triggered, this, &MainWindow::openDataFolder);
  connect(ui_->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog::show(this); });
  ui_->actionSettings->setToolTip(
      tr("Preferences (%1)")
          .arg(QKeySequence(QKeySequence::Preferences).toString(QKeySequence::NativeText)));
  connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::about);
  connect(ui_->actionDonate, &QAction::triggered, this, &MainWindow::donate);
  connect(ui_->actionSupport, &QAction::triggered, this, &MainWindow::support);
  connect(ui_->actionProfile, &QAction::triggered, this, &MainWindow::profile);
  connect(ui_->actionStatistics, &QAction::triggered, this, &MainWindow::statistics);
  connect(ui_->actionDisplayWindow, &QAction::triggered, this, &MainWindow::displayWindow);

  connect(ui_->actionSynchronize, &QAction::triggered, this, &MainWindow::startListSynchronization);
  ui_->actionSynchronize->setShortcuts(
      {QKeySequence{QKeySequence::Refresh}, QKeySequence{Qt::CTRL | Qt::Key_S}});
  ui_->actionSynchronize->setShortcutContext(Qt::ApplicationShortcut);
  ui_->actionSynchronize->setStatusTip(tr("Download your list from %1 (F5 or Ctrl+S).")
                                           .arg(sync::serviceName(sync::currentServiceId())));
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
  ui_->actionOpenDataFolder->setText(tr("Open anime folder"));
  ui_->actionOpenDataFolder->setToolTip(tr("Open the configured anime library folder in Explorer"));
  ui_->actionExit->setIcon(theme.getIcon("logout"));
  ui_->actionForward->setIcon(theme.getIcon("arrow_forward"));
  ui_->actionLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->actionMenu->setIcon(theme.getIcon("menu"));
  ui_->actionPlayNextEpisode->setIcon(theme.getIcon("skip_next"));
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
    // Keep selection on the List page (avoids leaving the sidebar highlight on an action row).
    navigateTo(MainWindowPage::List);
    applyMainPage(MainWindowPage::List);

    WatchNextDialog dlg(this);
    dlg.runModalRandomPlanningSession();
    dlg.exec();
    if (!dlg.didChangeList()) return;
    applyWatchNextListSideEffects();
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

  // Auto-download countdown label — visible on every page, right of the Scan button.
  {
    m_toolbarCountdownLabel = new QLabel(this);
    m_toolbarCountdownLabel->setTextFormat(Qt::RichText);
    m_toolbarCountdownLabel->setContentsMargins(8, 0, 4, 0);
    m_toolbarCountdownLabel->setCursor(Qt::PointingHandCursor);
    m_toolbarCountdownLabel->setToolTip(tr("Click to run auto-download now"));
    m_toolbarCountdownLabel->installEventFilter(this);

    // Place it right after the Scan action.
    ui_->toolbar->insertWidget(ui_->actionSynchronize, m_toolbarCountdownLabel);

    // 1-second timer shared between toolbar label and Home page label.
    if (!m_home_countdown_timer_) {
      m_home_countdown_timer_ = new QTimer(this);
      m_home_countdown_timer_->setInterval(1000);
      connect(m_home_countdown_timer_, &QTimer::timeout, this,
              &MainWindow::updateAutoDownloadCountdownLabel);
      m_home_countdown_timer_->start();
    }
    updateAutoDownloadCountdownLabel();
  }
}

void MainWindow::initTrayIcon() {
  auto menu = new QMenu(this);
  menu->addAction(ui_->actionDisplayWindow);
  menu->setDefaultAction(ui_->actionDisplayWindow);
  menu->addSeparator();
  menu->addAction(ui_->actionSynchronize);
  menu->addAction(ui_->actionScanAvailableEpisodes);
  menu->addAction(ui_->actionPlayNextEpisode);
  menu->addSeparator();
  menu->addAction(ui_->actionToggleDetection);
  menu->addAction(ui_->actionToggleSynchronization);
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

void MainWindow::openTorrentSearchInApp(const QString& title, const QString& fallback) {
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
  });

  refreshSyncActionState();
  updateNavHistoryActions();
}

void MainWindow::refreshSyncActionState() {
  const bool can_sync = sync::currentServiceId() != sync::ServiceId::Unknown &&
                        taiga::settings.listSynchronizationEnabled();
  ui_->actionSynchronize->setEnabled(can_sync);
}

void MainWindow::refreshServiceDependentUi() {
  const QString svc = sync::serviceName(sync::currentServiceId());
  ui_->actionSynchronize->setToolTip(tr("Synchronize with %1").arg(svc));
  ui_->actionSynchronize->setStatusTip(tr("Download your list from %1 (F5 or Ctrl+S).").arg(svc));
  refreshSyncActionState();
  updateToolbarSearchPlaceholder();
  if (m_navigationWidget) m_navigationWidget->refresh();
  refreshHomeDashboard();
  updateTrayTooltip();
  if (m_listWidget) m_listWidget->refreshListTitleDisplay();
  if (m_searchWidget) m_searchWidget->refreshListTitleDisplay();
}

void MainWindow::refreshAnimeListProgressDecorations() {
  if (m_listWidget) m_listWidget->refreshProgressColumnDisplay();
  if (m_searchWidget) m_searchWidget->refreshProgressColumnDisplay();
}

void MainWindow::refreshAnimeListNewEpisodeHighlight() {
  if (m_listWidget) m_listWidget->refreshNewEpisodeHighlightDisplay();
  if (m_searchWidget) m_searchWidget->refreshNewEpisodeHighlightDisplay();
}

void MainWindow::applyListSynchronizationToggleFromSettings() {
  const QSignalBlocker b(ui_->actionToggleSynchronization);
  ui_->actionToggleSynchronization->setChecked(taiga::settings.listSynchronizationEnabled());
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
    statusBar()->showMessage(tr("Exported list to %1").arg(path), 6000);
  } else {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not write the export file."));
  }
}

void MainWindow::exportAnimeListXml() {
  const QString def =
      QDir::home().filePath(u"animelist_%1.xml"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export anime list as MyAnimeList XML"), def, tr("XML (*.xml);;All files (*)"));
  if (path.isEmpty()) return;
  if (anime::list::exportAsXml(path.toStdString())) {
    statusBar()->showMessage(tr("Exported list to %1").arg(path), 6000);
  } else {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not write the export file."));
  }
}

void MainWindow::exportAnimeListCsv() {
  const QString def =
      QDir::home().filePath(u"animelist_%1.csv"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
  const QString path = QFileDialog::getSaveFileName(this, tr("Export anime list as CSV"), def,
                                                    tr("CSV (*.csv);;All files (*)"));
  if (path.isEmpty()) return;
  if (anime::list::exportAsCsv(path.toStdString())) {
    statusBar()->showMessage(tr("Exported list to %1").arg(path), 6000);
  } else {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not write the export file."));
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

  if (m_navigationWidget) m_navigationWidget->refresh();
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
  statusBar()->showMessage(msg, 12000);
}

void MainWindow::playNextEpisodeFromMenu() {
  if (const auto id = animeIdForPlaybackContext()) {
    if (track::playNextEpisode(*id)) {
      statusBar()->showMessage(tr("Playing next episode…"), 4000);
      return;
    }
    QMessageBox::information(
        this, tr("Taiga"),
        tr("Could not find the next episode file in your library folders for this title."));
    return;
  }
  QMessageBox::information(
      this, tr("Taiga"),
      tr("Select a title on your anime list, a recognized file in the Library, or start playback "
         "with media detection enabled so Taiga knows which title to use."));
}

void MainWindow::playRandomAnimeFromMenu() {
  if (track::playRandomFromListing()) {
    statusBar()->showMessage(tr("Playing a random title from your list…"), 4000);
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

void MainWindow::startListSynchronization() {
  if (sync::currentServiceId() == sync::ServiceId::Unknown) {
    return;
  }
  if (!taiga::settings.listSynchronizationEnabled()) {
    statusBar()->showMessage(tr("Synchronization is disabled (Tools → Enable synchronization)."),
                             5000);
    return;
  }

  QPointer<MainWindow> guard(this);
  statusBar()->showMessage(
      tr("Synchronizing with %1...").arg(sync::serviceName(sync::currentServiceId())));
  ui_->actionSynchronize->setEnabled(false);

  sync::fetchListEntries([guard](const bool ok, const QString& message) {
    if (!guard) return;
    QMetaObject::invokeMethod(guard.data(), "handleListSyncFinished", Qt::QueuedConnection,
                              Q_ARG(bool, ok), Q_ARG(QString, message));
  });
}

void MainWindow::handleListSyncFinished(bool ok, QString message) {
  refreshSyncActionState();
  if (ok) {
    // Invalidate the recognition cache so newly-synced titles are indexed
    // on the next library scan / media-detection lookup.
    track::recognition::cache()->clear();
    if (m_navigationWidget) m_navigationWidget->refresh();
    if (m_listWidget) m_listWidget->reloadAnimeList();
    if (m_searchWidget) m_searchWidget->reloadAnimeList();
    refreshHomeDashboard();
    statusBar()->showMessage(message.isEmpty() ? tr("Synchronized.") : message, 5000);

    // On the very first sync after startup, trigger a silent auto-download
    // so new episodes are picked up right away without waiting 2 hours.
    if (!m_startup_sync_done_) {
      m_startup_sync_done_ = true;
      // Startup order (when enabled): scan → sync → scan → auto-download.
      // We do an immediate scan in init() for Home uptime, then scan again after sync so
      // recognition uses the updated title DB before auto-download runs.
      if (taiga::settings.scanLibraryOnStartup()) {
        // Don't start auto-download until the scan completes, otherwise the library index can be
        // stale and we'd re-download episodes that are already on disk.
        m_startup_auto_download_pending_ = true;
        runLibraryScan(true, LibraryScanReason::StartupPostSync);
        return;
      }
      QTimer::singleShot(0, this, [this]() { runAutoDownload(true); });
    }

    if (m_upcoming_release_auto_download_pending_) {
      m_upcoming_release_auto_download_pending_ = false;
      QTimer::singleShot(0, this, [this]() { runAutoDownload(true); });
    }
  } else {
    statusBar()->showMessage(tr("Synchronization failed: %1").arg(message), 8000);
  }

  m_upcoming_release_sync_in_progress_ = false;
}

void MainWindow::showUserFeedback(QString message, bool error) {
  statusBar()->showMessage(message, error ? 12000 : 6000);
  if (error) QMessageBox::warning(this, tr("Taiga"), message);
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
  const auto folders = taiga::settings.libraryFolders();
  if (folders.empty()) {
    if (!startup_silent) {
      QMessageBox::information(this, tr("Taiga"), tr("No library folders are configured."));
    }
    return;
  }

  const auto reasonLabel = [reason]() -> QString {
    switch (reason) {
      case LibraryScanReason::StartupPreSync:
        return QStringLiteral("startup-pre-sync");
      case LibraryScanReason::StartupPostSync:
        return QStringLiteral("startup-post-sync");
      case LibraryScanReason::Watcher:
        return QStringLiteral("watcher");
      case LibraryScanReason::Manual:
      default:
        return QStringLiteral("manual");
    }
  }();
  track::appendLibraryEpisodeIndexCacheDebugLine(
      QStringLiteral("scan: begin (%1, silent=%2)").arg(reasonLabel).arg(startup_silent ? 1 : 0));

  statusBar()->showMessage(tr("Scanning library folders…"));

  // Startup (and watcher-triggered) scans should not block the UI for seconds.
  if (startup_silent) {
    m_library_scan_in_progress_ = true;
    QPointer<MainWindow> guard(this);

    struct ScanJob final : public QRunnable {
      QPointer<MainWindow> w;
      std::vector<std::string> folders;
      QString reasonLabel;
      void run() override {
        constexpr int kMaxEntriesLocal = 50'000;
        const bool allowApply = (reasonLabel != QStringLiteral("startup-pre-sync"));
        const track::LibraryScanSummary sum = track::scanLibraryFolders(
            folders, kMaxEntriesLocal, /*allow_regress_apply=*/allowApply);
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
              if (sum.entries_visited >= kMaxEntries) {
                msg += QObject::tr(" Scan stopped at safety limit.");
              }
              w->statusBar()->showMessage(msg, 6000);
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
              if (w->m_startup_auto_download_pending_) {
                w->m_startup_auto_download_pending_ = false;
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
    return;
  }

  const bool allowApply = (reasonLabel != QStringLiteral("startup-pre-sync"));
  const track::LibraryScanSummary sum =
      track::scanLibraryFolders(folders, kMaxEntries, /*allow_regress_apply=*/allowApply);

  QString msg = tr("Library scan: %1 video file(s), %2 recognized, %3 series with local episodes "
                   "(visited %4 paths).")
                    .arg(sum.video_files)
                    .arg(sum.recognized)
                    .arg(sum.series_with_local_episodes)
                    .arg(sum.entries_visited);
  if (sum.entries_visited >= kMaxEntries) {
    msg += tr(" Scan stopped at safety limit.");
  }
  statusBar()->showMessage(msg, startup_silent ? 6000 : 8000);
  if (!startup_silent) {
    QMessageBox::information(this, tr("Taiga"), msg);
  }
  refreshAnimeListProgressDecorations();
  refreshAnimeListNewEpisodeHighlight();
  // Keep the Home "Up next" section in sync after every scan (watcher or interactive).
  refreshHomeDashboard();

  track::appendLibraryEpisodeIndexCacheDebugLine(
      QStringLiteral("scan: end (%1) => %2 series, %3 recognized")
          .arg(reasonLabel)
          .arg(sum.series_with_local_episodes)
          .arg(sum.recognized));
  const bool allowRegress = (reasonLabel != QStringLiteral("startup-pre-sync"));
  track::saveLibraryEpisodeIndexCacheAfterScan(reasonLabel, allowRegress);
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

void MainWindow::onAutoDownloadTimer() {
  runAutoDownload(/*silent=*/true);
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
  if (!m_torrentFeedWidget) return;

  // Collect anime on the Watching list where an episode has aired but is not yet on disk.
  struct Candidate {
    int anime_id;
    QString english_title;
    QString romaji_title;
    QString folder_name;  // always English title (or romaji if no English)
  };
  QList<Candidate> candidates;
  QStringList skipped_twice_today_labels;
  const bool skip_failed = taiga::settings.torrentAutoDownloadSkipAfterTwoFailuresToday();
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
    const qint64 now_secs = QDateTime::currentSecsSinceEpoch();
    // Only queue downloads for episodes that have actually aired.
    // If the next episode air time is in the future and we don't have a reliable last-aired value,
    // treat this as "nothing new aired yet" to avoid early RSS hits for upcoming series.
    int last_aired = item->last_aired_episode;
    if (last_aired <= 0) {
      if (item->next_episode_time > now_secs) {
        last_aired = entry.watched_episodes;
      } else {
        // Fallback for titles without schedule metadata (or where next_episode_time is unset/0):
        // use episode_count as an upper bound, which is best-effort (may overestimate for
        // unreleased shows).
        last_aired = item->episode_count;
      }
    }
    const int watched = entry.watched_episodes;
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
      statusBar()->showMessage(summary, 10000);
      // Tray notification so both manual and timer-triggered runs are visible.
      if (state->found > 0) postTrayMessage(tr("Auto-download"), summary);
      return;
    }
    const auto c = state->queue.takeFirst();
    const QString label = c.english_title.isEmpty() ? c.romaji_title : c.english_title;
    statusBar()->showMessage(tr("Auto-download: fetching RSS for %1 (%2 remaining)…")
                                 .arg(label)
                                 .arg(state->queue.size() + 1),
                             0);
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
            statusBar()->showMessage(tr("Sent %1 episode(s) for %2.").arg(count).arg(label), 3000);
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
      upNext.push_back({QString::fromStdString(anime::preferredListTitleString(
                            *item, anime::TitleLanguage::English)),
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
            statusBar()->showMessage(tr("Episode not found in library folders."), 3000);
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
        QString when;
        if (days == 0)
          when = hours > 0 ? tr("in %1 h").arg(hours) : tr("today");
        else if (days == 1)
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
        whenLbl->setFixedWidth(90);

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
}

void MainWindow::refreshListColors() {
  if (m_listWidget) {
    m_listWidget->refreshNewEpisodeHighlightDisplay();
  }
  if (m_searchWidget) m_searchWidget->refreshNewEpisodeHighlightDisplay();
}

void MainWindow::updateAutoDownloadCountdownLabel() {
  if (!m_auto_download_timer_) return;
  const int remaining_ms = m_auto_download_timer_->remainingTime();

  QString toolbarText;
  if (remaining_ms <= 0) {
    toolbarText = tr("<span style=\"color:#888\">⬇ running…</span>");
  } else {
    const int total_secs = remaining_ms / 1000;
    const int h = total_secs / 3600;
    const int m = (total_secs % 3600) / 60;
    const int s = total_secs % 60;
    QString when;
    if (h > 0)
      when = tr("%1h %2m").arg(h).arg(m);
    else if (m > 0)
      when = tr("%1m %2s").arg(m).arg(s);
    else
      when = tr("%1s").arg(s);
    toolbarText = tr("<span style=\"font-size:large;font-weight:600\">⬇ %1</span>").arg(when);
  }

  if (m_toolbarCountdownLabel) m_toolbarCountdownLabel->setText(toolbarText);

  // If the soonest upcoming episode for a Watching title has just crossed "now", trigger a silent
  // resync so `next_episode_time` and related metadata refresh, then run a silent auto-download.
  // Debounced so it fires at most once per minute.
  if (taiga::settings.listSynchronizationEnabled() &&
      sync::currentServiceId() != sync::ServiceId::Unknown && sync::remoteListAccessConfigured() &&
      !m_upcoming_release_sync_in_progress_) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_last_upcoming_release_sync_trigger_secs_ == 0) {
      m_last_upcoming_release_sync_trigger_secs_ = now;
    }
    if (now - m_last_upcoming_release_sync_trigger_secs_ >= 60) {
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
        m_last_upcoming_release_sync_trigger_secs_ = now;
        m_upcoming_release_sync_in_progress_ = true;
        m_upcoming_release_auto_download_pending_ = true;
        statusBar()->showMessage(tr("Episode released — synchronizing…"), 4000);
        startListSynchronization();
      }
    }
  }
}

void MainWindow::restoreViewChromeFromSession() {
  // Sidebar is always-on.
  if (m_navigationWidget) m_navigationWidget->setVisible(true);
  {
    const QSignalBlocker b(ui_->actionToggleNavigationSidebar);
    ui_->actionToggleNavigationSidebar->setChecked(true);
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
    QMessageBox::information(this, tr("Taiga"),
                             tr("No anime library folder is configured.\n"
                                "Add one in Settings → Library."));
    return;
  }
  if (!QDir{}.mkpath(path)) {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not create or access the anime folder."));
    return;
  }
  const QUrl url = QUrl::fromLocalFile(path.endsWith(u'/') ? path : path + u'/');
  if (!QDesktopServices::openUrl(url)) {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not open the anime folder."));
    return;
  }
  statusBar()->showMessage(tr("Opened anime folder: %1").arg(path), 4000);
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
  statusBar()->showMessage(tr("Library folders updated."), 4000);
}

void MainWindow::applyWatchNextListSideEffects() {
  if (sync::currentServiceId() != sync::ServiceId::Unknown &&
      taiga::settings.listSynchronizationEnabled()) {
    QPointer<MainWindow> guard(this);
    statusBar()->showMessage(
        tr("Synchronizing with %1…").arg(sync::serviceName(sync::currentServiceId())));
    ui_->actionSynchronize->setEnabled(false);
    sync::fetchListEntries([guard](const bool ok, const QString& message) {
      if (!guard) return;
      QMetaObject::invokeMethod(guard.data(), "handleListSyncFinished", Qt::QueuedConnection,
                                Q_ARG(bool, ok), Q_ARG(QString, message));
      if (ok && guard) {
        QTimer::singleShot(0, guard.data(), [guard]() {
          if (guard) guard->runAutoDownload(true);
        });
      }
    });
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_toolbarCountdownLabel && event->type() == QEvent::MouseButtonPress) {
    runAutoDownload(false);
    return true;
  }
  return QMainWindow::eventFilter(watched, event);
}

}  // namespace gui
