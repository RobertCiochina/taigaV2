/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "settings_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpacerItem>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>

#include "base/settings.hpp"
#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/settings/anilist_auth_dialog.hpp"
#include "gui/settings/cache_diagnostics_dialog.hpp"
#include "gui/settings/torrent_filters_dialog.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/ui_strings.hpp"
#include "media/anime.hpp"
#include "media/anime_list_export.hpp"
#include "sync/anilist_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/list_row_action.hpp"
#include "taiga/network.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"
#include "taiga/torrent_discovery.hpp"
#include "track/library_watcher.hpp"
#include "track/media.hpp"
#include "track/recognition_cache.hpp"
#include "track/scanner.hpp"
#include "track/streaming_sites.hpp"
#include "ui_settings_dialog.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

namespace {

constexpr int kStackRole = Qt::UserRole + 5;
constexpr int kStackAccounts = 0;
constexpr int kStackApplication = 1;
constexpr int kStackLibrary = 2;
constexpr int kStackAnimeList = 3;
constexpr int kStackPlaceholder = 4;
constexpr int kStackTorrentsDownloads = 5;

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::SettingsDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  // Build the dedicated Torrents → Downloads page BEFORE setting up the tree
  // so the new stack index is available when the tree items are assigned.
  buildDownloadsPage();

  // Hide all torrent-related widgets from the Library page — fully consolidated in Torrents →
  // Downloads.
  const std::initializer_list<QWidget*> libraryDownloadWidgets = {
      ui_->labelTorrentPathsHelp,
      ui_->labelTorrentClientDownload,
      ui_->lineTorrentClientDownloadPath,
      ui_->buttonBrowseTorrentClientDownload,
      ui_->labelTorrentFileSave,
      ui_->lineTorrentFileSavePath,
      ui_->buttonBrowseTorrentFileSave,
      ui_->checkTorrentDownloadUseMagnet,
      ui_->groupTorrentHandling,
      ui_->groupTorrentSearch,  // RSS / feed URLs, auto-check, sort — now on Torrents → Downloads
  };
  for (auto* w : libraryDownloadWidgets) {
    if (w) w->hide();
  }

  ui_->treeWidget->setIndentation(22);

  const auto add_item = [this](const QString& icon, const QString& text, const int stack) {
    auto* item = new QTreeWidgetItem(ui_->treeWidget, QStringList(text));
    item->setIcon(0, theme.getIcon(icon));
    item->setSizeHint(0, QSize{0, 24});
    item->setData(0, kStackRole, stack);
    return item;
  };

  const auto add_child = [this](QTreeWidgetItem* parent, const QString& text) {
    auto* c = new QTreeWidgetItem(parent, QStringList(text));
    c->setData(0, kStackRole, kStackPlaceholder);
    return c;
  };

  add_item("account_circle", "Accounts", kStackAccounts);
  add_item("web_asset", "Application", kStackApplication);
  add_item("list_alt", "Anime List", kStackAnimeList);
  add_item("folder", "Library", kStackLibrary);
  add_item("check_circle", "Recognition", kStackPlaceholder);
  {
    auto* item = add_item("rss_feed", "Torrents", kStackPlaceholder);
    auto* dl_item = add_child(item, "Downloads");
    dl_item->setData(0, kStackRole, kStackTorrentsDownloads);
    add_child(item, "Filters");
  }
  {
    auto* item = add_item("warning", "Advanced", kStackPlaceholder);
    add_child(item, "Cache");
  }

  ui_->treeWidget->expandAll();

  connect(ui_->treeWidget, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (!current) return;
            const int stack = current->data(0, kStackRole).toInt();
            QString text = current->text(0);
            QString parentText;
            if (current->parent()) {
              parentText = current->parent()->text(0);
              text = u"%1 / %2"_s.arg(parentText, text);
            }

            // If clicking a parent group header that points to the placeholder
            // AND has children (e.g. Torrents), redirect to the first child automatically.
            // Top-level placeholders without children (e.g. Recognition) display inline.
            if (stack == kStackPlaceholder && parentText.isEmpty() && current->childCount() > 0) {
              ui_->treeWidget->setCurrentItem(current->child(0));
              return;
            }

            ui_->stackedWidget->setCurrentIndex(stack);
            ui_->titleLabel->setText(text);

            if (stack == kStackPlaceholder) {
              // Top-level placeholder (e.g. Recognition) — pass node text as parent, empty child.
              // Child-level placeholder — pass parent text + child text.
              if (parentText.isEmpty()) {
                populatePlaceholderPage(current->text(0), {});
              } else {
                populatePlaceholderPage(parentText, current->text(0));
              }
            }
          });

  {
    ui_->accountsInfoLabel->setTextFormat(Qt::RichText);

    const auto refresh_accounts_summary = [this] {
      const QString svc = sync::serviceName(sync::currentServiceId());
      const QString user =
          QString::fromStdString(taiga::accounts.serviceUsername(taiga::settings.service()));
      const bool has_anilist_token = !taiga::accounts.anilistToken().empty();
      const bool has_mal_token = !taiga::accounts.myanimelistAccessToken().empty();
      const bool has_kitsu =
          (!taiga::accounts.kitsuEmail().empty() || !taiga::accounts.kitsuUsername().empty()) &&
          !taiga::accounts.kitsuPassword().empty();
      ui_->accountsInfoLabel->setText(
          tr("<p><b>Active service:</b> %1<br/>"
             "<b>Username:</b> %2<br/>"
             "<b>AniList token:</b> %3<br/>"
             "<b>MyAnimeList token:</b> %4<br/>"
             "<b>Kitsu credentials:</b> %5</p>"
             "<p>Credentials are stored in <code>accounts.json</code> next to your data folder. "
             "Use the "
             "fields and buttons below — you do not need to edit the file by hand.</p>")
              .arg(svc.toHtmlEscaped())
              .arg(user.isEmpty() ? tr("(not set)").toHtmlEscaped() : user.toHtmlEscaped())
              .arg(has_anilist_token ? tr("Present").toHtmlEscaped()
                                     : tr("Missing").toHtmlEscaped())
              .arg(has_mal_token ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped())
              .arg(has_kitsu ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped()));
    };
    refresh_accounts_summary();

    ui_->checkSyncOnStart->setChecked(taiga::settings.syncAutoOnStart());
    ui_->labelSyncOnStartHint->setText(
        tr("If you turn this off, Taiga can work from stale data until you synchronize manually. "
           "Your list, search, and other flows that rely on the service may be incomplete or "
           "misbehave until the next successful sync."));
    ui_->labelSyncOnStartHint->setStyleSheet(
        QStringLiteral("QLabel{color: palette(placeholderText); font-size:12px;}"));
    ui_->checkSyncOnStart->setToolTip(
        tr("Recommended: keep this on so your local list matches your service after each start. "
           "Turning it off can leave search and list-related features out of date until you sync "
           "manually, and is not recommended if you depend on accurate service data."));

    auto* anilist_btn = new QPushButton(tr("Sign in with AniList…"), ui_->accountsPage);
    connect(anilist_btn, &QPushButton::clicked, this, [this, refresh_accounts_summary] {
      if (AnilistAuthDialog::signIn(this)) refresh_accounts_summary();
    });
    ui_->verticalLayout_3->addWidget(anilist_btn);

    auto* mal_box = new QGroupBox(tr("MyAnimeList"), ui_->accountsPage);
    auto* mal_form = new QFormLayout(mal_box);
    m_mal_username_ = new QLineEdit(mal_box);
    m_mal_access_ = new QLineEdit(mal_box);
    m_mal_refresh_ = new QLineEdit(mal_box);
    m_mal_access_->setEchoMode(QLineEdit::Password);
    m_mal_refresh_->setEchoMode(QLineEdit::Password);
    m_mal_username_->setText(QString::fromStdString(taiga::accounts.myanimelistUsername()));
    m_mal_access_->setText(QString::fromStdString(taiga::accounts.myanimelistAccessToken()));
    m_mal_refresh_->setText(QString::fromStdString(taiga::accounts.myanimelistRefreshToken()));
    mal_form->addRow(tr("Username:"), m_mal_username_);
    mal_form->addRow(tr("Access token:"), m_mal_access_);
    mal_form->addRow(tr("Refresh token:"), m_mal_refresh_);
    ui_->verticalLayout_3->addWidget(mal_box);

    auto* mal_btn =
        new QPushButton(tr("Open MyAnimeList API apps (create tokens)…"), ui_->accountsPage);
    connect(mal_btn, &QPushButton::clicked, this,
            []() { QDesktopServices::openUrl(QUrl("https://myanimelist.net/apiconfig")); });
    ui_->verticalLayout_3->addWidget(mal_btn);

    auto* kitsu_box = new QGroupBox(tr("Kitsu"), ui_->accountsPage);
    auto* kitsu_form = new QFormLayout(kitsu_box);
    m_kitsu_email_ = new QLineEdit(kitsu_box);
    m_kitsu_username_ = new QLineEdit(kitsu_box);
    m_kitsu_password_ = new QLineEdit(kitsu_box);
    m_kitsu_password_->setEchoMode(QLineEdit::Password);
    m_kitsu_email_->setText(QString::fromStdString(taiga::accounts.kitsuEmail()));
    m_kitsu_username_->setText(QString::fromStdString(taiga::accounts.kitsuUsername()));
    m_kitsu_password_->setText(QString::fromStdString(taiga::accounts.kitsuPassword()));
    kitsu_form->addRow(tr("Email:"), m_kitsu_email_);
    kitsu_form->addRow(tr("Username:"), m_kitsu_username_);
    kitsu_form->addRow(tr("Password:"), m_kitsu_password_);
    ui_->verticalLayout_3->addWidget(kitsu_box);

    auto* kitsu_btn = new QPushButton(tr("Open Kitsu in browser…"), ui_->accountsPage);
    connect(kitsu_btn, &QPushButton::clicked, this,
            []() { QDesktopServices::openUrl(QUrl("https://kitsu.app/settings")); });
    ui_->verticalLayout_3->addWidget(kitsu_btn);
  }

  ui_->checkUpdatesOnStartup->setChecked(taiga::settings.checkForUpdatesOnStartup());
  ui_->checkScanLibraryOnStartup->setChecked(taiga::settings.scanLibraryOnStartup());
  ui_->checkStartMinimized->setChecked(taiga::settings.startMinimized());
  ui_->checkStartWithWindows->setChecked(taiga::settings.startWithWindows());
#ifndef Q_OS_WIN
  ui_->checkStartWithWindows->setEnabled(false);
  ui_->checkStartWithWindows->setToolTip(tr("Windows only."));
#endif
  ui_->checkStartMinimized->setToolTip(
      tr("If \"Minimize to tray\" is also enabled, Taiga starts in the tray only."));

  ui_->checkCloseToTray->setChecked(taiga::settings.closeToTray());
  ui_->checkMinimizeToTray->setChecked(taiga::settings.minimizeToTray());
  {
    const bool tray = QSystemTrayIcon::isSystemTrayAvailable();
    ui_->checkCloseToTray->setEnabled(tray);
    ui_->checkMinimizeToTray->setEnabled(tray);
    if (!tray) {
      const QString tip =
          tr("No system tray is available; these options apply only when the tray is present.");
      ui_->groupWindowTray->setToolTip(tip);
    }
  }

  ui_->comboColorScheme->addItem(tr("Follow system"), static_cast<int>(Qt::ColorScheme::Unknown));
  ui_->comboColorScheme->addItem(tr("Light"), static_cast<int>(Qt::ColorScheme::Light));
  ui_->comboColorScheme->addItem(tr("Dark"), static_cast<int>(Qt::ColorScheme::Dark));
  {
    const int cur = static_cast<int>(taiga::settings.appColorScheme());
    int tidx = ui_->comboColorScheme->findData(cur);
    if (tidx < 0) tidx = 0;
    ui_->comboColorScheme->setCurrentIndex(tidx);
  }

  ui_->checkMediaDetection->setChecked(taiga::settings.mediaDetectionEnabled());
  ui_->checkMediaPlayerPolling->setChecked(taiga::settings.mediaDetectionPlayersEnabled());
  ui_->checkMediaStreaming->setChecked(taiga::settings.mediaDetectionStreamingEnabled());
  streaming_provider_checks_.clear();
  {
    int row = 0;
    int col = 0;
    for (const auto& e : track::streaming::providerUiEntries()) {
      auto* cb = new QCheckBox(tr(e.label), ui_->groupStreamingProviders);
      cb->setChecked(taiga::settings.streamProviderEnabled(std::string(e.slug)));
      ui_->gridStreamingProviders->addWidget(cb, row, col);
      streaming_provider_checks_.emplace_back(std::string(e.slug), cb);
      ++col;
      if (col >= 2) {
        col = 0;
        ++row;
      }
    }
  }
  ui_->checkNotifyMediaRecognized->setChecked(taiga::settings.mediaNotifyRecognizedBalloon());
  ui_->checkNotifyMediaUnrecognized->setChecked(taiga::settings.mediaNotifyUnrecognizedBalloon());
  ui_->plainBalloonFormatRecognized->setPlainText(
      QString::fromStdString(taiga::settings.mediaNotifyBalloonFormatRecognized()));
  ui_->plainBalloonFormatUnrecognized->setPlainText(
      QString::fromStdString(taiga::settings.mediaNotifyBalloonFormatUnrecognized()));
  ui_->checkBalloonUnrecognizedAppendHint->setChecked(
      taiga::settings.mediaNotifyBalloonUnrecognizedAppendHint());
  ui_->spinDetectionInterval->setValue(
      static_cast<int>(taiga::settings.mediaDetectionInterval().count() / 1000));
#ifndef Q_OS_WINDOWS
  ui_->checkMediaDetection->setEnabled(false);
  ui_->checkMediaPlayerPolling->setEnabled(false);
  ui_->checkMediaStreaming->setEnabled(false);
  ui_->checkNotifyMediaRecognized->setEnabled(false);
  ui_->checkNotifyMediaUnrecognized->setEnabled(false);
  ui_->spinDetectionInterval->setEnabled(false);
  ui_->groupStreamingProviders->setEnabled(false);
#endif
  {
    const auto rec_ui = [this] {
#ifdef Q_OS_WINDOWS
      const bool master = ui_->checkMediaDetection->isChecked();
      const bool any_source =
          ui_->checkMediaPlayerPolling->isChecked() || ui_->checkMediaStreaming->isChecked();
      ui_->checkMediaPlayerPolling->setEnabled(master);
      ui_->checkMediaStreaming->setEnabled(master);
      ui_->checkNotifyMediaRecognized->setEnabled(master);
      ui_->checkNotifyMediaUnrecognized->setEnabled(master);
      ui_->spinDetectionInterval->setEnabled(master && any_source);
      ui_->groupStreamingProviders->setEnabled(master && ui_->checkMediaStreaming->isChecked());
#else
      (void)0;
#endif
    };
    connect(ui_->checkMediaDetection, &QCheckBox::checkStateChanged, this,
            [rec_ui](Qt::CheckState) { rec_ui(); });
    connect(ui_->checkMediaPlayerPolling, &QCheckBox::checkStateChanged, this,
            [rec_ui](Qt::CheckState) { rec_ui(); });
    connect(ui_->checkMediaStreaming, &QCheckBox::checkStateChanged, this,
            [rec_ui](Qt::CheckState) { rec_ui(); });
    rec_ui();
  }

  ui_->checkAutoUpdateList->setChecked(taiga::settings.recognitionAutoUpdateList());
  ui_->spinAutoUpdateDelay->setValue(taiga::settings.recognitionUpdateDelaySeconds());
  ui_->checkAutoUpdateOutOfRange->setChecked(taiga::settings.recognitionUpdateOutOfRange());
  {
    const auto auto_update_ui = [this] {
      const bool on = ui_->checkAutoUpdateList->isChecked();
      ui_->spinAutoUpdateDelay->setEnabled(on);
      ui_->checkAutoUpdateOutOfRange->setEnabled(on);
    };
    connect(ui_->checkAutoUpdateList, &QCheckBox::checkStateChanged, this,
            [auto_update_ui](Qt::CheckState) { auto_update_ui(); });
    auto_update_ui();
  }
  // Dynamically create "delete after watched" checkbox (not in the .ui form).
  {
    auto* cb = new QCheckBox(tr("Delete local file after it is marked as watched"), this);
    cb->setChecked(taiga::settings.recognitionDeleteAfterWatched());
    cb->setToolTip(
        tr("Permanently deletes the video file after the episode is marked as watched. Use with "
           "care."));
    connect(cb, &QCheckBox::checkStateChanged, this, [cb](Qt::CheckState) {
      taiga::settings.setRecognitionDeleteAfterWatched(cb->isChecked());
    });
    if (ui_->checkAutoUpdateOutOfRange && ui_->checkAutoUpdateOutOfRange->parentWidget()) {
      auto* parent = ui_->checkAutoUpdateOutOfRange->parentWidget();
      if (auto* vl = qobject_cast<QVBoxLayout*>(parent->layout())) {
        const int idx = vl->indexOf(ui_->checkAutoUpdateOutOfRange);
        if (idx >= 0)
          vl->insertWidget(idx + 1, cb);
        else
          vl->addWidget(cb);
      } else if (parent->layout()) {
        parent->layout()->addWidget(cb);
      }
    }
  }

  ui_->checkLibraryLookupParent->setChecked(taiga::settings.libraryScanLookupParentDirectories());
  ui_->plainRecognitionIgnored->setPlainText(
      QString::fromStdString(taiga::settings.recognitionIgnoredSubstrings()));
  ui_->lineMediaPlayerExecutable->setText(
      QString::fromStdString(taiga::settings.mediaPlayerExecutablePath()));
  connect(ui_->buttonBrowseMediaPlayerExecutable, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
    const QString filter = tr("Executables (*.exe);;All files (*)");
#else
    const QString filter = tr("All files (*)");
#endif
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Media player"), ui_->lineMediaPlayerExecutable->text(), filter);
    if (!path.isEmpty()) ui_->lineMediaPlayerExecutable->setText(path);
  });

  {
    const auto focus_ui = [this] {
      ui_->spinFocusMinutes->setEnabled(ui_->checkSyncOnFocus->isChecked());
    };
    connect(ui_->checkSyncOnFocus, &QCheckBox::checkStateChanged, this,
            [focus_ui](Qt::CheckState) { focus_ui(); });
    focus_ui();
  }

  ui_->lineProxyHost->setText(QString::fromStdString(taiga::settings.proxyHost()));
  ui_->lineProxyUsername->setText(QString::fromStdString(taiga::settings.proxyUsername()));
  ui_->lineProxyPassword->setText(QString::fromStdString(taiga::settings.proxyPassword()));
  ui_->checkNetworkRelaxedTls->setChecked(taiga::settings.networkRelaxedTls());
  ui_->checkNetworkRelaxedTls->setToolTip(
      tr("Disables strict TLS certificate verification. Use only if you have proxy or certificate "
         "issues."));
  ui_->checkSyncOnFocus->setChecked(taiga::settings.syncOnWindowFocus());
  ui_->spinFocusMinutes->setValue(taiga::settings.syncOnWindowFocusMinutes());

  ui_->checkLibraryWatchFolders->setChecked(taiga::settings.libraryWatchFoldersEnabled());

  for (const auto& folder : taiga::settings.libraryFolders()) {
    ui_->libraryFolderList->addItem(QString::fromStdString(folder));
  }

  {
    constexpr qint64 kMiB = 1024LL * 1024;
    const qint64 b = taiga::settings.libraryScanMinFileSizeBytes();
    int mib = 0;
    if (b > 0) {
      mib = static_cast<int>(std::min((b + kMiB - 1) / kMiB, static_cast<qint64>(102400)));
      mib = std::max(mib, 1);
    }
    ui_->spinLibraryMinFileSizeMiB->setValue(mib);
  }

  {
    QString ts = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
    if (ts.isEmpty()) ts = taiga::defaultTorrentDiscoverySearchUrl();
    ui_->lineTorrentSearchUrl->setText(ts);
    QString fs = QString::fromStdString(taiga::settings.torrentDiscoveryFeedSourceUrl());
    if (fs.isEmpty()) fs = taiga::defaultTorrentDiscoveryFeedSourceUrl();
    ui_->lineTorrentFeedSourceUrl->setText(fs);
  }

  ui_->lineTorrentClientDownloadPath->setText(
      QString::fromStdString(taiga::settings.torrentClientDownloadPath()));
  ui_->lineTorrentFileSavePath->setText(
      QString::fromStdString(taiga::settings.torrentFileSavePath()));
  connect(ui_->buttonBrowseTorrentClientDownload, &QPushButton::clicked, this, [this] {
    constexpr auto options =
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Client download folder"), ui_->lineTorrentClientDownloadPath->text(), options);
    if (!directory.isEmpty()) ui_->lineTorrentClientDownloadPath->setText(directory);
  });
  connect(ui_->buttonBrowseTorrentFileSave, &QPushButton::clicked, this, [this] {
    constexpr auto options =
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr(".torrent save folder"), ui_->lineTorrentFileSavePath->text(), options);
    if (!directory.isEmpty()) ui_->lineTorrentFileSavePath->setText(directory);
  });

  ui_->checkTorrentAutocheckCatalog->setChecked(taiga::settings.torrentDiscoveryAutoCheckEnabled());
  ui_->spinTorrentAutocheckMinutes->setValue(
      taiga::settings.torrentDiscoveryAutoCheckIntervalMinutes());
  {
    QComboBox* const c = ui_->comboTorrentNewCatalogAction;
    if (c->count() == 0) {
      c->addItem(tr("Notify (status bar and tray)"),
                 static_cast<int>(taiga::TorrentDiscoveryNewCatalogAction::Notify));
      c->addItem(tr("Download automatically — status only until download queue exists"),
                 static_cast<int>(taiga::TorrentDiscoveryNewCatalogAction::Download));
    }
    const int want = static_cast<int>(taiga::settings.torrentDiscoveryNewCatalogAction());
    for (int i = 0; i < c->count(); ++i) {
      if (c->itemData(i).toInt() == want) {
        c->setCurrentIndex(i);
        break;
      }
    }
  }
  {
    QComboBox* const sb = ui_->comboTorrentRssSortBy;
    if (sb->count() == 0) {
      sb->addItem(tr("Episode number"), QStringLiteral("episode_number"));
      sb->addItem(tr("Published date"), QStringLiteral("release_date"));
    }
    const QString want_by = QString::fromStdString(taiga::settings.torrentRssSortBy());
    for (int i = 0; i < sb->count(); ++i) {
      if (sb->itemData(i).toString() == want_by) {
        sb->setCurrentIndex(i);
        break;
      }
    }
  }
  {
    QComboBox* const so = ui_->comboTorrentRssSortOrder;
    if (so->count() == 0) {
      so->addItem(tr("Ascending"), QStringLiteral("ascending"));
      so->addItem(tr("Descending"), QStringLiteral("descending"));
    }
    const QString want_o = QString::fromStdString(taiga::settings.torrentRssSortOrder());
    for (int i = 0; i < so->count(); ++i) {
      if (so->itemData(i).toString() == want_o) {
        so->setCurrentIndex(i);
        break;
      }
    }
  }
  ui_->checkTorrentFeedFilterEnabled->setChecked(taiga::settings.torrentFeedFilterEnabled());
  ui_->spinTorrentFeedArchiveMax->setValue(taiga::settings.torrentFeedArchiveMaxItems());
  ui_->plainTorrentFeedIncludeRegex->setPlainText(
      QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()));
  ui_->plainTorrentFeedExcludeRegex->setPlainText(
      QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()));
  {
    QPushButton* ok_btn = ui_->buttonBox->button(QDialogButtonBox::Ok);
    const auto update_valid = [this, ok_btn]() {
      const auto lines = [](const QString& t) {
        QStringList out;
        for (QString s :
             t.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
          s = s.trimmed();
          if (!s.isEmpty()) out.push_back(s);
        }
        return out;
      };
      const QStringList inc = lines(ui_->plainTorrentFeedIncludeRegex->toPlainText());
      const QStringList exc = lines(ui_->plainTorrentFeedExcludeRegex->toPlainText());
      const QSet<QString> exc_set(exc.begin(), exc.end());

      QString warning;
      bool valid = true;

      if (!inc.isEmpty() && exc_set.contains(QStringLiteral(".*"))) {
        warning =
            tr("Your “Hide if matches” list contains <code>.*</code>, which hides everything, so "
               "no results can appear.");
        valid = false;
      } else if (!inc.isEmpty()) {
        int covered = 0;
        for (const QString& s : inc) {
          if (exc_set.contains(s)) ++covered;
        }
        if (covered == inc.size()) {
          warning =
              tr("All “Show only if matches” rules are also present in “Hide if matches”, so no "
                 "results can appear.");
          valid = false;
        }
      }

      if (ui_->labelTorrentFeedRegexWarning) {
        ui_->labelTorrentFeedRegexWarning->setText(
            warning.isEmpty() ? QString{}
                              : (u"<span style=\"color:#c33\"><b>%1</b></span>"_s.arg(warning)));
      }
      if (ok_btn) ok_btn->setEnabled(valid);
    };
    connect(ui_->plainTorrentFeedIncludeRegex, &QPlainTextEdit::textChanged, this, update_valid);
    connect(ui_->plainTorrentFeedExcludeRegex, &QPlainTextEdit::textChanged, this, update_valid);
    update_valid();
  }
  {
    const auto addLine = [](QPlainTextEdit* edit, const QString& line) {
      if (!edit) return;
      const QString trimmed = line.trimmed();
      if (trimmed.isEmpty()) return;
      QStringList lines = edit->toPlainText().split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                    Qt::SkipEmptyParts);
      for (QString& s : lines) s = s.trimmed();
      if (lines.contains(trimmed)) return;
      QString cur = edit->toPlainText().trimmed();
      if (!cur.isEmpty() && !cur.endsWith('\n')) cur += QLatin1Char('\n');
      cur += trimmed;
      edit->setPlainText(cur + QLatin1Char('\n'));
    };

    connect(ui_->buttonTorrentPresetDiscardBadVideo, &QPushButton::clicked, this,
            [this, addLine]() {
              // v1 preset: discard AVI, DIVX, LQ, RMVB, SD, WMV, XVID
              addLine(ui_->plainTorrentFeedExcludeRegex,
                      QStringLiteral("\\b(AVI|DIVX|LQ|RMVB|SD|WMV|XVID)\\b"));
            });
    connect(ui_->buttonTorrentPresetPrefer1080p, &QPushButton::clicked, this, [this, addLine]() {
      // v1 preset: prefer high-res
      addLine(ui_->plainTorrentFeedIncludeRegex, QStringLiteral("\\b1080p\\b"));
    });
    connect(ui_->buttonTorrentPresetPreferV2, &QPushButton::clicked, this, [this, addLine]() {
      // v1 preset: prefer v2+ when multiple exist
      addLine(ui_->plainTorrentFeedIncludeRegex, QStringLiteral("\\bv[2-9]\\b"));
      addLine(ui_->plainTorrentFeedIncludeRegex, QStringLiteral("\\bv\\d{2,}\\b"));
    });
    connect(ui_->buttonTorrentPresetDiscardCams, &QPushButton::clicked, this, [this, addLine]() {
      addLine(ui_->plainTorrentFeedExcludeRegex, QStringLiteral("\\b(CAM|TS|TC)\\b"));
    });
    connect(ui_->buttonTorrentPresetPreferX265, &QPushButton::clicked, this, [this, addLine]() {
      addLine(ui_->plainTorrentFeedIncludeRegex, QStringLiteral("\\b(x265|hevc)\\b"));
    });
    connect(ui_->buttonTorrentPresetPreferMKV, &QPushButton::clicked, this, [this, addLine]() {
      addLine(ui_->plainTorrentFeedIncludeRegex, QStringLiteral("\\bMKV\\b"));
    });
    connect(ui_->buttonTorrentPresetPreferAAC, &QPushButton::clicked, this, [this, addLine]() {
      addLine(ui_->plainTorrentFeedIncludeRegex, QStringLiteral("\\b(AAC|FLAC)\\b"));
    });
    connect(ui_->buttonTorrentRegexAddRule, &QPushButton::clicked, this, [this, addLine]() {
      QDialog dlg(this);
      dlg.setWindowTitle(tr("Add torrent filter rule"));
      auto* layout = new QVBoxLayout(&dlg);

      auto* form = new QFormLayout();
      auto* target = new QComboBox(&dlg);
      target->addItem(tr("Show only if matches"), 0);
      target->addItem(tr("Hide if matches"), 1);
      auto* mode = new QComboBox(&dlg);
      mode->addItem(tr("Contains (case-insensitive)"), 0);
      mode->addItem(tr("Whole word (case-insensitive)"), 1);
      mode->addItem(tr("Regular expression"), 2);
      auto* text = new QLineEdit(&dlg);
      text->setPlaceholderText(tr("Example: 1080p"));
      form->addRow(tr("Apply to:"), target);
      form->addRow(tr("Match:"), mode);
      form->addRow(tr("Text / regex:"), text);
      layout->addLayout(form);

      auto* help = new QLabel(
          tr("This creates a line in the torrent RSS regex lists. You can always edit the result "
             "manually afterwards."),
          &dlg);
      help->setWordWrap(true);
      layout->addWidget(help);

      auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
      QPushButton* ok = box->button(QDialogButtonBox::Ok);
      ok->setEnabled(false);
      layout->addWidget(box);
      connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
      connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

      const auto rebuildOkEnabled = [ok, text, mode]() {
        const QString v = text->text().trimmed();
        if (v.isEmpty()) {
          ok->setEnabled(false);
          return;
        }
        if (mode->currentData().toInt() == 2) {
          QRegularExpression re(v, QRegularExpression::CaseInsensitiveOption);
          ok->setEnabled(re.isValid());
          return;
        }
        ok->setEnabled(true);
      };
      connect(text, &QLineEdit::textChanged, &dlg, [rebuildOkEnabled]() { rebuildOkEnabled(); });
      connect(mode, &QComboBox::currentIndexChanged, &dlg,
              [rebuildOkEnabled](int) { rebuildOkEnabled(); });
      rebuildOkEnabled();

      if (dlg.exec() != QDialog::Accepted) return;

      const QString raw = text->text().trimmed();
      const int where = target->currentData().toInt();
      const int how = mode->currentData().toInt();

      QString line;
      if (how == 2) {
        line = raw;
      } else {
        const QString escaped = QRegularExpression::escape(raw);
        if (how == 1) {
          line = QStringLiteral("\\b%1\\b").arg(escaped);
        } else {
          line = escaped;
        }
      }

      addLine(where == 1 ? ui_->plainTorrentFeedExcludeRegex : ui_->plainTorrentFeedIncludeRegex,
              line);
    });
    connect(ui_->buttonTorrentRegexClearInclude, &QPushButton::clicked, this,
            [this]() { ui_->plainTorrentFeedIncludeRegex->clear(); });
    connect(ui_->buttonTorrentRegexClearExclude, &QPushButton::clicked, this,
            [this]() { ui_->plainTorrentFeedExcludeRegex->clear(); });
  }
  {
    const auto upd_feed_cap = [this] {
      const bool on = ui_->checkTorrentFeedFilterEnabled->isChecked();
      ui_->spinTorrentFeedArchiveMax->setEnabled(on);
      ui_->labelTorrentFeedArchiveMax->setEnabled(on);
    };
    connect(ui_->checkTorrentFeedFilterEnabled, &QCheckBox::checkStateChanged, this,
            [upd_feed_cap](Qt::CheckState) { upd_feed_cap(); });
    upd_feed_cap();
  }
  ui_->checkTorrentDownloadUseMagnet->setChecked(taiga::settings.torrentDownloadUseMagnet());

  ui_->checkTorrentUseAnimeFolder->setChecked(taiga::settings.torrentDownloadUseAnimeFolder());
  ui_->checkTorrentFallbackClientPath->setChecked(
      taiga::settings.torrentDownloadFallbackOnClientPath());
  ui_->checkTorrentCreateSubfolder->setChecked(taiga::settings.torrentDownloadCreateSubfolder());
  ui_->checkTorrentAppOpen->setChecked(taiga::settings.torrentAppOpen());
  if (taiga::settings.torrentAppMode() == 2) {
    ui_->radioTorrentAppCustom->setChecked(true);
  } else {
    ui_->radioTorrentAppDefault->setChecked(true);
  }
  ui_->lineTorrentAppExecutable->setText(
      QString::fromStdString(taiga::settings.torrentAppExecutablePath()));
  {
    const auto upd_torrent_handling_ui = [this] {
      const bool open = ui_->checkTorrentAppOpen->isChecked();
      ui_->radioTorrentAppDefault->setEnabled(open);
      ui_->radioTorrentAppCustom->setEnabled(open);
      const bool custom = open && ui_->radioTorrentAppCustom->isChecked();
      ui_->lineTorrentAppExecutable->setEnabled(custom);
      ui_->buttonBrowseTorrentAppExecutable->setEnabled(custom);
    };
    connect(ui_->checkTorrentAppOpen, &QCheckBox::checkStateChanged, this,
            [upd_torrent_handling_ui](Qt::CheckState) { upd_torrent_handling_ui(); });
    connect(ui_->radioTorrentAppDefault, &QRadioButton::toggled, this,
            [upd_torrent_handling_ui](bool) { upd_torrent_handling_ui(); });
    connect(ui_->radioTorrentAppCustom, &QRadioButton::toggled, this,
            [upd_torrent_handling_ui](bool) { upd_torrent_handling_ui(); });
    upd_torrent_handling_ui();
  }
  ui_->checkTorrentCreateSubfolder->setEnabled(ui_->checkTorrentFallbackClientPath->isChecked());
  connect(ui_->checkTorrentFallbackClientPath, &QCheckBox::checkStateChanged, this,
          [this](Qt::CheckState) {
            ui_->checkTorrentCreateSubfolder->setEnabled(
                ui_->checkTorrentFallbackClientPath->isChecked());
            if (!ui_->checkTorrentFallbackClientPath->isChecked()) {
              ui_->checkTorrentCreateSubfolder->setChecked(false);
            }
          });
  connect(ui_->buttonBrowseTorrentAppExecutable, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
    const QString filter = tr("Executables (*.exe);;All files (*)");
#else
    const QString filter = tr("All files (*)");
#endif
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Torrent client application"), ui_->lineTorrentAppExecutable->text(), filter);
    if (!path.isEmpty()) ui_->lineTorrentAppExecutable->setText(path);
  });

  {
    const auto upd_autocheck_ui = [this] {
      const bool on = ui_->checkTorrentAutocheckCatalog->isChecked();
      ui_->spinTorrentAutocheckMinutes->setEnabled(on);
      ui_->comboTorrentNewCatalogAction->setEnabled(on);
      ui_->labelTorrentNewCatalogAction->setEnabled(on);
    };
    connect(ui_->checkTorrentAutocheckCatalog, &QCheckBox::checkStateChanged, this,
            [upd_autocheck_ui](Qt::CheckState) { upd_autocheck_ui(); });
    upd_autocheck_ui();
  }

  connect(ui_->buttonAddLibraryFolder, &QPushButton::clicked, this, [this] {
    constexpr auto options =
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;
    const QString directory =
        QFileDialog::getExistingDirectory(this, tr("Add library folder"), {}, options);
    if (directory.isEmpty()) return;
    const auto matches = ui_->libraryFolderList->findItems(directory, Qt::MatchFixedString);
    if (!matches.isEmpty()) return;
    ui_->libraryFolderList->addItem(directory);
  });
  connect(ui_->buttonRemoveLibraryFolder, &QPushButton::clicked, this, [this] {
    delete ui_->libraryFolderList->takeItem(ui_->libraryFolderList->currentRow());
  });
  connect(ui_->buttonScanLibraryNow, &QPushButton::clicked, this, [this] {
    std::vector<std::string> folders;
    folders.reserve(static_cast<size_t>(ui_->libraryFolderList->count()));
    for (int i = 0; i < ui_->libraryFolderList->count(); ++i) {
      folders.push_back(ui_->libraryFolderList->item(i)->text().toStdString());
    }
    taiga::settings.setLibraryFolders(std::move(folders));
    {
      constexpr qint64 kMiB = 1024LL * 1024;
      const int v = ui_->spinLibraryMinFileSizeMiB->value();
      taiga::settings.setLibraryScanMinFileSizeBytes(v > 0 ? static_cast<qint64>(v) * kMiB : 0);
    }
    if (gui::mainWindow()) gui::mainWindow()->refreshLibraryRootsFromSettings();

    if (auto* mw = gui::mainWindow()) {
      mw->runInteractiveLibraryScan();
    } else {
      QMessageBox::information(this, tr("Taiga"),
                               tr("Open the main window first, then try scanning again."));
    }
  });

  ui_->comboListService->addItem(sync::serviceName(sync::ServiceId::AniList),
                                 QStringLiteral("anilist"));
  ui_->comboListService->addItem(sync::serviceName(sync::ServiceId::MyAnimeList),
                                 QStringLiteral("myanimelist"));
  ui_->comboListService->addItem(sync::serviceName(sync::ServiceId::Kitsu),
                                 QStringLiteral("kitsu"));
  {
    const QString cur = QString::fromStdString(taiga::settings.service());
    int sidx = ui_->comboListService->findData(cur);
    if (sidx < 0) sidx = 0;
    ui_->comboListService->setCurrentIndex(sidx);
  }
  ui_->checkListUpdatesEnabled->setChecked(taiga::settings.listSynchronizationEnabled());
  ui_->checkSyncPushAskConfirm->setChecked(taiga::settings.syncListPushAskConfirm());
  ui_->checkSyncPushAskConfirm->setToolTip(
      tr("When off, list edits upload immediately without a confirmation prompt (subject to the "
         "debounce delay above)."));
  ui_->spinListUpdateApiDelay->setValue(taiga::settings.syncListUpdateDelaySeconds());
  ui_->spinListUpdateApiDelay->setToolTip(
      tr("Seconds to wait after a local list change before syncing to the remote service. "
         "0 = immediate. Useful to reduce API calls when adjusting progress multiple times."));

  ui_->comboListTitleLanguage->addItem(tr("Romaji"),
                                       static_cast<int>(anime::TitleLanguage::Romaji));
  ui_->comboListTitleLanguage->addItem(tr("English"),
                                       static_cast<int>(anime::TitleLanguage::English));
  ui_->comboListTitleLanguage->addItem(tr("Native title"),
                                       static_cast<int>(anime::TitleLanguage::Native));
  ui_->comboListTitleLanguage->setToolTip(
      tr("Primary title shown in the anime list and search cards. Hover a row to see all title "
         "variants."));
  {
    const int cur = static_cast<int>(taiga::settings.listTitleLanguage());
    int tidx = ui_->comboListTitleLanguage->findData(cur);
    if (tidx < 0) tidx = 0;
    ui_->comboListTitleLanguage->setCurrentIndex(tidx);
  }

  {
    const auto fill_row_actions = [this](QComboBox* box) {
      box->clear();
      box->addItem(tr("Do nothing"), static_cast<int>(taiga::ListRowAction::Nothing));
      box->addItem(editListEntryActionLabel(),
                   static_cast<int>(taiga::ListRowAction::EditListEntry));
      box->addItem(libraryOpenFolderActionLabel(),
                   static_cast<int>(taiga::ListRowAction::OpenFolder));
      box->addItem(playNextEpisodeActionLabel(), static_cast<int>(taiga::ListRowAction::PlayNext));
      box->addItem(mediaViewDetailsActionLabel(),
                   static_cast<int>(taiga::ListRowAction::ShowDetails));
      box->addItem(openAnimePageInBrowserActionLabel(),
                   static_cast<int>(taiga::ListRowAction::OpenAnimePage));
    };
    fill_row_actions(ui_->comboListDoubleClick);
    fill_row_actions(ui_->comboListMiddleClick);
    const int d = static_cast<int>(taiga::settings.listDoubleClickAction());
    int di = ui_->comboListDoubleClick->findData(d);
    if (di < 0) di = 0;
    ui_->comboListDoubleClick->setCurrentIndex(di);
    const int m = static_cast<int>(taiga::settings.listMiddleClickAction());
    int mi = ui_->comboListMiddleClick->findData(m);
    if (mi < 0) mi = 0;
    ui_->comboListMiddleClick->setCurrentIndex(mi);
  }

  ui_->checkListProgressShowAired->setChecked(taiga::settings.listProgressShowAired());
  ui_->checkListProgressShowAvailable->setChecked(taiga::settings.listProgressShowAvailable());
  ui_->checkListProgressShowAvailable->setToolTip(
      tr("Requires a library scan (Tools menu or startup option) so Taiga knows which episode "
         "files exist. No slow disk access while scrolling the list."));
  ui_->checkListHighlightNextOnDisk->setChecked(taiga::settings.listHighlightNextEpisodeOnDisk());
  ui_->checkListHighlightOnTop->setChecked(taiga::settings.listHighlightAvailableOnTop());
  ui_->checkListShowMatureContent->setChecked(taiga::settings.listShowMatureContent());

  ui_->checkLocalBackupEnabled->setChecked(taiga::settings.localListBackupEnabled());
  ui_->lineLocalBackupPath->setText(taiga::settings.localListBackupPath());
  ui_->lineLocalBackupPath->setEnabled(taiga::settings.localListBackupEnabled());
  ui_->btnLocalBackupBrowse->setEnabled(taiga::settings.localListBackupEnabled());
  connect(ui_->checkLocalBackupEnabled, &QCheckBox::toggled, this, [this](const bool on) {
    ui_->lineLocalBackupPath->setEnabled(on);
    ui_->btnLocalBackupBrowse->setEnabled(on);
  });
  connect(ui_->btnLocalBackupBrowse, &QPushButton::clicked, this, [this]() {
    const QString current = ui_->lineLocalBackupPath->text();
    const QString def = current.isEmpty()
                            ? QDir::home().filePath(u"animelist_backup.xml"_s)
                            : current;
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Choose backup file location"), def,
                                     tr("MAL XML (*.xml);;All files (*)"));
    if (!path.isEmpty()) ui_->lineLocalBackupPath->setText(path);
  });

  ui_->checkListHighlightNextOnDisk->setToolTip(
      tr("Uses the same library scan index as “episodes on disk” on the progress bar — no disk I/O "
         "while scrolling."));
  ui_->checkListHighlightOnTop->setToolTip(
      tr("Applies on the Anime list and Search results. Secondary sort order is unchanged from "
         "the imported secondary sort settings (this build uses a single visible sort column)."));

  ui_->treeWidget->setCurrentItem(ui_->treeWidget->topLevelItem(0));
}

void SettingsDialog::accept() {
  // Snapshot settings that affect Home / Announced so we only refresh those surfaces when needed.
  const bool prev_local_backup_enabled = taiga::settings.localListBackupEnabled();
  const QString prev_local_backup_path = taiga::settings.localListBackupPath();
  const std::string prev_service = taiga::settings.service();
  const std::vector<std::string> prev_library_folders = taiga::settings.libraryFolders();
  const bool prev_scan_library_on_startup = taiga::settings.scanLibraryOnStartup();
  const qint64 prev_library_min_file_bytes = taiga::settings.libraryScanMinFileSizeBytes();
  const bool prev_library_watch = taiga::settings.libraryWatchFoldersEnabled();
  const std::string prev_torrent_client_path = taiga::settings.torrentClientDownloadPath();
  const bool prev_torrent_create_subfolder = taiga::settings.torrentDownloadCreateSubfolder();
  const bool prev_qbit_api = taiga::settings.torrentQBitApiEnabled();
  const std::string prev_qbit_url = taiga::settings.torrentQBitApiUrl();

  // Persist all values from the UI while this dialog still exists (show() uses
  // WA_DeleteOnClose — do not defer writes on `this`). Batch JSON writes so OK is not delayed
  // by dozens of separate QSettings opens. Heavy UI refresh runs on the next event-loop tick.
  const base::Settings::BatchScope batch_settings(&taiga::settings);
  const base::Settings::BatchScope batch_accounts(&taiga::accounts);

  taiga::settings.setSyncAutoOnStart(ui_->checkSyncOnStart->isChecked());
  taiga::settings.setAppColorScheme(
      static_cast<Qt::ColorScheme>(ui_->comboColorScheme->currentData().toInt()));

  taiga::settings.setMediaDetectionEnabled(ui_->checkMediaDetection->isChecked());
  taiga::settings.setMediaDetectionPlayersEnabled(ui_->checkMediaPlayerPolling->isChecked());
  taiga::settings.setMediaDetectionStreamingEnabled(ui_->checkMediaStreaming->isChecked());
  for (const auto& p : streaming_provider_checks_) {
    taiga::settings.setStreamProviderEnabled(p.first, p.second->isChecked());
  }
  taiga::settings.setRecognitionIgnoredSubstrings(
      ui_->plainRecognitionIgnored->toPlainText().toStdString());
  taiga::settings.setMediaNotifyRecognizedBalloon(ui_->checkNotifyMediaRecognized->isChecked());
  taiga::settings.setMediaNotifyUnrecognizedBalloon(ui_->checkNotifyMediaUnrecognized->isChecked());
  taiga::settings.setMediaNotifyBalloonFormatRecognized(
      ui_->plainBalloonFormatRecognized->toPlainText().toStdString());
  taiga::settings.setMediaNotifyBalloonFormatUnrecognized(
      ui_->plainBalloonFormatUnrecognized->toPlainText().toStdString());
  taiga::settings.setMediaNotifyBalloonUnrecognizedAppendHint(
      ui_->checkBalloonUnrecognizedAppendHint->isChecked());
  taiga::settings.setMediaDetectionInterval(
      std::chrono::milliseconds(ui_->spinDetectionInterval->value() * 1000));
  taiga::settings.setRecognitionAutoUpdateList(ui_->checkAutoUpdateList->isChecked());
  taiga::settings.setRecognitionUpdateDelaySeconds(ui_->spinAutoUpdateDelay->value());
  taiga::settings.setRecognitionUpdateOutOfRange(ui_->checkAutoUpdateOutOfRange->isChecked());
  taiga::settings.setLibraryScanLookupParentDirectories(ui_->checkLibraryLookupParent->isChecked());
  taiga::settings.setMediaPlayerExecutablePath(
      ui_->lineMediaPlayerExecutable->text().trimmed().toStdString());

  taiga::settings.setCheckForUpdatesOnStartup(ui_->checkUpdatesOnStartup->isChecked());
  taiga::settings.setScanLibraryOnStartup(ui_->checkScanLibraryOnStartup->isChecked());
  taiga::settings.setStartMinimized(ui_->checkStartMinimized->isChecked());
  taiga::settings.setStartWithWindows(ui_->checkStartWithWindows->isChecked());
  if (QSystemTrayIcon::isSystemTrayAvailable()) {
    taiga::settings.setCloseToTray(ui_->checkCloseToTray->isChecked());
    taiga::settings.setMinimizeToTray(ui_->checkMinimizeToTray->isChecked());
  }
  taiga::settings.setProxyHost(ui_->lineProxyHost->text().toStdString());
  taiga::settings.setProxyUsername(ui_->lineProxyUsername->text().toStdString());
  taiga::settings.setProxyPassword(ui_->lineProxyPassword->text().toStdString());
  taiga::settings.setNetworkRelaxedTls(ui_->checkNetworkRelaxedTls->isChecked());
  taiga::settings.setSyncOnWindowFocus(ui_->checkSyncOnFocus->isChecked());
  taiga::settings.setSyncOnWindowFocusMinutes(ui_->spinFocusMinutes->value());

  if (m_kitsu_email_) {
    taiga::accounts.setKitsuEmail(m_kitsu_email_->text().toStdString());
    taiga::accounts.setKitsuUsername(m_kitsu_username_->text().toStdString());
    taiga::accounts.setKitsuPassword(m_kitsu_password_->text().toStdString());
  }
  if (m_mal_username_) {
    taiga::accounts.setMyanimelistUsername(m_mal_username_->text().toStdString());
    taiga::accounts.setMyanimelistAccessToken(m_mal_access_->text().toStdString());
    taiga::accounts.setMyanimelistRefreshToken(m_mal_refresh_->text().toStdString());
  }

  {
    std::vector<std::string> folders;
    folders.reserve(static_cast<size_t>(ui_->libraryFolderList->count()));
    for (int i = 0; i < ui_->libraryFolderList->count(); ++i) {
      folders.push_back(ui_->libraryFolderList->item(i)->text().toStdString());
    }
    taiga::settings.setLibraryFolders(std::move(folders));
  }

  taiga::settings.setLibraryWatchFoldersEnabled(ui_->checkLibraryWatchFolders->isChecked());

  {
    constexpr qint64 kMiB = 1024LL * 1024;
    const int v = ui_->spinLibraryMinFileSizeMiB->value();
    taiga::settings.setLibraryScanMinFileSizeBytes(v > 0 ? static_cast<qint64>(v) * kMiB : 0);
  }

  // RSS source settings are now saved by saveDownloadsPage() below (migrated from Library page).
  // The old Library-page widgets (lineTorrentSearchUrl, etc.) are hidden and should not be read.
  taiga::settings.setTorrentFeedFilterEnabled(ui_->checkTorrentFeedFilterEnabled->isChecked());
  taiga::settings.setTorrentFeedArchiveMaxItems(ui_->spinTorrentFeedArchiveMax->value());
  taiga::settings.setTorrentFeedIncludeRegexList(
      ui_->plainTorrentFeedIncludeRegex->toPlainText().trimmed().toStdString());
  taiga::settings.setTorrentFeedExcludeRegexList(
      ui_->plainTorrentFeedExcludeRegex->toPlainText().trimmed().toStdString());
  // Download settings are now on the dedicated Torrents → Downloads page.
  saveDownloadsPage();

  taiga::settings.setService(ui_->comboListService->currentData().toString().toStdString());
  taiga::settings.setListSynchronizationEnabled(ui_->checkListUpdatesEnabled->isChecked());
  taiga::settings.setSyncListPushAskConfirm(ui_->checkSyncPushAskConfirm->isChecked());
  taiga::settings.setSyncListUpdateDelaySeconds(ui_->spinListUpdateApiDelay->value());
  taiga::settings.setListTitleLanguage(
      static_cast<anime::TitleLanguage>(ui_->comboListTitleLanguage->currentData().toInt()));
  taiga::settings.setListDoubleClickAction(
      static_cast<taiga::ListRowAction>(ui_->comboListDoubleClick->currentData().toInt()));
  taiga::settings.setListMiddleClickAction(
      static_cast<taiga::ListRowAction>(ui_->comboListMiddleClick->currentData().toInt()));
  taiga::settings.setListProgressShowAired(ui_->checkListProgressShowAired->isChecked());
  taiga::settings.setListProgressShowAvailable(ui_->checkListProgressShowAvailable->isChecked());
  taiga::settings.setListHighlightNextEpisodeOnDisk(ui_->checkListHighlightNextOnDisk->isChecked());
  taiga::settings.setListHighlightAvailableOnTop(ui_->checkListHighlightOnTop->isChecked());
  taiga::settings.setListShowMatureContent(ui_->checkListShowMatureContent->isChecked());

  const bool new_backup_enabled = ui_->checkLocalBackupEnabled->isChecked();
  const QString new_backup_path = ui_->lineLocalBackupPath->text().trimmed();
  taiga::settings.setLocalListBackupEnabled(new_backup_enabled);
  taiga::settings.setLocalListBackupPath(new_backup_path);

  // Write the backup immediately when first enabled or when the path is changed while enabled.
  const bool backup_just_activated =
      new_backup_enabled && !new_backup_path.isEmpty() &&
      (!prev_local_backup_enabled || new_backup_path != prev_local_backup_path);
  if (backup_just_activated) {
    anime::list::exportAsXml(new_backup_path.toStdString());
  }

  QDialog::accept();

  MainWindow* const mw = gui::mainWindow();
  QTimer::singleShot(
      0, qApp,
      [mw, prev_service, prev_library_folders, prev_scan_library_on_startup,
       prev_library_min_file_bytes, prev_library_watch, prev_torrent_client_path,
       prev_torrent_create_subfolder, prev_qbit_api, prev_qbit_url]() {
        const bool home_data_changed =
            prev_service != taiga::settings.service() ||
            prev_library_folders != taiga::settings.libraryFolders() ||
            prev_scan_library_on_startup != taiga::settings.scanLibraryOnStartup() ||
            prev_library_min_file_bytes != taiga::settings.libraryScanMinFileSizeBytes() ||
            prev_library_watch != taiga::settings.libraryWatchFoldersEnabled() ||
            prev_torrent_client_path != taiga::settings.torrentClientDownloadPath() ||
            prev_torrent_create_subfolder != taiga::settings.torrentDownloadCreateSubfolder() ||
            prev_qbit_api != taiga::settings.torrentQBitApiEnabled() ||
            prev_qbit_url != taiga::settings.torrentQBitApiUrl();
        const bool service_changed = prev_service != taiga::settings.service();

        gui::theme.refreshFromSettings();
        track::media::detection()->refreshPollingFromSettings();
        taiga::network()->applyProxyFromSettings();
        if (mw) mw->refreshLibraryRootsFromSettings();
        if (mw) {
          mw->applyListSynchronizationToggleFromSettings();
          mw->applyMediaDetectionToggleFromSettings();
          track::media::detection()->setPollingEnabled(
              taiga::settings.mediaDetectionPollingActive());
          mw->refreshServiceDependentUi();
          mw->refreshAnimeListProgressDecorations();
          mw->refreshAnimeListNewEpisodeHighlight();
          mw->refreshMatureContentSurfaces();
          mw->updateNoStartupSyncBanner();
          mw->refreshTorrentCatalogAutocheckTimer();
          mw->resortTorrentRssTableFromSettings();
          if (home_data_changed) mw->refreshHomeDashboard();
          if (service_changed) mw->refreshAnnouncedReleasesPageAfterServiceChange();
        }
      });
}

void SettingsDialog::show(QWidget* parent) {
  auto* dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->QDialog::show();
}

void SettingsDialog::showAccounts(QWidget* parent) {
  auto* dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->selectStackPageByRole(kStackAccounts);
  dlg->QDialog::show();
}

void SettingsDialog::selectStackPageByRole(const int stack_index) {
  for (int i = 0; i < ui_->treeWidget->topLevelItemCount(); ++i) {
    QTreeWidgetItem* const top = ui_->treeWidget->topLevelItem(i);
    if (top->data(0, kStackRole).toInt() == stack_index) {
      ui_->treeWidget->setCurrentItem(top);
      return;
    }
    for (int c = 0; c < top->childCount(); ++c) {
      QTreeWidgetItem* const ch = top->child(c);
      if (ch->data(0, kStackRole).toInt() == stack_index) {
        ui_->treeWidget->setCurrentItem(ch);
        return;
      }
    }
  }
}

void SettingsDialog::buildDownloadsPage() {
  // Create a scrollable page and add it to the stackedWidget as index kStackTorrentsDownloads.
  auto* scroll = new QScrollArea(ui_->stackedWidget);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  auto* page = new QWidget();
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(6);

  auto addSection = [&](const QString& title) {
    auto* lbl = new QLabel(u"<b>%1</b>"_s.arg(title.toHtmlEscaped()), page);
    lbl->setTextFormat(Qt::RichText);
    layout->addWidget(lbl);
  };
  auto addHelp = [&](const QString& text) {
    auto* lbl = new QLabel(text, page);
    lbl->setTextFormat(Qt::RichText);
    lbl->setWordWrap(true);
    lbl->setContentsMargins(0, 0, 0, 4);
    layout->addWidget(lbl);
  };

  // ── RSS sources ─────────────────────────────────────────────────────────
  addSection(tr("RSS sources"));
  addHelp(
      tr("Search URL is used for in-app torrent search. Use <b>%title%</b> as the placeholder for "
         "the search term."));
  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Search URL:"), page));
    m_rss_search_url_ = new QLineEdit(page);
    m_rss_search_url_->setPlaceholderText(u"https://nyaa.si/?page=rss&q=%title%&c=1_2&f=0"_s);
    m_rss_search_url_->setText(QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl()));
    row->addWidget(m_rss_search_url_);
    layout->addLayout(row);
  }
  addHelp(tr(
      "Catalog feed: optional RSS browsed on the Torrents page. Leave empty to use the default."));
  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Catalog feed URL:"), page));
    m_rss_feed_url_ = new QLineEdit(page);
    m_rss_feed_url_->setPlaceholderText(tr("Optional — leave empty for default"));
    m_rss_feed_url_->setText(
        QString::fromStdString(taiga::settings.torrentDiscoveryFeedSourceUrl()));
    row->addWidget(m_rss_feed_url_);
    layout->addLayout(row);
  }
  // Auto-check row
  {
    auto* row = new QHBoxLayout();
    m_rss_autocheck_ = new QCheckBox(tr("Auto-check catalog feed every"), page);
    m_rss_autocheck_->setChecked(taiga::settings.torrentDiscoveryAutoCheckEnabled());
    row->addWidget(m_rss_autocheck_);
    m_rss_autocheck_mins_ = new QSpinBox(page);
    m_rss_autocheck_mins_->setRange(5, 1440);
    m_rss_autocheck_mins_->setSuffix(tr(" min"));
    m_rss_autocheck_mins_->setValue(taiga::settings.torrentDiscoveryAutoCheckIntervalMinutes());
    row->addWidget(m_rss_autocheck_mins_);
    row->addStretch();
    layout->addLayout(row);
  }
  // Sort options row
  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Sort results by:"), page));
    m_rss_sort_by_ = new QComboBox(page);
    m_rss_sort_by_->addItem(tr("Seeders"), QStringLiteral("seeders"));
    m_rss_sort_by_->addItem(tr("Title"), QStringLiteral("title"));
    m_rss_sort_by_->addItem(tr("Episode number"), QStringLiteral("episode_number"));
    m_rss_sort_by_->addItem(tr("Published date"), QStringLiteral("release_date"));
    const QString curBy = QString::fromStdString(taiga::settings.torrentRssSortBy());
    for (int i = 0; i < m_rss_sort_by_->count(); ++i)
      if (m_rss_sort_by_->itemData(i).toString() == curBy) {
        m_rss_sort_by_->setCurrentIndex(i);
        break;
      }
    row->addWidget(m_rss_sort_by_);
    m_rss_sort_order_ = new QComboBox(page);
    m_rss_sort_order_->addItem(tr("Ascending"), QStringLiteral("ascending"));
    m_rss_sort_order_->addItem(tr("Descending"), QStringLiteral("descending"));
    const QString curOrd = QString::fromStdString(taiga::settings.torrentRssSortOrder());
    for (int i = 0; i < m_rss_sort_order_->count(); ++i)
      if (m_rss_sort_order_->itemData(i).toString() == curOrd) {
        m_rss_sort_order_->setCurrentIndex(i);
        break;
      }
    row->addWidget(m_rss_sort_order_);
    row->addStretch();
    layout->addLayout(row);
  }
  layout->addSpacing(8);

  // ── Torrent file preference ─────────────────────────────────────────────
  addSection(tr("Torrent file preference"));
  m_dl_use_magnet_ = new QCheckBox(
      tr("Prefer magnet links when both a magnet and a .torrent URL are present"), page);
  m_dl_use_magnet_->setChecked(taiga::settings.torrentDownloadUseMagnet());
  m_dl_use_magnet_->setToolTip(
      tr("When off, the .torrent file URL is used instead of the magnet link."));
  layout->addWidget(m_dl_use_magnet_);

  // ── Download paths ──────────────────────────────────────────────────────
  layout->addSpacing(8);
  addSection(tr("Download paths"));
  addHelp(tr("Paths used when sending a torrent to your client or saving .torrent files."));

  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Torrent client download folder:"), page));
    m_dl_client_path_ = new QLineEdit(page);
    m_dl_client_path_->setPlaceholderText(
        tr("Optional — default save path for your torrent client"));
    m_dl_client_path_->setText(QString::fromStdString(taiga::settings.torrentClientDownloadPath()));
    row->addWidget(m_dl_client_path_);
    auto* browse = new QPushButton(tr("Browse…"), page);
    connect(browse, &QPushButton::clicked, page, [this]() {
      const auto dir = QFileDialog::getExistingDirectory(this, tr("Client download folder"),
                                                         m_dl_client_path_->text());
      if (!dir.isEmpty()) m_dl_client_path_->setText(dir);
    });
    row->addWidget(browse);
    layout->addLayout(row);
  }
  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr(".torrent save folder:"), page));
    m_dl_file_save_path_ = new QLineEdit(page);
    m_dl_file_save_path_->setPlaceholderText(tr("Optional — where to store .torrent files"));
    m_dl_file_save_path_->setText(QString::fromStdString(taiga::settings.torrentFileSavePath()));
    row->addWidget(m_dl_file_save_path_);
    auto* browse = new QPushButton(tr("Browse…"), page);
    connect(browse, &QPushButton::clicked, page, [this]() {
      const auto dir = QFileDialog::getExistingDirectory(this, tr(".torrent save folder"),
                                                         m_dl_file_save_path_->text());
      if (!dir.isEmpty()) m_dl_file_save_path_->setText(dir);
    });
    row->addWidget(browse);
    layout->addLayout(row);
  }

  // ── Folder rules ────────────────────────────────────────────────────────
  layout->addSpacing(8);
  addSection(tr("Torrent client & folder rules"));
  addHelp(tr("Controls where your torrent client saves downloaded files."));

  m_dl_use_anime_folder_ =
      new QCheckBox(tr("Use anime library folder as download target when available"), page);
  m_dl_use_anime_folder_->setChecked(taiga::settings.torrentDownloadUseAnimeFolder());
  m_dl_use_anime_folder_->setToolTip(
      tr("Saves the torrent directly into the matching anime library folder when found."));
  layout->addWidget(m_dl_use_anime_folder_);

  m_dl_fallback_client_ = new QCheckBox(
      tr("If no per-title folder, use \"Torrent client download folder\" above"), page);
  m_dl_fallback_client_->setChecked(taiga::settings.torrentDownloadFallbackOnClientPath());
  m_dl_fallback_client_->setToolTip(
      tr("Falls back to the torrent client's default folder if no anime library folder matches."));
  layout->addWidget(m_dl_fallback_client_);

  m_dl_create_subfolder_ = new QCheckBox(
      tr("Create a subfolder by anime title under that client download folder"), page);
  m_dl_create_subfolder_->setChecked(taiga::settings.torrentDownloadCreateSubfolder());
  m_dl_create_subfolder_->setToolTip(
      tr("Creates a subfolder named after the anime title inside the client download folder."));
  layout->addWidget(m_dl_create_subfolder_);

  const auto updateSubfolderEnabled = [this]() {
    if (m_dl_create_subfolder_) {
      m_dl_create_subfolder_->setEnabled(m_dl_fallback_client_ &&
                                         m_dl_fallback_client_->isChecked());
    }
    if (m_dl_autodl_cleanup_unrecognized_) {
      const bool allow = m_dl_create_subfolder_ && m_dl_create_subfolder_->isChecked();
      m_dl_autodl_cleanup_unrecognized_->setEnabled(allow);
      if (!allow) m_dl_autodl_cleanup_unrecognized_->setChecked(false);
    }
  };
  connect(m_dl_fallback_client_, &QCheckBox::checkStateChanged, page,
          [updateSubfolderEnabled](Qt::CheckState) { updateSubfolderEnabled(); });
  connect(m_dl_create_subfolder_, &QCheckBox::checkStateChanged, page,
          [updateSubfolderEnabled](Qt::CheckState) { updateSubfolderEnabled(); });
  updateSubfolderEnabled();

  // ── Torrent application ─────────────────────────────────────────────────
  layout->addSpacing(8);
  addSection(tr("Torrent application"));

  m_dl_app_open_ =
      new QCheckBox(tr("Open torrents with an application after download / on launch"), page);
  m_dl_app_open_->setChecked(taiga::settings.torrentAppOpen());
  m_dl_app_open_->setToolTip(
      tr("Opens each torrent with the configured application after adding it to the queue."));
  layout->addWidget(m_dl_app_open_);

  {
    auto* row = new QHBoxLayout();
    m_dl_app_default_ =
        new QRadioButton(tr("Default application (.torrent / magnet handler)"), page);
    m_dl_app_custom_ = new QRadioButton(tr("Custom executable"), page);
    const bool custom = taiga::settings.torrentAppMode() == 2;
    m_dl_app_default_->setChecked(!custom);
    m_dl_app_custom_->setChecked(custom);
    row->addWidget(m_dl_app_default_);
    row->addWidget(m_dl_app_custom_);
    row->addStretch();
    layout->addLayout(row);
  }
  {
    auto* row = new QHBoxLayout();
    m_dl_app_exe_ = new QLineEdit(page);
    m_dl_app_exe_->setPlaceholderText(tr("Path to qBittorrent.exe, Deluge, etc."));
    m_dl_app_exe_->setText(QString::fromStdString(taiga::settings.torrentAppExecutablePath()));
    row->addWidget(m_dl_app_exe_);
    auto* browse = new QPushButton(tr("Browse…"), page);
    connect(browse, &QPushButton::clicked, page, [this]() {
#ifdef Q_OS_WIN
      const QString filter = tr("Executables (*.exe);;All files (*)");
#else
      const QString filter = tr("All files (*)");
#endif
      const QString p = QFileDialog::getOpenFileName(this, tr("Torrent application"),
                                                     m_dl_app_exe_->text(), filter);
      if (!p.isEmpty()) m_dl_app_exe_->setText(p);
    });
    row->addWidget(browse);
    layout->addLayout(row);
  }

  const auto updateAppWidgets = [this]() {
    const bool on = m_dl_app_open_ && m_dl_app_open_->isChecked();
    if (m_dl_app_default_) m_dl_app_default_->setEnabled(on);
    if (m_dl_app_custom_) m_dl_app_custom_->setEnabled(on);
    if (m_dl_app_exe_)
      m_dl_app_exe_->setEnabled(on && m_dl_app_custom_ && m_dl_app_custom_->isChecked());
  };
  connect(m_dl_app_open_, &QCheckBox::checkStateChanged, page,
          [updateAppWidgets](Qt::CheckState) { updateAppWidgets(); });
  connect(m_dl_app_custom_, &QRadioButton::toggled, page,
          [updateAppWidgets](bool) { updateAppWidgets(); });
  updateAppWidgets();

  // ── Auto-download ────────────────────────────────────────────────────────
  layout->addSpacing(8);
  addSection(tr("Auto-download"));
  m_dl_autodl_skip_failed_twice_today_ =
      new QCheckBox(tr("Skip titles that failed twice today (auto-download only)"), page);
  m_dl_autodl_skip_failed_twice_today_->setChecked(
      taiga::settings.torrentAutoDownloadSkipAfterTwoFailuresToday());
  m_dl_autodl_skip_failed_twice_today_->setToolTip(
      tr("When enabled, if a Watching title fails to find torrents twice in a row in a day, it "
         "will be skipped for the rest of that day during auto-download runs. Manual downloads are "
         "not affected."));
  layout->addWidget(m_dl_autodl_skip_failed_twice_today_);

  m_dl_autodl_cleanup_unrecognized_ = new QCheckBox(
      tr("Delete unrecognized downloads from the torrent client folder (auto-download only)"),
      page);
  m_dl_autodl_cleanup_unrecognized_->setChecked(
      taiga::settings.torrentAutoCleanupUnrecognizedDownloads());
  m_dl_autodl_cleanup_unrecognized_->setToolTip(
      tr("When enabled, Taiga may delete files it cannot recognize after they are downloaded. "
         "This feature only operates under the torrent client download folder, and only when "
         "\"Create a subfolder by anime title\" is enabled."));
  layout->addWidget(m_dl_autodl_cleanup_unrecognized_);

  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Release event delay (minutes):"), page));
    m_dl_autodl_release_delay_mins_ = new QSpinBox(page);
    m_dl_autodl_release_delay_mins_->setRange(1, 180);
    m_dl_autodl_release_delay_mins_->setValue(
        taiga::settings.torrentAutoDownloadReleaseEventDelayMinutes());
    m_dl_autodl_release_delay_mins_->setToolTip(
        tr("When a new episode is detected as released for Watching titles, Taiga waits this long "
           "before running sync → scan → auto-download."));
    row->addWidget(m_dl_autodl_release_delay_mins_);
    row->addStretch();
    layout->addLayout(row);
  }

  // ── qBittorrent Web API ─────────────────────────────────────────────────
  layout->addSpacing(8);
  addSection(tr("qBittorrent Web API (recommended for auto-downloads)"));
  addHelp(
      tr("When enabled, Taiga sends torrents directly to qBittorrent via its Web API and sets the "
         "per-anime save path automatically — no .torrent file dialog needed. "
         "Enable in qBittorrent: <b>Tools → Preferences → Web UI → Enable Web User Interface</b>. "
         "Leave username/password blank if you have set no Web UI password."));

  m_dl_qbit_api_enabled_ = new QCheckBox(tr("Use qBittorrent Web API for all downloads"), page);
  m_dl_qbit_api_enabled_->setChecked(taiga::settings.torrentQBitApiEnabled());
  layout->addWidget(m_dl_qbit_api_enabled_);

  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("API URL:"), page));
    m_dl_qbit_api_url_ = new QLineEdit(page);
    m_dl_qbit_api_url_->setPlaceholderText(u"http://localhost:8080"_s);
    m_dl_qbit_api_url_->setText(QString::fromStdString(taiga::settings.torrentQBitApiUrl()));
    row->addWidget(m_dl_qbit_api_url_);
    layout->addLayout(row);
  }
  {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Username:"), page));
    m_dl_qbit_api_user_ = new QLineEdit(page);
    m_dl_qbit_api_user_->setPlaceholderText(tr("Optional"));
    m_dl_qbit_api_user_->setText(QString::fromStdString(taiga::settings.torrentQBitApiUsername()));
    row->addWidget(m_dl_qbit_api_user_);
    row->addWidget(new QLabel(tr("Password:"), page));
    m_dl_qbit_api_pass_ = new QLineEdit(page);
    m_dl_qbit_api_pass_->setEchoMode(QLineEdit::Password);
    m_dl_qbit_api_pass_->setPlaceholderText(tr("Optional"));
    m_dl_qbit_api_pass_->setText(QString::fromStdString(taiga::settings.torrentQBitApiPassword()));
    row->addWidget(m_dl_qbit_api_pass_);
    layout->addLayout(row);
  }

  const auto updateQBitWidgets = [this]() {
    const bool on = m_dl_qbit_api_enabled_ && m_dl_qbit_api_enabled_->isChecked();
    if (m_dl_qbit_api_url_) m_dl_qbit_api_url_->setEnabled(on);
    if (m_dl_qbit_api_user_) m_dl_qbit_api_user_->setEnabled(on);
    if (m_dl_qbit_api_pass_) m_dl_qbit_api_pass_->setEnabled(on);
  };
  connect(m_dl_qbit_api_enabled_, &QCheckBox::checkStateChanged, page,
          [updateQBitWidgets](Qt::CheckState) { updateQBitWidgets(); });
  updateQBitWidgets();

  layout->addStretch();
  scroll->setWidget(page);

  const int idx = ui_->stackedWidget->addWidget(scroll);
  Q_ASSERT(idx == kStackTorrentsDownloads);
}

void SettingsDialog::saveDownloadsPage() {
  if (!m_dl_use_magnet_) return;  // page not built
  // RSS sources (migrated from Library page)
  if (m_rss_search_url_)
    taiga::settings.setTorrentDiscoverySearchUrl(m_rss_search_url_->text().trimmed().toStdString());
  if (m_rss_feed_url_)
    taiga::settings.setTorrentDiscoveryFeedSourceUrl(
        m_rss_feed_url_->text().trimmed().toStdString());
  if (m_rss_autocheck_)
    taiga::settings.setTorrentDiscoveryAutoCheckEnabled(m_rss_autocheck_->isChecked());
  if (m_rss_autocheck_mins_)
    taiga::settings.setTorrentDiscoveryAutoCheckIntervalMinutes(m_rss_autocheck_mins_->value());
  if (m_rss_sort_by_)
    taiga::settings.setTorrentRssSortBy(m_rss_sort_by_->currentData().toString().toStdString());
  if (m_rss_sort_order_)
    taiga::settings.setTorrentRssSortOrder(
        m_rss_sort_order_->currentData().toString().toStdString());

  taiga::settings.setTorrentDownloadUseMagnet(m_dl_use_magnet_->isChecked());
  taiga::settings.setTorrentClientDownloadPath(m_dl_client_path_->text().trimmed().toStdString());
  taiga::settings.setTorrentFileSavePath(m_dl_file_save_path_->text().trimmed().toStdString());
  if (m_dl_autodl_skip_failed_twice_today_) {
    taiga::settings.setTorrentAutoDownloadSkipAfterTwoFailuresToday(
        m_dl_autodl_skip_failed_twice_today_->isChecked());
  }
  if (m_dl_autodl_release_delay_mins_) {
    taiga::settings.setTorrentAutoDownloadReleaseEventDelayMinutes(
        m_dl_autodl_release_delay_mins_->value());
  }
  if (m_dl_autodl_cleanup_unrecognized_) {
    taiga::settings.setTorrentAutoCleanupUnrecognizedDownloads(
        taiga::settings.torrentDownloadCreateSubfolder() &&
            m_dl_autodl_cleanup_unrecognized_->isChecked());
  }
  taiga::settings.setTorrentDownloadUseAnimeFolder(m_dl_use_anime_folder_->isChecked());
  taiga::settings.setTorrentDownloadFallbackOnClientPath(m_dl_fallback_client_->isChecked());
  taiga::settings.setTorrentDownloadCreateSubfolder(m_dl_fallback_client_->isChecked() &&
                                                    m_dl_create_subfolder_->isChecked());
  taiga::settings.setTorrentAppOpen(m_dl_app_open_->isChecked());
  taiga::settings.setTorrentAppMode(m_dl_app_custom_->isChecked() ? 2 : 1);
  taiga::settings.setTorrentAppExecutablePath(m_dl_app_exe_->text().trimmed().toStdString());
  if (m_dl_qbit_api_enabled_) {
    taiga::settings.setTorrentQBitApiEnabled(m_dl_qbit_api_enabled_->isChecked());
    taiga::settings.setTorrentQBitApiUrl(m_dl_qbit_api_url_->text().trimmed().toStdString());
    taiga::settings.setTorrentQBitApiUsername(m_dl_qbit_api_user_->text().trimmed().toStdString());
    taiga::settings.setTorrentQBitApiPassword(m_dl_qbit_api_pass_->text().toStdString());
  }
}

void SettingsDialog::populatePlaceholderPage(const QString& parent, const QString& child) {
  // Clear current placeholder content.
  auto* page = ui_->placeholderPage;
  auto* layout = page->layout();
  while (layout->count()) {
    QLayoutItem* it = layout->takeAt(0);
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }

  auto addHeading = [&](const QString& text) {
    auto* lbl = new QLabel(u"<b>%1</b>"_s.arg(text.toHtmlEscaped()), page);
    lbl->setTextFormat(Qt::RichText);
    lbl->setWordWrap(true);
    layout->addWidget(lbl);
  };
  auto addParagraph = [&](const QString& text) {
    auto* lbl = new QLabel(text, page);
    lbl->setTextFormat(Qt::RichText);
    lbl->setWordWrap(true);
    lbl->setContentsMargins(0, 0, 0, 8);
    layout->addWidget(lbl);
  };
  auto addButton = [&](const QString& label, auto&& callback) {
    auto* btn = new QPushButton(label, page);
    connect(btn, &QPushButton::clicked, page, callback);
    layout->addWidget(btn);
  };

  if (parent == u"Recognition") {
    // Single unified Recognition page.
    addHeading(tr("Media Players"));
    addParagraph(tr(
        "Taiga polls running processes on Windows to detect the file currently open in a media "
        "player. "
        "After identifying the filename, Taiga parses it with Anitomy to recognise the anime "
        "episode.<br><br>"
        "Players are matched automatically via their window title — no manual configuration "
        "needed. "
        "Supported player families include:<br>"
        "&nbsp;&nbsp;• VLC media player<br>"
        "&nbsp;&nbsp;• MPC-HC / MPC-BE (Media Player Classic)<br>"
        "&nbsp;&nbsp;• mpv<br>"
        "&nbsp;&nbsp;• PotPlayer / Daum PotPlayer<br>"
        "&nbsp;&nbsp;• GOM Player / KMPlayer<br>"
        "&nbsp;&nbsp;• Windows Media Player / Media Player Legacy<br>"
        "&nbsp;&nbsp;• (and many more via the bundled Anisthesia player database)<br><br>"
        "To open episodes from Taiga using a specific player, set its executable path in "
        "<b>Application → Media recognition</b>. "
        "To enable or disable player polling entirely, use the master toggle on the same page."));

    addHeading(tr("Web Browsers"));
    addParagraph(tr(
        "Taiga reads the title bar of your web browser to detect anime on streaming sites. "
        "This works with any Chromium- or Firefox-based browser on Windows.<br><br>"
        "To enable browser detection, make sure <b>Detect media in web browsers</b> is checked in "
        "<b>Application → Media recognition</b>.<br><br>"
        "The streaming providers below are the sites whose titles Taiga knows how to parse. "
        "Uncheck a site to ignore it during detection."));
    for (const auto& e : track::streaming::providerUiEntries()) {
      auto* cb = new QCheckBox(tr(e.label), page);
      const std::string slug(e.slug);
      cb->setChecked(taiga::settings.streamProviderEnabled(slug));
      connect(cb, &QCheckBox::checkStateChanged, page, [slug, cb](Qt::CheckState) {
        taiga::settings.setStreamProviderEnabled(slug, cb->isChecked());
      });
      layout->addWidget(cb);
    }
    addParagraph(
        tr("<br><i>Master toggle and detection interval are in "
           "<b>Application → Media recognition</b>.</i>"));
  } else if (parent == u"Torrents" && child == u"Downloads") {
    // The dedicated Downloads page is a proper stackedWidget page — this
    // code path should not be reached since kStackTorrentsDownloads != kStackPlaceholder.
    // Fallback just in case.
    selectStackPageByRole(kStackTorrentsDownloads);
  } else if (parent == u"Torrents" && child == u"Filters") {
    addHeading(tr("Torrent Feed Filters"));

    const bool filterEnabled = taiga::settings.torrentFeedFilterEnabled();
    const QString inc =
        QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()).trimmed();
    const QString exc =
        QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()).trimmed();
    const int incCount = inc.isEmpty() ? 0 : inc.split(u'\n').size();
    const int excCount = exc.isEmpty() ? 0 : exc.split(u'\n').size();

    QString summary;
    summary += filterEnabled ? tr("Feed archive limit: <b>enabled</b>")
                             : tr("Feed archive limit: <b>disabled</b>");
    summary += u"<br>";
    summary += incCount > 0 ? tr("Include filters: <b>%1 rule(s)</b>").arg(incCount)
                            : tr("Include filters: <i>none</i>");
    summary += u"<br>";
    summary += excCount > 0 ? tr("Exclude filters: <b>%1 rule(s)</b>").arg(excCount)
                            : tr("Exclude filters: <i>none</i>");
    addParagraph(summary);

    addButton(tr("Manage filter rules…"), [this]() {
      TorrentFiltersDialog dlg(this);
      if (dlg.exec() == QDialog::Accepted) {
        // Sync the Library page's regex widgets so that when accept() runs it
        // writes the values just saved by TorrentFiltersDialog, not the stale
        // pre-dialog text.
        ui_->plainTorrentFeedIncludeRegex->setPlainText(
            QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()));
        ui_->plainTorrentFeedExcludeRegex->setPlainText(
            QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()));
      }
      // Refresh summary after editing (whether saved or cancelled).
      populatePlaceholderPage(u"Torrents"_s, u"Filters"_s);
    });
    addParagraph(
        tr("<br><i>List-based filters (hide watched, hide not on list, etc.) are shown "
           "in the Torrents panel toolbar and persist in session settings.</i>"));
  } else if (parent == u"Advanced" && child == u"Cache") {
    addHeading(tr("Cache"));

    const QString dataPath =
        QDir::toNativeSeparators(QString::fromStdString(taiga::get_data_path()));
    addParagraph(tr("Data folder: <code>%1</code>").arg(dataPath.toHtmlEscaped()));
    addButton(tr("Open data folder…"), [dataPath]() {
      const QUrl url = QUrl::fromLocalFile(
          dataPath.endsWith(u'\\') || dataPath.endsWith(u'/') ? dataPath : dataPath + u'/');
      QDesktopServices::openUrl(url);
    });

    {
      int count = 0;
      const qint64 bytes = imageProvider.posterCacheSize(&count);
      const QString sizeStr = bytes < 1024 * 1024 ? tr("%1 KB").arg(bytes / 1024)
                                                  : tr("%1 MB").arg(bytes / (1024 * 1024));
      addParagraph(u"<br>"_s +
                   tr("Poster image cache: <b>%1 file(s), %2</b>").arg(count).arg(sizeStr));
    }
    addButton(tr("Clear poster image cache"), [this]() {
      imageProvider.clearPosterCache();
      QMessageBox::information(
          this, tr("Taiga"), tr("Poster image cache cleared. Images will re-download on demand."));
      // Refresh size display.
      populatePlaceholderPage(u"Advanced"_s, u"Cache"_s);
    });

    addParagraph(u"<br>"_s +
                 tr("Recognition cache: in-memory, rebuilt automatically after each sync."));
    addButton(tr("Clear recognition cache now"), [this]() {
      track::recognition::cache()->clear();
      QMessageBox::information(
          this, tr("Taiga"),
          tr("Recognition cache cleared. It will be rebuilt on next library scan."));
    });

    addParagraph(u"<br>"_s + tr("Cache diagnostics"));
    auto* enabled = new QCheckBox(tr("Enable cache diagnostics log (for debugging)"), page);
    enabled->setChecked(taiga::settings.cacheDiagnosticsEnabled());
    layout->addWidget(enabled);

    addParagraph(tr("Opens a separate window that updates as new log lines are written."));
    addButton(tr("View live log…"), [this]() { CacheDiagnosticsDialog::show(this); });

    connect(enabled, &QCheckBox::toggled, this, [](const bool on) {
      taiga::settings.setCacheDiagnosticsEnabled(on);
    });
  } else {
    // Generic fallback.
    auto* lbl = new QLabel(tr("This section is not available in the Qt 6 build yet."), page);
    lbl->setWordWrap(true);
    layout->addWidget(lbl);
  }

  // Push content to top.
  layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

}  // namespace gui
