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

#include <QByteArray>
#include <QString>

#include "base/settings.hpp"

namespace gui {
enum class ListViewMode;
struct AnimeListProxyModelFilter;
}

namespace taiga {

class Session final : public base::Settings {
public:
  int animeListSortColumn() const;
  Qt::SortOrder animeListSortOrder() const;
  gui::ListViewMode animeListViewMode() const;
  /// `QHeaderView::saveState` for the anime list table (widths, order, shown/hidden columns).
  QByteArray animeListHeaderState() const;
  /// One-shot JSON array from v1 `settings.xml` list columns migration (consumed when list view opens).
  void setPendingV1ListColumnLayout(const QString& json) const;
  QString takePendingV1ListColumnLayout() const;
  QByteArray mainWindowGeometry() const;
  QByteArray mainWindowSplitterState() const;
  bool mainWindowStatusBarVisible() const;
  bool mainWindowNowPlayingBarEnabled() const;
  QByteArray mediaDialogGeometry() const;
  QByteArray mediaDialogSplitterState() const;
  gui::AnimeListProxyModelFilter searchListFilters() const;
  int searchListSortColumn() const;
  Qt::SortOrder searchListSortOrder() const;
  gui::ListViewMode searchListViewMode() const;
  QString torrentPanelLastQuery() const;

  void setAnimeListSortColumn(const int column) const;
  void setAnimeListSortOrder(const Qt::SortOrder order) const;
  void setAnimeListViewMode(const gui::ListViewMode mode) const;
  void setAnimeListHeaderState(const QByteArray& state) const;
  void setMainWindowGeometry(const QByteArray& geometry) const;
  void setMainWindowSplitterState(const QByteArray& state) const;
  void setMainWindowStatusBarVisible(bool visible) const;
  void setMainWindowNowPlayingBarEnabled(bool enabled) const;
  void setMediaDialogGeometry(const QByteArray& geometry) const;
  void setMediaDialogSplitterState(const QByteArray& state) const;
  void setSearchListFilters(const gui::AnimeListProxyModelFilter& filters) const;
  void setSearchListSortColumn(const int column) const;
  void setSearchListSortOrder(const Qt::SortOrder order) const;
  void setSearchListViewMode(const gui::ListViewMode mode) const;
  void setTorrentPanelLastQuery(const QString& query) const;

private:
  QString fileName() const override;
};

inline Session session;

}  // namespace taiga
