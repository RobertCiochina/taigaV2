/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "settings_dialog.hpp"

#include <QDesktopServices>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QUrl>

#include "base/string.hpp"
#include "gui/utils/theme.hpp"
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
constexpr int kStackPlaceholder = 2;

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
  add_item("list_alt", "Anime List", kStackPlaceholder);
  add_item("folder", "Library", kStackPlaceholder);
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
    const QString svc = sync::serviceName(sync::currentServiceId());
    const QString user =
        QString::fromStdString(taiga::accounts.serviceUsername(taiga::settings.service()));
    const bool has_anilist_token = !taiga::accounts.anilistToken().empty();
    const bool has_mal_token = !taiga::accounts.myanimelistAccessToken().empty();
    const bool has_kitsu =
        (!taiga::accounts.kitsuEmail().empty() || !taiga::accounts.kitsuUsername().empty()) &&
        !taiga::accounts.kitsuPassword().empty();
    ui_->accountsInfoLabel->setTextFormat(Qt::RichText);
    ui_->accountsInfoLabel->setText(
        tr("<p><b>Active service:</b> %1<br/>"
           "<b>Username:</b> %2<br/>"
           "<b>AniList token:</b> %3<br/>"
           "<b>MyAnimeList token:</b> %4<br/>"
           "<b>Kitsu credentials:</b> %5</p>"
           "<p><b>AniList:</b> use the button below to open the OAuth page, then paste the token into "
           "<code>accounts.json</code> (or migrate from Taiga v1).<br/>"
           "<b>MyAnimeList:</b> create an API application and add tokens to <code>accounts.json</code> "
           "(see Taiga v1 OAuth flow or MAL docs).<br/>"
           "<b>Kitsu:</b> set email/username and password in <code>accounts.json</code> (base64 optional, "
           "same as v1).</p>")
            .arg(svc.toHtmlEscaped())
            .arg(user.isEmpty() ? tr("(not set)").toHtmlEscaped() : user.toHtmlEscaped())
            .arg(has_anilist_token ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped())
            .arg(has_mal_token ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped())
            .arg(has_kitsu ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped()));

    ui_->checkSyncOnStart->setChecked(taiga::settings.syncAutoOnStart());

    auto* auth_btn = new QPushButton(tr("Open AniList authorization…"), ui_->accountsPage);
    connect(auth_btn, &QPushButton::clicked, this, []() {
      QDesktopServices::openUrl(QUrl(QString::fromStdString(sync::anilist::requestTokenUrl())));
    });
    ui_->verticalLayout_3->addWidget(auth_btn);

    auto* mal_btn = new QPushButton(tr("Open MyAnimeList API configuration…"), ui_->accountsPage);
    connect(mal_btn, &QPushButton::clicked, this, []() {
      QDesktopServices::openUrl(QUrl("https://myanimelist.net/apiconfig"));
    });
    ui_->verticalLayout_3->addWidget(mal_btn);

    auto* kitsu_btn = new QPushButton(tr("Open Kitsu (account on website)…"), ui_->accountsPage);
    connect(kitsu_btn, &QPushButton::clicked, this, []() {
      QDesktopServices::openUrl(QUrl("https://kitsu.app/settings"));
    });
    ui_->verticalLayout_3->addWidget(kitsu_btn);
  }

  ui_->checkUpdatesOnStartup->setChecked(taiga::settings.checkForUpdatesOnStartup());
  ui_->checkScanLibraryOnStartup->setChecked(taiga::settings.scanLibraryOnStartup());

  ui_->lineProxyHost->setText(QString::fromStdString(taiga::settings.proxyHost()));
  ui_->lineProxyUsername->setText(QString::fromStdString(taiga::settings.proxyUsername()));
  ui_->lineProxyPassword->setText(QString::fromStdString(taiga::settings.proxyPassword()));
  ui_->checkSyncOnFocus->setChecked(taiga::settings.syncOnWindowFocus());
  ui_->spinFocusMinutes->setValue(taiga::settings.syncOnWindowFocusMinutes());

  ui_->treeWidget->setCurrentItem(ui_->treeWidget->topLevelItem(0));
}

void SettingsDialog::accept() {
  taiga::settings.setSyncAutoOnStart(ui_->checkSyncOnStart->isChecked());
  taiga::settings.setCheckForUpdatesOnStartup(ui_->checkUpdatesOnStartup->isChecked());
  taiga::settings.setScanLibraryOnStartup(ui_->checkScanLibraryOnStartup->isChecked());
  taiga::settings.setProxyHost(ui_->lineProxyHost->text().toStdString());
  taiga::settings.setProxyUsername(ui_->lineProxyUsername->text().toStdString());
  taiga::settings.setProxyPassword(ui_->lineProxyPassword->text().toStdString());
  taiga::network()->applyProxyFromSettings();
  taiga::settings.setSyncOnWindowFocus(ui_->checkSyncOnFocus->isChecked());
  taiga::settings.setSyncOnWindowFocusMinutes(ui_->spinFocusMinutes->value());
  QDialog::accept();
}

void SettingsDialog::show(QWidget* parent) {
  auto* dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->QDialog::show();
}

}  // namespace gui
