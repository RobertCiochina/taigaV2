/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "settings_dialog.hpp"

#include <chrono>

#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QUrl>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/settings/anilist_auth_dialog.hpp"
#include "gui/utils/theme.hpp"
#include "track/media.hpp"
#include "sync/anilist_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"
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
    add_child(item, "Streaming");
  }
  {
    auto* item = add_item("share", "Sharing", kStackPlaceholder);
    add_child(item, "Discord");
    add_child(item, "HTTP");
    add_child(item, "mIRC");
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
  ui_->spinDetectionInterval->setValue(static_cast<int>(taiga::settings.mediaDetectionInterval().count() / 1000));
#ifndef Q_OS_WINDOWS
  ui_->checkMediaDetection->setEnabled(false);
  ui_->spinDetectionInterval->setEnabled(false);
#endif
  {
    const auto rec_ui = [this] {
      ui_->spinDetectionInterval->setEnabled(
#ifdef Q_OS_WINDOWS
          ui_->checkMediaDetection->isChecked()
#else
          false
#endif
      );
    };
    connect(ui_->checkMediaDetection, &QCheckBox::checkStateChanged, this,
            [rec_ui](Qt::CheckState) { rec_ui(); });
    rec_ui();
  }

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
  ui_->checkSyncOnFocus->setChecked(taiga::settings.syncOnWindowFocus());
  ui_->spinFocusMinutes->setValue(taiga::settings.syncOnWindowFocusMinutes());

  for (const auto& folder : taiga::settings.libraryFolders()) {
    ui_->libraryFolderList->addItem(QString::fromStdString(folder));
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

  ui_->treeWidget->setCurrentItem(ui_->treeWidget->topLevelItem(0));
}

void SettingsDialog::accept() {
  taiga::settings.setSyncAutoOnStart(ui_->checkSyncOnStart->isChecked());
  taiga::settings.setAppColorScheme(
      static_cast<Qt::ColorScheme>(ui_->comboColorScheme->currentData().toInt()));
  gui::theme.refreshFromSettings();

  taiga::settings.setMediaDetectionEnabled(ui_->checkMediaDetection->isChecked());
  taiga::settings.setMediaDetectionInterval(
      std::chrono::milliseconds(ui_->spinDetectionInterval->value() * 1000));
  track::media::detection()->refreshPollingFromSettings();

  taiga::settings.setCheckForUpdatesOnStartup(ui_->checkUpdatesOnStartup->isChecked());
  taiga::settings.setScanLibraryOnStartup(ui_->checkScanLibraryOnStartup->isChecked());
  taiga::settings.setProxyHost(ui_->lineProxyHost->text().toStdString());
  taiga::settings.setProxyUsername(ui_->lineProxyUsername->text().toStdString());
  taiga::settings.setProxyPassword(ui_->lineProxyPassword->text().toStdString());
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

  taiga::settings.setService(ui_->comboListService->currentData().toString().toStdString());
  taiga::settings.setListSynchronizationEnabled(ui_->checkListUpdatesEnabled->isChecked());
  if (auto* mw = gui::mainWindow()) {
    mw->applyListSynchronizationToggleFromSettings();
    mw->applyMediaDetectionToggleFromSettings();
    mw->refreshServiceDependentUi();
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
