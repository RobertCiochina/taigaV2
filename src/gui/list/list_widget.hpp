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
#include "gui/models/anime_list_proxy_model.hpp"

class QAction;
class QSplitter;
#include "gui/common/page_widget.hpp"
#include "media/anime_list.hpp"

namespace gui {

class AnimeListModel;
class AnimeListProxyModel;
class ListView;
class ListViewCards;
class WatchNextDialog;

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

  /// Sidebar-driven list status filter (for restoring navigation after modal dialogs).
  AnimeListStatusFilter currentListSidebarFilter() const;

private:
  void initToolbar();
  void initViewMenu();
  void initMoreMenu();
  void initColorLegend();
  void applyWatchOrderPanelSession();
  void onWatchOrderPanelToggled(bool visible);
  void pinSelectedForWatchOrderPanel();

  AnimeListModel* m_model = nullptr;
  AnimeListProxyModel* m_proxyModel = nullptr;
  ListView* m_listView = nullptr;
  ListViewCards* m_listViewCards = nullptr;
  QSplitter* m_listSplitter = nullptr;
  QWidget* m_watchOrderPanel = nullptr;
  WatchNextDialog* m_embeddedWatchNext = nullptr;
  QAction* m_showWatchOrderPanelAction = nullptr;
  QAction* m_pinWatchOrderAction = nullptr;
  ListViewMode m_viewMode = ListViewMode::List;
  QMenu* m_viewMenu = nullptr;
  QMenu* m_moreMenu = nullptr;
  QWidget* m_colorLegend = nullptr;
};

}  // namespace gui
