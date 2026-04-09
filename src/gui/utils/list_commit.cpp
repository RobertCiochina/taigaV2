/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "list_commit.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>
#include <chrono>

#include "gui/main/main_window.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
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

bool maybePromptCompletion(QWidget* parent, const Anime& item, ListEntry& entry) {
  if (item.episode_count <= 0) return false;
  if (entry.watched_episodes < item.episode_count) return false;
  if (entry.status == anime::list::Status::Completed) return false;
  if (entry.status != anime::list::Status::Watching &&
      entry.status != anime::list::Status::PlanToWatch &&
      entry.status != anime::list::Status::OnHold) {
    return false;
  }

  entry.status = anime::list::Status::Completed;

  // Set completion date to today if not already set
  if (entry.date_completed.empty()) {
    const QDate today = QDate::currentDate();
    entry.date_completed = FuzzyDate{
        std::chrono::year{today.year()},
        std::chrono::month{static_cast<unsigned>(today.month())},
        std::chrono::day{static_cast<unsigned>(today.day())}};
  }

  entry.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  return true;
}

}  // namespace gui
