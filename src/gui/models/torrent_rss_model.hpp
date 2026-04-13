/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QUrl>
#include <vector>

namespace gui {

struct TorrentRssRow {
  QString title;
  QString published_text;
  qint64 published_ms = 0;
  QUrl page_url{};
  QString anime;
  int episode = -1;
  QString group;
  QString video;
  int seeds = 0;
  int downloads = 0;
  QString torrent_url;
  QString magnet_url;
  bool is_batch = false;
};

class TorrentRssModel final : public QAbstractTableModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentRssModel)

public:
  enum Column {
    COLUMN_TITLE,
    COLUMN_PUBLISHED,
    COLUMN_PAGE,
    COLUMN_ANIME,
    COLUMN_EP,
    COLUMN_GROUP,
    COLUMN_VIDEO,
    COLUMN_SEEDS,
    COLUMN_DOWNLOADS,
    NUM_COLUMNS,
  };

  enum Role {
    PrimaryUrlRole = Qt::UserRole + 1,
    TorrentUrlRole,
    MagnetUrlRole,
    PageUrlRole,
    PublishedMsRole,
    SeedsRole,
    DownloadsRole,
    EpisodeRole,
    IsBatchRole,
    TitleHintRole,
  };

  explicit TorrentRssModel(QObject* parent = nullptr);
  ~TorrentRssModel() override = default;

  void setRows(std::vector<TorrentRssRow> rows);
  const TorrentRssRow* rowAt(int row) const;

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  std::vector<TorrentRssRow> m_rows;
};

}  // namespace gui

