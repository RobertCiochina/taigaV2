/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "list_commit.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QWidget>

#include "gui/main/main_window.hpp"
#include "media/anime_db.hpp"
#include "sync/service.hpp"
#include "taiga/settings.hpp"

namespace gui {

void commitListEntryLocalAndMaybeRemote(const ListEntry& entry, QWidget* context_widget) {
  anime::db.updateEntry(entry);

  if (!taiga::settings.listSynchronizationEnabled()) return;
  if (!sync::remoteListAccessConfigured()) return;

  if (taiga::settings.syncListPushAskConfirm()) {
    QWidget* parent = context_widget ? context_widget : static_cast<QWidget*>(mainWindow());
    const auto answer =
        QMessageBox::question(parent, QApplication::translate("ListCommit", "Upload list change?"),
                              QApplication::translate(
                                  "ListCommit",
                                  "Send this update to %1 now?\n\n"
                                  "(Turn off “Ask before uploading list changes” in Settings → Anime list if "
                                  "you prefer silent sync.)")
                                  .arg(sync::serviceName(sync::currentServiceId())),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) return;
  }

  sync::saveListEntry(entry);
}

}  // namespace gui
