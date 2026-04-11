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
#include <QSet>
#include <QString>
#include <QStringList>

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
  std::optional<int> animeListSortColumnSecondary() const;
  Qt::SortOrder animeListSortOrderSecondary() const;
  gui::ListViewMode animeListViewMode() const;
  /// `QHeaderView::saveState` for the anime list table (widths, order, shown/hidden columns).
  QByteArray animeListHeaderState() const;
  /// Pinned anime id for the embedded watch-order panel below the list (0 = none).
  int animeListPinnedWatchOrderAnimeId() const;
  bool animeListWatchOrderPanelVisible() const;
  QByteArray animeListWatchOrderSplitterState() const;
  /// Watch-next / watch-order layout: 0 horizontal timeline, 1 list+detail. Legacy raw 2 maps to 1;
  /// former graph map (3) is normalized away on read.
  int watchNextLayoutVariant() const;
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
  /// True once the user explicitly interacted with the Search season/year filters (including clearing them).
  /// When false, Search will default to the current year+season on open.
  bool searchListSeasonYearCustomized() const;
  int searchListSortColumn() const;
  Qt::SortOrder searchListSortOrder() const;
  std::optional<int> searchListSortColumnSecondary() const;
  Qt::SortOrder searchListSortOrderSecondary() const;
  gui::ListViewMode searchListViewMode() const;
  /// Last auto-loaded season key for Search default load (e.g. "2026:Spring").
  QString searchListAutoLoadedSeasonKey() const;
  /// Epoch seconds when the last auto-load ran (0 = never).
  qint64 searchListAutoLoadedSeasonAtSecs() const;
  /// Season browse keys already fetched into the local DB (service+year+season).
  /// Used to skip redundant "Load all" network calls unless the user forces refresh.
  QStringList searchListSeasonBrowseLoadedKeys() const;
  QString torrentPanelLastQuery() const;
  /// Substring filter on the Torrents RSS table (restored when reopening the app).
  QString torrentPanelResultFilter() const;
  /// Fingerprints of recent catalog RSS items (for auto-check “new entries” detection).
  QStringList torrentCatalogSeenFingerprints() const;
  /// `QHeaderView::saveState` for the Torrents RSS table (column widths / order / visibility).
  QByteArray torrentRssTableHeaderState() const;
  /// Dismissed anime ids for the Announced releases tab (stored as JSON in session.json).
  QSet<int> announcedReleasesDismissedAnimeIds() const;

  void setAnimeListSortColumn(const int column) const;
  void setAnimeListSortOrder(const Qt::SortOrder order) const;
  void setAnimeListSortColumnSecondary(std::optional<int> column) const;
  void setAnimeListSortOrderSecondary(const Qt::SortOrder order) const;
  void setAnimeListViewMode(const gui::ListViewMode mode) const;
  void setAnimeListHeaderState(const QByteArray& state) const;
  void setAnimeListPinnedWatchOrderAnimeId(int anime_id) const;
  void setAnimeListWatchOrderPanelVisible(bool visible) const;
  void setAnimeListWatchOrderSplitterState(const QByteArray& state) const;
  void setWatchNextLayoutVariant(int variant) const;
  void setMainWindowGeometry(const QByteArray& geometry) const;
  void setMainWindowSplitterState(const QByteArray& state) const;
  void setMainWindowStatusBarVisible(bool visible) const;
  void setMainWindowNowPlayingBarEnabled(bool enabled) const;
  void setMediaDialogGeometry(const QByteArray& geometry) const;
  void setMediaDialogSplitterState(const QByteArray& state) const;
  void setSearchListFilters(const gui::AnimeListProxyModelFilter& filters) const;
  void setSearchListSeasonYearCustomized(bool customized) const;
  void setSearchListSortColumn(const int column) const;
  void setSearchListSortOrder(const Qt::SortOrder order) const;
  void setSearchListSortColumnSecondary(std::optional<int> column) const;
  void setSearchListSortOrderSecondary(const Qt::SortOrder order) const;
  void setSearchListViewMode(const gui::ListViewMode mode) const;
  void setSearchListAutoLoadedSeasonKey(const QString& key) const;
  void setSearchListAutoLoadedSeasonAtSecs(qint64 secs) const;
  void setSearchListSeasonBrowseLoadedKeys(const QStringList& keys) const;
  void setTorrentPanelLastQuery(const QString& query) const;
  void setTorrentPanelResultFilter(const QString& text) const;
  void setTorrentCatalogSeenFingerprints(const QStringList& keys) const;
  void setTorrentRssTableHeaderState(const QByteArray& state) const;
  void setAnnouncedReleasesDismissedAnimeIds(const QSet<int>& ids) const;
  void addAnnouncedReleaseDismissedAnimeId(int anime_id) const;

private:
  QString fileName() const override;
};

inline Session session;

}  // namespace taiga
