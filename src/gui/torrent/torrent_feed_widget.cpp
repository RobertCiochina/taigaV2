/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "torrent_feed_widget.hpp"

#include <QAbstractItemView>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
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
#include <QShortcut>
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

#include <algorithm>
#include <optional>
#include <string>

#include <QChar>
#include <QSet>

namespace gui {

namespace {
constexpr int kTableMagnetDataRole = Qt::UserRole + 7;
constexpr int kCatalogFingerprintCap = 100;

QString fingerprintForItem(const rss::Item& it) {
  if (!it.guid.value.empty()) {
    return QString::fromStdString(it.guid.value);
  }
  if (!it.link.empty()) {
    return QString::fromStdString(it.link);
  }
  return QString::fromStdString(it.title) + QChar(0x1E) + QString::fromStdString(it.pub_date);
}

QString sanitizedTorrentBaseName(QString title) {
  title = title.trimmed();
  for (const QChar c : QStringLiteral("\\/:*?\"<>|")) {
    title.replace(c, u'_');
  }
  if (title.isEmpty()) {
    title = QStringLiteral("torrent");
  }
  return title.left(120);
}

std::optional<QUrl> httpUrlFromUserString(const QString& s) {
  if (s.isEmpty()) return {};
  const QUrl u = QUrl::fromUserInput(s);
  if (!u.isValid()) return {};
  const QString sch = u.scheme().toLower();
  if (sch != u"http" && sch != u"https") return {};
  return u;
}

void openPrimaryTorrentUrl(const QString& url) {
  if (url.isEmpty()) return;
  const bool custom_client = taiga::settings.torrentAppOpen() && taiga::settings.torrentAppMode() == 2;
  const QString exe = QString::fromStdString(taiga::settings.torrentAppExecutablePath());
  if (custom_client && !exe.isEmpty() && QFileInfo::exists(exe)) {
    if (QProcess::startDetached(exe, QStringList{url})) {
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(
            QCoreApplication::translate("TorrentFeedWidget", "Launched torrent client."), 2500);
      }
      return;
    }
    taiga::userFeedback(QCoreApplication::translate(
                            "TorrentFeedWidget",
                            "Could not start the torrent client executable. Using the default URL handler "
                            "instead."),
                        true);
  }
  if (!QDesktopServices::openUrl(QUrl::fromUserInput(url))) {
    taiga::userFeedback(QCoreApplication::translate("TorrentFeedWidget", "Could not open the URL."), true);
  }
}
}  // namespace

TorrentFeedWidget::TorrentFeedWidget(QLineEdit* toolbar_query_edit, QWidget* parent)
    : QWidget(parent), m_query_edit_(toolbar_query_edit) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* hint = new QLabel(
      tr("Results load inside Taiga. Double-click opens the <b>primary</b> download link (magnet vs "
         ".torrent order follows the v1 prefer-magnet checkbox in Settings → Library). When a "
         "<b>custom torrent client</b> is configured, that executable receives the link instead of the "
         "default OS handler. Column headers sort the table; their layout is remembered between "
         "sessions. <b>F5</b> fetches search RSS; <b>Ctrl+F5</b> refreshes the catalog feed. If enabled "
         "in Settings, the catalog RSS also refreshes periodically in the background. <b>Ctrl+C</b> "
         "copies the primary link for the current row. The filter text is remembered between sessions; "
         "<b>Esc</b> in the filter field clears it. Use the toolbar search field, then <b>Fetch RSS</b> "
         "or <b>Enter</b>."),
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

  {
    auto* fr = new QHBoxLayout();
    fr->addWidget(new QLabel(tr("Filter results:"), this));
    m_filter_edit_ = new QLineEdit(this);
    m_filter_edit_->setClearButtonEnabled(true);
    m_filter_edit_->setPlaceholderText(tr("Substring match on title, dates, URLs…"));
    m_filter_edit_->setText(taiga::session.torrentPanelResultFilter());
    fr->addWidget(m_filter_edit_, 1);
    layout->addLayout(fr);
    connect(m_filter_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
      taiga::session.setTorrentPanelResultFilter(text);
      applyResultFilter();
    });
    auto* sc_esc = new QShortcut(QKeySequence{Qt::Key_Escape}, m_filter_edit_);
    sc_esc->setContext(Qt::WidgetShortcut);
    connect(sc_esc, &QShortcut::activated, this, [this]() { m_filter_edit_->clear(); });
  }

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
  m_table_->setSortingEnabled(true);
  layout->addWidget(m_table_, 1);

  if (const QByteArray hdr = taiga::session.torrentRssTableHeaderState(); !hdr.isEmpty()) {
    m_table_->horizontalHeader()->restoreState(hdr);
  }

  {
    auto* sc_refresh = new QShortcut(QKeySequence::Refresh, this);
    sc_refresh->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_refresh, &QShortcut::activated, this, &TorrentFeedWidget::runSearch);
    auto* sc_cat = new QShortcut(QKeySequence{Qt::CTRL | Qt::Key_F5}, this);
    sc_cat->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_cat, &QShortcut::activated, this, &TorrentFeedWidget::refreshCatalogFeed);
    auto* sc_copy = new QShortcut(QKeySequence::Copy, m_table_);
    sc_copy->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_copy, &QShortcut::activated, this, [this]() {
      const int row = m_table_->currentRow();
      if (row < 0) return;
      const QString url = primaryUrlForRow(row, m_table_);
      if (url.isEmpty()) return;
      QGuiApplication::clipboard()->setText(url);
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Copied primary link to clipboard."), 2500);
      }
    });
  }

  connect(m_btn_fetch_, &QPushButton::clicked, this, &TorrentFeedWidget::runSearch);
  connect(m_btn_browser_, &QPushButton::clicked, this, [this]() {
    if (!m_query_edit_) return;
    taiga::openTorrentDiscoverySearch(m_query_edit_->text().trimmed());
  });
  connect(m_btn_catalog_, &QPushButton::clicked, this, &TorrentFeedWidget::refreshCatalogFeed);

  connect(m_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    openPrimaryTorrentUrl(primaryUrlForRow(row, m_table_));
  });

  connect(m_table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    const QModelIndex idx = m_table_->indexAt(pos);
    if (!idx.isValid()) return;
    const int row = idx.row();
    const QString page_u = m_table_->item(row, 2) ? m_table_->item(row, 2)->text() : QString{};
    const QTableWidgetItem* c3 = m_table_->item(row, 3);
    const QString tor_u = c3 ? c3->text() : QString{};
    const QVariant mag_v = c3 ? c3->data(kTableMagnetDataRole) : QVariant{};
    const QString magnet_u = mag_v.isValid() ? mag_v.toString() : QString{};

    auto* menu = new QMenu(this);
    const QString client_dl = QString::fromStdString(taiga::settings.torrentClientDownloadPath());
    const QString torrent_save = QString::fromStdString(taiga::settings.torrentFileSavePath());
    const QString client_abs = client_dl.isEmpty() ? QString{} : QDir{client_dl}.absolutePath();
    const QString save_abs = torrent_save.isEmpty() ? QString{} : QDir{torrent_save}.absolutePath();
    if (!client_abs.isEmpty() && QDir{client_dl}.exists()) {
      menu->addAction(tr("Open client download folder"), this, [client_dl]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir{client_dl}.absolutePath()));
      });
    }
    if (!save_abs.isEmpty() && QDir{torrent_save}.exists() && save_abs != client_abs) {
      menu->addAction(tr("Open .torrent save folder"), this, [torrent_save]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir{torrent_save}.absolutePath()));
      });
    }
    if (!menu->actions().isEmpty()) {
      menu->addSeparator();
    }
    const QString primary = primaryUrlForRow(row, m_table_);
    if (!primary.isEmpty()) {
      menu->addAction(tr("Open primary link"), this, [primary]() { openPrimaryTorrentUrl(primary); });
      menu->addAction(tr("Copy primary link"), this, [primary]() {
        QGuiApplication::clipboard()->setText(primary);
      });
    }
    if (!magnet_u.isEmpty() && magnet_u != tor_u && !tor_u.isEmpty()) {
      menu->addAction(tr("Open .torrent URL"), this, [tor_u]() {
        QDesktopServices::openUrl(QUrl::fromUserInput(tor_u));
      });
      menu->addAction(tr("Copy .torrent URL"), this, [tor_u]() {
        QGuiApplication::clipboard()->setText(tor_u);
      });
    }
    if (const auto tor_http = httpUrlFromUserString(tor_u)) {
      const QString title_hint = m_table_->item(row, 0) ? m_table_->item(row, 0)->text() : QString{};
      menu->addAction(tr("Save .torrent file…"), this, [this, url = *tor_http, title_hint]() {
        beginSaveTorrent(url, title_hint);
      });
    }
    if (taiga::settings.torrentAppOpen() && taiga::settings.torrentAppMode() == 2) {
      const QString exe = QString::fromStdString(taiga::settings.torrentAppExecutablePath());
      if (!exe.isEmpty() && QFileInfo::exists(exe)) {
        QString link_for_client = !magnet_u.isEmpty() ? magnet_u : tor_u;
        if (link_for_client.isEmpty()) {
          link_for_client = primary;
        }
        if (!link_for_client.isEmpty()) {
          menu->addAction(tr("Launch configured torrent client with this link"), this,
                          [this, exe, link_for_client]() {
                            if (!QProcess::startDetached(exe, QStringList{link_for_client})) {
                              taiga::userFeedback(
                                  tr("Could not start the torrent client executable. Check the path in "
                                     "Settings."),
                                  true);
                            }
                          });
        }
      }
    }
    if (!page_u.isEmpty()) {
      menu->addAction(tr("Open info page in browser"), this, [page_u]() {
        QDesktopServices::openUrl(QUrl::fromUserInput(page_u));
      });
      menu->addAction(tr("Copy page URL"), this, [page_u]() {
        QGuiApplication::clipboard()->setText(page_u);
      });
    }
    menu->addSeparator();
    menu->addAction(tr("Copy row (tab-separated)"), this, [this, row]() {
      const auto cell = [this, row](const int col) {
        if (const QTableWidgetItem* it = m_table_->item(row, col)) return it->text();
        return QString{};
      };
      const QString title = cell(0);
      const QString pub = cell(1);
      const QString page = cell(2);
      QString tor_col = cell(3);
      if (const QTableWidgetItem* c3 = m_table_->item(row, 3)) {
        const QVariant mag = c3->data(kTableMagnetDataRole);
        if (mag.isValid() && !mag.toString().isEmpty()) {
          tor_col = mag.toString() + QStringLiteral("\t") + tor_col;
        }
      }
      const QString line = title + QLatin1Char('\t') + pub + QLatin1Char('\t') + page +
                           QLatin1Char('\t') + tor_col;
      QGuiApplication::clipboard()->setText(line);
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Copied row to clipboard."), 2500);
      }
    });
    menu->exec(m_table_->viewport()->mapToGlobal(pos));
  });
}

void TorrentFeedWidget::saveSessionState() {
  if (!m_table_) return;
  taiga::session.setTorrentRssTableHeaderState(m_table_->horizontalHeader()->saveState());
}

void TorrentFeedWidget::cancelPending() {
  cancelSaveTorrent();
  if (m_pending_) {
    m_pending_->disconnect();
    m_pending_->abort();
    m_pending_->deleteLater();
    m_pending_ = nullptr;
  }
}

void TorrentFeedWidget::cancelSaveTorrent() {
  if (m_save_reply_) {
    m_save_reply_->disconnect();
    m_save_reply_->abort();
    m_save_reply_->deleteLater();
    m_save_reply_ = nullptr;
  }
}

void TorrentFeedWidget::beginSaveTorrent(const QUrl& url, const QString& title_hint) {
  if (!url.isValid()) return;

  QString file_name = QFileInfo(url.path()).fileName();
  if (file_name.isEmpty() || !file_name.endsWith(u".torrent", Qt::CaseInsensitive)) {
    file_name = sanitizedTorrentBaseName(title_hint) + u".torrent";
  }

  const QString save_dir = QString::fromStdString(taiga::settings.torrentFileSavePath());
  QString full_path;
  if (!save_dir.isEmpty()) {
    const QDir dir(save_dir);
    if (dir.exists()) {
      full_path = dir.filePath(file_name);
    }
  }
  if (full_path.isEmpty()) {
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString def = downloads.isEmpty() ? file_name : QDir(downloads).filePath(file_name);
    full_path = QFileDialog::getSaveFileName(this, tr("Save .torrent file"), def,
                                             tr("Torrent files") + u" (*.torrent);;" + tr("All files") + u" (*)");
  }
  if (full_path.isEmpty()) return;

  cancelSaveTorrent();
  if (auto* mw = mainWindow()) {
    mw->statusBar()->showMessage(tr("Downloading .torrent file…"));
  }

  QNetworkRequest req{url};
  taiga::applyCommonHeaders(req);
  m_save_reply_ = taiga::network()->get(req);
  connect(m_save_reply_, &QNetworkReply::finished, this, [this, full_path] {
    QNetworkReply* reply = m_save_reply_;
    m_save_reply_ = nullptr;
    if (!reply) return;
    reply->deleteLater();
    if (auto* mw = mainWindow()) {
      mw->statusBar()->clearMessage();
    }
    if (reply->error() != QNetworkReply::NoError) {
      taiga::userFeedback(tr("Could not download .torrent file: %1").arg(reply->errorString()), true);
      return;
    }
    const QByteArray body = reply->readAll();
    const QByteArray t = body.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      taiga::userFeedback(tr("The server returned a web page, not a .torrent file."), true);
      return;
    }
    QFile f(full_path);
    if (!f.open(QIODevice::WriteOnly)) {
      taiga::userFeedback(tr("Could not write to %1").arg(full_path), true);
      return;
    }
    f.write(body);
    f.close();
    taiga::userFeedback(tr("Saved %1").arg(full_path), false);
    if (auto* mw = mainWindow()) {
      mw->statusBar()->showMessage(tr("Saved torrent file."), 5000);
    }
  });
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
  startFetch(url, tr("Fetching torrent RSS…"), FetchKind::SearchRss);
}

void TorrentFeedWidget::refreshCatalogFeed() {
  const QString src = QString::fromStdString(taiga::settings.torrentDiscoveryFeedSourceUrl());
  const QUrl url = taiga::torrentDiscoveryCatalogFeedUrl(src);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
    taiga::userFeedback(tr("Invalid catalog RSS URL in settings."), true);
    return;
  }
  startFetch(url, tr("Fetching catalog RSS…"), FetchKind::CatalogManual);
}

void TorrentFeedWidget::runCatalogAutocheckFetch() {
  const QString src = QString::fromStdString(taiga::settings.torrentDiscoveryFeedSourceUrl());
  const QUrl url = taiga::torrentDiscoveryCatalogFeedUrl(src);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
    return;
  }
  startFetch(url, {}, FetchKind::CatalogAutocheck);
}

void TorrentFeedWidget::startFetch(const QUrl& url, const QString& status_message,
                                   const FetchKind kind) {
  cancelPending();
  m_active_fetch_ = kind;
  if (auto* mw = mainWindow()) {
    if (!status_message.isEmpty()) {
      mw->statusBar()->showMessage(status_message);
    } else if (kind != FetchKind::CatalogAutocheck) {
      mw->statusBar()->clearMessage();
    }
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
  const FetchKind kind = m_active_fetch_;
  m_active_fetch_ = FetchKind::None;

  const bool silent = (kind == FetchKind::CatalogAutocheck);
  if (auto* mw = mainWindow()) {
    if (!silent) {
      mw->statusBar()->clearMessage();
    }
  }

  if (reply->error() != QNetworkReply::NoError) {
    if (!silent) {
      taiga::userFeedback(
          tr("Could not download feed: %1").arg(reply->errorString()),
          true);
    }
    return;
  }

  const QByteArray body = reply->readAll();
  {
    const QByteArray t = body.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      if (!silent) {
        taiga::userFeedback(
            tr("The server returned a web page, not an RSS feed. Check the URL in Settings → Library."),
            true);
      }
      return;
    }
  }
  QString err;
  const auto feed = parseSyndicationFeed(body, &err);
  if (!feed) {
    if (!silent) {
      taiga::userFeedback(tr("Could not parse feed: %1").arg(err), true);
    }
    return;
  }
  if (feed->items.empty()) {
    if (!silent) {
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Feed contained no items."), 5000);
      }
    }
  }

  if (kind == FetchKind::CatalogManual || kind == FetchKind::CatalogAutocheck) {
    applyCatalogFingerprintState(*feed, kind == FetchKind::CatalogAutocheck);
  }

  populateTable(*feed);
  if (!silent) {
    if (auto* mw = mainWindow()) {
      const int total = static_cast<int>(feed->items.size());
      const int shown = m_table_ ? m_table_->rowCount() : total;
      QString msg = tr("Loaded %1 item(s).").arg(shown);
      if (shown < total) {
        msg = tr("Showing %1 of %2 item(s) (feed archive limit is on in Settings → Library).")
                  .arg(shown)
                  .arg(total);
      }
      mw->statusBar()->showMessage(msg, 6000);
    }
  }
}

void TorrentFeedWidget::applyCatalogFingerprintState(const rss::Feed& feed, const bool notify_if_new) {
  QStringList keys;
  const size_t n = std::min(feed.items.size(), static_cast<size_t>(kCatalogFingerprintCap));
  keys.reserve(static_cast<int>(n));
  for (size_t i = 0; i < n; ++i) {
    keys.append(fingerprintForItem(feed.items[i]));
  }

  const QStringList old_list = taiga::session.torrentCatalogSeenFingerprints();
  const QSet<QString> old_set(old_list.begin(), old_list.end());
  int fresh = 0;
  for (const QString& k : keys) {
    if (!old_set.contains(k)) {
      ++fresh;
    }
  }

  taiga::session.setTorrentCatalogSeenFingerprints(keys);

  if (notify_if_new && !old_list.isEmpty() && fresh > 0) {
    const auto act = taiga::settings.torrentDiscoveryNewCatalogAction();
    if (act == taiga::TorrentDiscoveryNewCatalogAction::Download) {
      const QString msg =
          tr("Catalog auto-check: %1 new item(s). Download-on-new is selected; automatic queuing is not "
             "available yet — open Torrents to add them.")
              .arg(fresh);
      taiga::userFeedback(msg, false);
    } else {
      const QString msg =
          tr("Catalog auto-check: %1 new item(s). Open the Torrents page to review.").arg(fresh);
      taiga::userFeedback(msg, false);
      if (auto* mw = mainWindow()) {
        mw->postTrayMessage(tr("Taiga"), msg);
      }
    }
  }
}

void TorrentFeedWidget::populateTable(const rss::Feed& feed) {
  m_table_->setSortingEnabled(false);
  m_table_->setRowCount(0);
  size_t n = feed.items.size();
  if (taiga::settings.torrentFeedFilterEnabled()) {
    const int cap = taiga::settings.torrentFeedArchiveMaxItems();
    if (cap > 0 && n > static_cast<size_t>(cap)) n = static_cast<size_t>(cap);
  }
  m_table_->setRowCount(static_cast<int>(n));
  for (int i = 0; i < static_cast<int>(n); ++i) {
    const auto& it = feed.items[static_cast<size_t>(i)];
    m_table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(it.title)));
    m_table_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(it.pub_date)));
    m_table_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(it.link)));
    QString magnet;
    if (const auto m = it.namespace_elements.find(kTorrentFeedMagnetKey);
        m != it.namespace_elements.end()) {
      magnet = QString::fromStdString(m->second);
    }
    const QString tor = QString::fromStdString(it.enclosure.url);
    const QString col3 = !tor.isEmpty() ? tor : magnet;
    auto* c3 = new QTableWidgetItem(col3);
    if (!magnet.isEmpty()) c3->setData(kTableMagnetDataRole, magnet);
    m_table_->setItem(i, 3, c3);
  }
  m_table_->resizeRowsToContents();
  m_table_->setSortingEnabled(true);
  applyRssTableSortFromSettings();
  applyResultFilter();
}

void TorrentFeedWidget::applyRssTableSortFromSettings() {
  if (!m_table_ || m_table_->rowCount() <= 0) return;
  const std::string sb = taiga::settings.torrentRssSortBy();
  int sort_col = 0;
  if (sb == "release_date") sort_col = 1;
  const bool desc = taiga::settings.torrentRssSortOrder() == std::string{"descending"};
  m_table_->sortItems(sort_col, desc ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void TorrentFeedWidget::resortRssTableFromSettings() {
  applyRssTableSortFromSettings();
}

void TorrentFeedWidget::applyResultFilter() {
  if (!m_table_) return;
  const QString needle = m_filter_edit_ ? m_filter_edit_->text().trimmed().toLower() : QString{};
  const bool show_all = needle.isEmpty();
  m_table_->setSortingEnabled(false);
  for (int r = 0; r < m_table_->rowCount(); ++r) {
    if (show_all) {
      m_table_->setRowHidden(r, false);
      continue;
    }
    bool match = false;
    for (int c = 0; c < m_table_->columnCount(); ++c) {
      if (const QTableWidgetItem* it = m_table_->item(r, c)) {
        if (it->text().toLower().contains(needle)) {
          match = true;
          break;
        }
      }
    }
    if (!match) {
      if (const QTableWidgetItem* tor = m_table_->item(r, 3)) {
        const QVariant mag = tor->data(kTableMagnetDataRole);
        if (mag.isValid() && mag.toString().toLower().contains(needle)) match = true;
      }
    }
    m_table_->setRowHidden(r, !match);
  }
  m_table_->setSortingEnabled(true);
  applyRssTableSortFromSettings();
}

QString TorrentFeedWidget::primaryUrlForRow(const int row, const QTableWidget* table) {
  if (!table || row < 0 || row >= table->rowCount()) return {};
  const QTableWidgetItem* c3 = table->item(row, 3);
  const QString tor_text = c3 ? c3->text() : QString{};
  const QVariant mag_v = c3 ? c3->data(kTableMagnetDataRole) : QVariant{};
  const QString magnet_u = mag_v.isValid() ? mag_v.toString() : QString{};
  const bool prefer_magnet = taiga::settings.torrentDownloadUseMagnet();

  if (prefer_magnet) {
    if (!magnet_u.isEmpty()) return magnet_u;
    if (!tor_text.isEmpty()) return tor_text;
  } else {
    if (!tor_text.isEmpty() && !tor_text.startsWith(u"magnet:", Qt::CaseInsensitive)) {
      return tor_text;
    }
    if (!magnet_u.isEmpty()) return magnet_u;
    if (!tor_text.isEmpty()) return tor_text;
  }
  if (const QTableWidgetItem* pg = table->item(row, 2); pg && !pg->text().isEmpty()) {
    return pg->text();
  }
  return {};
}

}  // namespace gui
