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

#include <QDesktopServices>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtWidgets>

#include <ranges>

#include "base/string.hpp"
#include "gui/history/history_widget.hpp"
#include "gui/library/library_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/main/about_dialog.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/tray_icon.hpp"
#include "gui/utils/widgets.hpp"
#include "sync/service.hpp"
#include "media/anime_db.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
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
  initActions();
  initIcons();
  initTrayIcon();
  initToolbar();
  initNavigation();
  initStatusbar();
  initNowPlaying();
  updateTitle();
}

void MainWindow::initActions() {
  ui_->actionProfile->setToolTip(tr("Profile"));
  ui_->actionSynchronize->setToolTip(
      tr("Synchronize with %1").arg(sync::serviceName(sync::currentServiceId())));

  connect(ui_->actionAddNewFolder, &QAction::triggered, this, &MainWindow::addNewFolder);
  connect(ui_->actionExit, &QAction::triggered, this, &QApplication::quit, Qt::QueuedConnection);
  connect(ui_->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog::show(this); });
  connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::about);
  connect(ui_->actionDonate, &QAction::triggered, this, &MainWindow::donate);
  connect(ui_->actionSupport, &QAction::triggered, this, &MainWindow::support);
  connect(ui_->actionProfile, &QAction::triggered, this, &MainWindow::profile);
  connect(ui_->actionDisplayWindow, &QAction::triggered, this, &MainWindow::displayWindow);

  connect(ui_->actionSynchronize, &QAction::triggered, this, [this]() {
    QPointer<MainWindow> guard(this);
    statusBar()->showMessage(
        tr("Synchronizing with %1...").arg(sync::serviceName(sync::currentServiceId())));
    ui_->actionSynchronize->setEnabled(false);

    sync::fetchListEntries([guard](const bool ok, const QString& message) {
      if (!guard) return;
      QMetaObject::invokeMethod(guard.data(), "handleListSyncFinished", Qt::QueuedConnection,
                                Q_ARG(bool, ok), Q_ARG(QString, message));
    });
  });
}

void MainWindow::initIcons() {
  ui_->menuLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->menuExport->setIcon(theme.getIcon("export_notes"));

  ui_->actionAddNewFolder->setIcon(theme.getIcon("create_new_folder"));
  ui_->actionAbout->setIcon(theme.getIcon("info"));
  ui_->actionBack->setIcon(theme.getIcon("arrow_back"));
  ui_->actionCheckForUpdates->setIcon(theme.getIcon("cloud_download"));
  ui_->actionDonate->setIcon(theme.getIcon("favorite"));
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
}

void MainWindow::initNavigation() {
  m_navigationWidget = new NavigationWidget(this);

  connect(m_navigationWidget, &NavigationWidget::currentPageChanged, this, &MainWindow::setPage);

  navigateTo(MainWindowPage::List);

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
      if (auto* l = qobject_cast<QVBoxLayout*>(ui_->homePage->layout())) {
        while (l->count()) {
          QLayoutItem* it = l->takeAt(0);
          if (it->widget()) delete it->widget();
          delete it;
        }
        auto* title = new QLabel(tr("Taiga"), ui_->homePage);
        QFont tf = title->font();
        tf.setBold(true);
        tf.setPointSizeF(tf.pointSizeF() + 6);
        title->setFont(tf);
        title->setAlignment(Qt::AlignHCenter);
        auto* body = new QLabel(
            tr("<p style=\"margin-top:0.5em\">You have <b>%1</b> titles on your list and <b>%2</b> "
               "anime in the local database.</p>"
               "<p>Use the sidebar for <b>Anime list</b>, <b>Search</b>, <b>History</b>, and "
               "<b>Library</b>.</p>"
               "<p>Toolbar <b>Synchronize</b> downloads your list from the active site (AniList is "
               "supported).</p>")
                .arg(anime::db.entries().size())
                .arg(anime::db.items().size()),
            ui_->homePage);
        body->setWordWrap(true);
        body->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        l->addStretch(1);
        l->addWidget(title);
        l->addWidget(body);
        l->addStretch(2);
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
        auto* body = new QLabel(
            tr("<p>RSS feeds and automatic downloads are not available in this build.</p>"
               "<p>You can open this page from a title’s context menu; the toolbar search box still "
               "works for manual lookups.</p>"),
            ui_->torrentsPage);
        body->setWordWrap(true);
        body->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        l->addStretch(1);
        l->addWidget(title);
        l->addWidget(body);
        l->addStretch(2);
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
        l->addStretch(1);
        l->addWidget(title);
        l->addWidget(info);
        l->addWidget(btn, 0, Qt::AlignHCenter);
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
      menu->addAction(ui_->actionToggleDetection);
      menu->addAction(ui_->actionToggleSharing);
      menu->addAction(ui_->actionToggleSynchronization);
      menu->addSeparator();
      menu->addMenu(ui_->menuHelp);
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
    m_searchBox->setPlaceholderText(tr("Search"));

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
  }
}

void MainWindow::initTrayIcon() {
  auto menu = new QMenu(this);
  menu->addAction(ui_->actionDisplayWindow);
  menu->setDefaultAction(ui_->actionDisplayWindow);
  menu->addSeparator();
  menu->addAction(ui_->actionSettings);
  menu->addSeparator();
  menu->addAction(ui_->actionExit);

  m_trayIcon = new TrayIcon(this, windowIcon(), menu);

  connect(m_trayIcon, &TrayIcon::activated, this, &MainWindow::displayWindow);
  connect(m_trayIcon, &TrayIcon::messageClicked, this,
          []() { QMessageBox::information(nullptr, "Taiga", tr("Clicked message")); });
}

void MainWindow::closeEvent(QCloseEvent* event) {
  taiga::session.setMainWindowGeometry(saveGeometry());
  if (m_listWidget) m_listWidget->saveState();
  if (m_searchWidget) m_searchWidget->saveState();
  event->accept();
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
}

void MainWindow::navigateTo(MainWindowPage page) {
  if (const auto item = m_navigationWidget->findItemByPage(page)) {
    m_navigationWidget->setCurrentItem(item);
  }
}

void MainWindow::setPage(MainWindowPage page) {
  initPage(page);
  ui_->statusbar->clearMessage();
  ui_->stackedWidget->setCurrentIndex(static_cast<int>(page));
}

void MainWindow::updateTitle() {
  auto title = u"Taiga"_s;

  if (taiga::app()->isDebug()) {
    title += u" [debug]"_s;
  }

  setWindowTitle(title);
}

void MainWindow::displayWindow() {
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

void MainWindow::handleListSyncFinished(bool ok, QString message) {
  ui_->actionSynchronize->setEnabled(true);
  if (ok) {
    if (m_listWidget) m_listWidget->reloadAnimeList();
    if (m_searchWidget) m_searchWidget->reloadAnimeList();
    statusBar()->showMessage(message.isEmpty() ? tr("Synchronized.") : message, 5000);
  } else {
    statusBar()->showMessage(tr("Synchronization failed: %1").arg(message), 8000);
  }
}

}  // namespace gui
