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

#include <QAbstractListModel>
#include <QList>

#include "media/anime.hpp"
#include "media/anime_list.hpp"

namespace gui {

enum class AnimeListItemDataRole {
  Anime = Qt::UserRole,
  ListEntry,
  Poster,
};

/// How `AnimeListModel` builds its backing id list.
enum class AnimeListModelSource {
  /// All catalog items (Search page; natural DB key order).
  AllCatalogItems,
  /// List entries that have catalog metadata, ordered by `ListEntry::last_updated` (newest first).
  ListEntriesByLastUpdated,
};

class AnimeListModel final : public QAbstractListModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AnimeListModel)

public:
  enum Column {
    COLUMN_TITLE,
    COLUMN_PROGRESS,
    COLUMN_DURATION,
    COLUMN_REWATCHES,
    COLUMN_SCORE,
    COLUMN_AVERAGE,
    COLUMN_TYPE,
    COLUMN_SEASON,
    COLUMN_STARTED,
    COLUMN_COMPLETED,
    COLUMN_LAST_UPDATED,
    COLUMN_NOTES,
    COLUMN_WATCH_ORDER_GUIDE,
    NUM_COLUMNS
  };

  explicit AnimeListModel(QObject* parent,
                          AnimeListModelSource source = AnimeListModelSource::AllCatalogItems);
  ~AnimeListModel() = default;

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role) override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;

  const Anime* getAnime(const QModelIndex& index) const;
  const ListEntry* getListEntry(const QModelIndex& index) const;

  void reloadFromDatabase();

  /// Call after list title language preference changes (avoids full model reset).
  void emitTitleColumnDataChanged();
  /// Repaint progress column after list progress bar display settings change or library scan.
  void emitProgressColumnDataChanged();
  void emitNewEpisodeHighlightDataChanged();
  /// List page: rebuild row order (next-on-disk pin + last_updated) and repaint; Search: repaint only.
  void refreshNewEpisodeHighlightDisplay();

private:
  void rebuildIdList();

  AnimeListModelSource m_source = AnimeListModelSource::AllCatalogItems;
  QList<int> m_ids;
};

}  // namespace gui
