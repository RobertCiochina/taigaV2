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
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVBoxLayout>
#include <QtWidgets>

#include <algorithm>
#include <functional>
#include <memory>
#include <ranges>

#include <anitomy.hpp>

#include "base/string.hpp"
#include "gui/history/history_widget.hpp"
#include "gui/library/library_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/main/about_dialog.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/main/stats_dialog.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/torrent/torrent_feed_widget.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/tray_icon.hpp"
#include "gui/utils/widgets.hpp"
#include "sync/service.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_list_import.hpp"
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
#include "media/anime_history.hpp"
#include "media/anime_list.hpp"
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
    // First run (or no session): center like v1's CenterOwner() for a new default placement.
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

  initActions();
  initIcons();
  initTrayIcon();
  initToolbar();
  initNavigation();
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
    QTimer::singleShot(2800, this, [this]() { runLibraryScan(true); });
  }

  connect(track::media::detection(), &track::media::Detection::currentEpisodeChanged, this,
          [this](const std::optional<track::Episode>& ep) {
            updateTrayTooltip();
            maybeNotifyMediaDetectionBalloon(ep);
          });

  connect(track::libraryFolderWatcher(), &track::LibraryFolderWatcher::debouncedRescanTriggered, this,
          [this]() {
            statusBar()->showMessage(tr("Library folders changed — rescanning…"), 4000);
            runLibraryScan(true);
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
  connect(ui_->actionExit, &QAction::triggered, this, &QApplication::quit, Qt::QueuedConnection);
  connect(ui_->actionOpenDataFolder, &QAction::triggered, this, &MainWindow::openDataFolder);
  connect(ui_->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog::show(this); });
  ui_->actionSettings->setToolTip(
      tr("Preferences (%1)").arg(QKeySequence(QKeySequence::Preferences).toString(QKeySequence::NativeText)));
  connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::about);
  connect(ui_->actionDonate, &QAction::triggered, this, &MainWindow::donate);
  connect(ui_->actionSupport, &QAction::triggered, this, &MainWindow::support);
  connect(ui_->actionProfile, &QAction::triggered, this, &MainWindow::profile);
  connect(ui_->actionStatistics, &QAction::triggered, this, &MainWindow::statistics);
  connect(ui_->actionDisplayWindow, &QAction::triggered, this, &MainWindow::displayWindow);

  connect(ui_->actionSynchronize, &QAction::triggered, this, &MainWindow::startListSynchronization);
  ui_->actionSynchronize->setShortcuts({QKeySequence{QKeySequence::Refresh},
                                        QKeySequence{Qt::CTRL | Qt::Key_S}});
  ui_->actionSynchronize->setShortcutContext(Qt::ApplicationShortcut);
  ui_->actionSynchronize->setStatusTip(
      tr("Download your list from %1 (F5 or Ctrl+S).").arg(sync::serviceName(sync::currentServiceId())));
  connect(ui_->actionCheckForUpdates, &QAction::triggered, this, &MainWindow::checkForUpdatesManually);
  connect(ui_->actionScanAvailableEpisodes, &QAction::triggered, this, [this]() {
    runLibraryScan(false);
  });

  connect(ui_->actionExportListAsMarkdown, &QAction::triggered, this,
          &MainWindow::exportAnimeListMarkdown);
  connect(ui_->actionExportListAsMyAnimeListXML, &QAction::triggered, this,
          &MainWindow::exportAnimeListXml);
  connect(ui_->actionExportListAsCsv, &QAction::triggered, this, &MainWindow::exportAnimeListCsv);
  connect(ui_->actionImportListFromMalXml, &QAction::triggered, this, &MainWindow::importAnimeListMalXml);

  connect(ui_->actionPlayNextEpisode, &QAction::triggered, this, &MainWindow::playNextEpisodeFromMenu);
  connect(ui_->actionPlayRandomAnime, &QAction::triggered, this, &MainWindow::playRandomAnimeFromMenu);

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
      tr("Show or hide the left navigation pane (Ctrl+B). Matches Taiga v1 View → sidebar."));
  connect(ui_->actionToggleNavigationSidebar, &QAction::toggled, this, [this](const bool on) {
    if (m_navigationWidget) m_navigationWidget->setVisible(on);
    taiga::settings.setNavigationSidebarVisible(on);
  });
  connect(ui_->actionLibraryFolders, &QAction::triggered, this, &MainWindow::showLibraryFoldersDialog);
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
        auto* upNextHeader = new QLabel(tr("<span style=\"font-size:large\"><b>▶ Up next — episodes ready to watch</b></span>"), body);
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

        // ── Auto-download countdown ───────────────────────────────────────────
        lay->addSpacing(8);
        auto* adLbl = new QLabel(body);
        adLbl->setTextFormat(Qt::RichText);
        adLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_homeAutoDownloadLabel = adLbl;
        lay->addWidget(adLbl);

        // The countdown label here is a secondary display — the toolbar label is the primary one.
        // The shared timer (m_home_countdown_timer_) was already created in initToolbar().
        updateAutoDownloadCountdownLabel();

        // ── Action buttons ───────────────────────────────────────────────────
        lay->addSpacing(8);
        auto* actionsRow = new QHBoxLayout();
        auto* sync_btn = new QPushButton(tr("Synchronize now"), body);
        connect(sync_btn, &QPushButton::clicked, this, &MainWindow::startListSynchronization);
        auto* scan_btn = new QPushButton(tr("Scan episodes"), body);
        connect(scan_btn, &QPushButton::clicked, this, &MainWindow::runInteractiveLibraryScan);
        auto* ad_btn = new QPushButton(tr("Auto-download now"), body);
        connect(ad_btn, &QPushButton::clicked, this, [this]() { runAutoDownload(false); });
        auto* settings_btn = new QPushButton(tr("Settings…"), body);
        connect(settings_btn, &QPushButton::clicked, this,
                [this]() { SettingsDialog::show(this); });
        actionsRow->addWidget(sync_btn);
        actionsRow->addWidget(scan_btn);
        actionsRow->addWidget(ad_btn);
        actionsRow->addWidget(settings_btn);
        actionsRow->addStretch(1);
        lay->addLayout(actionsRow);

        lay->addStretch(1);
        scroll->setWidget(body);
        outerLayout->addWidget(scroll);

        refreshHomeDashboard();
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
        const QString user = QString::fromStdString(
            taiga::accounts.serviceUsername(taiga::settings.service()));
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
      view_menu->addAction(ui_->actionToggleNavigationSidebar);
      view_menu->addAction(ui_->actionToggleStatusbar);
      view_menu->addAction(ui_->actionToggleNowPlaying);
      view_menu->addSeparator();
      view_menu->addAction(ui_->actionToggleDetection);
      view_menu->addAction(ui_->actionToggleSynchronization);
      menu->addSeparator();
      auto* act_autodownload = menu->addAction(tr("Auto-download new episodes…"));
      act_autodownload->setToolTip(
          tr("Search and download best-seeded torrent for each Watching anime with unreleased episodes."));
      connect(act_autodownload, &QAction::triggered, this, [this]() { runAutoDownload(false); });
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
    ui_->toolbar->insertWidget(before, m_searchBox);
    insertSpacer(before);

    connect(m_searchBox, &QLineEdit::textChanged, this, &MainWindow::routeToolbarSearchToActivePage);
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
  runLibraryScan(false);
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
  ui_->actionSynchronize->setStatusTip(
      tr("Download your list from %1 (F5 or Ctrl+S).").arg(svc));
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
  const QString def = QDir::home().filePath(
      u"animelist_%1.md"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
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
  const QString def = QDir::home().filePath(
      u"animelist_%1.xml"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
  const QString path =
      QFileDialog::getSaveFileName(this, tr("Export anime list as MyAnimeList XML"), def,
                                   tr("XML (*.xml);;All files (*)"));
  if (path.isEmpty()) return;
  if (anime::list::exportAsXml(path.toStdString())) {
    statusBar()->showMessage(tr("Exported list to %1").arg(path), 6000);
  } else {
    QMessageBox::warning(this, tr("Taiga"), tr("Could not write the export file."));
  }
}

void MainWindow::exportAnimeListCsv() {
  const QString def = QDir::home().filePath(
      u"animelist_%1.csv"_s.arg(QDate::currentDate().toString(Qt::ISODate)));
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
    msg += u" "_s +
           tr("Skipped %1 (no local anime with that id).").arg(r.skipped_unknown_anime);
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
    statusBar()->showMessage(tr("Synchronization is disabled (Tools → Enable synchronization)."), 5000);
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
      QTimer::singleShot(3000, this, [this]() { runAutoDownload(true); });
    }
  } else {
    statusBar()->showMessage(tr("Synchronization failed: %1").arg(message), 8000);
  }
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

void MainWindow::runLibraryScan(const bool startup_silent) {
  constexpr int kMaxEntries = 50'000;
  const auto folders = taiga::settings.libraryFolders();
  if (folders.empty()) {
    if (!startup_silent) {
      QMessageBox::information(this, tr("Taiga"), tr("No library folders are configured."));
    }
    return;
  }

  statusBar()->showMessage(tr("Scanning library folders…"));
  const track::LibraryScanSummary sum = track::scanLibraryFolders(folders, kMaxEntries);

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
          QString::fromStdString(taiga::settings.mediaNotifyBalloonFormatRecognized()), *episode, item);
      if (body.trimmed().isEmpty()) {
        const QString displayTitle = QString::fromStdString(
            anime::preferredListTitleString(*item, taiga::settings.listTitleLanguage()));
        body = item->episode_count > 1
                   ? tr("%1 — episode %2").arg(displayTitle, epn)
                   : displayTitle;
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
          anime::preferredListTitleString(*item, taiga::settings.listTitleLanguage()));
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

void MainWindow::runAutoDownload(const bool silent) {
  if (!m_torrentFeedWidget) return;

  // Collect anime on the Watching list where an episode has aired but is not yet on disk.
  struct Candidate {
    int anime_id;
    QString english_title;
    QString romaji_title;
    QString folder_name;    // always English title (or romaji if no English)
  };
  QList<Candidate> candidates;
  for (const auto& [anime_id, entry] : anime::db.entries().asKeyValueRange()) {
    if (entry.status != anime::list::Status::Watching) continue;
    const auto* item = anime::db.item(anime_id);
    if (!item) continue;
    // Use episode_count as a fallback when last_aired_episode is not populated
    // (e.g. completed series whose airing schedule was not tracked in detail).
    const int last_aired = item->last_aired_episode > 0
                               ? item->last_aired_episode
                               : item->episode_count;
    const int watched = entry.watched_episodes;
    if (last_aired <= watched) continue;
    // Skip only if every episode in [watched+1 .. last_aired] is already on disk.
    // This correctly handles "episodes downloaded but not yet watched" —
    // e.g. eps 1-3 on disk (unwatched), ep 4 not on disk → ep 4 should still be queued.
    bool has_missing = false;
    for (int ep = watched + 1; ep <= last_aired; ++ep) {
      if (!track::libraryHasLocalEpisode(anime_id, ep)) { has_missing = true; break; }
    }
    if (!has_missing) continue;

    const QString en = QString::fromStdString(item->titles.english);
    const QString romaji = QString::fromStdString(item->titles.romaji);
    const QString folder = en.isEmpty() ? romaji : en;
    if (!en.isEmpty() || !romaji.isEmpty()) {
      candidates.push_back(Candidate{anime_id, en, romaji, folder});
    }
  }

  if (candidates.isEmpty()) {
    if (!silent) {
      QMessageBox::information(this, tr("Auto-download"),
                               tr("No anime require episode downloads right now.\n"
                                  "All watching entries are either caught up or the next episode is already on disk."));
    }
    return;
  }

  if (!silent) {
    QString msg = tr("The following anime have unreleased/undownloaded episodes. "
                     "The app will try multiple title variants and download the best matching "
                     "torrent for each:\n\n");
    for (const auto& c : candidates) {
      const QString label = c.english_title.isEmpty() ? c.romaji_title : c.english_title;
      msg += u"  • %1\n"_s.arg(label);
    }
    msg += u"\n%1 anime total.\n\nProceed?"_s.arg(candidates.size());
    if (QMessageBox::question(this, tr("Auto-download"), msg,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
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
      const QString summary = tr("Auto-download: %1 torrent(s) sent for %2 anime.")
                                  .arg(state->found).arg(state->total);
      statusBar()->showMessage(summary, 10000);
      // Tray notification so both manual and timer-triggered runs are visible.
      if (state->found > 0)
        postTrayMessage(tr("Auto-download"), summary);
      return;
    }
    const auto c = state->queue.takeFirst();
    const QString label = c.english_title.isEmpty() ? c.romaji_title : c.english_title;
    statusBar()->showMessage(
        tr("Auto-download: fetching RSS for %1 (%2 remaining)…")
            .arg(label).arg(state->queue.size() + 1),
        0);
    initPage(MainWindowPage::Torrents);  // ensure widget is initialized
    // Download ALL missing episodes for this anime (not just the newest).
    m_torrentFeedWidget->downloadAllEpisodesForAnime(
        c.anime_id, c.english_title, c.romaji_title, c.folder_name,
        [this, state, step_fn, label](int count) {
          state->found += count;
          if (count > 0) {
            // Per-anime status feedback.
            statusBar()->showMessage(
                tr("Sent %1 episode(s) for %2.").arg(count).arg(label), 3000);
          }
          // Small delay to avoid hammering the RSS server.
          QTimer::singleShot(2000, this, [step_fn]() { (*step_fn)(); });
        });
  };

  (*step_fn)();
}

void MainWindow::refreshHomeDashboard() {
  if (!m_homeBodyLabel) return;

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
  const QString mean =
      st.scored_title_count > 0
          ? tr("%1 / 10").arg(
                QString::number(static_cast<double>(st.mean_score_0_100) / 10.0, 'f', 1))
          : tr("—");
  m_homeBodyLabel->setText(
      tr("<span style=\"font-size:large; font-weight:600\">"
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

  // ── "Up next" section ────────────────────────────────────────────────────
  if (m_homeUpNextContainer) {
    clearContainer(m_homeUpNextContainer);
    auto* vl = qobject_cast<QVBoxLayout*>(m_homeUpNextContainer->layout());

    // If qBittorrent Web API is enabled, we can gray out the Play button while that anime's
    // folder is still downloading (best-effort: per-savepath progress).
    struct UpNextButton {
      QPointer<QPushButton> btn;
      QString save_path;
      int anime_id = 0;
    };
    QList<UpNextButton> playButtons;

    // Collect Watching entries that have the next episode on disk, sorted by title
    struct UpNextEntry { QString title; int anime_id; int next_ep; };
    QList<UpNextEntry> upNext;
    for (const auto& entry : anime::db.entries()) {
      if (entry.status != anime::list::Status::Watching) continue;
      const int next_ep = entry.watched_episodes + 1;
      if (!track::libraryHasLocalEpisode(entry.anime_id, next_ep)) continue;
      const auto* item = anime::db.item(entry.anime_id);
      if (!item) continue;
      upNext.push_back({QString::fromStdString(
                            anime::preferredListTitleString(*item, taiga::settings.listTitleLanguage())),
                        entry.anime_id, next_ep});
    }
    std::sort(upNext.begin(), upNext.end(),
              [](const UpNextEntry& a, const UpNextEntry& b) {
                return a.title.toLower() < b.title.toLower();
              });

    if (upNext.isEmpty()) {
      auto* empty = new QLabel(
          tr("<span style=\"color:#888\">None — scan your library or "
             "add a folder in Settings → Library.</span>"),
          m_homeUpNextContainer);
      empty->setTextFormat(Qt::RichText);
      vl->addWidget(empty);
    } else {
      // Helper: compute the same qBittorrent save-path Taiga uses when sending torrents.
      // IMPORTANT: must NOT create any directories here (Home should never create folders).
      const auto resolvedTorrentDownloadDir = [](const QString& folder_name) -> QString {
        QString base = QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed();
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

      for (const auto& ue : upNext) {
        auto* row = new QWidget(m_homeUpNextContainer);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);

        // Eye-catching accent: teal for dark, deep-teal for light.
        const QString accentColor = theme.isDark() ? u"#2dd4bf"_s : u"#0d9488"_s;
        auto* titleLbl = new QLabel(
            u"<span style=\"color:%1; font-weight:600\">%2</span>"_s
                .arg(accentColor, ue.title.toHtmlEscaped()),
            row);
        titleLbl->setTextFormat(Qt::RichText);
        titleLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        const QString epLabel = tr("Ep %1").arg(ue.next_ep);
        auto* epLbl = new QLabel(
            u"<span style=\"color:%1; font-weight:600\">%2</span>"_s
                .arg(accentColor, epLabel.toHtmlEscaped()),
            row);
        epLbl->setTextFormat(Qt::RichText);
        epLbl->setFixedWidth(54);

        auto* playBtn = new QPushButton(tr("▶ Play"), row);
        playBtn->setFixedWidth(72);
        playBtn->setCursor(Qt::PointingHandCursor);
        // Make it look more like an action button.
        playBtn->setStyleSheet(QStringLiteral(
            "QPushButton{padding:4px 10px; font-weight:600;}"
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

        // Capture button for qBittorrent progress gating.
        if (taiga::settings.torrentQBitApiEnabled()) {
          const auto* item = anime::db.item(aid);
          const QString folder = item ? QString::fromStdString(item->titles.english).trimmed() : QString{};
          const QString folder_name = folder.isEmpty() ? ue.title : folder;
          const QString save_path = resolvedTorrentDownloadDir(folder_name);
          playButtons.append({playBtn, save_path, aid});
        }
      }
    }

    // Gray out Play while qBittorrent reports active downloads in that save path (best-effort).
    if (taiga::settings.torrentQBitApiEnabled() && !playButtons.isEmpty()) {
      const QString base_url = QString::fromStdString(taiga::settings.torrentQBitApiUrl()).trimmed();
      const QString username = QString::fromStdString(taiga::settings.torrentQBitApiUsername()).trimmed();
      const QString password = QString::fromStdString(taiga::settings.torrentQBitApiPassword());

      const auto applyEnabled = [this, playButtons](const QSet<QString>& downloading_paths) {
        for (const auto& pb : playButtons) {
          if (!pb.btn) continue;
          const bool downloading = !pb.save_path.isEmpty() &&
                                   downloading_paths.contains(QDir::cleanPath(pb.save_path));
          pb.btn->setEnabled(!downloading);
          pb.btn->setCursor(downloading ? Qt::ArrowCursor : Qt::PointingHandCursor);
          if (downloading) {
            pb.btn->setToolTip(tr("Downloading in qBittorrent…"));
          } else {
            pb.btn->setToolTip({});
          }
        }
      };

      // Default: enabled; will be disabled when we confirm active downloads for that folder.
      applyEnabled({});

      const auto fetchInfo = [=](const QString& cookie) {
        if (base_url.isEmpty()) return;
        QNetworkRequest req(QUrl(base_url + QStringLiteral("/api/v2/torrents/info")));
        taiga::applyCommonHeaders(req);
        if (!cookie.isEmpty()) req.setRawHeader("Cookie", cookie.toUtf8());
        auto* reply = taiga::network()->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, applyEnabled]() mutable {
          reply->deleteLater();
          if (reply->error() != QNetworkReply::NoError) {
            // On failure, keep buttons enabled (non-blocking UX).
            applyEnabled({});
            return;
          }
          const QByteArray body = reply->readAll();
          const QJsonDocument doc = QJsonDocument::fromJson(body);
          if (!doc.isArray()) { applyEnabled({}); return; }

          QSet<QString> downloading;
          for (const QJsonValue& v : doc.array()) {
            if (!v.isObject()) continue;
            const QJsonObject o = v.toObject();
            const double progress = o.value(QStringLiteral("progress")).toDouble(1.0);
            const QString state = o.value(QStringLiteral("state")).toString();
            const QString save_path = QDir::cleanPath(o.value(QStringLiteral("save_path")).toString());
            if (save_path.isEmpty()) continue;
            // Treat anything not fully complete as "downloading" for our purposes.
            if (progress < 1.0) {
              // Prefer state hints, but progress alone is enough.
              if (state.contains(QStringLiteral("down"), Qt::CaseInsensitive) ||
                  state.contains(QStringLiteral("dl"), Qt::CaseInsensitive) ||
                  state.contains(QStringLiteral("meta"), Qt::CaseInsensitive) ||
                  state.contains(QStringLiteral("stalled"), Qt::CaseInsensitive) ||
                  state.contains(QStringLiteral("paused"), Qt::CaseInsensitive) ||
                  true) {
                downloading.insert(save_path);
              }
            }
          }
          applyEnabled(downloading);
        });
      };

      if (username.isEmpty()) {
        fetchInfo({});
      } else {
        QNetworkRequest login_req(QUrl(base_url + QStringLiteral("/api/v2/auth/login")));
        taiga::applyCommonHeaders(login_req);
        login_req.setHeader(QNetworkRequest::ContentTypeHeader,
                            QStringLiteral("application/x-www-form-urlencoded"));
        const QByteArray login_body =
            QByteArrayLiteral("username=") + username.toUtf8() +
            QByteArrayLiteral("&password=") + password.toUtf8();
        auto* login_reply = taiga::network()->post(login_req, login_body);
        connect(login_reply, &QNetworkReply::finished, this, [login_reply, fetchInfo]() mutable {
          login_reply->deleteLater();
          QString cookie_str;
          const QVariant cv = login_reply->header(QNetworkRequest::SetCookieHeader);
          if (cv.isValid()) {
            for (const QNetworkCookie& c : cv.value<QList<QNetworkCookie>>()) {
              if (!cookie_str.isEmpty()) cookie_str += QStringLiteral("; ");
              cookie_str += QString::fromUtf8(c.name()) + QStringLiteral("=") +
                            QString::fromUtf8(c.value());
            }
          }
          fetchInfo(cookie_str);
        });
      }
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
          anime::preferredListTitleString(item, taiga::settings.listTitleLanguage()));
    };

    // ---- Sub-section: Upcoming episodes this week -------------------------
    struct UpcomingEntry {
      QString title;
      int anime_id;
      int episode;
      qint64 air_time;
    };
    QList<UpcomingEntry> upcoming;
    for (const auto& entry : anime::db.entries()) {
      if (entry.status != anime::list::Status::Watching &&
          entry.status != anime::list::Status::PlanToWatch)
        continue;
      const auto* item = anime::db.item(entry.anime_id);
      if (!item || item->next_episode_time <= now_secs) continue;
      const qint64 secs_until = item->next_episode_time - now_secs;
      if (secs_until > 7LL * 86400) continue;
      const int next_ep = item->last_aired_episode + 1;
      upcoming.push_back({preferredTitle(*item), entry.anime_id, next_ep, item->next_episode_time});
    }
    std::sort(upcoming.begin(), upcoming.end(),
              [](const UpcomingEntry& a, const UpcomingEntry& b) {
                return a.air_time < b.air_time;
              });

    if (!upcoming.isEmpty()) {
      auto* subHdr = new QLabel(
          u"<span style=\"color:#aaa;font-size:medium\"><b>%1</b></span>"_s.arg(
              tr("UPCOMING THIS WEEK")),
          m_homeRecentContainer);
      subHdr->setTextFormat(Qt::RichText);
      vl->addWidget(subHdr);

      for (const auto& ue : upcoming) {
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

        auto* titleBtn = new QPushButton(ue.title, row);
        titleBtn->setFlat(true);
        titleBtn->setCursor(Qt::PointingHandCursor);
        titleBtn->setStyleSheet(u"text-align:left; color: palette(link); text-decoration: underline; border: none; padding: 0;"_s);
        titleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(titleBtn, &QPushButton::clicked, this, [this, anime_id = ue.anime_id]() {
          const auto* item = anime::db.item(anime_id);
          if (!item) return;
          const auto* e = anime::db.entry(anime_id);
          std::optional<ListEntry> entry;
          if (e) entry = *e;
          gui::MediaDialog::show(this, gui::MediaDialogPage::Details, *item, entry);
        });

        auto* epLbl = new QLabel(
            u"<span style=\"color:#888\">Ep %1</span>"_s.arg(ue.episode), row);
        epLbl->setTextFormat(Qt::RichText);
        epLbl->setFixedWidth(54);

        auto* whenLbl = new QLabel(
            u"<span style=\"color:#888;font-size:medium\">%1</span>"_s.arg(
                when.toHtmlEscaped()),
            row);
        whenLbl->setTextFormat(Qt::RichText);
        whenLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        whenLbl->setFixedWidth(90);

        rl->addWidget(titleBtn);
        rl->addWidget(epLbl);
        rl->addWidget(whenLbl);
        vl->addWidget(row);
      }
      vl->addSpacing(8);
    }

    // ---- Sub-section: Finished airing in the last 7 days -----------------
    struct RecentlyAiredEntry {
      QString title;
      int anime_id;
      QDate finished;
    };
    QList<RecentlyAiredEntry> recentlyAired;
    for (const auto& entry : anime::db.entries()) {
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
      const bool already = std::any_of(recentlyAired.cbegin(), recentlyAired.cend(),
                                       [&](const RecentlyAiredEntry& r) {
                                         return r.anime_id == entry.anime_id;
                                       });
      if (already) continue;
      recentlyAired.push_back({preferredTitle(*item), entry.anime_id, finishedDate});
    }
    std::sort(recentlyAired.begin(), recentlyAired.end(),
              [](const RecentlyAiredEntry& a, const RecentlyAiredEntry& b) {
                return b.finished < a.finished;  // most recently finished first
              });

    if (!recentlyAired.isEmpty()) {
      auto* subHdr = new QLabel(
          u"<span style=\"color:#aaa;font-size:medium\"><b>%1</b></span>"_s.arg(
              tr("FINISHED AIRING THIS WEEK")),
          m_homeRecentContainer);
      subHdr->setTextFormat(Qt::RichText);
      vl->addWidget(subHdr);

      for (const auto& ra : recentlyAired) {
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

        auto* titleBtn = new QPushButton(ra.title, row);
        titleBtn->setFlat(true);
        titleBtn->setCursor(Qt::PointingHandCursor);
        titleBtn->setStyleSheet(u"text-align:left; color: palette(link); text-decoration: underline; border: none; padding: 0;"_s);
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
            u"<span style=\"color:#888;font-size:medium\">%1</span>"_s.arg(
                when.toHtmlEscaped()),
            row);
        whenLbl->setTextFormat(Qt::RichText);
        whenLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        whenLbl->setFixedWidth(96);

        rl->addWidget(titleBtn);
        rl->addWidget(whenLbl);
        vl->addWidget(row);
      }
    }

    if (upcoming.isEmpty() && recentlyAired.isEmpty()) {
      auto* empty = new QLabel(
          tr("<span style=\"color:#888\">Nothing upcoming or finished airing in the last 7 "
             "days — sync your list to get airing schedules.</span>"),
          m_homeRecentContainer);
      empty->setTextFormat(Qt::RichText);
      vl->addWidget(empty);
    }
  }
}

void MainWindow::refreshListColors() {
  if (m_listWidget) {
    m_listWidget->refreshNewEpisodeHighlightDisplay();
    m_listWidget->refreshStatusTabCountsNow();
  }
  if (m_searchWidget) m_searchWidget->refreshNewEpisodeHighlightDisplay();
}

void MainWindow::updateAutoDownloadCountdownLabel() {
  if (!m_auto_download_timer_) return;
  const int remaining_ms = m_auto_download_timer_->remainingTime();

  QString homeText, toolbarText;
  if (remaining_ms <= 0) {
    homeText    = tr("<span style=\"color:#888;font-size:small\">Auto-download: running…</span>");
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
    homeText    = tr("<span style=\"color:#888;font-size:small\">Next auto-download in <b>%1</b></span>").arg(when);
    toolbarText = tr("<span style=\"font-size:large;font-weight:600\">⬇ %1</span>").arg(when);
  }

  if (m_homeAutoDownloadLabel) m_homeAutoDownloadLabel->setText(homeText);
  if (m_toolbarCountdownLabel) m_toolbarCountdownLabel->setText(toolbarText);
}

void MainWindow::restoreViewChromeFromSession() {
  const bool nav_visible = taiga::settings.navigationSidebarVisible();
  if (m_navigationWidget) m_navigationWidget->setVisible(nav_visible);
  {
    const QSignalBlocker b(ui_->actionToggleNavigationSidebar);
    ui_->actionToggleNavigationSidebar->setChecked(nav_visible);
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
  const QString path = !folders.empty()
      ? QString::fromStdString(folders.front())
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
  connect(remove_btn, &QPushButton::clicked, &dlg, [list]() {
    delete list->takeItem(list->currentRow());
  });

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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_toolbarCountdownLabel && event->type() == QEvent::MouseButtonPress) {
    runAutoDownload(false);
    return true;
  }
  return QMainWindow::eventFilter(watched, event);
}

}  // namespace gui
