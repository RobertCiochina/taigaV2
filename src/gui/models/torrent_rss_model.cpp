/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#include "torrent_rss_model.hpp"

#include <QBrush>

namespace gui {

TorrentRssModel::TorrentRssModel(QObject* parent) : QAbstractTableModel(parent) {}

void TorrentRssModel::setRows(std::vector<TorrentRssRow> rows) {
  beginResetModel();
  m_rows = std::move(rows);
  endResetModel();
}

const TorrentRssRow* TorrentRssModel::rowAt(const int row) const {
  if (row < 0 || row >= static_cast<int>(m_rows.size())) return nullptr;
  return &m_rows[static_cast<size_t>(row)];
}

int TorrentRssModel::rowCount(const QModelIndex&) const {
  return static_cast<int>(m_rows.size());
}

int TorrentRssModel::columnCount(const QModelIndex&) const {
  return NUM_COLUMNS;
}

QVariant TorrentRssModel::data(const QModelIndex& index, const int role) const {
  if (!index.isValid()) return {};
  const TorrentRssRow* r = rowAt(index.row());
  if (!r) return {};

  switch (role) {
    case Qt::DisplayRole: {
      switch (index.column()) {
        case COLUMN_TITLE:
          return r->title;
        case COLUMN_PUBLISHED:
          return r->published_text;
        case COLUMN_PAGE:
          return r->page_url.isValid() ? r->page_url.toString() : QString{};
        case COLUMN_ANIME:
          return r->anime;
        case COLUMN_EP:
          return r->episode >= 0 ? QVariant{r->episode} : QVariant{};
        case COLUMN_GROUP:
          return r->group;
        case COLUMN_VIDEO:
          return r->video;
        case COLUMN_SEEDS:
          return r->seeds ? QVariant{r->seeds} : QVariant{};
        case COLUMN_DOWNLOADS:
          return r->downloads ? QVariant{r->downloads} : QVariant{};
      }
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (index.column()) {
        case COLUMN_EP:
        case COLUMN_SEEDS:
        case COLUMN_DOWNLOADS:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        default:
          return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
      }
    }

    case PrimaryUrlRole:
      return !r->magnet_url.isEmpty() ? r->magnet_url : r->torrent_url;
    case TorrentUrlRole:
      return r->torrent_url;
    case MagnetUrlRole:
      return r->magnet_url;
    case PageUrlRole:
      return r->page_url.isValid() ? r->page_url.toString() : QString{};
    case PublishedMsRole:
      return r->published_ms;
    case SeedsRole:
      return r->seeds;
    case DownloadsRole:
      return r->downloads;
    case EpisodeRole:
      return r->episode;
    case IsBatchRole:
      return r->is_batch;
    case TitleHintRole:
      return r->title;
  }

  return {};
}

QVariant TorrentRssModel::headerData(const int section, const Qt::Orientation orientation,
                                    const int role) const {
  if (orientation != Qt::Horizontal) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }

  switch (role) {
    case Qt::DisplayRole: {
      // clang-format off
      switch (section) {
        case COLUMN_TITLE: return tr("Title");
        case COLUMN_PUBLISHED: return tr("Published");
        case COLUMN_PAGE: return tr("Page");
        case COLUMN_ANIME: return tr("Anime");
        case COLUMN_EP: return tr("Ep");
        case COLUMN_GROUP: return tr("Group");
        case COLUMN_VIDEO: return tr("Video");
        case COLUMN_SEEDS: return tr("Seeds");
        case COLUMN_DOWNLOADS: return tr("Downloads");
      }
      // clang-format on
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (section) {
        case COLUMN_EP:
        case COLUMN_SEEDS:
        case COLUMN_DOWNLOADS:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        default:
          return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
      }
    }

    case Qt::InitialSortOrderRole: {
      // Keep current UX: newest first by default.
      if (section == COLUMN_PUBLISHED) return Qt::DescendingOrder;
      return Qt::AscendingOrder;
    }
  }

  return QAbstractTableModel::headerData(section, orientation, role);
}

}  // namespace gui

