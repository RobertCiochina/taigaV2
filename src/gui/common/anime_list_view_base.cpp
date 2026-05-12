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

#include "anime_list_view_base.hpp"

#include <QDesktopServices>
#include <QEvent>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTreeView>
#include <QUrl>

#include "gui/main/main_window.hpp"
#include "gui/main/navigation_item_delegate.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/media/media_menu.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/ui_strings.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "sync/service.hpp"
#include "taiga/settings.hpp"
#include "track/play.hpp"
#include "track/scanner.hpp"

namespace gui {

QModelIndex ListViewBase::mapViewIndexToAnimeProxy(const QModelIndex& viewIndex) const {
  if (!viewIndex.isValid() || !m_view || !m_proxyModel) return {};
  if (m_view->model() == m_proxyModel) return viewIndex;

  QModelIndex idx = viewIndex;
  const QAbstractItemModel* m = m_view->model();
  while (m && m != m_proxyModel) {
    const auto* p = qobject_cast<const QAbstractProxyModel*>(m);
    if (!p) return {};
    idx = p->mapToSource(idx);
    if (!idx.isValid()) return {};
    m = p->sourceModel();
  }
  return (m == m_proxyModel) ? idx : QModelIndex{};
}

ListViewBase::ListViewBase(QWidget* parent, QAbstractItemView* view, AnimeListModel* model,
                           AnimeListProxyModel* proxyModel)
    : QObject(parent), m_view(view), m_model(model), m_proxyModel(proxyModel) {
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  m_view->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);

  m_proxyModel->setSourceModel(m_model);
  m_view->setModel(m_proxyModel);

  m_view->viewport()->installEventFilter(this);

  connect(m_view, &QAbstractItemView::doubleClicked, this,
          [this](const QModelIndex& index) {
            runListRowAction(taiga::settings.listDoubleClickAction(), index);
          });

  connect(m_view, &QWidget::customContextMenuRequested, this, &ListViewBase::showMediaMenu);

  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &ListViewBase::updateSelectionStatus);
}

bool ListViewBase::eventFilter(QObject* watched, QEvent* event) {
  if (watched != m_view->viewport()) {
    return QObject::eventFilter(watched, event);
  }

  if (event->type() == QEvent::MouseButtonRelease) {
    const auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() == Qt::LeftButton) {
      const QModelIndex proxyIndex = m_view->indexAt(me->pos());
      const QModelIndex proxy = mapViewIndexToAnimeProxy(proxyIndex);
      if (proxy.isValid() && proxy.column() == AnimeListModel::COLUMN_WATCH_ORDER_GUIDE) {
        const auto mapped = m_proxyModel->mapToSource(proxy);
        if (const auto* anime = m_model->getAnime(mapped)) {
          if (auto* mw = mainWindow()) {
            mw->openWatchOrderGuideForAnime(anime->id);
          }
          return true;
        }
      }
      if (proxy.isValid() && proxy.column() == AnimeListModel::COLUMN_MOVIE_TORRENTS) {
        const auto mapped = m_proxyModel->mapToSource(proxy);
        const auto* anime = m_model->getAnime(mapped);
        const auto* entry = m_model->getListEntry(mapped);
        if (!anime || !entry) return true;
        const bool show = anime->type == anime::Type::Movie &&
                          entry->status == anime::list::Status::Watching;
        if (!show) return true;
        if (auto* mw = mainWindow()) {
          const QString primary = QString::fromStdString(anime->titles.romaji).trimmed().isEmpty()
                                      ? QString::fromStdString(anime->titles.english)
                                      : QString::fromStdString(anime->titles.romaji);
          QString fallback;
          if (!anime->titles.english.empty() &&
              QString::fromStdString(anime->titles.english).compare(primary, Qt::CaseInsensitive) !=
                  0) {
            fallback = QString::fromStdString(anime->titles.english);
          }
          mw->openTorrentSearchInApp(primary, fallback, anime->id);
        }
        return true;
      }
    }
  }

  if (event->type() == QEvent::MouseButtonPress) {
    const auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() == Qt::MiddleButton) {
      const QModelIndex index = m_view->indexAt(me->pos());
      if (index.isValid()) {
        m_view->setCurrentIndex(index);
        runListRowAction(taiga::settings.listMiddleClickAction(), index);
        return true;
      }
    }
  }
  return QObject::eventFilter(watched, event);
}

void ListViewBase::runListRowAction(const taiga::ListRowAction action, const QModelIndex& proxyIndex) {
  const QModelIndex proxy = mapViewIndexToAnimeProxy(proxyIndex);
  if (!proxy.isValid()) return;
  const auto mappedIndex = m_proxyModel->mapToSource(proxy);
  const auto anime = m_model->getAnime(mappedIndex);
  if (!anime) return;
  const auto entry = m_model->getListEntry(mappedIndex);
  QWidget* const anchor = m_view->window();

  switch (action) {
    case taiga::ListRowAction::Nothing:
      return;
    case taiga::ListRowAction::EditListEntry:
      MediaDialog::show(mainWindow(), MediaDialogPage::List, *anime,
                        entry ? std::optional<ListEntry>{*entry} : std::nullopt);
      return;
    case taiga::ListRowAction::OpenFolder: {
      for (const auto& path : taiga::settings.libraryFolders()) {
        if (const auto folder = track::findFolder(QString::fromStdString(path), anime->id)) {
          QDesktopServices::openUrl(QUrl::fromLocalFile(*folder));
          return;
        }
      }
      QMessageBox::information(anchor, libraryOpenFolderMessageTitle(),
                               libraryNoFolderForTitleMessage());
      return;
    }
    case taiga::ListRowAction::PlayNext:
      if (!track::playNextEpisode(anime->id)) {
        QMessageBox::information(anchor, tr("Taiga"), playNextEpisodeNotFoundMessage());
      }
      return;
    case taiga::ListRowAction::ShowDetails:
      MediaDialog::show(mainWindow(), MediaDialogPage::Details, *anime,
                        entry ? std::optional<ListEntry>{*entry} : std::nullopt);
      return;
    case taiga::ListRowAction::OpenAnimePage:
      QDesktopServices::openUrl(sync::animePageUrl(anime->id));
      return;
  }
}

void ListViewBase::filterByText(const QString& text) {
  m_proxyModel->setTextFilter(text);
}

void ListViewBase::playNextEpisode(const QModelIndex& index) {
  const QModelIndex proxy = mapViewIndexToAnimeProxy(index);
  if (!proxy.isValid()) return;
  const auto mappedIndex = m_proxyModel->mapToSource(proxy);
  const auto anime = m_model->getAnime(mappedIndex);
  if (!anime) return;
  track::playNextEpisode(anime->id);
}

void ListViewBase::showMediaDialog(const QModelIndex& index) {
  const QModelIndex proxy = mapViewIndexToAnimeProxy(index);
  if (!proxy.isValid()) return;
  const auto mappedIndex = m_proxyModel->mapToSource(proxy);
  const auto anime = m_model->getAnime(mappedIndex);
  if (!anime) return;
  const auto entry = m_model->getListEntry(mappedIndex);
  MediaDialog::show(mainWindow(), MediaDialogPage::Details, *anime,
                    entry ? std::optional<ListEntry>{*entry} : std::nullopt);
}

void ListViewBase::showMediaMenu() {
  const auto indexes = selectedIndexes();
  if (indexes.isEmpty()) return;

  QList<Anime> items;
  QMap<int, ListEntry> entries;

  for (auto selectedIndex : indexes) {
    const QModelIndex proxy = mapViewIndexToAnimeProxy(selectedIndex);
    if (!proxy.isValid()) continue;
    const auto index = m_proxyModel->mapToSource(proxy);
    if (const auto item = m_model->getAnime(index)) {
      items.push_back(*item);
      if (const auto entry = m_model->getListEntry(index)) {
        entries[item->id] = *entry;
      }
    }
  }

  auto* menu = new MediaMenu(m_view, items, entries, m_view->selectionModel());
  menu->popup();
}

void ListViewBase::updateSelectionStatus(const QItemSelection&, const QItemSelection&) {
  auto* mw = mainWindow();
  if (!mw || !mw->statusBar()) return;

  const auto n_selected = selectedIndexes().size();

  if (!n_selected) {
    mw->statusBar()->clearMessage();
    return;
  }

  int n_episodes = 0;
  int n_score = 0;
  double total_score = 0.0;
  double average_score = 0.0;

  for (const auto index : selectedIndexes()) {
    const QModelIndex proxy = mapViewIndexToAnimeProxy(index);
    if (!proxy.isValid()) continue;
    const auto anime = m_model->getAnime(m_proxyModel->mapToSource(proxy));
    if (!anime) continue;
    if (anime->episode_count > 0) n_episodes += anime->episode_count;
    if (anime->score) {
      ++n_score;
      total_score += anime->score;
    }
  }

  if (n_score) average_score = total_score / n_score;

  const QStringList parts{
      tr("%n item(s) selected", nullptr, n_selected),
      tr("%n episode(s)", nullptr, n_episodes),
      tr("%1 average").arg(formatScore(average_score)),
  };

  mw->statusBar()->showMessage(parts.join(" · "));
}

QModelIndexList ListViewBase::selectedIndexes() {
  const auto model = m_view->selectionModel();
  return model->selectedRows().size() ? model->selectedRows() : model->selectedIndexes();
}

}  // namespace gui
