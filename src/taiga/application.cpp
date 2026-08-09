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

#include "application.hpp"

#include <QAbstractItemView>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLocalSocket>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <format>
#include <memory>
#include <optional>

#include "base/log.hpp"
#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/startup_splash.hpp"
#include "gui/utils/table_view_defaults.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "taiga/config.h"
#include "taiga/network.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"
#include "taiga/update_check.hpp"
#include "taiga/version.hpp"
#include "track/library_watcher.hpp"
#include "track/media.hpp"
#include "track/scanner.hpp"

namespace taiga {

Application::Application(int argc, char* argv[])
    : QApplication(argc, argv), shared_memory_("Taiga") {
  instance_server_.setParent(this);
  setApplicationName("taiga");
  setApplicationDisplayName("Taiga");
  setApplicationVersion(QString::fromStdString(taiga::version().to_string()));
  setOrganizationDomain("taiga.moe");
  setOrganizationName("erengy");
}

Application::~Application() {
  if (window_) {
    window_->hide();
  }
}

int Application::run() {
  parseCommandLine();

  initLogger();

  LOGD("Version {} ({})", taiga::version().to_string(),
       QFileInfo{QCoreApplication::applicationFilePath()}
           .lastModified()
           .toString(Qt::DateFormat::ISODate)
           .toStdString());
  if (!parser_.optionNames().isEmpty()) {
    LOGD("Options: {}", parser_.optionNames().join(", ").toStdString());
  }

  if (hasPreviousInstance()) {
    tryActivateRunningInstance();
    return 0;
  }

  taiga::settings.init();
  taiga::settings.ensureWindowsAutoStartFromSettings();
  taiga::network()->applyProxyFromSettings();
  anime::db.init();
  anime::history().init();
  track::media::detection()->init();
  track::libraryFolderWatcher()->refreshFromSettings();

  gui::theme.initStyle();
  setWindowIcon(gui::theme.getIcon("taiga", "png"));

  QTranslator translator;
  if (translator.load(QLocale::system(), "taiga", "_", ":/i18n")) {
    installTranslator(&translator);
  }

  // If we map the window and then hide() to the tray, Windows briefly paints a normal
  // frame first. Match legacy behavior: keep the main window unmapped until the user
  // opens it from the tray (see MainWindow::displayWindow).
  const bool start_to_tray = taiga::settings.startMinimized() && taiga::settings.minimizeToTray() &&
                             QSystemTrayIcon::isSystemTrayAvailable();
  window_ = new gui::MainWindow();
  window_->initUi(/*startup_blocking=*/true);

  std::unique_ptr<gui::StartupSplash> splash;
  // Show splash even when starting to tray so startup work is visible.
  // When starting to tray, do not steal focus from the user's current app.
  splash = std::make_unique<gui::StartupSplash>();
  splash->setStepText(QObject::tr("Preparing startup tasks…"));
  splash->appendLine(QObject::tr("Preparing startup tasks…"));
  splash->show();
  if (!start_to_tray) {
    splash->raise();
    splash->activateWindow();
  }

  const auto setStep = [&](const QString& step) {
    if (!splash) return;
    splash->setStepText(step);
    splash->appendLine(step);
    splash->repaint();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  };

  // ── Startup pipeline (blocking, before first show) ─────────────────────────
  // 1) Startup sync (if enabled).
  bool startup_sync_ok = true;
  bool startup_sync_ran = false;
  if (taiga::settings.syncAutoOnStart() && taiga::settings.listSynchronizationEnabled()) {
    setStep(QObject::tr("Synchronizing list…"));
    startup_sync_ran = true;
    QEventLoop loop;
    // QueuedConnection: if the signal is emitted synchronously before exec(), DirectConnection
    // would call quit() too early and the nested loop can stay running forever.
    QObject::connect(
        window_.get(), &gui::MainWindow::listSyncFinished, &loop,
        [&loop, &startup_sync_ok](const bool ok, const QString&) {
          startup_sync_ok = ok;
          loop.quit();
        },
        Qt::QueuedConnection);
    window_->startListSynchronization(false);
    loop.exec();
  }

  // 2) One scan after sync.
  // If scan-on-startup is disabled, we still run this scan when startup sync ran successfully.
  // If scan-on-startup is enabled, we also only scan here (no pre-sync scan).
  // Always ensure an episode index exists before auto-download (cache load and/or scan).
  bool scanned_after_sync = false;
  const bool should_scan_after_sync =
      taiga::settings.scanLibraryOnStartup() || (startup_sync_ran && startup_sync_ok);
  if (should_scan_after_sync) {
    setStep(QObject::tr("Scanning library…"));
    QEventLoop loop;
    QObject::connect(
        window_.get(), &gui::MainWindow::libraryScanFinished, &loop,
        [&loop](const QString& reason, const QString&) {
          if (reason == QStringLiteral("startup-post-sync")) loop.quit();
        },
        Qt::QueuedConnection);
    window_->runStartupPostSyncScan();
    loop.exec();
    scanned_after_sync = true;
  }

  // 3) Startup auto-download.
  // Always run against the local Watching list (even if sync failed) so missing episodes are still
  // queued. Report the outcome on the splash — a 0-candidate run finishes instantly and was easy
  // to miss / previously skipped entirely when sync failed.
  {
    if (!track::libraryScanHasResults() && !scanned_after_sync) {
      setStep(QObject::tr("Scanning library…"));
      QEventLoop loop;
      QObject::connect(
          window_.get(), &gui::MainWindow::libraryScanFinished, &loop,
          [&loop](const QString& reason, const QString&) {
            if (reason == QStringLiteral("startup-post-sync")) loop.quit();
          },
          Qt::QueuedConnection);
      window_->runStartupPostSyncScan();
      loop.exec();
    }

    setStep(QObject::tr("Auto-downloading new episodes…"));
    int torrents_sent = 0;
    int anime_total = 0;
    QEventLoop dlLoop;
    QObject::connect(
        window_.get(), &gui::MainWindow::autoDownloadFinished, &dlLoop,
        [&dlLoop, &torrents_sent, &anime_total](const int sent, const int total) {
          torrents_sent = sent;
          anime_total = total;
          dlLoop.quit();
        },
        Qt::QueuedConnection);
    window_->runAutoDownload(/*silent=*/true);
    dlLoop.exec();

    if (anime_total <= 0) {
      setStep(QObject::tr("Auto-download: nothing to fetch"));
      splash->appendLine(QObject::tr("No Watching titles need episode downloads right now."));
    } else {
      setStep(QObject::tr("Auto-download: %1/%2 sent").arg(torrents_sent).arg(anime_total));
      splash->appendLine(QObject::tr("Auto-download finished: %1 torrent(s) sent for %2 anime.")
                             .arg(torrents_sent)
                             .arg(anime_total));
    }
    // Keep the summary visible briefly so a fast no-op run is still readable.
    {
      QEventLoop pause;
      QTimer::singleShot(900, &pause, &QEventLoop::quit);
      pause.exec();
    }
  }

  // 4) Update check (network only) before first show; prompt after show if needed.
  std::optional<taiga::UpdateCheckResult> update_res;
  if (taiga::settings.checkForUpdatesOnStartup()) {
    setStep(QObject::tr("Checking for updates…"));
    QEventLoop loop;
    taiga::checkForUpdatesSilent([&](taiga::UpdateCheckResult r) {
      update_res = std::move(r);
      loop.quit();
    });
    loop.exec();
  }

  // 5) Pre-warm Anime List layout as the LAST pre-show step.
  setStep(QObject::tr("Pre-warming Anime List layout…"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  window_->ensurePageInitialized(gui::MainWindowPage::List);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (auto* v = window_->findChild<QAbstractItemView*>(QStringLiteral("animeList"))) {
    gui::tables::warmupSizingNow(v);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  // 6) Prepare the initial Home UI so first paint doesn't hitch.
  setStep(QObject::tr("Preparing main window UI…"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  window_->prepareForFirstShow();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

  // 7) Adaptive "settle" while the window is still hidden.
  // Some expensive first-run timers/slots can still execute shortly after `show()`. Process queued
  // work now behind the splash so the first visible seconds feel responsive, without forcing a
  // fixed long delay on every startup.
  setStep(QObject::tr("Finalizing…"));
  {
    QElapsedTimer settle;
    settle.start();
    // Minimum covers common 1s/2s/3s startup timers; quiet window ensures we exit only after
    // no long pump iterations have occurred recently.
    constexpr qint64 kMinMs = 3500;
    constexpr qint64 kMaxMs = 20000;
    constexpr qint64 kQuietWindowMs = 1500;
    qint64 last_long_pump_ms = 0;
    qint64 last_iter_ms = settle.elapsed();
    while (settle.elapsed() < kMaxMs) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      // Short sleep to avoid pegging CPU but keep splash responsive.
      QThread::msleep(2);

      const qint64 now = settle.elapsed();
      const qint64 iter_ms = now - last_iter_ms;
      last_iter_ms = now;
      if (iter_ms >= 120) last_long_pump_ms = now;

      // Past minimum: if we've had no long iterations for a while, we can show the window.
      if (now >= kMinMs && last_long_pump_ms > 0 && now - last_long_pump_ms >= kQuietWindowMs) {
        break;
      }
      // If nothing ever took long, just exit at min.
      if (now >= kMinMs && last_long_pump_ms == 0) break;
    }
  }

  // End of blocking startup.
  window_->setStartupBlockingMode(false);
  if (splash) {
    splash->hide();
    splash.reset();
  }

  if (!start_to_tray) {
    window_->show();
    if (taiga::settings.startMinimized()) {
      window_->showMinimized();
    }
  }

  // Present any deferred update prompt after the main window is visible.
  if (update_res && update_res->ok && update_res->has_newer && !start_to_tray) {
    taiga::promptUpdateAvailable(window_.get(), update_res->latest, update_res->link);
  }

  startInstanceServer();

  return QApplication::exec();
}

bool Application::isDebug() const {
  return options_.debug;
}

bool Application::isVerbose() const {
  return options_.verbose;
}

gui::MainWindow* Application::mainWindow() const {
  return window_.get();
}

bool Application::hasPreviousInstance() {
  return !shared_memory_.create(1);
}

QString Application::instanceServerName() {
  return u"%1_instance"_s.arg(QCoreApplication::applicationName());
}

void Application::tryActivateRunningInstance() const {
  QLocalSocket socket;
  socket.connectToServer(instanceServerName());
  if (socket.waitForConnected(1500)) {
    socket.write("x");
    socket.waitForBytesWritten(500);
  }
}

void Application::startInstanceServer() {
  QLocalServer::removeServer(instanceServerName());
  if (!instance_server_.listen(instanceServerName())) return;

  connect(&instance_server_, &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket* socket = instance_server_.nextPendingConnection()) {
      connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
      if (gui::MainWindow* w = mainWindow()) {
        QMetaObject::invokeMethod(w, "displayWindow", Qt::QueuedConnection);
      }
      socket->disconnectFromServer();
    }
  });
}

void Application::initLogger() const {
  using monolog::Level;

  const auto directory = std::format("{}/logs", get_data_path());
  QDir().mkpath(QString::fromStdString(directory));

  const auto date = QDate::currentDate().toString(Qt::DateFormat::ISODate).toStdString();
  const auto path = std::format("{}/{}_{}.log", directory, TAIGA_APP_NAME, date);

  monolog::log.enable_console_output(false);
  monolog::log.set_path(path);
  monolog::log.set_level(options_.debug ? Level::Debug : Level::Warning);
}

void Application::parseCommandLine() {
  parser_.addOptions({
      {"debug", QCoreApplication::translate("main", "Enable debug mode")},
      {"verbose", QCoreApplication::translate("main", "Enable verbose output")},
  });

  // This stops the current process in case of an error (e.g. an unknown option was passed).
  parser_.process(QApplication::arguments());

#ifdef _DEBUG
  options_.debug = true;
#else
  options_.debug = parser_.isSet("debug");
#endif
  options_.verbose = parser_.isSet("verbose");
}

}  // namespace taiga
