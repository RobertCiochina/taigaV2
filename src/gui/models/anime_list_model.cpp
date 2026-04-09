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

#include "anime_list_model.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QFont>
#include <QList>
#include <QPalette>
#include <QSize>
#include <ctime>

#include "gui/main/main_window.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/list_commit.hpp"
#include "media/anime_db.hpp"
#include "media/anime_season.hpp"
#include "taiga/settings.hpp"
#include "track/scanner.hpp"

namespace gui {

namespace {

void commitListEntry(const ListEntry& entry) {
  gui::commitListEntryLocalAndMaybeRemote(entry, gui::mainWindow());
}

}  // namespace

void AnimeListModel::rebuildIdList() {
  if (m_source == AnimeListModelSource::ListEntriesByLastUpdated) {
    std::vector<int> ids;
    ids.reserve(static_cast<size_t>(anime::db.entries().size()));
    for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
      const int id = it.key();
      if (!anime::db.item(id)) continue;
      ids.push_back(id);
    }
    const bool pin_next_on_disk = taiga::settings.listHighlightNextEpisodeOnDisk() &&
                                  taiga::settings.listHighlightAvailableOnTop();
    std::sort(ids.begin(), ids.end(), [pin_next_on_disk](int a, int b) {
      const auto* ea = anime::db.entry(a);
      const auto* eb = anime::db.entry(b);
      const auto la = static_cast<int64_t>(ea ? ea->last_updated : 0);
      const auto lb = static_cast<int64_t>(eb ? eb->last_updated : 0);
      if (pin_next_on_disk) {
        const auto* da = anime::db.item(a);
        const auto* db_item = anime::db.item(b);
        const bool oa = track::nextEpisodeIsOnDisk(a, da, ea);
        const bool ob = track::nextEpisodeIsOnDisk(b, db_item, eb);
        if (oa != ob) return oa > ob;
      }
      if (la != lb) return la > lb;
      return a < b;
    });
    m_ids.clear();
    for (const int id : ids) {
      m_ids.append(id);
    }
  } else {
    m_ids = anime::db.items().keys();
  }
}

AnimeListModel::AnimeListModel(QObject* parent, const AnimeListModelSource source)
    : QAbstractListModel(parent), m_source(source) {
  rebuildIdList();
  if (!m_ids.isEmpty()) {
    beginInsertRows({}, 0, m_ids.size() - 1);
    endInsertRows();
  }

  connect(&imageProvider, &ImageProvider::posterChanged, this, [this](int id) {
    if (const auto row = m_ids.indexOf(id); row > -1) {
      emit dataChanged(index(row), index(row), {static_cast<int>(AnimeListItemDataRole::Poster)});
    }
  });

  const auto emitRowOrReloadCatalog = [this](int id) {
    const int row = m_ids.indexOf(id);
    if (row < 0) {
      reloadFromDatabase();
      return;
    }
    const QModelIndex topLeft = index(row, 0);
    const QModelIndex bottomRight = index(row, columnCount() - 1);
    emit dataChanged(topLeft, bottomRight, QList<int>{});
  };

  connect(&anime::db, &anime::Database::entryUpdated, this,
          [this, emitRowOrReloadCatalog](int id) {
            if (m_source == AnimeListModelSource::ListEntriesByLastUpdated) {
              reloadFromDatabase();
              return;
            }
            emitRowOrReloadCatalog(id);
          });
  connect(&anime::db, &anime::Database::itemUpdated, this, [this, emitRowOrReloadCatalog](int id) {
    if (m_source == AnimeListModelSource::ListEntriesByLastUpdated) {
      if (m_ids.indexOf(id) < 0) {
        reloadFromDatabase();
        return;
      }
      emitRowOrReloadCatalog(id);
      return;
    }
    emitRowOrReloadCatalog(id);
  });
}

int AnimeListModel::rowCount(const QModelIndex&) const {
  return m_ids.size();
}

int AnimeListModel::columnCount(const QModelIndex&) const {
  return NUM_COLUMNS;
}

QVariant AnimeListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};

  const auto anime = getAnime(index);
  if (!anime) return {};

  const auto entry = getListEntry(index);

  switch (role) {
    case Qt::DisplayRole:
      switch (index.column()) {
        case COLUMN_TITLE:
          return QString::fromStdString(
              anime::preferredListTitleString(*anime, taiga::settings.listTitleLanguage()));
        case COLUMN_DURATION:
          return formatEpisodeLength(anime->episode_length);
        case COLUMN_REWATCHES:
          if (entry) return entry->rewatched_times;
          break;
        case COLUMN_SCORE:
          if (entry) return formatListScore(entry->score);
          break;
        case COLUMN_AVERAGE:
          return formatScore(anime->score);
        case COLUMN_TYPE:
          return formatType(anime->type);
        case COLUMN_SEASON:
          return formatSeason(anime::Season(anime->date_started));
        case COLUMN_STARTED:
          if (entry) return formatFuzzyDate(entry->date_started);
          break;
        case COLUMN_COMPLETED:
          if (entry) return formatFuzzyDate(entry->date_completed);
          break;
        case COLUMN_LAST_UPDATED:
          if (entry) return formatAsRelativeTime(entry->last_updated, "-");
          break;
        case COLUMN_NOTES:
          if (entry) return QString::fromStdString(entry->notes);
          break;
        case COLUMN_WATCH_ORDER_GUIDE:
          return {};
      }
      break;

    case Qt::ToolTipRole:
      switch (index.column()) {
        case COLUMN_TITLE: {
          QStringList lines;
          if (!anime->titles.romaji.empty()) {
            lines += QCoreApplication::translate("AnimeListModel", "Romaji: %1")
                         .arg(QString::fromStdString(anime->titles.romaji));
          }
          if (!anime->titles.english.empty()) {
            lines += QCoreApplication::translate("AnimeListModel", "English: %1")
                         .arg(QString::fromStdString(anime->titles.english));
          }
          if (!anime->titles.japanese.empty()) {
            lines += QCoreApplication::translate("AnimeListModel", "Native: %1")
                         .arg(QString::fromStdString(anime->titles.japanese));
          }
          return lines.join(QLatin1Char('\n'));
        }
        case COLUMN_SEASON:
          return formatFuzzyDate(anime->date_started);
        case COLUMN_LAST_UPDATED:
          if (entry) return formatTimestamp(entry->last_updated);
          break;
        case COLUMN_NOTES:
          if (entry) return QString::fromStdString(entry->notes);
          break;
        case COLUMN_WATCH_ORDER_GUIDE:
          return QCoreApplication::translate("AnimeListModel",
                                             "Open the watch order guide for this title");
      }
      break;

    case Qt::TextAlignmentRole: {
      switch (index.column()) {
        case COLUMN_PROGRESS:
        case COLUMN_REWATCHES:
        case COLUMN_SCORE:
        case COLUMN_AVERAGE:
        case COLUMN_TYPE:
          return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
        case COLUMN_DURATION:
        case COLUMN_SEASON:
        case COLUMN_STARTED:
        case COLUMN_COMPLETED:
        case COLUMN_LAST_UPDATED:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case COLUMN_WATCH_ORDER_GUIDE:
          return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
        default:
          return {};
      }
      break;
    }

    case Qt::ForegroundRole: {
      const auto disabledTextColor =
          qApp->palette().color(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Text);
      switch (index.column()) {
        case COLUMN_TITLE: {
          // Items not in the user's list (e.g. catalog results loaded via "Load season")
          // are shown in a muted slate-blue so they are visually distinct from list entries.
          if (!entry) return qApp->palette().color(QPalette::PlaceholderText);

          // Priority 1 – "Seen / caught up": all aired episodes watched.
          // Use a muted green that reads well on both light and dark themes.
          const int last_aired = anime->last_aired_episode;
          const int watched = entry->watched_episodes;
          const bool caught_up = last_aired > 0 && watched >= last_aired;
          const bool fully_done = anime->episode_count > 0 && watched >= anime->episode_count;
          if (caught_up || fully_done) {
            return QColor(0x4c, 0xaf, 0x50);  // material green
          }

          // Priority 2 – "Downloaded / ready to watch": next episode file is on disk.
          if (taiga::settings.listHighlightNextEpisodeOnDisk() &&
              track::nextEpisodeIsOnDisk(anime->id, anime, entry)) {
            return QColor(0x42, 0xa5, 0xf5);  // material blue
          }

          // Priority 3 – "Released but not yet downloaded": a new episode aired
          // but is not on disk yet.
          if (last_aired > watched) {
            return QColor(0x9e, 0x9e, 0x9e);  // material grey
          }
          break;
        }
        case COLUMN_AVERAGE:
          if (!anime->score) return disabledTextColor;
          break;
        case COLUMN_DURATION:
          if (anime->episode_length < 1) return disabledTextColor;
          break;
        case COLUMN_SEASON:
          if (!anime->date_started) return disabledTextColor;
          break;
        case COLUMN_TYPE:
          if (anime->type == anime::Type::Unknown) return disabledTextColor;
          break;
        case COLUMN_SCORE:
          if (entry && !entry->score) return disabledTextColor;
          break;
        case COLUMN_STARTED:
          if (entry && !entry->date_started) return disabledTextColor;
          break;
        case COLUMN_COMPLETED:
          if (entry && !entry->date_completed) return disabledTextColor;
          break;
        case COLUMN_LAST_UPDATED:
          if (entry && !entry->last_updated) return disabledTextColor;
          break;
      }
      break;
    }

    case static_cast<int>(AnimeListItemDataRole::Anime): {
      return QVariant::fromValue(anime);
    }
    case static_cast<int>(AnimeListItemDataRole::ListEntry): {
      return QVariant::fromValue(entry);
    }
    case static_cast<int>(AnimeListItemDataRole::Poster): {
      // Pass QPixmap by value — `const QPixmap*` in QVariant is not reliably registered, so
      // delegates often got nullptr and drew no poster (Cards view). QPixmap is implicitly shared.
      return QVariant::fromValue(*imageProvider.loadPoster(anime->id));
    }
  }

  return {};
}

bool AnimeListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (!index.isValid() || role != Qt::EditRole) return false;
  if (index.column() != COLUMN_SCORE) return false;

  const int id = m_ids.at(index.row());
  const auto it = anime::db.entries().find(id);
  if (it == anime::db.entries().end()) return false;

  ListEntry entry = *it;
  entry.score = value.toInt();
  entry.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  commitListEntry(entry);

  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
  return true;
}

void AnimeListModel::reloadFromDatabase() {
  beginResetModel();
  rebuildIdList();
  endResetModel();
}

void AnimeListModel::emitTitleColumnDataChanged() {
  if (m_ids.isEmpty()) return;
  const QModelIndex top_left = index(0, COLUMN_TITLE);
  const QModelIndex bottom_right = index(m_ids.size() - 1, COLUMN_TITLE);
  emit dataChanged(top_left, bottom_right, {Qt::DisplayRole, Qt::ToolTipRole});
}

void AnimeListModel::emitProgressColumnDataChanged() {
  if (m_ids.isEmpty()) return;
  const int last = m_ids.size() - 1;
  emit dataChanged(index(0, COLUMN_PROGRESS), index(last, COLUMN_PROGRESS), {Qt::DisplayRole});
  // Card views use column 0 only; table list uses COLUMN_PROGRESS for the bar.
  emit dataChanged(index(0, 0), index(last, 0), {Qt::DisplayRole});
}

void AnimeListModel::emitNewEpisodeHighlightDataChanged() {
  if (m_ids.isEmpty()) return;
  const int last = m_ids.size() - 1;
  emit dataChanged(index(0, COLUMN_TITLE), index(last, COLUMN_TITLE), {Qt::ForegroundRole});
  emit dataChanged(index(0, 0), index(last, 0), {Qt::ForegroundRole});
}

void AnimeListModel::refreshNewEpisodeHighlightDisplay() {
  if (m_source == AnimeListModelSource::ListEntriesByLastUpdated) {
    reloadFromDatabase();
    return;
  }
  emitNewEpisodeHighlightDataChanged();
}

QVariant AnimeListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  switch (role) {
    case Qt::DisplayRole: {
      // clang-format off
      switch (section) {
        case COLUMN_TITLE: return tr("Title");
        case COLUMN_PROGRESS: return tr("Progress");
        case COLUMN_DURATION: return tr("Duration");
        case COLUMN_REWATCHES: return tr("Rewatches");
        case COLUMN_SCORE: return tr("Score");
        case COLUMN_AVERAGE: return tr("Average");
        case COLUMN_TYPE: return tr("Type");
        case COLUMN_SEASON: return tr("Season");
        case COLUMN_STARTED: return tr("Started");
        case COLUMN_COMPLETED: return tr("Completed");
        case COLUMN_LAST_UPDATED: return tr("Last updated");
        case COLUMN_NOTES: return tr("Notes");
        case COLUMN_WATCH_ORDER_GUIDE: return tr("Guide");
      }
      // clang-format on
      break;
    }

    case Qt::ToolTipRole: {
      if (orientation == Qt::Horizontal && section == COLUMN_WATCH_ORDER_GUIDE) {
        return tr("Open the watch order guide for this title");
      }
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (section) {
        case COLUMN_PROGRESS:
        case COLUMN_REWATCHES:
        case COLUMN_SCORE:
        case COLUMN_AVERAGE:
        case COLUMN_TYPE:
          return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
        case COLUMN_DURATION:
        case COLUMN_SEASON:
        case COLUMN_STARTED:
        case COLUMN_COMPLETED:
        case COLUMN_LAST_UPDATED:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case COLUMN_WATCH_ORDER_GUIDE:
          return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
      }
      break;
    }

    case Qt::InitialSortOrderRole: {
      switch (section) {
        case COLUMN_PROGRESS:
        case COLUMN_DURATION:
        case COLUMN_REWATCHES:
        case COLUMN_SCORE:
        case COLUMN_AVERAGE:
        case COLUMN_SEASON:
        case COLUMN_STARTED:
        case COLUMN_COMPLETED:
        case COLUMN_LAST_UPDATED:
          return Qt::DescendingOrder;
        case COLUMN_WATCH_ORDER_GUIDE:
          return Qt::AscendingOrder;
        default:
          return Qt::AscendingOrder;
      }
      break;
    }
  }

  return QAbstractListModel::headerData(section, orientation, role);
}

Qt::ItemFlags AnimeListModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;

  const auto base = QAbstractListModel::flags(index);
  if (index.column() == COLUMN_WATCH_ORDER_GUIDE) return base;
  return base | Qt::ItemIsEditable;
}

const Anime* AnimeListModel::getAnime(const QModelIndex& index) const {
  if (!index.isValid()) return nullptr;
  return anime::db.item(m_ids.at(index.row()));
}

const ListEntry* AnimeListModel::getListEntry(const QModelIndex& index) const {
  if (!index.isValid()) return nullptr;
  return anime::db.entry(m_ids.at(index.row()));
}

}  // namespace gui
