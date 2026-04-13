/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#include "torrent_rss_proxy_model.hpp"

#include <QAbstractItemModel>

#include "gui/models/torrent_rss_model.hpp"
#include "taiga/settings.hpp"

namespace gui {

TorrentRssProxyModel::TorrentRssProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  setDynamicSortFilter(true);
}

void TorrentRssProxyModel::setShowBatches(const bool show) {
  if (m_show_batches == show) return;
  m_show_batches = show;
  invalidateFilter();
}

bool TorrentRssProxyModel::showBatches() const {
  return m_show_batches;
}

void TorrentRssProxyModel::setFilterText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (m_filter_text == trimmed) return;
  m_filter_text = trimmed;
  invalidateFilter();
}

QString TorrentRssProxyModel::filterText() const {
  return m_filter_text;
}

void TorrentRssProxyModel::refresh() {
  invalidateFilter();
}

bool TorrentRssProxyModel::filterAcceptsRow(const int source_row,
                                            const QModelIndex& source_parent) const {
  const QModelIndex idx0 = sourceModel()->index(source_row, 0, source_parent);
  const bool is_batch =
      sourceModel()->data(idx0, TorrentRssModel::IsBatchRole).toBool();
  if (is_batch != m_show_batches) return false;

  const QString title_trimmed = sourceModel()->data(idx0, Qt::DisplayRole).toString().trimmed();
  if (!title_trimmed.isEmpty()) {
    const QStringList archived = taiga::settings.torrentFeedDiscardedTitleArchive();
    if (archived.contains(title_trimmed)) return false;
  }

  if (m_filter_text.isEmpty()) return true;

  for (int c = 0; c < TorrentRssModel::NUM_COLUMNS; ++c) {
    const QModelIndex idx = sourceModel()->index(source_row, c, source_parent);
    const QString s = sourceModel()->data(idx, Qt::DisplayRole).toString();
    if (s.contains(m_filter_text, Qt::CaseInsensitive)) return true;
  }

  // Also match URLs even if not shown (magnet/torrent/page).
  const QString page = sourceModel()->data(idx0, TorrentRssModel::PageUrlRole).toString();
  if (page.contains(m_filter_text, Qt::CaseInsensitive)) return true;
  const QString tor = sourceModel()->data(idx0, TorrentRssModel::TorrentUrlRole).toString();
  if (tor.contains(m_filter_text, Qt::CaseInsensitive)) return true;
  const QString mag = sourceModel()->data(idx0, TorrentRssModel::MagnetUrlRole).toString();
  if (mag.contains(m_filter_text, Qt::CaseInsensitive)) return true;

  return false;
}

bool TorrentRssProxyModel::lessThan(const QModelIndex& source_left,
                                   const QModelIndex& source_right) const {
  if (source_left.column() != source_right.column()) {
    return QSortFilterProxyModel::lessThan(source_left, source_right);
  }

  const int col = source_left.column();
  switch (col) {
    case TorrentRssModel::COLUMN_PUBLISHED: {
      const qint64 l = sourceModel()->data(source_left, TorrentRssModel::PublishedMsRole).toLongLong();
      const qint64 r = sourceModel()->data(source_right, TorrentRssModel::PublishedMsRole).toLongLong();
      return l < r;
    }
    case TorrentRssModel::COLUMN_EP: {
      const int l = sourceModel()->data(source_left, TorrentRssModel::EpisodeRole).toInt();
      const int r = sourceModel()->data(source_right, TorrentRssModel::EpisodeRole).toInt();
      return l < r;
    }
    case TorrentRssModel::COLUMN_SEEDS: {
      const int l = sourceModel()->data(source_left, TorrentRssModel::SeedsRole).toInt();
      const int r = sourceModel()->data(source_right, TorrentRssModel::SeedsRole).toInt();
      return l < r;
    }
    case TorrentRssModel::COLUMN_DOWNLOADS: {
      const int l = sourceModel()->data(source_left, TorrentRssModel::DownloadsRole).toInt();
      const int r = sourceModel()->data(source_right, TorrentRssModel::DownloadsRole).toInt();
      return l < r;
    }
    default:
      return QSortFilterProxyModel::lessThan(source_left, source_right);
  }
}

}  // namespace gui

