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

#include "settings_dialog.hpp"

#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>

#include "base/string.hpp"
#include "gui/utils/theme.hpp"
#include "sync/anilist_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"
#include "ui_settings_dialog.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::SettingsDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  ui_->treeWidget->setIndentation(22);

  const auto add_item = [this](QString icon, QString text) {
    auto item = new QTreeWidgetItem(ui_->treeWidget, QStringList(text));
    item->setIcon(0, theme.getIcon(icon));
    item->setSizeHint(0, QSize{0, 24});
    return item;
  };

  const auto add_child = [this](QTreeWidgetItem* parent, QString text) {
    new QTreeWidgetItem(parent, QStringList(text));
  };

  add_item("account_circle", "Accounts");
  add_item("web_asset", "Application");
  add_item("list_alt", "Anime List");
  add_item("folder", "Library");
  {
    auto item = add_item("check_circle", "Recognition");
    add_child(item, "Media players");
    add_child(item, "Streaming");
  }
  {
    auto item = add_item("share", "Sharing");
    add_child(item, "Discord");
    add_child(item, "HTTP");
    add_child(item, "mIRC");
  }
  {
    auto item = add_item("rss_feed", "Torrents");
    add_child(item, "Downloads");
    add_child(item, "Filters");
  }
  {
    auto item = add_item("warning", "Advanced");
    add_child(item, "Cache");
  }

  ui_->treeWidget->expandAll();

  connect(ui_->treeWidget, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current) {
              auto text = current->text(0);
              if (current->parent()) {
                text = u"%1 / %2"_s.arg(current->parent()->text(0), text);
              }
              ui_->titleLabel->setText(text);
            }
          });

  {
    const QString svc = sync::serviceName(sync::currentServiceId());
    const QString user =
        QString::fromStdString(taiga::accounts.serviceUsername(taiga::settings.service()));
    const bool has_anilist_token = !taiga::accounts.anilistToken().empty();
    ui_->accountsInfoLabel->setTextFormat(Qt::RichText);
    ui_->accountsInfoLabel->setText(
        tr("<p><b>Active service:</b> %1<br/>"
           "<b>Username:</b> %2<br/>"
           "<b>AniList token:</b> %3</p>"
           "<p>For AniList, use <i>Open AniList authorization…</i> in the browser, then ensure your "
           "token is saved (e.g. via migration from Taiga v1 or manual <code>accounts.json</code>).</p>")
            .arg(svc.toHtmlEscaped())
            .arg(user.isEmpty() ? tr("(not set)").toHtmlEscaped() : user.toHtmlEscaped())
            .arg(has_anilist_token ? tr("Present").toHtmlEscaped() : tr("Missing").toHtmlEscaped()));

    auto* auth_btn = new QPushButton(tr("Open AniList authorization…"), ui_->accountsPage);
    connect(auth_btn, &QPushButton::clicked, this, []() {
      QDesktopServices::openUrl(QUrl(QString::fromStdString(sync::anilist::requestTokenUrl())));
    });
    ui_->verticalLayout_3->addWidget(auth_btn);
  }
}

void SettingsDialog::show(QWidget* parent) {
  auto dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->QDialog::show();
}

}  // namespace gui
