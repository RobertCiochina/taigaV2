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

#pragma once

#include <QMenu>
#include <QString>
#include <optional>

#include "gui/common/anime_list_view_base.hpp"
#include "gui/common/page_widget.hpp"
#include "media/anime_list.hpp"

class QTabBar;

namespace gui {

class AnimeListModel;
class AnimeListProxyModel;
class ListView;
class ListViewCards;

class ListWidget final : public PageWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ListWidget)

public:
  ListWidget(QWidget* parent);
  ~ListWidget() = default;

  ListViewMode viewMode() const;
  void setViewMode(ListViewMode mode);

  void saveState();

  void reloadAnimeList();

  /// Refresh title column after Settings changes list title language (no full reload).
  void refreshListTitleDisplay();
  void refreshProgressColumnDisplay();
  void refreshNewEpisodeHighlightDisplay();

  /// First selected list row's anime id, if the list view exists and something is selected.
  std::optional<int> selectedAnimeId() const;

  void applyToolbarTextFilter(const QString& text);

  /// Programmatically select a status tab (e.g. from sidebar navigation).
  void selectStatusTab(anime::list::Status status);

  /// Refresh the status tab bar text/counts from the current database entries.
  /// Useful when list entries are updated without a full model reset (e.g. media recognition commits).
  void refreshStatusTabCountsNow();

private:
  void initToolbar();
  void initSortMenu();
  void initViewMenu();
  void initMoreMenu();
  void initStatusTabBar();
  void refreshStatusTabCounts();

  AnimeListModel* m_model = nullptr;
  AnimeListProxyModel* m_proxyModel = nullptr;
  ListView* m_listView = nullptr;
  ListViewCards* m_listViewCards = nullptr;
  ListViewMode m_viewMode = ListViewMode::List;
  QMenu* m_sortMenu = nullptr;
  QMenu* m_viewMenu = nullptr;
  QMenu* m_moreMenu = nullptr;
  QTabBar* m_statusTabBar = nullptr;
  bool m_suppressTabSync = false;
};

}  // namespace gui
