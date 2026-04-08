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

#include "media_menu.hpp"

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QUrl>
#include <QUrlQuery>
#include <chrono>
#include <ranges>

#include "base/chrono.hpp"
#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"
#include "track/media.hpp"
#include "track/scanner.hpp"

namespace {

// Temporary id for entries created offline until sync assigns an AniList list entry id.
int64_t localListEntryId(const int anime_id) {
  return -static_cast<int64_t>(anime_id);
}

void commitListEntry(const gui::MediaMenu* menu, const ListEntry& entry) {
  gui::commitListEntryLocalAndMaybeRemote(entry, const_cast<gui::MediaMenu*>(menu));
}

}  // namespace

namespace gui {

MediaMenu::MediaMenu(QWidget* parent, const QList<Anime>& items, const QMap<int, ListEntry> entries,
                     QItemSelectionModel* selectionModel)
    : QMenu(parent), m_items(items), m_entries(entries), m_selectionModel(selectionModel) {
  setAttribute(Qt::WA_DeleteOnClose);
}

void MediaMenu::popup() {
  if (m_items.empty()) return;

  addMediaItems();
  addSeparator();
  addListItems();
  addSeparator();
  addLibraryItems();
  addSeparator();
  addTorrentsItems();
  addSeparator();
  addMetaItems();

  QMenu::popup(QCursor::pos());
}

bool MediaMenu::isBatch() const {
  return m_items.size() > 1;
}

bool MediaMenu::isInList() const {
  return m_entries.size() == m_items.size();
}

bool MediaMenu::isNowPlaying() const {
  const auto ep = track::media::detection()->getCurrentEpisode();
  if (!ep || m_items.empty()) return false;
  return ep->animeId() == m_items.front().id;
}

void MediaMenu::addToList(const anime::list::Status status) const {
  const auto now = QDateTime::currentSecsSinceEpoch();
  for (const auto& item : m_items) {
    anime::db.updateItem(item);
    ListEntry entry{};
    entry.id = localListEntryId(item.id);
    entry.anime_id = item.id;
    entry.status = status;
    entry.watched_episodes = 0;
    entry.last_updated = static_cast<std::time_t>(now);
    commitListEntry(this, entry);
  }
}

void MediaMenu::editEpisode() const {
  QSet<int> watchedEpisodes;
  int maxValue = anime::kMaxEpisodeCount;

  for (const auto& item : m_items) {
    if (const auto entry = getEntry(item.id)) watchedEpisodes.insert(entry->watched_episodes);
    if (item.episode_count > 0) maxValue = std::min(maxValue, item.episode_count);
  }

  const int initalValue = watchedEpisodes.size() == 1 ? watchedEpisodes.values().front() : 0;

  bool ok = false;
  const auto value = QInputDialog::getInt(parentWidget(), tr("Edit Episodes Watched"),
                                          tr("Enter a number:"), initalValue, 0, maxValue, 1, &ok);
  if (!ok) return;

  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.watched_episodes = value;
    e.last_updated = now;
    gui::maybePromptCompletion(parentWidget(), item, e);
    commitListEntry(this, e);
  }
}

void MediaMenu::incrementEpisode() const {
  if (m_items.empty()) return;
  const auto& item = m_items.front();
  const auto* entry = getEntry(item.id);
  if (!entry) return;

  const int max = item.episode_count > 0 ? item.episode_count : anime::kMaxEpisodeCount;
  if (entry->watched_episodes >= max) return;

  ListEntry e = *entry;
  e.watched_episodes += 1;
  e.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  gui::maybePromptCompletion(parentWidget(), item, e);
  commitListEntry(this, e);
}

void MediaMenu::decrementEpisode() const {
  if (m_items.empty()) return;
  const auto& item = m_items.front();
  const auto* entry = getEntry(item.id);
  if (!entry) return;
  if (entry->watched_episodes <= 0) return;

  ListEntry e = *entry;
  e.watched_episodes -= 1;
  e.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  if (e.status == anime::list::Status::Completed && item.episode_count > 0 &&
      e.watched_episodes < item.episode_count) {
    e.status = anime::list::Status::Watching;
  }
  commitListEntry(this, e);
}

void MediaMenu::editNotes() const {
  QString initial;
  if (m_items.size() == 1) {
    if (const auto* e = getEntry(m_items.front().id)) {
      initial = QString::fromStdString(e->notes);
    }
  }

  bool ok = false;
  const auto notes =
      QInputDialog::getMultiLineText(parentWidget(), tr("Edit Notes"), tr("Enter notes:"), initial,
                                     &ok);
  if (!ok) return;

  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  const auto notes_std = notes.toStdString();
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.notes = notes_std;
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::editStatus(const anime::list::Status status) const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.status = status;
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::openFolder() const {
  const auto& item = m_items.front();

  const auto libraryFolders = taiga::settings.libraryFolders();

  for (const auto& path : libraryFolders) {
    const auto folder = track::findFolder(QString::fromStdString(path), item.id);
    if (folder) {
      qDebug() << "Found folder:" << *folder;
      QDesktopServices::openUrl(QUrl::fromLocalFile(*folder));
      return;
    }
  }

  QMessageBox::information(nullptr, tr("Open Folder"),
                           tr("Could not find folder for %1.").arg(item.titles.romaji));
}

void MediaMenu::removeFromList() const {
  QMessageBox msgBox;
  msgBox.setIcon(QMessageBox::Icon::Question);
  msgBox.setText(tr("Do you want to remove the selected items from your list?"));

  QList<QString> titles;
  for (const auto& item : m_items) {
    titles.push_back(u"<li>%1</li>"_s.arg(QString::fromStdString(item.titles.romaji)));
  }
  msgBox.setInformativeText(u"<ul>%1</ul>"_s.arg(titles.join("")));

  auto removeButton = msgBox.addButton(tr("Remove"), QMessageBox::ButtonRole::DestructiveRole);
  msgBox.addButton(QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Cancel);

  msgBox.exec();

  if (msgBox.clickedButton() == reinterpret_cast<QAbstractButton*>(removeButton)) {
    for (const auto& item : m_items) {
      sync::deleteListEntry(item.id);
    }
  }
}

void MediaMenu::searchAniDB() const {
  for (const auto& item : m_items) {
    QUrl url{"https://anidb.net/anime/"};
    url.setQuery({{"adb.search", QString::fromStdString(item.titles.romaji)}});
    QDesktopServices::openUrl(url);
  }
}

void MediaMenu::searchAniList() const {
  for (const auto& item : m_items) {
    if (sync::currentServiceId() == sync::ServiceId::AniList) {
      QUrl url{sync::animePageUrl(item.id)};
      QDesktopServices::openUrl(url);
    } else {
      QUrl url{"https://anilist.co/search/anime"};
      QUrlQuery query{{"search", QString::fromStdString(item.titles.romaji)}};
      if (anime::isNsfw(item)) query.addQueryItem("adult", "true");
      url.setQuery(query);
      QDesktopServices::openUrl(url);
    }
  }
}

void MediaMenu::searchANN() const {
  for (const auto& item : m_items) {
    QUrl url{"https://www.animenewsnetwork.com/search"};
    url.setQuery({{"q", QString::fromStdString(item.titles.romaji)}});
    QDesktopServices::openUrl(url);
  }
}

void MediaMenu::searchKitsu() const {
  for (const auto& item : m_items) {
    if (sync::currentServiceId() == sync::ServiceId::Kitsu) {
      QUrl url{sync::animePageUrl(item.id)};
      QDesktopServices::openUrl(url);
    } else {
      QUrl url{"https://kitsu.app/anime"};
      url.setQuery({{"text", QString::fromStdString(item.titles.romaji)}});
      QDesktopServices::openUrl(url);
    }
  }
}

void MediaMenu::searchMyAnimeList() const {
  for (const auto& item : m_items) {
    if (sync::currentServiceId() == sync::ServiceId::MyAnimeList) {
      QUrl url{sync::animePageUrl(item.id)};
      QDesktopServices::openUrl(url);
    } else {
      QUrl url{"https://myanimelist.net/anime.php"};
      url.setQuery({{"q", QString::fromStdString(item.titles.romaji)}});
      QDesktopServices::openUrl(url);
    }
  }
}

void MediaMenu::searchReddit() const {
  for (const auto& item : m_items) {
    QUrl url{"https://www.reddit.com/search"};
    const auto title = QString::fromStdString(item.titles.romaji);
    url.setQuery({
        {"q", u"subreddit:anime title:%1 episode discussion"_s.arg(title)},
        {"sort", "new"},
    });
    QDesktopServices::openUrl(url);
  }
}

void MediaMenu::searchWikipedia() const {
  for (const auto& item : m_items) {
    QUrl url{"https://en.wikipedia.org/wiki/Special:Search"};
    url.setQuery({{"search", QString::fromStdString(item.titles.romaji)}});
    QDesktopServices::openUrl(url);
  }
}

void MediaMenu::searchYouTube() const {
  for (const auto& item : m_items) {
    if (!item.trailer_id.empty()) {
      QUrl url{u"https://youtu.be/%1"_s.arg(QString::fromStdString(item.trailer_id))};
      QDesktopServices::openUrl(url);
    } else {
      QUrl url{"https://www.youtube.com/results"};
      url.setQuery({{"search_query", QString::fromStdString(item.titles.romaji)}});
      QDesktopServices::openUrl(url);
    }
  }
}

void MediaMenu::torrents() const {
  const auto& item = m_items.front();
  // Primary: romaji — Nyaa/torrent sites index by Japanese/romaji title.
  // Fallback: English title, tried automatically if primary returns 0 results.
  const bool has_romaji = !item.titles.romaji.empty();
  const QString primary = has_romaji
      ? QString::fromStdString(item.titles.romaji)
      : QString::fromStdString(
            anime::preferredListTitleString(item, taiga::settings.listTitleLanguage()));
  // Build a fallback: use English title if available and different from primary.
  QString fallback;
  if (has_romaji && !item.titles.english.empty()) {
    const QString english = QString::fromStdString(item.titles.english);
    if (english.compare(primary, Qt::CaseInsensitive) != 0) {
      fallback = english;
    }
  }
  if (auto* mw = mainWindow()) {
    mw->openTorrentSearchInApp(primary, fallback);
  }
}

void MediaMenu::batchSetScore(const int score) const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.score = score;
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::clearDateStarted() const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.date_started = FuzzyDate{};
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::setDateStartedToAiring() const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.date_started = item.date_started;
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::clearDateCompleted() const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.date_completed = FuzzyDate{};
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::setDateCompletedToAiring() const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    e.date_completed = item.date_finished;
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::setDateCompletedToLastUpdated() const {
  const auto now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  for (const auto& item : m_items) {
    const auto it = m_entries.find(item.id);
    if (it == m_entries.end()) continue;
    ListEntry e = *it;
    if (e.last_updated <= 0) continue;
    const QDate d = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.last_updated)).date();
    const Date ymd{std::chrono::year{d.year()}, std::chrono::month{static_cast<unsigned>(d.month())},
                   std::chrono::day{static_cast<unsigned>(d.day())}};
    e.date_completed = FuzzyDate{ymd};
    e.last_updated = now;
    commitListEntry(this, e);
  }
}

void MediaMenu::setAsNowPlaying() const {
  QMessageBox::information(
      parentWidget(), tr("Now playing"),
      tr("Manual selection of the active title is not available in this build. "
         "Recognition uses the media player automatically."));
}

void MediaMenu::viewDetails() const {
  if (m_items.empty()) return;

  const auto& anime = m_items.front();
  const auto entry = getEntry(anime.id);

  MediaDialog::show(parentWidget(), MediaDialogPage::Details, anime,
                    entry ? std::optional<ListEntry>{*entry} : std::nullopt);
}

void MediaMenu::edit() const {
  if (m_items.empty()) return;

  const auto& anime = m_items.front();
  const auto entry = getEntry(anime.id);

  MediaDialog::show(parentWidget(), MediaDialogPage::List, anime,
                    entry ? std::optional<ListEntry>{*entry} : std::nullopt);
}

void MediaMenu::addMediaItems() {
  if (!isBatch()) {
    addAction(theme.getIcon("info"), tr("Details"), tr("Enter"), this, &MediaMenu::viewDetails);
  }

  // External
  addMenu([this]() {
    auto menu = new QMenu(tr("External"), this);
    menu->setIcon(theme.getIcon("open_in_new"));

    using slot_t = void (MediaMenu::*)() const;
    const QList<QPair<QString, slot_t>> items = {
        {"AniDB", &MediaMenu::searchAniDB},
        {"AniList", &MediaMenu::searchAniList},
        {"Anime News Network", &MediaMenu::searchANN},
        {"Kitsu", &MediaMenu::searchKitsu},
        {"MyAnimeList", &MediaMenu::searchMyAnimeList},
        {"Reddit", &MediaMenu::searchReddit},
        {"Wikipedia", &MediaMenu::searchWikipedia},
        {"YouTube", &MediaMenu::searchYouTube},
    };
    for (const auto [text, slot] : items) {
      menu->addAction(text, this, slot);
    }

    return menu;
  }());
}

void MediaMenu::addListItems() {
  if (!isInList()) {
    // Add to list
    addMenu([this]() {
      auto menu = new QMenu(tr("Add to list"), this);
      menu->setIcon(theme.getIcon("list_alt"));
      for (const auto& status : anime::list::kStatuses) {
        menu->addAction(formatListStatus(status), this, [this, status]() { addToList(status); });
      }
      return menu;
    }());

    return;
  }

  // Edit
  if (!isBatch()) {
    // Quick increment (+1 episode) — v1-style fast update
    {
      const auto& item = m_items.front();
      const auto* entry = getEntry(item.id);
      if (entry) {
        const int max = item.episode_count > 0 ? item.episode_count : anime::kMaxEpisodeCount;
        if (entry->watched_episodes < max) {
          const int next = entry->watched_episodes + 1;
          const QString label = item.episode_count > 0
              ? tr("+1 episode (→ %1/%2)").arg(next).arg(item.episode_count)
              : tr("+1 episode (→ %1)").arg(next);
          addAction(theme.getIcon("add_box"), label, this, &MediaMenu::incrementEpisode);
        }
        if (entry->watched_episodes > 0) {
          const int prev = entry->watched_episodes - 1;
          const QString label = item.episode_count > 0
              ? tr("-1 episode (→ %1/%2)").arg(prev).arg(item.episode_count)
              : tr("-1 episode (→ %1)").arg(prev);
          addAction(label, this, &MediaMenu::decrementEpisode);
        }
      }
    }

    addAction(theme.getIcon("edit"), tr("Edit..."), this, &MediaMenu::edit);

  } else {
    addMenu([this]() {
      auto menu = new QMenu(tr("Edit"), this);
      menu->setIcon(theme.getIcon("edit"));

      menu->addMenu([this]() {
        auto menu = new QMenu(tr("Date started"), this);
        menu->addAction(tr("Clear"), this, &MediaMenu::clearDateStarted);
        menu->addAction(tr("Set to date started airing"), this, &MediaMenu::setDateStartedToAiring);
        return menu;
      }());

      menu->addMenu([this]() {
        auto menu = new QMenu(tr("Date completed"), this);
        menu->addAction(tr("Clear"), this, &MediaMenu::clearDateCompleted);
        menu->addAction(tr("Set to date finished airing"), this,
                        &MediaMenu::setDateCompletedToAiring);
        menu->addAction(tr("Set to last updated"), this, &MediaMenu::setDateCompletedToLastUpdated);
        return menu;
      }());

      menu->addAction(tr("Episode..."), this, &MediaMenu::editEpisode);
      menu->addAction(tr("Notes..."), this, &MediaMenu::editNotes);

      menu->addMenu([this]() {
        auto menu = new QMenu(tr("Score"), this);
        for (int i = 0; i <= 10; ++i) {
          menu->addAction(tr("%1").arg(i), this, [this, i]() { batchSetScore(i * 10); });
        }
        return menu;
      }());

      menu->addMenu([this]() {
        auto menu = new QMenu(tr("Status"), this);
        for (const auto status : anime::list::kStatuses) {
          auto action = new QAction(formatListStatus(status), this);
          menu->addAction(action);
          connect(action, &QAction::triggered, this, [this, status]() { editStatus(status); });
        }
        return menu;
      }());

      return menu;
    }());
  }

  // Remove from list
  addAction(theme.getIcon("delete"), tr("Remove..."), QKeySequence::Delete, this,
            &MediaMenu::removeFromList);
}

void MediaMenu::addLibraryItems() {
  addAction(theme.getIcon("folder"), tr("Open folder"), this, &MediaMenu::openFolder);
}

void MediaMenu::addTorrentsItems() {
  if (isBatch()) return;

  addAction(theme.getIcon("rss_feed"), tr("Search torrents…"), this, &MediaMenu::torrents);
}

void MediaMenu::addMetaItems() {
  if (!isBatch()) {
    addAction(tr("Copy title"), this, [this]() {
      const QString t = QString::fromStdString(m_items.front().titles.romaji);
      QGuiApplication::clipboard()->setText(t);
      if (auto* w = mainWindow()) {
        w->statusBar()->showMessage(tr("Copied title to clipboard."), 2500);
      }
    });
    const auto& eng = m_items.front().titles.english;
    if (!eng.empty()) {
      addAction(tr("Copy English title"), this, [this, eng]() {
        QGuiApplication::clipboard()->setText(QString::fromStdString(eng));
        if (auto* w = mainWindow()) {
          w->statusBar()->showMessage(tr("Copied English title to clipboard."), 2500);
        }
      });
    }
    const auto& native = m_items.front().titles.japanese;
    if (!native.empty()) {
      addAction(tr("Copy native title"), this, [this, native]() {
        QGuiApplication::clipboard()->setText(QString::fromStdString(native));
        if (auto* w = mainWindow()) {
          w->statusBar()->showMessage(tr("Copied native title to clipboard."), 2500);
        }
      });
    }
  }

  if (isBatch() && m_selectionModel) {
    addAction(tr("Invert selection"), this, [this]() {
      for (int row = 0; row < m_selectionModel->model()->rowCount(); ++row) {
        const auto index = m_selectionModel->model()->index(row, 0);
        m_selectionModel->select(index, QItemSelectionModel::Toggle);
      }
    });
  }

  if (isNowPlaying() && !isBatch()) {
    addAction(tr("Set as now playing..."), this, &MediaMenu::setAsNowPlaying);
  }
}

const ListEntry* MediaMenu::getEntry(int id) const {
  const auto it = m_entries.find(id);
  return it != m_entries.end() ? &*it : nullptr;
}

}  // namespace gui
