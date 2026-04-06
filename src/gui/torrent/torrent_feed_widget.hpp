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
  /// Background catalog RSS fetch (Taiga v1 timer); silent unless new items vs last session snapshot.
  void runCatalogAutocheckFetch();
  /// Re-apply **Settings → Library** RSS sort (after changing options or refilling the table).
  void resortRssTableFromSettings();
  /// Persist torrent table header layout to `session.json` (call on app close).
  void saveSessionState();

private:
  enum class FetchKind { None, SearchRss, CatalogManual, CatalogAutocheck };

  void cancelPending();
  void startFetch(const QUrl& url, const QString& status_message, FetchKind kind);
  void onFetchFinished(QNetworkReply* reply);
  void populateTable(const rss::Feed& feed);
  void applyCatalogFingerprintState(const rss::Feed& feed, bool notify_if_new);
  void applyRssTableSortFromSettings();
  void applyResultFilter();
  static QString primaryUrlForRow(int row, const QTableWidget* table);
  void cancelSaveTorrent();
  void beginSaveTorrent(const QUrl& url, const QString& title_hint);

  QLineEdit* m_query_edit_ = nullptr;
  QLineEdit* m_filter_edit_ = nullptr;
  QPushButton* m_btn_fetch_ = nullptr;
  QPushButton* m_btn_browser_ = nullptr;
  QPushButton* m_btn_catalog_ = nullptr;
  QTableWidget* m_table_ = nullptr;
  QNetworkReply* m_pending_ = nullptr;
  QNetworkReply* m_save_reply_ = nullptr;
  FetchKind m_active_fetch_ = FetchKind::None;
};

}  // namespace gui
