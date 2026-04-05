/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <QWidget>

#include "base/rss.hpp"

class QLineEdit;
class QNetworkReply;
class QPushButton;
class QTableWidget;

namespace gui {

/// In-app RSS torrent discovery (Taiga v1 parity: fetch search/catalog URLs, show items; open links on
/// demand).
class TorrentFeedWidget final : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentFeedWidget)

public:
  explicit TorrentFeedWidget(QLineEdit* toolbar_query_edit, QWidget* parent = nullptr);

  void runSearch();
  void refreshCatalogFeed();

private:
  void cancelPending();
  void startFetch(const QUrl& url, const QString& status_message);
  void onFetchFinished(QNetworkReply* reply);
  void populateTable(const rss::Feed& feed);
  static QString primaryUrlForRow(int row, const QTableWidget* table);

  QLineEdit* m_query_edit_ = nullptr;
  QPushButton* m_btn_fetch_ = nullptr;
  QPushButton* m_btn_browser_ = nullptr;
  QPushButton* m_btn_catalog_ = nullptr;
  QTableWidget* m_table_ = nullptr;
  QNetworkReply* m_pending_ = nullptr;
};

}  // namespace gui
