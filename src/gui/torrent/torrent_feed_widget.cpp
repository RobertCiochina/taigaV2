/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "torrent_feed_widget.hpp"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QModelIndex>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "gui/main/main_window.hpp"
#include "gui/utils/rss_feed_parser.hpp"
#include "taiga/network.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "taiga/torrent_discovery.hpp"
#include "taiga/user_feedback.hpp"

namespace gui {

TorrentFeedWidget::TorrentFeedWidget(QLineEdit* toolbar_query_edit, QWidget* parent)
    : QWidget(parent), m_query_edit_(toolbar_query_edit) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* hint = new QLabel(
      tr("Results load inside Taiga. Double-click a row to open the torrent or magnet link; "
         "right-click for more actions. Use your toolbar search field as the query, then "
         "<b>Fetch RSS</b> (or press <b>Enter</b> on the Torrents page)."),
      this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto* row = new QHBoxLayout();
  m_btn_fetch_ = new QPushButton(tr("Fetch RSS"), this);
  m_btn_browser_ = new QPushButton(tr("Open in web browser…"), this);
  m_btn_catalog_ = new QPushButton(tr("Refresh catalog feed…"), this);
  m_btn_catalog_->setToolTip(
      tr("Uses the catalog RSS URL from Settings → Library (Taiga v1: rss/torrent/source/address)."));
  row->addWidget(m_btn_fetch_);
  row->addWidget(m_btn_browser_);
  row->addWidget(m_btn_catalog_);
  row->addStretch();
  layout->addLayout(row);

  m_table_ = new QTableWidget(this);
  m_table_->setColumnCount(4);
  m_table_->setHorizontalHeaderLabels({tr("Title"), tr("Published"), tr("Page"), tr("Torrent")});
  m_table_->horizontalHeader()->setStretchLastSection(true);
  m_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table_->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table_->setAlternatingRowColors(true);
  m_table_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(m_table_, 1);

  connect(m_btn_fetch_, &QPushButton::clicked, this, &TorrentFeedWidget::runSearch);
  connect(m_btn_browser_, &QPushButton::clicked, this, [this]() {
    if (!m_query_edit_) return;
    taiga::openTorrentDiscoverySearch(m_query_edit_->text().trimmed());
  });
  connect(m_btn_catalog_, &QPushButton::clicked, this, &TorrentFeedWidget::refreshCatalogFeed);

  connect(m_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    const QString url = primaryUrlForRow(row, m_table_);
    if (url.isEmpty()) return;
    if (!QDesktopServices::openUrl(QUrl::fromUserInput(url))) {
      taiga::userFeedback(tr("Could not open the URL."), true);
    }
  });

  connect(m_table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    const QModelIndex idx = m_table_->indexAt(pos);
    if (!idx.isValid()) return;
    const int row = idx.row();
    const QString page_u = m_table_->item(row, 2) ? m_table_->item(row, 2)->text() : QString{};
    const QString tor_u = m_table_->item(row, 3) ? m_table_->item(row, 3)->text() : QString{};

    auto* menu = new QMenu(this);
    if (!tor_u.isEmpty()) {
      menu->addAction(tr("Open torrent / magnet"), this, [tor_u]() {
        QDesktopServices::openUrl(QUrl::fromUserInput(tor_u));
      });
      menu->addAction(tr("Copy torrent / magnet URL"), this, [tor_u]() {
        QGuiApplication::clipboard()->setText(tor_u);
      });
    }
    if (!page_u.isEmpty()) {
      menu->addAction(tr("Open info page in browser"), this, [page_u]() {
        QDesktopServices::openUrl(QUrl::fromUserInput(page_u));
      });
      menu->addAction(tr("Copy page URL"), this, [page_u]() {
        QGuiApplication::clipboard()->setText(page_u);
      });
    }
    menu->exec(m_table_->viewport()->mapToGlobal(pos));
  });
}

void TorrentFeedWidget::cancelPending() {
  if (m_pending_) {
    m_pending_->disconnect();
    m_pending_->abort();
    m_pending_->deleteLater();
    m_pending_ = nullptr;
  }
}

void TorrentFeedWidget::runSearch() {
  if (!m_query_edit_) return;
  const QString q = m_query_edit_->text().trimmed();
  if (q.isEmpty()) {
    taiga::userFeedback(tr("Enter a title in the toolbar search field first."), true);
    return;
  }
  taiga::session.setTorrentPanelLastQuery(q);
  const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
  const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, q);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
    taiga::userFeedback(tr("Invalid torrent search URL in settings."), true);
    return;
  }
  startFetch(url, tr("Fetching torrent RSS…"));
}

void TorrentFeedWidget::refreshCatalogFeed() {
  const QString src = QString::fromStdString(taiga::settings.torrentDiscoveryFeedSourceUrl());
  const QUrl url = taiga::torrentDiscoveryCatalogFeedUrl(src);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
    taiga::userFeedback(tr("Invalid catalog RSS URL in settings."), true);
    return;
  }
  startFetch(url, tr("Fetching catalog RSS…"));
}

void TorrentFeedWidget::startFetch(const QUrl& url, const QString& status_message) {
  cancelPending();
  if (auto* mw = mainWindow()) {
    mw->statusBar()->showMessage(status_message);
  }

  QNetworkRequest req{url};
  taiga::applyCommonHeaders(req);
  m_pending_ = taiga::network()->get(req);
  connect(m_pending_, &QNetworkReply::finished, this, [this] {
    QNetworkReply* reply = m_pending_;
    m_pending_ = nullptr;
    if (!reply) return;
    onFetchFinished(reply);
    reply->deleteLater();
  });
}

void TorrentFeedWidget::onFetchFinished(QNetworkReply* reply) {
  if (auto* mw = mainWindow()) {
    mw->statusBar()->clearMessage();
  }

  if (reply->error() != QNetworkReply::NoError) {
    taiga::userFeedback(
        tr("Could not download feed: %1").arg(reply->errorString()),
        true);
    return;
  }

  const QByteArray body = reply->readAll();
  {
    const QByteArray t = body.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      taiga::userFeedback(
          tr("The server returned a web page, not an RSS feed. Check the URL in Settings → Library."),
          true);
      return;
    }
  }
  QString err;
  const auto feed = parseRss2Feed(body, &err);
  if (!feed) {
    taiga::userFeedback(tr("Could not parse RSS: %1").arg(err), true);
    return;
  }
  if (feed->items.empty()) {
    if (auto* mw = mainWindow()) {
      mw->statusBar()->showMessage(tr("Feed contained no items."), 5000);
    }
  }
  populateTable(*feed);
  if (auto* mw = mainWindow()) {
    mw->statusBar()->showMessage(tr("Loaded %1 item(s).").arg(feed->items.size()), 5000);
  }
}

void TorrentFeedWidget::populateTable(const rss::Feed& feed) {
  m_table_->setRowCount(0);
  m_table_->setRowCount(static_cast<int>(feed.items.size()));
  for (int i = 0; i < static_cast<int>(feed.items.size()); ++i) {
    const auto& it = feed.items[static_cast<size_t>(i)];
    m_table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(it.title)));
    m_table_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(it.pub_date)));
    m_table_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(it.link)));
    m_table_->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(it.enclosure.url)));
  }
  m_table_->resizeRowsToContents();
}

QString TorrentFeedWidget::primaryUrlForRow(const int row, const QTableWidget* table) {
  if (!table || row < 0 || row >= table->rowCount()) return {};
  if (const QTableWidgetItem* tor = table->item(row, 3);
      tor && !tor->text().isEmpty()) {
    return tor->text();
  }
  if (const QTableWidgetItem* pg = table->item(row, 2); pg && !pg->text().isEmpty()) {
    return pg->text();
  }
  return {};
}

}  // namespace gui
