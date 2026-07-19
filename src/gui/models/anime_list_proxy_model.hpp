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

#include <QHash>
#include <QSortFilterProxyModel>
#include <optional>

#include "media/anime.hpp"

namespace gui {

struct AnimeListStatusFilter {
  std::optional<int> status;
  bool anyStatus = false;
  // When true, keep only titles that are NOT on the user's list (overrides `status`/`anyStatus`).
  bool notInList = false;
};

struct AnimeListProxyModelFilter {
  std::optional<int> year;
  std::optional<int> season;
  std::optional<int> type;
  std::optional<int> status;
  AnimeListStatusFilter listStatus;
  QString text;
};

class AnimeListProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AnimeListProxyModel)

public:
  AnimeListProxyModel(QObject* parent);
  ~AnimeListProxyModel() = default;

  const AnimeListProxyModelFilter& filters() const;
  void setFilters(const AnimeListProxyModelFilter& filters);

  void setYearFilter(std::optional<int> year);
  void setSeasonFilter(std::optional<int> season);
  void setTypeFilter(std::optional<int> type);
  void setStatusFilter(std::optional<int> status);
  void setListStatusFilter(AnimeListStatusFilter filter);
  void setTextFilter(const QString& text);

  std::optional<int> secondarySortColumn() const;
  Qt::SortOrder secondarySortOrder() const;
  void setSecondarySort(std::optional<int> column, Qt::SortOrder order);

protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
  bool lessThan(const QModelIndex& lhs, const QModelIndex& rhs) const override;

private:
  QString cachedPreferredTitleLower(int anime_id, const Anime* anime) const;

  AnimeListProxyModelFilter m_filter;
  std::optional<int> m_secondarySortColumn;
  Qt::SortOrder m_secondarySortOrder = Qt::AscendingOrder;

  // Cache to avoid recomputing preferred titles during heavy sorts (e.g. Search reset expanding
  // result set). Keyed by anime id; values are lowercased for case-insensitive compare.
  mutable QHash<int, QString> m_cachedPreferredTitleLower;
};

}  // namespace gui
