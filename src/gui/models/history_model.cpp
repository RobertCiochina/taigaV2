/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
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

#include "history_model.hpp"

#include <QApplication>
#include <QPalette>
#include <anitomy.hpp>
#include <anitomy/detail/keyword.hpp>  // don't try this at home
#include <ranges>
#include <string_view>

#include "base/string.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"

namespace gui {

namespace {

bool historyRowIsImportedQueue(const anime::HistoryItem& h) {
  const auto p = anime::kHistoryItemQueuedImportPrefix;
  return h.time.size() >= p.size() && std::string_view{h.time}.substr(0, p.size()) == p;
}

}  // namespace

HistoryModel::HistoryModel(QObject* parent) : QAbstractListModel(parent) {
  connect(&anime::history(), &anime::History::itemsChanged, this, [this]() {
    beginResetModel();
    endResetModel();
  });
}

int HistoryModel::rowCount(const QModelIndex&) const {
  return anime::history().items().size();
}

int HistoryModel::columnCount(const QModelIndex&) const {
  return NUM_COLUMNS;
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};

  switch (role) {
    case Qt::DisplayRole: {
      const auto& historyItem = anime::history().items().at(index.row());
      const auto item = anime::db.item(historyItem.anime_id);
      switch (index.column()) {
        case COLUMN_TITLE:
          if (item) return QString::fromStdString(item->titles.romaji);
          return {};
        case COLUMN_DETAILS:
          if (historyRowIsImportedQueue(historyItem)) {
            if (historyItem.episode > 0) return tr("Queued: episode %1").arg(historyItem.episode);
            return tr("Queued list update");
          }
          return tr("Episode: %1").arg(historyItem.episode);
        case COLUMN_MODIFIED:
          if (historyRowIsImportedQueue(historyItem)) {
            std::string_view tail{historyItem.time};
            tail.remove_prefix(anime::kHistoryItemQueuedImportPrefix.size());
            if (tail.empty()) return tr("Pending list sync (imported queue)");
            return tr("Pending sync — %1").arg(QString::fromStdString(std::string{tail}));
          }
          return QString::fromStdString(historyItem.time);
      }
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (index.column()) {
        case COLUMN_MODIFIED:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
      }
      break;
    }

    case AnimeIdRole:
      return anime::history().items().at(index.row()).anime_id;
  }

  return {};
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  switch (role) {
    case Qt::DisplayRole: {
      // clang-format off
      switch (section) {
        case COLUMN_TITLE: return tr("Title");
        case COLUMN_DETAILS: return tr("Details");
        case COLUMN_MODIFIED: return tr("Last modified");
      }
      // clang-format on
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (section) {
        case COLUMN_TITLE:
        case COLUMN_DETAILS:
          return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        case COLUMN_MODIFIED:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
      }
      break;
    }

    case Qt::InitialSortOrderRole: {
      switch (section) {
        case COLUMN_MODIFIED:
          return Qt::DescendingOrder;
        default:
          return Qt::AscendingOrder;
      }
      break;
    }
  }

  return QAbstractListModel::headerData(section, orientation, role);
}

}  // namespace gui
