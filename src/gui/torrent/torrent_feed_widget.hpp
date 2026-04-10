/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <QList>
#include <QQueue>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QWidget>
#include <functional>

#include "base/rss.hpp"

class QLineEdit;
class QNetworkReply;
class QPushButton;
class QTableWidget;
class QListWidget;
class QListWidgetItem;

namespace gui {

/// In-app RSS torrent discovery (fetch search/catalog URLs, show items; open links on demand).
class TorrentFeedWidget final : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentFeedWidget)

public:
  explicit TorrentFeedWidget(QLineEdit* toolbar_query_edit, QWidget* parent = nullptr);

  void runSearch();
  /// Set an alternative search title to retry automatically if the primary search returns 0 results.
  void setSearchFallback(const QString& fallback);
  void refreshCatalogFeed();
  /// Background catalog RSS fetch; silent unless new items vs last session snapshot.
  void runCatalogAutocheckFetch();
  /// Re-apply **Settings → Library** RSS sort (after changing options or refilling the table).
  void resortRssTableFromSettings();
  /// Persist torrent table header layout to `session.json` (call on app close).
  void saveSessionState();

  /// Auto-download: fetch RSS for `search_title`, apply current filters, pick the best-seeded
  /// match and download it using `folder_name` as the subfolder hint.
  /// If the primary search returns no results and `fallback_title` is non-empty, it is tried next.
  /// `on_done(found)` is called when finished (on the GUI thread).
  void downloadBestMatchForTitle(const QString& search_title,
                                 const QString& folder_name,
                                 std::function<void(bool found)> on_done,
                                 const QString& fallback_title = {});

  /// Higher-level auto-download: builds multiple title variants (English, romaji, season-code
  /// abbreviations), tries them sequentially until RSS results are found, and saves the winning
  /// title to settings so future calls for the same anime use it directly.
  /// `anime_id_cache` is the anime DB id used for caching (0 = skip cache).
  void downloadBestMatchWithFallbacks(const QString& english_title,
                                      const QString& romaji_title,
                                      const QString& folder_name,
                                      std::function<void(bool found)> on_done,
                                      int anime_id_cache = 0);

  /// Download ALL missing episodes for an anime.
  /// Tries multiple title variants, parses every RSS item for its episode number,
  /// then downloads the best-seeded version of each episode not yet on disk.
  /// For completed anime with a batch torrent available and ≥3 missing episodes, prefers the batch.
  /// `on_done(downloaded_count)` called when all queued; downloaded_count = items sent.
  void downloadAllEpisodesForAnime(int anime_id,
                                   const QString& english_title,
                                   const QString& romaji_title,
                                   const QString& folder_name,
                                   std::function<void(int downloaded)> on_done);

private:
  enum class FetchKind { None, SearchRss, CatalogManual, CatalogAutocheck };
  enum class BgRssOp { None, BestMatch, BatchEpisodes };

  struct BestMatchWaiter {
    std::function<void(bool found)> on_done;
    QString folder_name;
    QString fallback_title;
  };

  struct PendingTorrentSave {
    QUrl url{};
    QString title_hint;
    QString target_path;
    int ui_row = -1;  // row in queue list widget
  };

  void cancelPending();
  /// Aborts `m_bg_fetch_reply_` and fails any queued best-match waiters (superseded fetches).
  void abortBackgroundRss();
  void deliverBestMatchFromFiltered(const QList<const rss::Item*>& filtered,
                                    const QString& folder_name,
                                    std::function<void(bool found)> on_done);
  void startFetch(const QUrl& url, const QString& status_message, FetchKind kind);
  void onFetchFinished(QNetworkReply* reply);
  void populateTable(const rss::Feed& feed);
  void applyCatalogFingerprintState(const rss::Feed& feed, bool notify_if_new);
  void applyRssTableSortFromSettings();
  void applyResultFilter();
  static QString primaryUrlForRow(int row, const QTableWidget* table);
  void cancelSaveTorrent();
  void enqueueSaveTorrent(const QUrl& url, const QString& title_hint);
  void startNextQueuedSave();
  void setQueueRowStatus(int row, const QString& status, bool error);
  void beginSaveTorrent(const QUrl& url, const QString& title_hint);

  QNetworkReply* m_bg_fetch_reply_ = nullptr;
  BgRssOp m_bg_rss_op_ = BgRssOp::None;
  QString m_bg_best_match_key_;
  QVector<BestMatchWaiter> m_bg_best_match_waiters_;
  QNetworkReply* m_qbit_login_reply_ = nullptr;

  /// qBittorrent Web API: send a magnet/torrent URL with a target save path.
  /// Authenticates first if username/password are set, then POSTs to /api/v2/torrents/add.
  void addTorrentViaQBitApi(const QString& torrent_url, const QString& save_path,
                             std::function<void(bool ok, QString error)> on_done);

  QLineEdit* m_query_edit_ = nullptr;
  QLineEdit* m_filter_edit_ = nullptr;
  QPushButton* m_btn_fetch_ = nullptr;
  QPushButton* m_btn_browser_ = nullptr;
  QPushButton* m_btn_catalog_ = nullptr;
  QPushButton* m_btn_download_selected_ = nullptr;
  QPushButton* m_btn_download_best_ = nullptr;
  QPushButton* m_btn_cancel_downloads_ = nullptr;
  QPushButton* m_btn_clear_queue_ = nullptr;
  QTableWidget* m_table_ = nullptr;
  QListWidget* m_queue_list_ = nullptr;
  QNetworkReply* m_pending_ = nullptr;
  QNetworkReply* m_save_reply_ = nullptr;
  QQueue<PendingTorrentSave> m_save_queue_;
  int m_save_queue_total_ = 0;
  QString m_save_queue_dir_;
  FetchKind m_active_fetch_ = FetchKind::None;
  // When the primary search returns 0 results, the fallback title is tried once.
  QString m_search_fallback_title_;
  QString m_catalog_autocheck_last_ok_url_;
  qint64 m_catalog_autocheck_last_ok_ms_ = 0;
};

}  // namespace gui
