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
#include <QDialog>
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
#include <QListWidget>
#include <QMenu>
#include <QModelIndex>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "gui/main/main_window.hpp"
#include "gui/utils/rss_feed_parser.hpp"
#include "taiga/network.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "taiga/torrent_discovery.hpp"
#include "taiga/user_feedback.hpp"

#include "media/anime_db.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/scanner.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include <QChar>
#include <QSet>

namespace gui {

namespace {
constexpr int kTableMagnetDataRole = Qt::UserRole + 7;
constexpr int kCatalogFingerprintCap = 100;
constexpr int kNumericSortKeyRole = Qt::UserRole + 8;

class NumericSortItem final : public QTableWidgetItem {
public:
  explicit NumericSortItem(const QString& text = {}) : QTableWidgetItem(text) {}

  bool operator<(const QTableWidgetItem& other) const override {
    const QVariant a = data(kNumericSortKeyRole);
    const QVariant b = other.data(kNumericSortKeyRole);
    if (a.isValid() && b.isValid()) {
      return a.toLongLong() < b.toLongLong();
    }
    return QTableWidgetItem::operator<(other);
  }
};

QString fingerprintForItem(const rss::Item& it) {
  if (!it.guid.value.empty()) {
    return QString::fromStdString(it.guid.value);
  }
  if (!it.link.empty()) {
    return QString::fromStdString(it.link);
  }
  return QString::fromStdString(it.title) + QChar(0x1E) + QString::fromStdString(it.pub_date);
}

QString rowTextForItem(const rss::Item& it) {
  const QString title = QString::fromStdString(it.title);
  const QString pub = QString::fromStdString(it.pub_date);
  const QString page = QString::fromStdString(it.link);
  QString magnet;
  if (const auto m = it.namespace_elements.find(kTorrentFeedMagnetKey);
      m != it.namespace_elements.end()) {
    magnet = QString::fromStdString(m->second);
  }
  const QString tor = QString::fromStdString(it.enclosure.url);
  return title + QLatin1Char('\n') + pub + QLatin1Char('\n') + page + QLatin1Char('\n') + tor +
         QLatin1Char('\n') + magnet;
}

QStringList splitRegexLines(const QString& text) {
  QStringList out;
  for (QString line :
       text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
    line = line.trimmed();
    if (!line.isEmpty()) out.push_back(line);
  }
  return out;
}

QList<QRegularExpression> compileRegexList(const QStringList& lines) {
  QList<QRegularExpression> out;
  out.reserve(lines.size());
  for (const QString& s : lines) {
    QRegularExpression re(s, QRegularExpression::CaseInsensitiveOption);
    if (re.isValid()) out.push_back(re);
  }
  return out;
}

QList<const rss::Item*> filterRssItemsBySettings(const rss::Feed& feed) {
  const QStringList includeLines =
      splitRegexLines(QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()));
  const QStringList excludeLines =
      splitRegexLines(QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()));
  const QList<QRegularExpression> include = compileRegexList(includeLines);
  const QList<QRegularExpression> exclude = compileRegexList(excludeLines);

  const bool hide_dropped = taiga::settings.torrentFeedHideDropped();
  const bool hide_not_in_list = taiga::settings.torrentFeedHideNotInList();
  const bool hide_watched = taiga::settings.torrentFeedHideWatchedEpisodes();
  const bool hide_available = taiga::settings.torrentFeedHideAvailableEpisodes();
  const bool hide_older_versions = taiga::settings.torrentFeedHideOlderVersionsWhenNewerExists();
  const QSet<QString> archived_titles = [&]() {
    const QStringList t = taiga::settings.torrentFeedDiscardedTitleArchive();
    return QSet<QString>(t.begin(), t.end());
  }();

  // Prefer new versions: within the current RSS view, hide older versions of the same episode
  // when a newer version exists.
  QHash<qulonglong, int> max_version_for_key;
  if (hide_older_versions) {
    max_version_for_key.reserve(static_cast<int>(feed.items.size()));
    for (const rss::Item& it : feed.items) {
      track::Episode ep = track::recognition::parse(it.title);
      const int anime_id = track::recognition::identify(ep);
      const int ep_no = QString::fromStdString(ep.element(anitomy::ElementKind::Episode)).toInt();
      if (anime_id == anime::kUnknownId || ep_no <= 0) continue;
      const int ver = std::max(
          1, QString::fromStdString(ep.element(anitomy::ElementKind::ReleaseVersion, std::string{"1"})).toInt());
      const qulonglong key = (static_cast<qulonglong>(anime_id) << 32) | static_cast<qulonglong>(ep_no);
      const int cur = max_version_for_key.value(key, 1);
      if (ver > cur) max_version_for_key.insert(key, ver);
    }
  }

  QList<const rss::Item*> filtered;
  filtered.reserve(static_cast<int>(feed.items.size()));
  for (const rss::Item& it : feed.items) {
    if (!archived_titles.isEmpty()) {
      const QString title = QString::fromStdString(it.title);
      if (archived_titles.contains(title)) continue;
    }
    const QString rowText = rowTextForItem(it);

    bool ok = include.isEmpty();
    for (const QRegularExpression& re : include) {
      if (re.match(rowText).hasMatch()) {
        ok = true;
        break;
      }
    }
    if (!ok) continue;

    bool blocked = false;
    for (const QRegularExpression& re : exclude) {
      if (re.match(rowText).hasMatch()) {
        blocked = true;
        break;
      }
    }
    if (blocked) continue;

    if (hide_dropped || hide_not_in_list || hide_watched || hide_available || hide_older_versions) {
      track::Episode ep = track::recognition::parse(it.title);
      const int id = track::recognition::identify(ep);
      const ListEntry* entry = (id != anime::kUnknownId) ? anime::db.entry(id) : nullptr;
      const auto st = entry ? entry->status : anime::list::Status::NotInList;
      // Only apply "not in list" when the anime was positively identified.
      // Unrecognized items (id == kUnknownId) have no list membership data —
      // hiding them would silently discard Specials/OVAs that merely failed recognition.
      if (hide_not_in_list && id != anime::kUnknownId && st == anime::list::Status::NotInList) continue;
      if (hide_dropped && st == anime::list::Status::Dropped) continue;

      const int ep_no = QString::fromStdString(ep.element(anitomy::ElementKind::Episode)).toInt();
      // S00 (season 0) is the Nyaa / AniDB convention for Specials/OVAs.
      // Their episode numbers live in a different namespace from the main series, so
      // comparing S00E01 against main-series watched_episodes gives false positives.
      // Skip episode-based checks entirely for season-0 releases.
      const auto season_val_str = ep.element(anitomy::ElementKind::Season);
      const bool is_season_zero =
          !season_val_str.empty() && QString::fromStdString(season_val_str).toInt() == 0;
      if (id != anime::kUnknownId && ep_no > 0 && !is_season_zero) {
        if (hide_watched && entry && ep_no <= entry->watched_episodes) continue;
        if (hide_available && track::libraryHasLocalEpisode(id, ep_no)) continue;
        if (hide_older_versions && !max_version_for_key.isEmpty()) {
          const qulonglong key = (static_cast<qulonglong>(id) << 32) | static_cast<qulonglong>(ep_no);
          const int maxv = max_version_for_key.value(key, 1);
          const int v = std::max(
              1, QString::fromStdString(ep.element(anitomy::ElementKind::ReleaseVersion, std::string{"1"})).toInt());
          if (v < maxv) continue;
        }
      }
    }

    filtered.push_back(&it);
  }
  return filtered;
}

/// Return the best folder name for an anime download:
/// English title if available, otherwise the provided hint (anitomy-parsed title).
/// Falls back to the hint if no database match is found.
QString bestFolderNameForAnime(const QString& anitomy_title_hint) {
  if (anitomy_title_hint.isEmpty()) return anitomy_title_hint;
  for (const auto& [id, item] : anime::db.items().asKeyValueRange()) {
    const QString romaji = QString::fromStdString(item.titles.romaji);
    if (romaji.compare(anitomy_title_hint, Qt::CaseInsensitive) == 0) {
      const QString en = QString::fromStdString(item.titles.english);
      return en.isEmpty() ? anitomy_title_hint : en;
    }
  }
  return anitomy_title_hint;
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

QString resolvedTorrentDownloadDirForSavedTorrent(const QString& title_hint) {
  QString base = QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed();
  if (base.isEmpty()) return {};
  if (!QDir(base).exists()) return {};

  if (taiga::settings.torrentDownloadCreateSubfolder()) {
    QString sub = sanitizedTorrentBaseName(title_hint);
    if (sub.isEmpty()) return base;
    QDir d(base);
    if (!d.exists(sub)) {
      if (!d.mkpath(sub)) return base;
    }
    return d.filePath(sub);
  }

  return QDir(base).absolutePath();
}

/// Ensure the base "torrent client download path" exists when Taiga is about to pass a save path.
/// Returns a valid existing base directory, or std::nullopt if the user cancels.
std::optional<QString> ensureClientDownloadBaseDir(QWidget* parent) {
  QString base = QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed();
  if (!base.isEmpty() && QDir(base).exists()) return base;

  // Blocking prompt: create configured folder or choose a different one.
  QDialog dlg(parent);
  dlg.setWindowTitle(QObject::tr("Torrent download folder"));
  dlg.setModal(true);
  dlg.resize(560, 160);

  auto* layout = new QVBoxLayout(&dlg);
  auto* msg = new QLabel(&dlg);
  msg->setWordWrap(true);
  msg->setText(QObject::tr(
      "The configured <b>torrent client download folder</b> does not exist.\n\n"
      "Choose a folder or type a path and create it. This setting is used when Taiga passes a save "
      "path (qBittorrent Web API / compatible clients)."));
  layout->addWidget(msg);

  auto* pathRow = new QHBoxLayout();
  pathRow->addWidget(new QLabel(QObject::tr("Folder:"), &dlg));
  auto* edit = new QLineEdit(&dlg);
  edit->setPlaceholderText(QObject::tr("e.g. D:\\Anime\\Downloads"));
  edit->setText(base);
  pathRow->addWidget(edit, 1);
  layout->addLayout(pathRow);

  auto* row = new QHBoxLayout();
  row->addStretch(1);

  auto* btnCreate = new QPushButton(QObject::tr("Create folder"), &dlg);
  btnCreate->setToolTip(QObject::tr("Create the configured folder path."));
  row->addWidget(btnCreate);

  auto* btnChoose = new QPushButton(QObject::tr("Choose folder…"), &dlg);
  btnChoose->setToolTip(QObject::tr("Pick a different folder and update settings."));
  row->addWidget(btnChoose);

  auto* btnCancel = new QPushButton(QObject::tr("Cancel"), &dlg);
  btnCancel->setDefault(true);
  row->addWidget(btnCancel);

  layout->addLayout(row);

  std::optional<QString> result;

  const auto refreshButtons = [&]() { btnCreate->setEnabled(!base.isEmpty()); };
  refreshButtons();

  QObject::connect(edit, &QLineEdit::textChanged, &dlg, [&](const QString& t) {
    base = t.trimmed();
    refreshButtons();
  });

  QObject::connect(btnCancel, &QPushButton::clicked, &dlg, [&]() { dlg.reject(); });

  QObject::connect(btnCreate, &QPushButton::clicked, &dlg, [&]() {
    if (base.isEmpty()) return;
    // Persist the chosen/typed path so subsequent saves use it.
    taiga::settings.setTorrentClientDownloadPath(base.toStdString());
    if (QDir{}.mkpath(base) && QDir(base).exists()) {
      result = base;
      dlg.accept();
      return;
    }
    QMessageBox::warning(&dlg, QObject::tr("Taiga"),
                         QObject::tr("Could not create the folder:\n%1")
                             .arg(QDir::toNativeSeparators(base)));
  });

  QObject::connect(btnChoose, &QPushButton::clicked, &dlg, [&]() {
    const QString start = !base.isEmpty() ? base
                          : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString picked = QFileDialog::getExistingDirectory(
        &dlg, QObject::tr("Select torrent download folder"), start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) return;
    taiga::settings.setTorrentClientDownloadPath(picked.trimmed().toStdString());
    base = QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed();
    edit->setText(base);
    if (!base.isEmpty() && QDir(base).exists()) {
      result = base;
      dlg.accept();
      return;
    }
    QMessageBox::warning(&dlg, QObject::tr("Taiga"),
                         QObject::tr("That folder does not exist or is not accessible."));
  });

  if (dlg.exec() != QDialog::Accepted) return std::nullopt;
  return result;
}

struct QBitCreds {
  QString username;
  QString password;
};

std::optional<QBitCreds> promptQBitCredentials(QWidget* parent, const QString& details = {}) {
  QDialog dlg(parent);
  dlg.setWindowTitle(QObject::tr("qBittorrent Web API credentials"));
  dlg.setModal(true);
  dlg.resize(620, 220);

  auto* layout = new QVBoxLayout(&dlg);

  auto* msg = new QLabel(&dlg);
  msg->setWordWrap(true);
  msg->setText(QObject::tr(
      "Taiga could not talk to qBittorrent’s Web API.\n\n"
      "Enter credentials for the qBittorrent Web UI. If authentication is disabled in qBittorrent, "
      "leave these blank.\n\n"
      "If this still fails, check qBittorrent: <b>Tools → Preferences → Web UI</b> and enable the "
      "Web UI. For localhost setups you may also need: <b>Bypass authentication for clients on "
      "localhost</b>."));
  layout->addWidget(msg);

  if (!details.trimmed().isEmpty()) {
    auto* det = new QLabel(&dlg);
    det->setWordWrap(true);
    det->setText(QObject::tr("<b>Details:</b> %1").arg(details.toHtmlEscaped()));
    layout->addWidget(det);
  }

  auto* userRow = new QHBoxLayout();
  userRow->addWidget(new QLabel(QObject::tr("Username:"), &dlg));
  auto* userEdit = new QLineEdit(&dlg);
  userEdit->setText(QString::fromStdString(taiga::settings.torrentQBitApiUsername()).trimmed());
  userEdit->setPlaceholderText(QObject::tr("admin"));
  userRow->addWidget(userEdit, 1);
  layout->addLayout(userRow);

  auto* passRow = new QHBoxLayout();
  passRow->addWidget(new QLabel(QObject::tr("Password:"), &dlg));
  auto* passEdit = new QLineEdit(&dlg);
  passEdit->setEchoMode(QLineEdit::Password);
  passEdit->setText(QString::fromStdString(taiga::settings.torrentQBitApiPassword()));
  passEdit->setPlaceholderText(QObject::tr("(empty)"));
  passRow->addWidget(passEdit, 1);
  layout->addLayout(passRow);

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);
  auto* ok = new QPushButton(QObject::tr("Save and retry"), &dlg);
  auto* cancel = new QPushButton(QObject::tr("Cancel"), &dlg);
  cancel->setDefault(true);
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  layout->addLayout(buttons);

  std::optional<QBitCreds> out;
  QObject::connect(cancel, &QPushButton::clicked, &dlg, [&]() { dlg.reject(); });
  QObject::connect(ok, &QPushButton::clicked, &dlg, [&]() {
    QBitCreds c;
    c.username = userEdit->text().trimmed();
    c.password = passEdit->text();
    out = c;
    dlg.accept();
  });

  if (dlg.exec() != QDialog::Accepted) return std::nullopt;
  return out;
}

QStringList argsForTorrentClient(const QString& exe_path, const QString& torrent_file,
                                 const QString& download_dir) {
  if (exe_path.isEmpty()) return {torrent_file};
  const QString exe = QFileInfo(exe_path).fileName().toLower();
  const bool have_dir = !download_dir.isEmpty() && QDir(download_dir).exists();

  // Best-effort compatibility with common clients.
  if (have_dir) {
    if (exe.contains(QStringLiteral("qbittorrent"))) {
      return {QStringLiteral("--save-path=%1").arg(download_dir),
              QStringLiteral("--skip-dialog=true"), torrent_file};
    }
    if (exe.contains(QStringLiteral("picotorrent"))) {
      return {QStringLiteral("--save-path=%1").arg(download_dir), QStringLiteral("--silent"),
              torrent_file};
    }
    if (exe.contains(QStringLiteral("utorrent"))) {
      return {QStringLiteral("/directory"), download_dir, torrent_file};
    }
    if (exe.contains(QStringLiteral("aria2c"))) {
      return {QStringLiteral("--dir=%1").arg(download_dir), torrent_file};
    }
  }

  return {torrent_file};
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
         ".torrent order follows the prefer-magnet checkbox in Settings → Library). When a "
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
      tr("Uses the catalog RSS URL from Settings → Library."));
  row->addWidget(m_btn_fetch_);
  row->addWidget(m_btn_browser_);
  row->addWidget(m_btn_catalog_);
  m_btn_download_selected_ = new QPushButton(tr("Download selected"), this);
  m_btn_download_selected_->setToolTip(
      tr("Save the selected .torrent files in sequence and open them in your configured torrent app "
         "(default handler or custom executable). Magnet-only rows are opened directly.\n"
         "Tip: use Ctrl/Shift to select multiple rows."));
  row->addWidget(m_btn_download_selected_);
  m_btn_download_best_ = new QPushButton(tr("⬇ Best match"), this);
  m_btn_download_best_->setToolTip(
      tr("Download the visible result with the highest seeder count (or the first visible result "
         "if seeder data is unavailable). Apply filters first for best results."));
  row->addWidget(m_btn_download_best_);
  m_btn_cancel_downloads_ = new QPushButton(tr("Cancel downloads"), this);
  m_btn_cancel_downloads_->setToolTip(tr("Cancel the active .torrent download and clear the queue."));
  m_btn_cancel_downloads_->setEnabled(false);
  row->addWidget(m_btn_cancel_downloads_);
  m_btn_clear_queue_ = new QPushButton(tr("Clear queue"), this);
  m_btn_clear_queue_->setToolTip(tr("Clear the queue list and pending items (does not delete files)."));
  row->addWidget(m_btn_clear_queue_);
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
  m_table_->setColumnCount(9);
  m_table_->setHorizontalHeaderLabels(
      {tr("Title"), tr("Published"), tr("Page"), tr("Torrent"), tr("Anime"), tr("Ep"), tr("Group"),
       tr("Video"), tr("Seeds")});
  m_table_->horizontalHeader()->setStretchLastSection(false);
  m_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  m_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
  m_table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
  m_table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
  m_table_->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
  m_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table_->setAlternatingRowColors(true);
  m_table_->setContextMenuPolicy(Qt::CustomContextMenu);
  m_table_->setSortingEnabled(true);
  layout->addWidget(m_table_, 1);

  {
    auto* ql = new QVBoxLayout();
    auto* qhdr = new QHBoxLayout();
    qhdr->addWidget(new QLabel(tr("Download queue:"), this));
    qhdr->addStretch();
    ql->addLayout(qhdr);

    m_queue_list_ = new QListWidget(this);
    m_queue_list_->setSelectionMode(QAbstractItemView::NoSelection);
    m_queue_list_->setMinimumHeight(90);
    ql->addWidget(m_queue_list_);
    layout->addLayout(ql);
  }

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
  connect(m_btn_download_selected_, &QPushButton::clicked, this, [this]() {
    if (!m_table_) return;
    if (m_save_reply_ || !m_save_queue_.isEmpty()) {
      taiga::userFeedback(tr("A download queue is already running. Cancel it first if needed."), true);
      return;
    }
    const QList<QTableWidgetSelectionRange> ranges = m_table_->selectedRanges();
    if (ranges.isEmpty()) {
      taiga::userFeedback(tr("Select one or more rows first."), true);
      return;
    }

    QSet<int> rows;
    for (const auto& r : ranges) {
      for (int i = r.topRow(); i <= r.bottomRow(); ++i) rows.insert(i);
    }
    QList<int> ordered = rows.values();
    std::sort(ordered.begin(), ordered.end());

    // Resolve URL + folder for each selected row.
    struct SelItem { QString url; QString folder; bool is_http_torrent; };
    QList<SelItem> items;
    for (const int rowIdx : ordered) {
      const QTableWidgetItem* c3 = m_table_->item(rowIdx, 3);
      const QString tor_u = c3 ? c3->text() : QString{};
      const QVariant mag_v = c3 ? c3->data(kTableMagnetDataRole) : QVariant{};
      const QString magnet_u = mag_v.isValid() ? mag_v.toString() : QString{};
      const QTableWidgetItem* animecol = m_table_->item(rowIdx, 4);
      const QString anime_title = animecol ? animecol->text().trimmed() : QString{};
      const QTableWidgetItem* titlecol = m_table_->item(rowIdx, 0);
      const QString raw_hint = anime_title.isEmpty()
          ? (titlecol ? titlecol->text() : QString{}) : anime_title;
      const QString title = bestFolderNameForAnime(raw_hint);
      const bool has_http = httpUrlFromUserString(tor_u).has_value();
      // Prefer magnet → .torrent URL → page link.
      const QString url = !magnet_u.isEmpty() ? magnet_u
                          : (!tor_u.isEmpty() ? tor_u : primaryUrlForRow(rowIdx, m_table_));
      if (!url.isEmpty()) items.append({url, title, has_http && magnet_u.isEmpty()});
    }

    if (taiga::settings.torrentQBitApiEnabled()) {
      // Only block when we're about to pass a save path to the client.
      if (!ensureClientDownloadBaseDir(this).has_value()) {
        cancelSaveTorrent();
        if (auto* mw = mainWindow()) {
          mw->statusBar()->showMessage(tr("Download cancelled."), 4000);
        }
        return;
      }
      // Send everything directly to qBittorrent with per-item save path.
      const int total = items.size();
      const auto sent = std::make_shared<int>(0);
      for (const auto& it : items) {
        const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(it.folder);
        addTorrentViaQBitApi(it.url, save_path,
            [this, it, sent, total](bool ok, const QString& err) {
              if (!err.isEmpty())
                taiga::userFeedback(QStringLiteral("qBit: ") + err, true);
              if (ok && ++(*sent) == total) {
                if (auto* mw = mainWindow())
                  mw->statusBar()->showMessage(
                      tr("Sent %1 torrent(s) to qBittorrent.").arg(total), 6000);
              }
            });
      }
      return;
    }

    // Fallback: .torrent file download queue or magnet open.
    int enqueued = 0;
    int opened = 0;
    for (const auto& it : items) {
      if (it.is_http_torrent) {
        if (const auto u = httpUrlFromUserString(it.url)) {
          enqueueSaveTorrent(*u, it.folder);
          ++enqueued;
        }
      } else {
        openPrimaryTorrentUrl(it.url);
        ++opened;
      }
    }

    if (enqueued > 0) {
      const QString save_dir = QString::fromStdString(taiga::settings.torrentFileSavePath()).trimmed();
      if (enqueued > 1 && (save_dir.isEmpty() || !QDir(save_dir).exists())) {
        const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        const QString picked = QFileDialog::getExistingDirectory(
            this, tr("Select folder to save .torrent files"), downloads,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (picked.isEmpty()) {
          cancelSaveTorrent();
          if (auto* mw = mainWindow()) {
            mw->statusBar()->showMessage(tr("Torrent download queue cancelled."), 4000);
          }
          return;
        }
        m_save_queue_dir_ = picked;
      }
      startNextQueuedSave();
    }
    if (auto* mw = mainWindow()) {
      QString msg = tr("Queued %1 .torrent file(s).").arg(enqueued);
      if (opened > 0) msg += tr(" Opened %1 link(s).").arg(opened);
      mw->statusBar()->showMessage(msg, 6000);
    }
  });
  connect(m_btn_download_best_, &QPushButton::clicked, this, [this]() {
    if (!m_table_) return;
    if (m_save_reply_ || !m_save_queue_.isEmpty()) {
      taiga::userFeedback(tr("A download queue is already running. Cancel it first if needed."), true);
      return;
    }

    // Build a virtual feed from the visible table rows to reuse selectBestPerEpisode.
    // For each visible row, collect data needed for episode resolution and download.
    struct VisibleRow {
      int episode = 0;     // parsed episode no (-1=batch, 0=unknown)
      qlonglong seeds = 0;
      QString magnet;
      QString tor_url;
      QString page_url;
      QString folder;      // per-row folder hint
    };

    QList<VisibleRow> rows;
    for (int r = 0; r < m_table_->rowCount(); ++r) {
      if (m_table_->isRowHidden(r)) continue;
      const QTableWidgetItem* c3 = m_table_->item(r, 3);
      const QString tor_u = c3 ? c3->text() : QString{};
      const QVariant mag_v = c3 ? c3->data(kTableMagnetDataRole) : QVariant{};
      const QTableWidgetItem* animecol = m_table_->item(r, 4);
      const QString anime_title = animecol ? animecol->text().trimmed() : QString{};
      const QTableWidgetItem* titlecol = m_table_->item(r, 0);
      const QString raw_hint = anime_title.isEmpty()
          ? (titlecol ? titlecol->text() : QString{}) : anime_title;
      const QString ep_str_col =
          m_table_->item(r, 5) ? m_table_->item(r, 5)->text() : QString{};
      int ep_no = 0;
      if (ep_str_col.contains('-')) ep_no = -1;
      else if (!ep_str_col.isEmpty()) { bool ok; ep_no = ep_str_col.toInt(&ok); if (!ok) ep_no = 0; }
      const qlonglong seeds =
          m_table_->item(r, 8) ? m_table_->item(r, 8)->data(kNumericSortKeyRole).toLongLong() : 0;

      rows.append({ep_no, seeds,
                   mag_v.isValid() ? mag_v.toString() : QString{},
                   tor_u,
                   primaryUrlForRow(r, m_table_),
                   bestFolderNameForAnime(raw_hint)});
    }

    if (rows.isEmpty()) {
      taiga::userFeedback(tr("No results visible — try fetching the RSS feed first."), true);
      return;
    }

    // Group by episode: keep the best-seeded row per unique episode number.
    QMap<int, int> best_idx_per_ep;   // episode → rows[] index
    QMap<int, qlonglong> best_seeds_per_ep;
    for (int i = 0; i < rows.size(); ++i) {
      const auto& row = rows[i];
      const auto it = best_seeds_per_ep.find(row.episode);
      if (it == best_seeds_per_ep.end() || row.seeds > it.value()) {
        best_seeds_per_ep[row.episode] = row.seeds;
        best_idx_per_ep[row.episode] = i;
      }
    }

    // Collect items to download (one per unique episode).
    struct Target { QString url; QString folder; int episode; };
    QList<Target> targets;
    for (auto it = best_idx_per_ep.begin(); it != best_idx_per_ep.end(); ++it) {
      const auto& row = rows[it.value()];
      const QString url = !row.magnet.isEmpty() ? row.magnet
                          : (!row.tor_url.isEmpty() ? row.tor_url : row.page_url);
      if (!url.isEmpty()) targets.append({url, row.folder, row.episode});
    }

    if (targets.isEmpty()) {
      taiga::userFeedback(tr("No download link found for the best match."), true);
      return;
    }

    const int total = targets.size();

    if (taiga::settings.torrentQBitApiEnabled()) {
      if (!ensureClientDownloadBaseDir(this).has_value()) {
        if (auto* mw = mainWindow()) mw->statusBar()->showMessage(tr("Download cancelled."), 4000);
        return;
      }
      const auto sent = std::make_shared<int>(0);
      for (const auto& t : targets) {
        const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(t.folder);
        addTorrentViaQBitApi(t.url, save_path,
            [this, t, sent, total](bool ok, const QString& err) {
              if (!err.isEmpty())
                taiga::userFeedback(QStringLiteral("qBit: ") + err, true);
              if (ok) {
                ++(*sent);
                if (auto* mw = mainWindow())
                  mw->statusBar()->showMessage(
                      tr("Sent to qBittorrent: %1 ep%2 (%3/%4)").arg(t.folder).arg(t.episode).arg(*sent).arg(total),
                      4000);
              }
            });
      }
    } else {
      int enqueued = 0;
      for (const auto& t : targets) {
        if (const auto u = httpUrlFromUserString(t.url)) {
          enqueueSaveTorrent(*u, t.folder); ++enqueued;
        } else {
          openPrimaryTorrentUrl(t.url);
        }
      }
      if (enqueued > 0) startNextQueuedSave();
      if (auto* mw = mainWindow())
        mw->statusBar()->showMessage(tr("Queued %1 torrent(s) — best per episode.").arg(total), 5000);
    }
  });
  connect(m_btn_cancel_downloads_, &QPushButton::clicked, this, [this]() {
    if (!m_save_reply_ && m_save_queue_.isEmpty()) return;
    cancelSaveTorrent();
    if (auto* mw = mainWindow()) {
      mw->statusBar()->showMessage(tr("Torrent download queue cancelled."), 4000);
    }
  });
  connect(m_btn_clear_queue_, &QPushButton::clicked, this, [this]() {
    if (m_save_reply_ || !m_save_queue_.isEmpty()) {
      taiga::userFeedback(tr("Cancel downloads first to clear an active queue."), true);
      return;
    }
    if (m_queue_list_) m_queue_list_->clear();
    m_save_queue_total_ = 0;
    m_save_queue_dir_.clear();
    if (auto* mw = mainWindow()) mw->statusBar()->showMessage(tr("Queue cleared."), 2500);
  });

  {
    auto* sc_cancel = new QShortcut(QKeySequence{Qt::CTRL | Qt::Key_Escape}, this);
    sc_cancel->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_cancel, &QShortcut::activated, this, [this]() {
      if (!m_save_reply_ && m_save_queue_.isEmpty()) return;
      cancelSaveTorrent();
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Torrent download queue cancelled."), 4000);
      }
    });
  }

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
    const QString title_u = m_table_->item(row, 0) ? m_table_->item(row, 0)->text() : QString{};

    auto* menu = new QMenu(this);

    // Bulk actions (when multiple rows are selected and the clicked row is part of the selection).
    const QList<int> selected_rows = [this]() {
      QList<int> rows;
      if (!m_table_ || !m_table_->selectionModel()) return rows;
      const QModelIndexList sel = m_table_->selectionModel()->selectedRows();
      rows.reserve(sel.size());
      for (const QModelIndex& mi : sel) rows.push_back(mi.row());
      std::sort(rows.begin(), rows.end());
      rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
      return rows;
    }();
    if (selected_rows.size() > 1 && selected_rows.contains(row)) {
      menu->addAction(tr("Download selected"), this, [this]() {
        if (m_btn_download_selected_) m_btn_download_selected_->click();
      });
      menu->addAction(tr("Discard selected titles (hide in future)"), this, [this, selected_rows]() {
        QStringList archive = taiga::settings.torrentFeedDiscardedTitleArchive();
        int added = 0;
        for (const int r : selected_rows) {
          const QString t = m_table_->item(r, 0) ? m_table_->item(r, 0)->text().trimmed() : QString{};
          if (t.isEmpty()) continue;
          if (!archive.contains(t)) {
            archive.push_back(t);
            ++added;
          }
          m_table_->setRowHidden(r, true);
        }
        if (added > 0) {
          taiga::settings.setTorrentFeedDiscardedTitleArchive(archive);
        }
        if (auto* mw = mainWindow()) {
          mw->statusBar()->showMessage(tr("Discarded %1 title(s).").arg(added), 4000);
        }
      });
      menu->addAction(tr("Cancel downloads"), this, [this]() {
        if (!m_save_reply_ && m_save_queue_.isEmpty()) return;
        cancelSaveTorrent();
      });
      menu->addAction(tr("Copy primary links (newline-separated)"), this, [this, selected_rows]() {
        QStringList lines;
        lines.reserve(selected_rows.size());
        for (const int r : selected_rows) {
          const QString u = primaryUrlForRow(r, m_table_);
          if (!u.isEmpty()) lines.push_back(u);
        }
        if (!lines.isEmpty()) QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
      });
      menu->addSeparator();
    }

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
    if (!title_u.trimmed().isEmpty()) {
      menu->addAction(tr("Discard this title (hide in future)"), this, [this, title_u, row]() {
        QStringList archive = taiga::settings.torrentFeedDiscardedTitleArchive();
        const QString t = title_u.trimmed();
        if (!archive.contains(t)) {
          archive.push_back(t);
          taiga::settings.setTorrentFeedDiscardedTitleArchive(archive);
        }
        m_table_->setRowHidden(row, true);
        if (auto* mw = mainWindow()) {
          mw->statusBar()->showMessage(tr("Discarded \"%1\".").arg(t), 4000);
        }
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

void TorrentFeedWidget::addTorrentViaQBitApi(const QString& torrent_url, const QString& save_path,
                                              std::function<void(bool ok, QString error)> on_done) {
  const QString base_url =
      QString::fromStdString(taiga::settings.torrentQBitApiUrl()).trimmed().trimmed();

  const auto showFinalGuidance = [&](const QString& err) {
    QMessageBox::warning(this, tr("Taiga"),
                         tr("qBittorrent Web API request failed.\n\n"
                            "Error: %1\n\n"
                            "In qBittorrent: Tools → Preferences → Web UI → enable the Web UI.\n"
                            "Then, under Authentication, check “Bypass authentication for clients on localhost”.")
                             .arg(err.toHtmlEscaped()));
  };

  std::function<void(const QString& user, const QString& pass, bool allow_retry)> attemptWithCreds;
  attemptWithCreds = [&](const QString& user, const QString& pass, const bool allow_retry) {
    const auto do_add = [=](const QString& cookie) {
      QNetworkRequest req(QUrl(base_url + QStringLiteral("/api/v2/torrents/add")));
      req.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/x-www-form-urlencoded"));
      if (!cookie.isEmpty()) req.setRawHeader("Cookie", cookie.toUtf8());

      QByteArray body = QByteArrayLiteral("urls=") + QUrl::toPercentEncoding(torrent_url);
      if (!save_path.isEmpty()) {
        QDir().mkpath(save_path);
        body += QByteArrayLiteral("&savepath=") + QUrl::toPercentEncoding(save_path);
      }

      auto* reply = taiga::network()->post(req, body);
      connect(reply, &QNetworkReply::finished, this, [=]() mutable {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
          const QString err = reply->errorString();
          if (allow_retry) {
            if (const auto creds = promptQBitCredentials(this, err)) {
              taiga::settings.setTorrentQBitApiUsername(creds->username.toStdString());
              taiga::settings.setTorrentQBitApiPassword(creds->password.toStdString());
              attemptWithCreds(creds->username.trimmed(), creds->password, false);
              return;
            }
          }
          if (allow_retry) showFinalGuidance(err);
          if (on_done) on_done(false, err);
          return;
        }

        const QString resp = QString::fromUtf8(reply->readAll()).trimmed();
        const bool ok = resp.compare(QStringLiteral("Ok."), Qt::CaseInsensitive) == 0 ||
                        resp.startsWith(QStringLiteral("Ok"), Qt::CaseInsensitive);
        if (!ok) {
          const QString err = resp.isEmpty() ? tr("Unexpected response from qBittorrent.") : resp;
          if (allow_retry) {
            if (const auto creds = promptQBitCredentials(this, err)) {
              taiga::settings.setTorrentQBitApiUsername(creds->username.toStdString());
              taiga::settings.setTorrentQBitApiPassword(creds->password.toStdString());
              attemptWithCreds(creds->username.trimmed(), creds->password, false);
              return;
            }
            showFinalGuidance(err);
          }
          if (on_done) on_done(false, err);
          return;
        }

        if (on_done) on_done(true, {});
      });
    };

    // If username is empty, try add directly (works when qBittorrent auth is disabled).
    if (user.isEmpty()) {
      do_add({});
      return;
    }

    QNetworkRequest login_req(QUrl(base_url + QStringLiteral("/api/v2/auth/login")));
    login_req.setHeader(QNetworkRequest::ContentTypeHeader,
                        QStringLiteral("application/x-www-form-urlencoded"));
    const QByteArray login_body =
        QByteArrayLiteral("username=") + user.toUtf8() +
        QByteArrayLiteral("&password=") + pass.toUtf8();

    m_qbit_login_reply_ = taiga::network()->post(login_req, login_body);
    connect(m_qbit_login_reply_, &QNetworkReply::finished, this, [=]() mutable {
      auto* r = m_qbit_login_reply_;
      m_qbit_login_reply_ = nullptr;
      if (!r) {
        const QString err = tr("qBittorrent login request was cancelled.");
        if (allow_retry) showFinalGuidance(err);
        if (on_done) on_done(false, err);
        return;
      }
      r->deleteLater();
      if (r->error() != QNetworkReply::NoError) {
        const QString err = r->errorString();
        if (allow_retry) {
          if (const auto creds = promptQBitCredentials(this, err)) {
            taiga::settings.setTorrentQBitApiUsername(creds->username.toStdString());
            taiga::settings.setTorrentQBitApiPassword(creds->password.toStdString());
            attemptWithCreds(creds->username.trimmed(), creds->password, false);
            return;
          }
          showFinalGuidance(err);
        }
        if (on_done) on_done(false, err);
        return;
      }

      QString cookie_str;
      const QVariant cv = r->header(QNetworkRequest::SetCookieHeader);
      if (cv.isValid()) {
        for (const QNetworkCookie& c : cv.value<QList<QNetworkCookie>>()) {
          if (!cookie_str.isEmpty()) cookie_str += QStringLiteral("; ");
          cookie_str += QString::fromUtf8(c.name()) + QStringLiteral("=") +
                        QString::fromUtf8(c.value());
        }
      }
      do_add(cookie_str);
    });
  };

  const QString username = QString::fromStdString(taiga::settings.torrentQBitApiUsername()).trimmed();
  const QString password = QString::fromStdString(taiga::settings.torrentQBitApiPassword());
  attemptWithCreds(username, password, true);
}

void TorrentFeedWidget::saveSessionState() {
  if (!m_table_) return;
  taiga::session.setTorrentRssTableHeaderState(m_table_->horizontalHeader()->saveState());
}

/// Generates all reasonable RSS search title variants for an anime to try in order.
/// Priority: saved-cache first, then English variants (full, stripped, season-code), then romaji.
static QStringList buildTitleVariants(const QString& english, const QString& romaji) {
  QStringList result;
  const auto addIfNew = [&](const QString& s) {
    const QString t = s.trimmed();
    if (!t.isEmpty() && !result.contains(t, Qt::CaseInsensitive)) result.append(t);
  };

  // Strip subtitle after ": " from a title (e.g. "Foo 4th Season: Bar" → "Foo 4th Season").
  const auto stripSubtitle = [](const QString& s) {
    const int idx = s.indexOf(QStringLiteral(": "));
    return idx > 0 ? s.left(idx).trimmed() : s;
  };

  // Convert ordinal/keyword season markers to compact "SNN" (e.g. "4th Season" → "S04").
  const auto toSeasonCode = [](const QString& s) -> QString {
    // "Nth Season" → "SNN"
    const QRegularExpression re_nth(
        QStringLiteral("\\b(\\d+)(?:st|nd|rd|th)\\s+[Ss]eason\\b"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = re_nth.match(s);
    if (m.hasMatch()) {
      QString r = s;
      r.replace(m.capturedStart(), m.capturedLength(),
                QStringLiteral("S%1").arg(m.captured(1).toInt(), 2, 10, QChar('0')));
      return r.trimmed();
    }
    // "Season N" → "SNN"
    const QRegularExpression re_s(QStringLiteral("\\b[Ss]eason\\s+(\\d+)\\b"),
                                  QRegularExpression::CaseInsensitiveOption);
    m = re_s.match(s);
    if (m.hasMatch()) {
      QString r = s;
      r.replace(m.capturedStart(), m.capturedLength(),
                QStringLiteral("S%1").arg(m.captured(1).toInt(), 2, 10, QChar('0')));
      return r.trimmed();
    }
    // "Part N" → "Part N" is kept as-is (common on Nyaa); also try "SNN" variant
    const QRegularExpression re_p(QStringLiteral("\\bPart\\s+(\\d+)\\b"),
                                  QRegularExpression::CaseInsensitiveOption);
    m = re_p.match(s);
    if (m.hasMatch()) {
      QString r = s;
      r.replace(m.capturedStart(), m.capturedLength(),
                QStringLiteral("S%1").arg(m.captured(1).toInt(), 2, 10, QChar('0')));
      return r.trimmed();
    }
    return {};
  };

  // English title variants.
  if (!english.isEmpty()) {
    addIfNew(english);                               // "Classroom of the Elite 4th Season: …"
    const QString en_s = stripSubtitle(english);
    addIfNew(en_s);                                  // "Classroom of the Elite 4th Season"
    addIfNew(toSeasonCode(en_s));                    // "Classroom of the Elite S04"
    addIfNew(toSeasonCode(english));                 // (already stripped — same or different)
  }

  // Romaji title variants.
  if (!romaji.isEmpty()) {
    addIfNew(romaji);                                // "Youkoso Jitsuryoku … 2-nensei-hen"
    addIfNew(stripSubtitle(romaji));                 // "Youkoso Jitsuryoku …"
  }

  return result;
}

void TorrentFeedWidget::downloadBestMatchWithFallbacks(const QString& english_title,
                                                       const QString& romaji_title,
                                                       const QString& folder_name,
                                                       std::function<void(bool found)> on_done,
                                                       const int anime_id_cache) {
  QStringList variants;

  // If a previously winning title is cached, try it first so successful animes stay fast.
  if (anime_id_cache > 0) {
    const QString cached = taiga::settings.torrentSearchTitleForAnime(anime_id_cache);
    if (!cached.isEmpty()) variants.append(cached);
  }

  for (const auto& v : buildTitleVariants(english_title, romaji_title)) {
    if (!variants.contains(v, Qt::CaseInsensitive)) variants.append(v);
  }

  if (variants.isEmpty()) {
    if (on_done) on_done(false);
    return;
  }

  // Recursive variant-chain using a shared index.
  const auto state = std::make_shared<int>(0);
  const auto step_fn = std::make_shared<std::function<void()>>();
  *step_fn = [this, variants, folder_name, on_done, anime_id_cache, state, step_fn]() {
    const int idx = *state;
    if (idx >= variants.size()) {
      if (on_done) on_done(false);
      return;
    }
    ++(*state);
    const QString title = variants[idx];
    downloadBestMatchForTitle(
        title, folder_name,
        [this, title, anime_id_cache, on_done, step_fn](bool found) {
          if (found) {
            if (anime_id_cache > 0)
              taiga::settings.setTorrentSearchTitleForAnime(anime_id_cache, title);
            if (on_done) on_done(true);
          } else {
            (*step_fn)();
          }
        },
        {} /* no internal fallback — we manage variants here */);
  };
  (*step_fn)();
}

/// Returns the best-seeded RSS item per unique episode number found in `filtered`.
/// Key = episode number (1-based). Key = -1 means a batch/range item.
/// Key = 0 means the episode could not be parsed (rare; stored separately).
static QMap<int, const rss::Item*> selectBestPerEpisode(const QList<const rss::Item*>& filtered) {
  QMap<int, const rss::Item*> best;
  QMap<int, int> seeds_for;
  static const QRegularExpression kBatchLike(
      QStringLiteral(R"((\bBatch\b|\bComplete\s+Collection\b|\bBD\s*Batch\b))"),
      QRegularExpression::CaseInsensitiveOption);
  for (const rss::Item* it : filtered) {
    track::Episode ep = track::recognition::parse(it->title);
    const QString ep_str =
        QString::fromStdString(ep.element(anitomy::ElementKind::Episode));
    const QString title_full = QString::fromStdString(it->title);
    int ep_no = 0;
    if (ep_str.contains(QChar('-'))) {
      ep_no = -1;  // range → batch
    } else if (!ep_str.isEmpty()) {
      bool ok = false;
      ep_no = ep_str.toInt(&ok);
      if (!ok) ep_no = 0;
    }
    // Nyaa season packs often omit an episode token; anitomy leaves Episode empty while the
    // title still says "(Batch)" — treat those as a single multi-episode item (key -1).
    if (ep_no == 0 && title_full.contains(kBatchLike)) ep_no = -1;
    const auto seed_it = it->namespace_elements.find("seeders");
    const int seeds = (seed_it != it->namespace_elements.end())
                          ? QString::fromStdString(seed_it->second).toInt()
                          : 0;
    const auto existing = seeds_for.find(ep_no);
    if (existing == seeds_for.end() || seeds > existing.value()) {
      seeds_for[ep_no] = seeds;
      best[ep_no] = it;
    }
  }
  return best;
}

/// Returns the best URL to use for an RSS item (magnet > .torrent URL > page link).
static QString bestUrlForItem(const rss::Item* it) {
  if (!it) return {};
  if (const auto m = it->namespace_elements.find(kTorrentFeedMagnetKey);
      m != it->namespace_elements.end() && !m->second.empty()) {
    return QString::fromStdString(m->second);
  }
  if (!it->enclosure.url.empty()) return QString::fromStdString(it->enclosure.url);
  return QString::fromStdString(it->link);
}

void TorrentFeedWidget::downloadAllEpisodesForAnime(const int anime_id,
                                                    const QString& english_title,
                                                    const QString& romaji_title,
                                                    const QString& folder_name,
                                                    std::function<void(int downloaded)> on_done) {
  // Build variant list (cache first, then generated).
  QStringList variants;
  if (anime_id > 0) {
    const QString cached = taiga::settings.torrentSearchTitleForAnime(anime_id);
    if (!cached.isEmpty()) variants.append(cached);
  }
  for (const auto& v : buildTitleVariants(english_title, romaji_title)) {
    if (!variants.contains(v, Qt::CaseInsensitive)) variants.append(v);
  }
  if (variants.isEmpty()) { if (on_done) on_done(0); return; }

  const auto variantIdx = std::make_shared<int>(0);
  const auto try_fn = std::make_shared<std::function<void()>>();

  *try_fn = [this, variants, folder_name, on_done, variantIdx, try_fn, anime_id]() {
    const int idx = *variantIdx;
    if (idx >= variants.size()) { if (on_done) on_done(0); return; }
    ++(*variantIdx);
    const QString title = variants[idx];

    if (m_bg_fetch_reply_) {
      m_bg_fetch_reply_->disconnect(); m_bg_fetch_reply_->abort();
      m_bg_fetch_reply_->deleteLater(); m_bg_fetch_reply_ = nullptr;
    }
    const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
    const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, title);
    if (!url.isValid()) { (*try_fn)(); return; }

    QNetworkRequest req(url);
    taiga::applyCommonHeaders(req);
    m_bg_fetch_reply_ = taiga::network()->get(req);

    connect(m_bg_fetch_reply_, &QNetworkReply::finished, this,
            [this, title, folder_name, on_done, try_fn, anime_id]() {
              auto* reply = m_bg_fetch_reply_;
              m_bg_fetch_reply_ = nullptr;
              if (!reply) { if (on_done) on_done(0); return; }
              reply->deleteLater();
              if (reply->error() != QNetworkReply::NoError) { (*try_fn)(); return; }

              const rss::Feed feed =
                  gui::parseSyndicationFeed(reply->readAll()).value_or(rss::Feed{});
              const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed);
              if (filtered.isEmpty()) { (*try_fn)(); return; }

              // Determine which episodes are missing.
              const auto* item_db = anime::db.item(anime_id);
              const auto* entry_db = anime::db.entry(anime_id);
              QList<int> missing;
              if (item_db && entry_db) {
                // Use episode_count as fallback when last_aired_episode is not populated.
                const int last_aired = item_db->last_aired_episode > 0
                                           ? item_db->last_aired_episode
                                           : item_db->episode_count;
                const int watched = entry_db->watched_episodes;
                for (int ep = watched + 1; ep <= last_aired; ++ep) {
                  if (!track::libraryHasLocalEpisode(anime_id, ep)) missing.append(ep);
                }
              }
              if (missing.isEmpty()) { if (on_done) on_done(0); return; }

              // Build best-per-episode map from filtered feed.
              const QMap<int, const rss::Item*> best_ep = selectBestPerEpisode(filtered);

              const int effective_last = item_db ? (item_db->last_aired_episode > 0
                                                        ? item_db->last_aired_episode
                                                        : item_db->episode_count)
                                                 : 0;

              // Cache only after we actually queue a download (below).

              const auto enqueue_batch = [&](const rss::Item* batch) -> bool {
                if (!batch) return false;
                const QString batch_url = bestUrlForItem(batch);
                if (batch_url.isEmpty()) return false;
                if (anime_id > 0) taiga::settings.setTorrentSearchTitleForAnime(anime_id, title);
                const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(folder_name);
                if (taiga::settings.torrentQBitApiEnabled()) {
                  addTorrentViaQBitApi(
                      batch_url, save_path,
                      [on_done](bool ok, const QString& err) {
                        if (!err.isEmpty())
                          taiga::userFeedback(QStringLiteral("qBit: ") + err, true);
                        if (on_done) on_done(ok ? 1 : 0);
                      });
                } else {
                  if (const auto u = httpUrlFromUserString(batch_url)) {
                    enqueueSaveTorrent(*u, folder_name);
                    startNextQueuedSave();
                  } else {
                    openPrimaryTorrentUrl(batch_url);
                  }
                  if (on_done) on_done(1);
                }
                return true;
              };

              // ── Batch preference: cour/series complete in DB, several eps missing ─
              if (item_db && item_db->episode_count > 0 &&
                  effective_last >= item_db->episode_count &&
                  missing.size() >= 3) {
                if (enqueue_batch(best_ep.value(-1, nullptr))) return;
              }

              // ── Individual episode downloads ──────────────────────────────────
              struct DownloadItem { int ep; QString url; };
              QList<DownloadItem> targets;
              for (const int ep : missing) {
                if (const auto* best = best_ep.value(ep, nullptr)) {
                  const QString ep_url = bestUrlForItem(best);
                  if (!ep_url.isEmpty()) targets.append({ep, ep_url});
                }
              }
              if (targets.isEmpty()) {
                // Season packs often have no per-episode rows; try a batch before the next query
                // variant (e.g. romaji) or giving up.
                if (enqueue_batch(best_ep.value(-1, nullptr))) return;
                (*try_fn)();
                return;
              }

              if (anime_id > 0) taiga::settings.setTorrentSearchTitleForAnime(anime_id, title);

              if (taiga::settings.torrentQBitApiEnabled()) {
                const QString save_path =
                    resolvedTorrentDownloadDirForSavedTorrent(folder_name);
                const int total = targets.size();
                const auto downloaded = std::make_shared<int>(0);
                const auto done_count = std::make_shared<int>(0);
                for (const auto& t : targets) {
                  addTorrentViaQBitApi(
                      t.url, save_path,
                      [downloaded, done_count, total, on_done](bool ok,
                                                                const QString& err) {
                        if (!err.isEmpty())
                          taiga::userFeedback(QStringLiteral("qBit: ") + err, true);
                        if (ok) ++(*downloaded);
                        if (++(*done_count) >= total) {
                          if (on_done) on_done(*downloaded);
                        }
                      });
                }
              } else {
                for (const auto& t : targets) {
                  if (const auto u = httpUrlFromUserString(t.url)) {
                    enqueueSaveTorrent(*u, folder_name);
                  } else {
                    openPrimaryTorrentUrl(t.url);
                  }
                }
                startNextQueuedSave();
                if (on_done) on_done(static_cast<int>(targets.size()));
              }
            });
  };
  (*try_fn)();
}

void TorrentFeedWidget::downloadBestMatchForTitle(const QString& search_title,
                                                  const QString& folder_name,
                                                  std::function<void(bool found)> on_done,
                                                  const QString& fallback_title) {
  // Cancel any in-progress background fetch.
  if (m_bg_fetch_reply_) {
    m_bg_fetch_reply_->disconnect();
    m_bg_fetch_reply_->abort();
    m_bg_fetch_reply_->deleteLater();
    m_bg_fetch_reply_ = nullptr;
  }

  const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
  const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, search_title);
  if (!url.isValid()) {
    if (on_done) on_done(false);
    return;
  }

  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  m_bg_fetch_reply_ = taiga::network()->get(req);

  connect(m_bg_fetch_reply_, &QNetworkReply::finished, this,
          [this, folder_name, fallback_title, on_done](){ 
            auto* reply = m_bg_fetch_reply_;
            m_bg_fetch_reply_ = nullptr;
            if (!reply) { if (on_done) on_done(false); return; }
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
              if (on_done) on_done(false);
              return;
            }
            const QByteArray data = reply->readAll();
            const rss::Feed feed = gui::parseSyndicationFeed(data).value_or(rss::Feed{});
            const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed);

            // If no results and we have a fallback title, retry once with that.
            if (filtered.isEmpty() && !fallback_title.isEmpty()) {
              downloadBestMatchForTitle(fallback_title, folder_name, on_done, {});
              return;
            }
            if (filtered.isEmpty()) { if (on_done) on_done(false); return; }

            // Pick the item with the highest seeder count.
            const rss::Item* best = nullptr;
            int best_seeds = -1;
            for (const rss::Item* it : filtered) {
              const auto seed_it = it->namespace_elements.find("seeders");
              const int seeds = (seed_it != it->namespace_elements.end())
                                    ? QString::fromStdString(seed_it->second).toInt()
                                    : 0;
              if (seeds > best_seeds) { best_seeds = seeds; best = it; }
            }
            if (!best) best = filtered.first();

            const QString effective_folder =
                folder_name.isEmpty() ? QString::fromStdString(best->title) : folder_name;

            // ── Mode 1: qBittorrent Web API (preferred) ───────────────────
            if (taiga::settings.torrentQBitApiEnabled()) {
              if (!ensureClientDownloadBaseDir(this).has_value()) {
                if (on_done) on_done(false);
                return;
              }
              // Prefer magnet link → .torrent URL → page link.
              QString api_url;
              if (const auto m = best->namespace_elements.find(kTorrentFeedMagnetKey);
                  m != best->namespace_elements.end()) {
                api_url = QString::fromStdString(m->second);
              }
              if (api_url.isEmpty()) {
                api_url = QString::fromStdString(best->enclosure.url);
              }
              if (api_url.isEmpty()) api_url = QString::fromStdString(best->link);
              if (api_url.isEmpty()) { if (on_done) on_done(false); return; }

              const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(effective_folder);
              addTorrentViaQBitApi(api_url, save_path, [on_done](bool ok, const QString& err) {
                if (!err.isEmpty())
                  taiga::userFeedback(
                      QStringLiteral("qBittorrent Web API error: ") + err, true);
                if (on_done) on_done(ok);
              });
              return;
            }

            // ── Mode 2: Prefer magnet (open directly) ─────────────────────
            const QString tor_url = QString::fromStdString(best->enclosure.url);
            const bool has_http = !tor_url.isEmpty() && httpUrlFromUserString(tor_url).has_value();

            if (taiga::settings.torrentDownloadUseMagnet() || !has_http) {
              QString link;
              if (const auto m = best->namespace_elements.find(kTorrentFeedMagnetKey);
                  m != best->namespace_elements.end()) {
                link = QString::fromStdString(m->second);
              }
              if (!link.isEmpty()) {
                openPrimaryTorrentUrl(link);
                if (on_done) on_done(true);
                return;
              }
            }

            // ── Mode 3: Download .torrent file then open client ────────────
            if (has_http) {
              if (const auto u = httpUrlFromUserString(tor_url)) {
                enqueueSaveTorrent(*u, effective_folder);
                startNextQueuedSave();  // kick off the queue (safe to call even if active)
                if (on_done) on_done(true);
                return;
              }
            }

            // Last resort: open page link
            const QString page_link = QString::fromStdString(best->link);
            if (!page_link.isEmpty()) {
              openPrimaryTorrentUrl(page_link);
              if (on_done) on_done(true);
            } else {
              if (on_done) on_done(false);
            }
          });
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
  if (m_queue_list_) {
    for (int i = 0; i < m_queue_list_->count(); ++i) {
      if (auto* it = m_queue_list_->item(i)) {
        if (!it->text().contains(QStringLiteral("[cancelled]"))) {
          it->setText(it->text() + QStringLiteral("  [cancelled]"));
        }
      }
    }
  }
  m_save_queue_.clear();
  m_save_queue_total_ = 0;
  m_save_queue_dir_.clear();
  if (m_btn_download_selected_) m_btn_download_selected_->setEnabled(true);
  if (m_btn_cancel_downloads_) m_btn_cancel_downloads_->setEnabled(false);
}

void TorrentFeedWidget::enqueueSaveTorrent(const QUrl& url, const QString& title_hint) {
  if (!url.isValid()) return;
  PendingTorrentSave p;
  p.url = url;
  p.title_hint = title_hint;
  if (m_queue_list_) {
    auto* it = new QListWidgetItem(title_hint.isEmpty() ? url.toString() : title_hint);
    m_queue_list_->addItem(it);
    p.ui_row = m_queue_list_->count() - 1;
  }
  m_save_queue_.enqueue(p);
}

void TorrentFeedWidget::setQueueRowStatus(const int row, const QString& status, const bool error) {
  if (!m_queue_list_ || row < 0 || row >= m_queue_list_->count()) return;
  auto* it = m_queue_list_->item(row);
  if (!it) return;
  QString base = it->text();
  // strip old status suffix
  const int idx = base.indexOf(QStringLiteral("  ["));
  if (idx >= 0) base = base.left(idx);
  it->setText(base + QStringLiteral("  [") + status + QStringLiteral("]"));
  it->setForeground(error ? QBrush(QColor(180, 40, 40)) : QBrush());
}

void TorrentFeedWidget::startNextQueuedSave() {
  if (m_save_reply_) return;
  if (m_save_queue_.isEmpty()) {
    if (m_btn_download_selected_) m_btn_download_selected_->setEnabled(true);
    if (m_btn_cancel_downloads_) m_btn_cancel_downloads_->setEnabled(false);
    if (m_save_queue_total_ > 0) {
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Torrent download queue finished."), 4000);
      }
    }
    m_save_queue_total_ = 0;
    m_save_queue_dir_.clear();
    return;
  }
  if (m_save_queue_total_ <= 0) {
    m_save_queue_total_ = m_save_queue_.size();
  }
  if (m_btn_download_selected_) m_btn_download_selected_->setEnabled(false);
  if (m_btn_cancel_downloads_) m_btn_cancel_downloads_->setEnabled(true);
  const PendingTorrentSave next = m_save_queue_.dequeue();
  if (next.ui_row >= 0) setQueueRowStatus(next.ui_row, QStringLiteral("downloading"), false);
  if (auto* mw = mainWindow()) {
    const int done = m_save_queue_total_ - m_save_queue_.size();
    mw->statusBar()->showMessage(tr("Downloading .torrent files… (%1/%2)").arg(done).arg(m_save_queue_total_), 3000);
  }
  beginSaveTorrent(next.url, next.title_hint);
}

void TorrentFeedWidget::beginSaveTorrent(const QUrl& url, const QString& title_hint) {
  if (!url.isValid()) return;

  QString file_name = QFileInfo(url.path()).fileName();
  if (file_name.isEmpty() || !file_name.endsWith(u".torrent", Qt::CaseInsensitive)) {
    file_name = sanitizedTorrentBaseName(title_hint) + u".torrent";
  }

  const QString save_dir = QString::fromStdString(taiga::settings.torrentFileSavePath());
  QString full_path;
  if (!m_save_queue_dir_.isEmpty() && QDir(m_save_queue_dir_).exists()) {
    full_path = QDir(m_save_queue_dir_).filePath(file_name);
  } else if (!save_dir.isEmpty()) {
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
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  m_save_reply_ = taiga::network()->get(req);
  connect(m_save_reply_, &QNetworkReply::finished, this, [this, full_path, title_hint] {
    const auto done = [this]() { startNextQueuedSave(); };
    QNetworkReply* reply = m_save_reply_;
    m_save_reply_ = nullptr;
    if (!reply) {
      done();
      return;
    }
    reply->deleteLater();
    if (auto* mw = mainWindow()) {
      mw->statusBar()->clearMessage();
    }
    if (reply->error() != QNetworkReply::NoError) {
      taiga::userFeedback(tr("Could not download .torrent file: %1").arg(reply->errorString()), true);
      // mark latest queue row as failed (best-effort)
      if (m_queue_list_ && m_queue_list_->count() > 0) {
        setQueueRowStatus(m_queue_list_->count() - 1, QStringLiteral("failed"), true);
      }
      done();
      return;
    }
    const QVariant ct = reply->header(QNetworkRequest::ContentTypeHeader);
    if (ct.isValid()) {
      const QString s = ct.toString().toLower();
      const bool ok =
          s.contains(QStringLiteral("application/x-bittorrent")) ||
          s.contains(QStringLiteral("application/torrent")) ||
          s.contains(QStringLiteral("application/x-torrent")) ||
          s.contains(QStringLiteral("application/octet-stream")) ||
          s.contains(QStringLiteral("application/force-download"));
      const bool has_cd = !reply->rawHeader("content-disposition").isEmpty();
      if (!ok && !has_cd) {
        taiga::userFeedback(tr("Invalid content type for .torrent file: %1").arg(ct.toString()),
                            true);
        if (m_queue_list_ && m_queue_list_->count() > 0) {
          setQueueRowStatus(m_queue_list_->count() - 1, QStringLiteral("failed"), true);
        }
        done();
        return;
      }
    }
    const QByteArray body = reply->readAll();
    const QByteArray t = body.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      taiga::userFeedback(tr("The server returned a web page, not a .torrent file."), true);
      if (m_queue_list_ && m_queue_list_->count() > 0) {
        setQueueRowStatus(m_queue_list_->count() - 1, QStringLiteral("failed"), true);
      }
      done();
      return;
    }
    QFile f(full_path);
    if (!f.open(QIODevice::WriteOnly)) {
      taiga::userFeedback(tr("Could not write to %1").arg(full_path), true);
      if (m_queue_list_ && m_queue_list_->count() > 0) {
        setQueueRowStatus(m_queue_list_->count() - 1, QStringLiteral("failed"), true);
      }
      done();
      return;
    }
    f.write(body);
    f.close();
    taiga::userFeedback(tr("Saved %1").arg(full_path), false);
    if (auto* mw = mainWindow()) {
      mw->statusBar()->showMessage(tr("Saved torrent file."), 5000);
    }
    if (m_queue_list_ && m_queue_list_->count() > 0) {
      setQueueRowStatus(m_queue_list_->count() - 1, QStringLiteral("saved"), false);
    }

    // Optional: hand off the saved file to the configured torrent app (self-use; no broadcast).
    if (!taiga::settings.torrentAppOpen()) {
      done();
      return;
    }

    // Mode 1: default OS handler
    if (taiga::settings.torrentAppMode() != 2) {
      if (!QDesktopServices::openUrl(QUrl::fromLocalFile(full_path))) {
        taiga::userFeedback(tr("Could not open the torrent file with the default handler."), true);
      }
      done();
      return;
    }

    // Mode 2: custom executable
    const QString exe = QString::fromStdString(taiga::settings.torrentAppExecutablePath()).trimmed();
    if (!exe.isEmpty() && QFileInfo::exists(exe)) {
      // Only block when we would pass a save path to the client.
      if (!ensureClientDownloadBaseDir(this).has_value()) {
        done();
        return;
      }
      const QString dl_dir = resolvedTorrentDownloadDirForSavedTorrent(title_hint);
      const QStringList args = argsForTorrentClient(exe, full_path, dl_dir);
      if (!QProcess::startDetached(exe, args)) {
        taiga::userFeedback(tr("Could not start the torrent client executable. Check the path in Settings."), true);
      }
    }
    done();
  });
}

void TorrentFeedWidget::setSearchFallback(const QString& fallback) {
  m_search_fallback_title_ = fallback.trimmed();
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
    // Auto-retry with the fallback title (e.g. English title when romaji gave 0 results).
    if (kind == FetchKind::SearchRss && !m_search_fallback_title_.isEmpty() && m_query_edit_) {
      const QString fallback = m_search_fallback_title_;
      m_search_fallback_title_.clear();
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(
            tr("No results for '%1', retrying with '%2'…")
                .arg(m_query_edit_->text().trimmed(), fallback),
            4000);
      }
      m_query_edit_->setText(fallback);
      QTimer::singleShot(0, this, &TorrentFeedWidget::runSearch);
      return;
    }
    if (!silent) {
      if (auto* mw = mainWindow()) {
        QString msg = tr("Feed contained no items.");
        if (kind == FetchKind::SearchRss) {
          msg += u" " + tr("Tip: try a shorter title — e.g. remove the subtitle after ':' or ' — '.");
        }
        mw->statusBar()->showMessage(msg, 8000);
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
      if (shown == 0 && total > 0) {
        // Feed returned items but all were hidden by active filters.
        msg = tr("All %1 result(s) were hidden by active filters (list filters, regex, or archive "
                 "limit). Disable some filters in Settings → Library → Torrents → Filters.")
                  .arg(total);
        if (kind == FetchKind::SearchRss) {
          msg += u" " + tr("Tip: try a shorter search title too.");
        }
      } else if (shown < total) {
        const bool any_regex =
            !QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()).trimmed().isEmpty() ||
            !QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()).trimmed().isEmpty();
        const bool cap_on = taiga::settings.torrentFeedFilterEnabled();
        QStringList reasons;
        if (any_regex) reasons << tr("regex filters");
        if (cap_on) reasons << tr("feed archive limit");
        const QString why = reasons.isEmpty() ? tr("filters") : reasons.join(tr(" and "));
        msg = tr("Showing %1 of %2 item(s) (%3 active in Settings → Library).")
                  .arg(shown)
                  .arg(total)
                  .arg(why);
      }
      mw->statusBar()->showMessage(msg, 6000);
    }
  }
}

void TorrentFeedWidget::applyCatalogFingerprintState(const rss::Feed& feed, const bool notify_if_new) {
  QStringList keys;
  const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed);
  const size_t n = std::min(static_cast<size_t>(filtered.size()), static_cast<size_t>(kCatalogFingerprintCap));
  keys.reserve(static_cast<int>(n));
  for (size_t i = 0; i < n; ++i) {
    keys.append(fingerprintForItem(*filtered[static_cast<int>(i)]));
  }

  const QStringList old_list = taiga::session.torrentCatalogSeenFingerprints();
  const QSet<QString> old_set(old_list.begin(), old_list.end());
  int fresh = 0;
  QList<const rss::Item*> fresh_items;
  fresh_items.reserve(static_cast<int>(n));
  for (const QString& k : keys) {
    if (!old_set.contains(k)) {
      ++fresh;
    }
  }

  taiga::session.setTorrentCatalogSeenFingerprints(keys);

  if (notify_if_new && !old_list.isEmpty() && fresh > 0) {
    const auto act = taiga::settings.torrentDiscoveryNewCatalogAction();
    if (act == taiga::TorrentDiscoveryNewCatalogAction::Download) {
      // Auto-queue downloads only when it can run silently (no Save-As prompts).
      const QString save_dir = QString::fromStdString(taiga::settings.torrentFileSavePath());
      const bool can_queue_silently = !save_dir.trimmed().isEmpty() && QDir(save_dir).exists();
      if (!can_queue_silently) {
        const QString msg =
            tr("Catalog auto-check: %1 new item(s). Download-on-new is selected, but no torrent save "
               "folder is configured. Set one in Settings → Library → Torrents to enable auto-queue.")
                .arg(fresh);
        taiga::userFeedback(msg, false);
        return;
      }

      // Identify the specific fresh items within the fingerprint cap.
      for (int i = 0; i < static_cast<int>(n); ++i) {
        const QString k = keys[i];
        if (!old_set.contains(k)) {
          fresh_items.push_back(filtered[i]);
        }
      }

      // Enqueue only HTTP(S) .torrent enclosures/links. Magnet-only items are skipped (no safe silent path).
      const bool was_idle = m_save_queue_.isEmpty() && !m_save_reply_;
      int queued = 0;
      for (const rss::Item* it : fresh_items) {
        if (!it) continue;
        QString u = QString::fromStdString(it->enclosure.url).trimmed();
        if (u.isEmpty()) {
          const QString link = QString::fromStdString(it->link).trimmed();
          if (link.endsWith(u".torrent", Qt::CaseInsensitive)) u = link;
        }
        if (u.isEmpty()) continue;
        const QUrl url{u};
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || (scheme != u"http" && scheme != u"https")) continue;
        if (!url.path().toLower().endsWith(u".torrent")) continue;
        enqueueSaveTorrent(url, QString::fromStdString(it->title));
        ++queued;
      }

      if (queued <= 0) {
        const QString msg =
            tr("Catalog auto-check: %1 new item(s). Download-on-new is selected, but none had a "
               "downloadable .torrent enclosure. Open Torrents to review.")
                .arg(fresh);
        taiga::userFeedback(msg, false);
        return;
      }

      if (m_save_queue_dir_.isEmpty()) {
        // Ensure the queue prefers the configured folder (prevents any Save-As dialog during auto-check).
        m_save_queue_dir_ = save_dir;
      }

      const QString msg =
          tr("Catalog auto-check: queued %1 of %2 new item(s) for download.").arg(queued).arg(fresh);
      taiga::userFeedback(msg, false);
      if (was_idle) startNextQueuedSave();
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
  const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed);

  size_t n = static_cast<size_t>(filtered.size());
  if (taiga::settings.torrentFeedFilterEnabled()) {
    const int cap = taiga::settings.torrentFeedArchiveMaxItems();
    if (cap > 0 && n > static_cast<size_t>(cap)) n = static_cast<size_t>(cap);
  }

  m_table_->setRowCount(static_cast<int>(n));
  for (int i = 0; i < static_cast<int>(n); ++i) {
    const rss::Item& it = *filtered[static_cast<size_t>(i)];
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

    // Nicety: show parsed episode metadata (best-effort; does not affect links).
    const track::Episode ep = track::recognition::parse(it.title);
    const QString anime = QString::fromStdString(ep.element(anitomy::ElementKind::Title));
    const QString ep_no = QString::fromStdString(ep.element(anitomy::ElementKind::Episode));
    const QString group = QString::fromStdString(ep.element(anitomy::ElementKind::ReleaseGroup));
    const QString video = QString::fromStdString(ep.element(anitomy::ElementKind::VideoResolution));
    m_table_->setItem(i, 4, new QTableWidgetItem(anime));
    auto* ep_item = new NumericSortItem(ep_no);
    {
      bool ok = false;
      const qlonglong n = ep_no.toLongLong(&ok);
      if (ok) ep_item->setData(kNumericSortKeyRole, n);
    }
    m_table_->setItem(i, 5, ep_item);
    m_table_->setItem(i, 6, new QTableWidgetItem(group));
    m_table_->setItem(i, 7, new QTableWidgetItem(video));

    // Column 8: Seeders (from nyaa/extended namespace elements).
    int seeders_val = 0;
    if (const auto s = it.namespace_elements.find("seeders"); s != it.namespace_elements.end()) {
      bool ok = false;
      const int v = QString::fromStdString(s->second).toInt(&ok);
      if (ok && v >= 0) seeders_val = v;
    }
    auto* seed_item = new NumericSortItem(seeders_val > 0 ? QString::number(seeders_val) : QString{});
    if (seeders_val > 0) seed_item->setData(kNumericSortKeyRole, static_cast<qlonglong>(seeders_val));
    m_table_->setItem(i, 8, seed_item);
  }

  m_table_->resizeRowsToContents();
  m_table_->setSortingEnabled(true);
  applyRssTableSortFromSettings();
  applyResultFilter();
}

void TorrentFeedWidget::applyRssTableSortFromSettings() {
  if (!m_table_ || m_table_->rowCount() <= 0) return;
  const std::string sb = taiga::settings.torrentRssSortBy();
  // Columns: Title=0, Published=1, Page=2, Torrent=3, Anime=4, Ep=5, Group=6, Video=7, Seeds=8
  int sort_col = 0;
  if (sb == "release_date") {
    sort_col = 1;
  } else if (sb == "episode_number") {
    // Sort by parsed episode number when available.
    sort_col = 5;
  }
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
