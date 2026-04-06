/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "settings_dialog.hpp"

#include <algorithm>
#include <chrono>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QSystemTrayIcon>
#include <QTreeWidgetItem>
#include <QUrl>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/settings/anilist_auth_dialog.hpp"
#include "gui/settings/torrent_filters_dialog.hpp"
#include "gui/utils/theme.hpp"
#include "track/library_watcher.hpp"
#include "track/media.hpp"
#include "track/streaming_sites.hpp"
#include "sync/anilist_utils.hpp"
#include "sync/service.hpp"
#include "media/anime.hpp"
#include "taiga/accounts.hpp"
#include "taiga/list_row_action.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"
#include "taiga/torrent_discovery.hpp"
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

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::SettingsDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

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
  {
    auto* item = add_item("check_circle", "Recognition", kStackPlaceholder);
    add_child(item, "Media players");
    add_child(item, "Web browsers");
  }
  {
    auto* item = add_item("rss_feed", "Torrents", kStackPlaceholder);
    add_child(item, "Downloads");
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
            ui_->stackedWidget->setCurrentIndex(stack);
            QString text = current->text(0);
            if (current->parent()) {
              text = u"%1 / %2"_s.arg(current->parent()->text(0), text);
            }
            ui_->titleLabel->setText(text);

            if (current->parent() && current->parent()->text(0) == u"Torrents" &&
                current->text(0) == u"Filters") {
              TorrentFiltersDialog dlg(this);
              dlg.exec();
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
             "<p>Credentials are stored in <code>accounts.json</code> next to your data folder. Use the "
             "fields and buttons below — you do not need to edit the file by hand.</p>")
              .arg(svc.toHtmlEscaped())
              .arg(user.isEmpty() ? tr("(not set)").toHtmlEscaped() : user.toHtmlEscaped())
              .arg(has_anilist_token ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped())
              .arg(has_mal_token ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped())
              .arg(has_kitsu ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped()));
    };
    refresh_accounts_summary();

    ui_->checkSyncOnStart->setChecked(taiga::settings.syncAutoOnStart());

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

    auto* mal_btn = new QPushButton(tr("Open MyAnimeList API apps (create tokens)…"), ui_->accountsPage);
    connect(mal_btn, &QPushButton::clicked, this, []() {
      QDesktopServices::openUrl(QUrl("https://myanimelist.net/apiconfig"));
    });
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
    connect(kitsu_btn, &QPushButton::clicked, this, []() {
      QDesktopServices::openUrl(QUrl("https://kitsu.app/settings"));
    });
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
      tr("If \"Minimize to tray\" is also enabled, Taiga starts in the tray only (v1 behavior)."));

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
  ui_->spinDetectionInterval->setValue(static_cast<int>(taiga::settings.mediaDetectionInterval().count() / 1000));
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
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Media player"), ui_->lineMediaPlayerExecutable->text(),
                                     filter);
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
      tr("Disables strict TLS peer verification for Taiga HTTP requests (Taiga v1: sslnorevoke). Use "
         "only with broken proxies or certificates."));
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
  ui_->spinTorrentAutocheckMinutes->setValue(taiga::settings.torrentDiscoveryAutoCheckIntervalMinutes());
  {
    QComboBox* const c = ui_->comboTorrentNewCatalogAction;
    if (c->count() == 0) {
      c->addItem(tr("Notify (status bar and tray)"),
                 static_cast<int>(taiga::TorrentDiscoveryNewCatalogAction::Notify));
      c->addItem(tr("Download automatically (v1) — status only until download queue exists"),
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
      sb->addItem(tr("Episode number (v1)"), QStringLiteral("episode_number"));
      sb->addItem(tr("Published date (v1: release_date)"), QStringLiteral("release_date"));
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
        for (QString s : t.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
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
        warning = tr("Your “Hide if matches” list contains <code>.*</code>, which hides everything, so no results can appear.");
        valid = false;
      } else if (!inc.isEmpty()) {
        int covered = 0;
        for (const QString& s : inc) {
          if (exc_set.contains(s)) ++covered;
        }
        if (covered == inc.size()) {
          warning = tr("All “Show only if matches” rules are also present in “Hide if matches”, so no results can appear.");
          valid = false;
        }
      }

      if (ui_->labelTorrentFeedRegexWarning) {
        ui_->labelTorrentFeedRegexWarning->setText(
            warning.isEmpty() ? QString{} : (u"<span style=\"color:#c33\"><b>%1</b></span>"_s.arg(warning)));
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

    connect(ui_->buttonTorrentPresetDiscardBadVideo, &QPushButton::clicked, this, [this, addLine]() {
      // v1 preset: discard AVI, DIVX, LQ, RMVB, SD, WMV, XVID
      addLine(ui_->plainTorrentFeedExcludeRegex, QStringLiteral("\\b(AVI|DIVX|LQ|RMVB|SD|WMV|XVID)\\b"));
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
    connect(ui_->buttonTorrentRegexClearInclude, &QPushButton::clicked, this, [this]() {
      ui_->plainTorrentFeedIncludeRegex->clear();
    });
    connect(ui_->buttonTorrentRegexClearExclude, &QPushButton::clicked, this, [this]() {
      ui_->plainTorrentFeedExcludeRegex->clear();
    });
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
  ui_->checkTorrentFallbackClientPath->setChecked(taiga::settings.torrentDownloadFallbackOnClientPath());
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
            ui_->checkTorrentCreateSubfolder->setEnabled(ui_->checkTorrentFallbackClientPath->isChecked());
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
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Torrent client application"),
                                     ui_->lineTorrentAppExecutable->text(), filter);
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

  ui_->comboListService->addItem(sync::serviceName(sync::ServiceId::AniList), QStringLiteral("anilist"));
  ui_->comboListService->addItem(sync::serviceName(sync::ServiceId::MyAnimeList),
                                 QStringLiteral("myanimelist"));
  ui_->comboListService->addItem(sync::serviceName(sync::ServiceId::Kitsu), QStringLiteral("kitsu"));
  {
    const QString cur = QString::fromStdString(taiga::settings.service());
    int sidx = ui_->comboListService->findData(cur);
    if (sidx < 0) sidx = 0;
    ui_->comboListService->setCurrentIndex(sidx);
  }
  ui_->checkListUpdatesEnabled->setChecked(taiga::settings.listSynchronizationEnabled());
  ui_->checkSyncPushAskConfirm->setChecked(taiga::settings.syncListPushAskConfirm());
  ui_->checkSyncPushAskConfirm->setToolTip(
      tr("Taiga v1: account/update/asktoconfirm. When off, list edits upload without a prompt (subject to "
         "the debounce delay above)."));
  ui_->spinListUpdateApiDelay->setValue(taiga::settings.syncListUpdateDelaySeconds());
  ui_->spinListUpdateApiDelay->setToolTip(
      tr("Seconds to wait after a local list change before pushing the same title to the remote service "
         "again (Taiga v1: account/update delay; default there was often 120). 0 = immediate. Reduces "
         "API traffic when you adjust progress several times in a row."));

  ui_->comboListTitleLanguage->addItem(tr("Romaji"), static_cast<int>(anime::TitleLanguage::Romaji));
  ui_->comboListTitleLanguage->addItem(tr("English"), static_cast<int>(anime::TitleLanguage::English));
  ui_->comboListTitleLanguage->addItem(tr("Native title"), static_cast<int>(anime::TitleLanguage::Native));
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
      box->addItem(tr("Edit list entry"), static_cast<int>(taiga::ListRowAction::EditListEntry));
      box->addItem(tr("Open folder"), static_cast<int>(taiga::ListRowAction::OpenFolder));
      box->addItem(tr("Play next episode"), static_cast<int>(taiga::ListRowAction::PlayNext));
      box->addItem(tr("Show details"), static_cast<int>(taiga::ListRowAction::ShowDetails));
      box->addItem(tr("Open anime page in browser"),
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
  ui_->checkListHighlightNextOnDisk->setToolTip(
      tr("Uses the same library scan index as “episodes on disk” on the progress bar — no disk I/O "
         "while scrolling."));
  ui_->checkListHighlightOnTop->setToolTip(
      tr("Applies on the Anime list and Search results. Secondary sort order is unchanged from "
         "Taiga v1 (Qt build uses a single visible sort column)."));

  ui_->treeWidget->setCurrentItem(ui_->treeWidget->topLevelItem(0));
}

void SettingsDialog::accept() {
  taiga::settings.setSyncAutoOnStart(ui_->checkSyncOnStart->isChecked());
  taiga::settings.setAppColorScheme(
      static_cast<Qt::ColorScheme>(ui_->comboColorScheme->currentData().toInt()));
  gui::theme.refreshFromSettings();

  taiga::settings.setMediaDetectionEnabled(ui_->checkMediaDetection->isChecked());
  taiga::settings.setMediaDetectionPlayersEnabled(ui_->checkMediaPlayerPolling->isChecked());
  taiga::settings.setMediaDetectionStreamingEnabled(ui_->checkMediaStreaming->isChecked());
  for (const auto& p : streaming_provider_checks_) {
    taiga::settings.setStreamProviderEnabled(p.first, p.second->isChecked());
  }
  taiga::settings.setRecognitionIgnoredSubstrings(ui_->plainRecognitionIgnored->toPlainText().toStdString());
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
  taiga::settings.setMediaPlayerExecutablePath(ui_->lineMediaPlayerExecutable->text().trimmed().toStdString());
  track::media::detection()->refreshPollingFromSettings();

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
  taiga::network()->applyProxyFromSettings();
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
    if (gui::mainWindow()) gui::mainWindow()->refreshLibraryRootsFromSettings();
  }

  taiga::settings.setLibraryWatchFoldersEnabled(ui_->checkLibraryWatchFolders->isChecked());
  track::libraryFolderWatcher()->refreshFromSettings();

  {
    constexpr qint64 kMiB = 1024LL * 1024;
    const int v = ui_->spinLibraryMinFileSizeMiB->value();
    taiga::settings.setLibraryScanMinFileSizeBytes(v > 0 ? static_cast<qint64>(v) * kMiB : 0);
  }

  taiga::settings.setTorrentDiscoverySearchUrl(ui_->lineTorrentSearchUrl->text().trimmed().toStdString());
  taiga::settings.setTorrentDiscoveryFeedSourceUrl(ui_->lineTorrentFeedSourceUrl->text().trimmed().toStdString());
  taiga::settings.setTorrentDiscoveryAutoCheckEnabled(ui_->checkTorrentAutocheckCatalog->isChecked());
  taiga::settings.setTorrentDiscoveryAutoCheckIntervalMinutes(ui_->spinTorrentAutocheckMinutes->value());
  taiga::settings.setTorrentDiscoveryNewCatalogAction(static_cast<taiga::TorrentDiscoveryNewCatalogAction>(
      ui_->comboTorrentNewCatalogAction->currentData().toInt()));
  taiga::settings.setTorrentRssSortBy(ui_->comboTorrentRssSortBy->currentData().toString().toStdString());
  taiga::settings.setTorrentRssSortOrder(
      ui_->comboTorrentRssSortOrder->currentData().toString().toStdString());
  taiga::settings.setTorrentFeedFilterEnabled(ui_->checkTorrentFeedFilterEnabled->isChecked());
  taiga::settings.setTorrentFeedArchiveMaxItems(ui_->spinTorrentFeedArchiveMax->value());
  taiga::settings.setTorrentFeedIncludeRegexList(
      ui_->plainTorrentFeedIncludeRegex->toPlainText().trimmed().toStdString());
  taiga::settings.setTorrentFeedExcludeRegexList(
      ui_->plainTorrentFeedExcludeRegex->toPlainText().trimmed().toStdString());
  taiga::settings.setTorrentDownloadUseMagnet(ui_->checkTorrentDownloadUseMagnet->isChecked());
  taiga::settings.setTorrentClientDownloadPath(ui_->lineTorrentClientDownloadPath->text().trimmed().toStdString());
  taiga::settings.setTorrentFileSavePath(ui_->lineTorrentFileSavePath->text().trimmed().toStdString());
  taiga::settings.setTorrentDownloadUseAnimeFolder(ui_->checkTorrentUseAnimeFolder->isChecked());
  taiga::settings.setTorrentDownloadFallbackOnClientPath(ui_->checkTorrentFallbackClientPath->isChecked());
  taiga::settings.setTorrentDownloadCreateSubfolder(
      ui_->checkTorrentFallbackClientPath->isChecked() && ui_->checkTorrentCreateSubfolder->isChecked());
  taiga::settings.setTorrentAppOpen(ui_->checkTorrentAppOpen->isChecked());
  taiga::settings.setTorrentAppMode(ui_->radioTorrentAppCustom->isChecked() ? 2 : 1);
  taiga::settings.setTorrentAppExecutablePath(ui_->lineTorrentAppExecutable->text().trimmed().toStdString());

  taiga::settings.setService(ui_->comboListService->currentData().toString().toStdString());
  taiga::settings.setListSynchronizationEnabled(ui_->checkListUpdatesEnabled->isChecked());
  taiga::settings.setSyncListPushAskConfirm(ui_->checkSyncPushAskConfirm->isChecked());
  taiga::settings.setSyncListUpdateDelaySeconds(ui_->spinListUpdateApiDelay->value());
  taiga::settings.setListTitleLanguage(static_cast<anime::TitleLanguage>(
      ui_->comboListTitleLanguage->currentData().toInt()));
  taiga::settings.setListDoubleClickAction(static_cast<taiga::ListRowAction>(
      ui_->comboListDoubleClick->currentData().toInt()));
  taiga::settings.setListMiddleClickAction(static_cast<taiga::ListRowAction>(
      ui_->comboListMiddleClick->currentData().toInt()));
  taiga::settings.setListProgressShowAired(ui_->checkListProgressShowAired->isChecked());
  taiga::settings.setListProgressShowAvailable(ui_->checkListProgressShowAvailable->isChecked());
  taiga::settings.setListHighlightNextEpisodeOnDisk(ui_->checkListHighlightNextOnDisk->isChecked());
  taiga::settings.setListHighlightAvailableOnTop(ui_->checkListHighlightOnTop->isChecked());
  if (auto* mw = gui::mainWindow()) {
    mw->applyListSynchronizationToggleFromSettings();
    mw->applyMediaDetectionToggleFromSettings();
    track::media::detection()->setPollingEnabled(taiga::settings.mediaDetectionPollingActive());
    mw->refreshServiceDependentUi();
    mw->refreshAnimeListProgressDecorations();
    mw->refreshAnimeListNewEpisodeHighlight();
    mw->refreshTorrentCatalogAutocheckTimer();
    mw->resortTorrentRssTableFromSettings();
  }

  QDialog::accept();
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

}  // namespace gui
