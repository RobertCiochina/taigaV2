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

#include "anime_list_view.hpp"

#include <QHeaderView>
#include <QKeyEvent>

#include "gui/common/anime_list_item_delegate.hpp"
#include "gui/common/anime_list_view_base.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "gui/utils/painters.hpp"
#include "taiga/settings.hpp"

namespace gui {

void positionAnimeListGuideColumnAfterTitle(QHeaderView* header) {
  if (!header) return;
  const int guide = AnimeListModel::COLUMN_WATCH_ORDER_GUIDE;
  const int title = AnimeListModel::COLUMN_TITLE;
  const int from = header->visualIndex(guide);
  const int titleVis = header->visualIndex(title);
  if (from < 0 || titleVis < 0) return;
  const int target = titleVis + 1;
  if (from != target) {
    header->moveSection(from, target);
  }
}

void applyAnimeListHorizontalStretch(QHeaderView* header) {
  if (!header) return;
  header->setStretchLastSection(false);
  header->setSectionResizeMode(AnimeListModel::COLUMN_TITLE, QHeaderView::Stretch);
  header->setSectionResizeMode(AnimeListModel::COLUMN_WATCH_ORDER_GUIDE, QHeaderView::Fixed);
}

ListView::ListView(QWidget* parent, AnimeListModel* model, AnimeListProxyModel* proxyModel,
                   const bool enableSorting)
    // ListViewBase must be parented to this view, not the page widget: switching List/Cards
    // destroys the view while the page (ListWidget/SearchWidget) lives on; a ListViewBase left
    // under the page would outlive the view and hold a dangling m_view.
    : m_base(new ListViewBase(this, this, model, proxyModel)) {
  setObjectName("animeList");

  setFrameShape(QFrame::Shape::NoFrame);

  setAlternatingRowColors(true);
  setItemDelegate(new ListItemDelegate(this));

  setAllColumnsShowFocus(true);
  setExpandsOnDoubleClick(false);
  setItemsExpandable(false);
  setRootIsDecorated(false);
  // Word wrap on the title column: rows auto-size to fit. setUniformRowHeights(false)
  // is required for variable row heights; the modest perf cost is acceptable.
  setUniformRowHeights(false);
  setWordWrap(true);
  setMouseTracking(true);
  viewport()->setAttribute(Qt::WA_Hover, true);

  header()->setFirstSectionMovable(true);
  header()->setStretchLastSection(false);
  header()->setTextElideMode(Qt::ElideRight);
  header()->hideSection(AnimeListModel::COLUMN_DURATION);
  header()->hideSection(AnimeListModel::COLUMN_REWATCHES);
  header()->hideSection(AnimeListModel::COLUMN_AVERAGE);
  header()->hideSection(AnimeListModel::COLUMN_STARTED);
  header()->hideSection(AnimeListModel::COLUMN_COMPLETED);
  header()->hideSection(AnimeListModel::COLUMN_NOTES);
  header()->resizeSection(AnimeListModel::COLUMN_TITLE, 295);
  header()->resizeSection(AnimeListModel::COLUMN_PROGRESS, 150);
  header()->resizeSection(AnimeListModel::COLUMN_DURATION, 75);
  header()->resizeSection(AnimeListModel::COLUMN_SCORE, 75);
  header()->resizeSection(AnimeListModel::COLUMN_AVERAGE, 75);
  header()->resizeSection(AnimeListModel::COLUMN_TYPE, 75);
  header()->resizeSection(AnimeListModel::COLUMN_LAST_UPDATED, 110);
  header()->setSectionResizeMode(AnimeListModel::COLUMN_WATCH_ORDER_GUIDE, QHeaderView::Fixed);
  header()->resizeSection(AnimeListModel::COLUMN_WATCH_ORDER_GUIDE, 42);
  positionAnimeListGuideColumnAfterTitle(header());
  applyAnimeListHorizontalStretch(header());

  if (enableSorting) {
    // `sortByColumn` needs to be called before `setSortingEnabled`.
    // Otherwise the sort column is set to `0`.
    sortByColumn(proxyModel->sortColumn(), proxyModel->sortOrder());
    setSortingEnabled(true);
  } else {
    setSortingEnabled(false);
  }

  connect(this, &QAbstractItemView::clicked, this,
          qOverload<const QModelIndex&>(&QAbstractItemView::edit));
}

void ListView::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key::Key_Return || event->key() == Qt::Key::Key_Enter) {
    const auto indexes = selectionModel()->selectedRows();
    for (const auto& index : indexes) {
      m_base->runListRowAction(taiga::settings.listDoubleClickAction(), index);
    }
    return;
  }

  QTreeView::keyPressEvent(event);
}

void ListView::paintEvent(QPaintEvent* event) {
  if (model() && model()->rowCount() == 0) {
    paintEmptyListText(
        this,
        tr("No items found.\nDouble-click, Enter, and middle-click actions can be changed under "
           "Settings → Anime List."));
  }

  QTreeView::paintEvent(event);
}

}  // namespace gui
