/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#pragma once

#include <QSortFilterProxyModel>

namespace gui {

class TorrentRssProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentRssProxyModel)

public:
  explicit TorrentRssProxyModel(QObject* parent = nullptr);
  ~TorrentRssProxyModel() override = default;

  void setShowBatches(bool show);
  bool showBatches() const;

  void setFilterText(const QString& text);
  QString filterText() const;

  void refresh();

protected:
  bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
  bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;

private:
  bool m_show_batches = false;
  QString m_filter_text;
};

}  // namespace gui

