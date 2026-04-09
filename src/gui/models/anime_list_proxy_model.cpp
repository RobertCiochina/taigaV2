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

#include "anime_list_proxy_model.hpp"

#include <ranges>
#include <string>

#include "base/string.hpp"
#include "gui/models/anime_list_model.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_utils.hpp"
#include "media/anime_season.hpp"
#include "media/anime_utils.hpp"
#include "taiga/settings.hpp"
#include "track/scanner.hpp"

namespace {

const Anime* getAnime(const QModelIndex& index) {
  const int role = static_cast<int>(gui::AnimeListItemDataRole::Anime);
  return index.data(role).value<const Anime*>();
}

const ListEntry* getListEntry(const QModelIndex& index) {
  const int role = static_cast<int>(gui::AnimeListItemDataRole::ListEntry);
  return index.data(role).value<const ListEntry*>();
}

}  // namespace

namespace gui {

AnimeListProxyModel::AnimeListProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setFilterKeyColumn(AnimeListModel::COLUMN_TITLE);
  setFilterRole(Qt::DisplayRole);

  setSortCaseSensitivity(Qt::CaseInsensitive);
  setSortRole(Qt::UserRole);

  // List entry / anime metadata edits can change filter (e.g. status) and sort order.
  connect(&anime::db, &anime::Database::entryUpdated, this, [this](int) {
    m_cachedPreferredTitleLower.clear();
    invalidate();
  });
  connect(&anime::db, &anime::Database::itemUpdated, this, [this](int) {
    m_cachedPreferredTitleLower.clear();
    invalidate();
  });
}

QString AnimeListProxyModel::cachedPreferredTitleLower(const int anime_id,
                                                       const Anime* anime) const {
  const auto lang = taiga::settings.listTitleLanguage();
  const int lang_i = static_cast<int>(lang);
  if (m_cachedPreferredTitleLang != lang_i) {
    m_cachedPreferredTitleLang = lang_i;
    m_cachedPreferredTitleLower.clear();
  }
  if (!anime) return {};
  if (const auto it = m_cachedPreferredTitleLower.find(anime_id);
      it != m_cachedPreferredTitleLower.end()) {
    return it.value();
  }
  const std::string s = anime::preferredListTitleString(*anime, lang);
  const QString q = QString::fromStdString(s).toLower();
  m_cachedPreferredTitleLower.insert(anime_id, q);
  return q;
}

std::optional<int> AnimeListProxyModel::secondarySortColumn() const {
  return m_secondarySortColumn;
}

Qt::SortOrder AnimeListProxyModel::secondarySortOrder() const {
  return m_secondarySortOrder;
}

void AnimeListProxyModel::setSecondarySort(std::optional<int> column, const Qt::SortOrder order) {
  m_secondarySortColumn = column;
  m_secondarySortOrder = order;
  invalidate();
  sort(sortColumn(), sortOrder());
}

const AnimeListProxyModelFilter& AnimeListProxyModel::filters() const {
  return m_filter;
}

void AnimeListProxyModel::setFilters(const AnimeListProxyModelFilter& filters) {
  m_filter = filters;
  invalidateRowsFilter();
}

void AnimeListProxyModel::setYearFilter(std::optional<int> year) {
  m_filter.year = year;
  invalidateRowsFilter();
}

void AnimeListProxyModel::setSeasonFilter(std::optional<int> season) {
  m_filter.season = season;
  invalidateRowsFilter();
}

void AnimeListProxyModel::setTypeFilter(std::optional<int> type) {
  m_filter.type = type;
  invalidateRowsFilter();
}

void AnimeListProxyModel::setStatusFilter(std::optional<int> status) {
  m_filter.status = status;
  invalidateRowsFilter();
}

void AnimeListProxyModel::setListStatusFilter(AnimeListStatusFilter filter) {
  m_filter.listStatus = filter;
  invalidateRowsFilter();
}

void AnimeListProxyModel::setTextFilter(const QString& text) {
  m_filter.text = text;
  invalidateRowsFilter();
}

bool AnimeListProxyModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
  const auto model = static_cast<AnimeListModel*>(sourceModel());
  if (!model) return false;
  const auto index = model->index(row, 0, parent);
  const auto anime = getAnime(index);
  if (!anime) return false;
  const auto entry = getListEntry(index);

  static const auto contains = [](const std::string& str, const QStringView view) {
    return QString::fromStdString(str).contains(view, Qt::CaseInsensitive);
  };

  static const auto list_contains = [](const std::vector<std::string>& list,
                                       const QStringView view) {
    return std::ranges::any_of(list,
                               [view](const std::string& str) { return contains(str, view); });
  };

  // Year
  if (m_filter.year) {
    if (anime->date_started.year() != *m_filter.year) return false;
  }

  // Season
  if (m_filter.season) {
    const anime::Season season{anime->date_started};
    if (static_cast<int>(season.name) != *m_filter.season) return false;
  }

  // Type
  if (m_filter.type) {
    if (static_cast<int>(anime->type) != *m_filter.type) return false;
  }

  // Status
  if (m_filter.status) {
    if (static_cast<int>(anime->status) != *m_filter.status) return false;
  }

  // List status
  if (m_filter.listStatus.status) {
    const auto status = entry ? entry->status : anime::list::Status::NotInList;
    if (m_filter.listStatus.anyStatus) {
      if (status == anime::list::Status::NotInList) return false;
    } else {
      if (static_cast<int>(status) != *m_filter.listStatus.status) return false;
    }
  }

  // Titles
  if (!m_filter.text.isEmpty()) {
    if (!contains(anime->titles.romaji, m_filter.text) &&
        !contains(anime->titles.english, m_filter.text) &&
        !contains(anime->titles.japanese, m_filter.text) &&
        !list_contains(anime->titles.synonyms, m_filter.text)) {
      return false;
    }
  }

  return true;
}

bool AnimeListProxyModel::lessThan(const QModelIndex& lhs, const QModelIndex& rhs) const {
  const auto lhs_anime = getAnime(lhs);
  const auto rhs_anime = getAnime(rhs);
  if (!lhs_anime || !rhs_anime) return false;

  const auto lhs_entry = getListEntry(lhs);
  const auto rhs_entry = getListEntry(rhs);

  const auto cmp_title = [&] {
    const QString l = cachedPreferredTitleLower(lhs_anime->id, lhs_anime);
    const QString r = cachedPreferredTitleLower(rhs_anime->id, rhs_anime);
    const int c = QString::compare(l, r, Qt::CaseInsensitive);
    if (c != 0) return c;
    // Stable tie-breaker: keep strict weak ordering even when titles match.
    return lhs_anime->id < rhs_anime->id ? -1 : (lhs_anime->id > rhs_anime->id ? 1 : 0);
  };

  const auto cmp_col = [&](const int col) -> int {
    switch (col) {
      case AnimeListModel::COLUMN_TITLE:
        return cmp_title();

      case AnimeListModel::COLUMN_DURATION:
        if (lhs_anime->episode_length != rhs_anime->episode_length) {
          return lhs_anime->episode_length < rhs_anime->episode_length ? -1 : 1;
        }
        return 0;

      case AnimeListModel::COLUMN_AVERAGE:
        if (lhs_anime->score != rhs_anime->score)
          return lhs_anime->score < rhs_anime->score ? -1 : 1;
        return 0;

      case AnimeListModel::COLUMN_TYPE:
        if (lhs_anime->type != rhs_anime->type) return lhs_anime->type < rhs_anime->type ? -1 : 1;
        return 0;

      case AnimeListModel::COLUMN_PROGRESS: {
        const auto lhs_ratio = anime::list::getProgressRatio(lhs_anime, lhs_entry);
        const auto rhs_ratio = anime::list::getProgressRatio(rhs_anime, rhs_entry);
        if (lhs_ratio != rhs_ratio) return lhs_ratio < rhs_ratio ? -1 : 1;
        const int lhs_ec = anime::estimateEpisodeCount(*lhs_anime, 0);
        const int rhs_ec = anime::estimateEpisodeCount(*rhs_anime, 0);
        if (lhs_ec != rhs_ec) return lhs_ec < rhs_ec ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_REWATCHES: {
        const int l = lhs_entry ? lhs_entry->rewatched_times : 0;
        const int r = rhs_entry ? rhs_entry->rewatched_times : 0;
        if (l != r) return l < r ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_SCORE: {
        const int l = lhs_entry ? lhs_entry->score : 0;
        const int r = rhs_entry ? rhs_entry->score : 0;
        if (l != r) return l < r ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_SEASON:
        if (lhs_anime->date_started != rhs_anime->date_started) {
          return lhs_anime->date_started < rhs_anime->date_started ? -1 : 1;
        }
        return 0;

      case AnimeListModel::COLUMN_STARTED: {
        const FuzzyDate l = lhs_entry ? lhs_entry->date_started : FuzzyDate{};
        const FuzzyDate r = rhs_entry ? rhs_entry->date_started : FuzzyDate{};
        if (l != r) return l < r ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_COMPLETED: {
        const FuzzyDate l = lhs_entry ? lhs_entry->date_completed : FuzzyDate{};
        const FuzzyDate r = rhs_entry ? rhs_entry->date_completed : FuzzyDate{};
        if (l != r) return l < r ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_LAST_UPDATED: {
        const int64_t l = lhs_entry ? lhs_entry->last_updated : 0;
        const int64_t r = rhs_entry ? rhs_entry->last_updated : 0;
        if (l != r) return l < r ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_NOTES: {
        const std::string& l = lhs_entry ? lhs_entry->notes : std::string{};
        const std::string& r = rhs_entry ? rhs_entry->notes : std::string{};
        if (l != r) return l < r ? -1 : 1;
        return 0;
      }

      case AnimeListModel::COLUMN_WATCH_ORDER_GUIDE:
        return 0;
    }
    return 0;
  };

  if (taiga::settings.listHighlightNextEpisodeOnDisk() &&
      taiga::settings.listHighlightAvailableOnTop()) {
    const int rank_lhs = track::nextEpisodeIsOnDisk(lhs_anime->id, lhs_anime, lhs_entry) ? 0 : 1;
    const int rank_rhs = track::nextEpisodeIsOnDisk(rhs_anime->id, rhs_anime, rhs_entry) ? 0 : 1;
    if (rank_lhs != rank_rhs) {
      // Keep "next episode on disk" items on top regardless of sort order.
      return sortOrder() == Qt::SortOrder::DescendingOrder ? (rank_lhs > rank_rhs)
                                                           : (rank_lhs < rank_rhs);
    }
  }

  int c = cmp_col(sortColumn());
  if (c == 0 && m_secondarySortColumn && *m_secondarySortColumn != sortColumn()) {
    // QSortFilterProxyModel in descending order swaps lhs/rhs when calling lessThan.
    // Secondary sort order is user-defined, so keep it stable regardless of primary order.
    const QModelIndex& lhs2 = sortOrder() == Qt::SortOrder::DescendingOrder ? rhs : lhs;
    const QModelIndex& rhs2 = sortOrder() == Qt::SortOrder::DescendingOrder ? lhs : rhs;
    const auto lhs2_anime = getAnime(lhs2);
    const auto rhs2_anime = getAnime(rhs2);
    if (lhs2_anime && rhs2_anime) {
      const auto lhs2_entry = getListEntry(lhs2);
      const auto rhs2_entry = getListEntry(rhs2);
      // Re-evaluate the secondary comparison using the unswapped order.
      // We intentionally do not reuse cmp_col() directly since it closes over lhs/rhs.
      const auto cmp_col2 = [&](const int col) -> int {
        switch (col) {
          case AnimeListModel::COLUMN_TITLE: {
            const QString l = cachedPreferredTitleLower(lhs2_anime->id, lhs2_anime);
            const QString r = cachedPreferredTitleLower(rhs2_anime->id, rhs2_anime);
            const int cc = QString::compare(l, r, Qt::CaseInsensitive);
            if (cc != 0) return cc;
            return lhs2_anime->id < rhs2_anime->id ? -1 : (lhs2_anime->id > rhs2_anime->id ? 1 : 0);
          }
          case AnimeListModel::COLUMN_DURATION:
            if (lhs2_anime->episode_length != rhs2_anime->episode_length) {
              return lhs2_anime->episode_length < rhs2_anime->episode_length ? -1 : 1;
            }
            return 0;
          case AnimeListModel::COLUMN_AVERAGE:
            if (lhs2_anime->score != rhs2_anime->score)
              return lhs2_anime->score < rhs2_anime->score ? -1 : 1;
            return 0;
          case AnimeListModel::COLUMN_TYPE:
            if (lhs2_anime->type != rhs2_anime->type)
              return lhs2_anime->type < rhs2_anime->type ? -1 : 1;
            return 0;
          case AnimeListModel::COLUMN_PROGRESS: {
            const auto lhs_ratio = anime::list::getProgressRatio(lhs2_anime, lhs2_entry);
            const auto rhs_ratio = anime::list::getProgressRatio(rhs2_anime, rhs2_entry);
            if (lhs_ratio != rhs_ratio) return lhs_ratio < rhs_ratio ? -1 : 1;
            const int lhs_ec = anime::estimateEpisodeCount(*lhs2_anime, 0);
            const int rhs_ec = anime::estimateEpisodeCount(*rhs2_anime, 0);
            if (lhs_ec != rhs_ec) return lhs_ec < rhs_ec ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_REWATCHES: {
            const int l = lhs2_entry ? lhs2_entry->rewatched_times : 0;
            const int r = rhs2_entry ? rhs2_entry->rewatched_times : 0;
            if (l != r) return l < r ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_SCORE: {
            const int l = lhs2_entry ? lhs2_entry->score : 0;
            const int r = rhs2_entry ? rhs2_entry->score : 0;
            if (l != r) return l < r ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_SEASON:
            if (lhs2_anime->date_started != rhs2_anime->date_started) {
              return lhs2_anime->date_started < rhs2_anime->date_started ? -1 : 1;
            }
            return 0;
          case AnimeListModel::COLUMN_STARTED: {
            const FuzzyDate l = lhs2_entry ? lhs2_entry->date_started : FuzzyDate{};
            const FuzzyDate r = rhs2_entry ? rhs2_entry->date_started : FuzzyDate{};
            if (l != r) return l < r ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_COMPLETED: {
            const FuzzyDate l = lhs2_entry ? lhs2_entry->date_completed : FuzzyDate{};
            const FuzzyDate r = rhs2_entry ? rhs2_entry->date_completed : FuzzyDate{};
            if (l != r) return l < r ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_LAST_UPDATED: {
            const int64_t l = lhs2_entry ? lhs2_entry->last_updated : 0;
            const int64_t r = rhs2_entry ? rhs2_entry->last_updated : 0;
            if (l != r) return l < r ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_NOTES: {
            const std::string& l = lhs2_entry ? lhs2_entry->notes : std::string{};
            const std::string& r = rhs2_entry ? rhs2_entry->notes : std::string{};
            if (l != r) return l < r ? -1 : 1;
            return 0;
          }
          case AnimeListModel::COLUMN_WATCH_ORDER_GUIDE:
            return 0;
        }
        return 0;
      };
      c = cmp_col2(*m_secondarySortColumn);
      if (m_secondarySortOrder == Qt::SortOrder::DescendingOrder) c = -c;
    }
  }
  if (c == 0) c = cmp_title();
  return c < 0;
}

}  // namespace gui
