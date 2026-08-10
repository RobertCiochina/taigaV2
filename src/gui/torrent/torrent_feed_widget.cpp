/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "torrent_feed_widget.hpp"

#include <QAbstractItemView>
#include <QChar>
#include <QClipboard>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTime>
#include <QTimeZone>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include "gui/main/main_window.hpp"
#include "gui/models/torrent_rss_model.hpp"
#include "gui/models/torrent_rss_proxy_model.hpp"
#include "gui/torrent/torrent_auto_cleanup.hpp"
#include "gui/utils/rss_feed_parser.hpp"
#include "gui/utils/table_view_defaults.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "taiga/network.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "taiga/torrent_discovery.hpp"
#include "taiga/user_feedback.hpp"
#include "track/episode.hpp"
#include "track/episode_offset.hpp"
#include "track/recognition.hpp"
#include "track/recognition_normalize.hpp"
#include "track/recognition_titles.hpp"
#include "track/scanner.hpp"

namespace gui {

namespace {
constexpr int kCatalogFingerprintCap = 100;
constexpr int kManualSearchSynonymCap = 6;

bool isMovieOrSpecial(const track::Episode& ep, const QString& title_full) {
  // Anitomy (Qt port) does not expose the v1-style AnimeType element, so use robust heuristics:
  // - Season 0 is the common convention for Specials/OVAs.
  // - Many releases include "(Movie)" / "(Special)" tokens in the title.
  bool season_zero = false;
  {
    bool ok = false;
    const int season = QString::fromStdString(ep.element(anitomy::ElementKind::Season)).toInt(&ok);
    season_zero = ok && season == 0;
  }
  static const QRegularExpression kMovieOrSpecialToken(QStringLiteral(R"(\b(movie|special)\b)"),
                                                       QRegularExpression::CaseInsensitiveOption);
  return season_zero || kMovieOrSpecialToken.match(title_full).hasMatch();
}

/// Strip subtitle after ": " (e.g. "Foo 4th Season: Bar" → "Foo 4th Season").
QString stripTitleSubtitle(const QString& s) {
  const int idx = s.indexOf(QStringLiteral(": "));
  return idx > 0 ? s.left(idx).trimmed() : s;
}

/// True when the title already carries a season token Nyaa-style search can use.
bool titleHasNyaaSeasonToken(const QString& s) {
  static const QRegularExpression re(
      QStringLiteral(
          R"(\b(?:\d+(?:st|nd|rd|th)\s+[Ss]eason|[Ss]eason\s+\d+|Part\s+\d+|S\d{1,2})\b)"),
      QRegularExpression::CaseInsensitiveOption);
  return re.match(s).hasMatch();
}

/// Convert ordinal/keyword season markers to compact "SNN" (e.g. "4th Season" → "S04").
QString toNyaaSeasonCodeTitle(const QString& s) {
  const QRegularExpression re_nth(QStringLiteral("\\b(\\d+)(?:st|nd|rd|th)\\s+[Ss]eason\\b"),
                                  QRegularExpression::CaseInsensitiveOption);
  auto m = re_nth.match(s);
  if (m.hasMatch()) {
    QString r = s;
    r.replace(m.capturedStart(), m.capturedLength(),
              QStringLiteral("S%1").arg(m.captured(1).toInt(), 2, 10, QChar('0')));
    return r.trimmed();
  }
  const QRegularExpression re_s(QStringLiteral("\\b[Ss]eason\\s+(\\d+)\\b"),
                                QRegularExpression::CaseInsensitiveOption);
  m = re_s.match(s);
  if (m.hasMatch()) {
    QString r = s;
    r.replace(m.capturedStart(), m.capturedLength(),
              QStringLiteral("S%1").arg(m.captured(1).toInt(), 2, 10, QChar('0')));
    return r.trimmed();
  }
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
}

/// Nyaa RSS only returns the newest ~75 hits. Bare franchise titles are flooded by the airing
/// season; older cours only show up when the query includes `SNN`. Convert "Season N" → `SNN`,
/// or append `S01` when the official title has no season marker (typical for season 1).
QString nyaaSeasonQualifiedTitle(const QString& title) {
  const QString t = title.trimmed();
  if (t.isEmpty()) return {};
  if (const QString coded = toNyaaSeasonCodeTitle(t); !coded.isEmpty()) return coded;
  if (titleHasNyaaSeasonToken(t)) return {};
  return t + QStringLiteral(" S01");
}

/// Season number from release/list title text (`S03`, `3rd Season`, `Season 3`, `Part 2`).
std::optional<int> seasonNumberFromTitleText(const QString& s) {
  const QString t = s.trimmed();
  if (t.isEmpty()) return std::nullopt;
  static const QRegularExpression re_compact(QStringLiteral(R"(\bS(\d{1,2})\b)"),
                                             QRegularExpression::CaseInsensitiveOption);
  if (const auto m = re_compact.match(t); m.hasMatch()) {
    const int n = m.captured(1).toInt();
    if (n > 0) return n;
  }
  static const QRegularExpression re_nth(QStringLiteral(R"(\b(\d+)(?:st|nd|rd|th)\s+[Ss]eason\b)"),
                                         QRegularExpression::CaseInsensitiveOption);
  if (const auto m = re_nth.match(t); m.hasMatch()) {
    const int n = m.captured(1).toInt();
    if (n > 0) return n;
  }
  static const QRegularExpression re_s(QStringLiteral(R"(\b[Ss]eason\s+(\d+)\b)"),
                                       QRegularExpression::CaseInsensitiveOption);
  if (const auto m = re_s.match(t); m.hasMatch()) {
    const int n = m.captured(1).toInt();
    if (n > 0) return n;
  }
  static const QRegularExpression re_p(QStringLiteral(R"(\bPart\s+(\d+)\b)"),
                                       QRegularExpression::CaseInsensitiveOption);
  if (const auto m = re_p.match(t); m.hasMatch()) {
    const int n = m.captured(1).toInt();
    if (n > 0) return n;
  }
  return std::nullopt;
}

/// Expected Nyaa/anitomy season for a list entry. Unmarked titles → season 1 (AniList S1 style).
int expectedTorrentSeasonForAnime(const anime::Details& item) {
  for (const QString& t :
       {QString::fromStdString(item.titles.english), QString::fromStdString(item.titles.romaji),
        QString::fromStdString(item.titles.japanese)}) {
    if (const auto n = seasonNumberFromTitleText(t)) return *n;
  }
  for (const auto& syn : item.titles.synonyms) {
    if (const auto n = seasonNumberFromTitleText(QString::fromStdString(syn))) return *n;
  }
  return 1;
}

bool isBatchLikeTitle(const QString& title_full) {
  static const QRegularExpression kBatchLike(
      QStringLiteral(R"((\bBatch\b|\bComplete\s+Collection\b|\bBD\s*Batch\b))"),
      QRegularExpression::CaseInsensitiveOption);
  return kBatchLike.match(title_full).hasMatch();
}

bool isBatchItem(const rss::Item& it) {
  const track::Episode ep = track::recognition::parse(it.title);
  const QString title_full = QString::fromStdString(it.title);
  const QString ep_str = QString::fromStdString(ep.element(anitomy::ElementKind::Episode));
  if (ep_str.contains(QChar('-'))) return true;
  if (isBatchLikeTitle(title_full)) return true;
  if (ep_str.isEmpty() && !isMovieOrSpecial(ep, title_full)) return true;
  return false;
}

std::optional<int> readNamespaceInt(const rss::Item& it, const QStringView key_suffix) {
  // rss_feed_parser stores the *original* tag name as the map key (e.g. "nyaa:downloads").
  // We want to match either "downloads" or any "prefix:downloads", case-insensitively.
  const QString want = key_suffix.toString().toLower();
  for (const auto& [k_raw, v_raw] : it.namespace_elements) {
    const QString k = QString::fromStdString(k_raw);
    const qsizetype colon = k.lastIndexOf(QLatin1Char(':'));
    const QString suffix = (colon >= 0 ? k.mid(colon + 1) : k).toLower();
    if (suffix != want) continue;
    bool ok = false;
    const int v = QString::fromStdString(v_raw).toInt(&ok);
    if (ok) return v;
  }
  return std::nullopt;
}

int downloadsForItem(const rss::Item& it) {
  return readNamespaceInt(it, u"downloads").value_or(0);
}

int seedersForItem(const rss::Item& it) {
  return readNamespaceInt(it, u"seeders").value_or(0);
}

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

bool normalizedTitlesOverlap(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  if (a == b) return true;
  // Require a reasonably long shared key so "bleach" alone does not pass.
  constexpr size_t kMin = 12;
  if (a.size() >= kMin && b.find(a) != std::string::npos) return true;
  if (b.size() >= kMin && a.find(b) != std::string::npos) return true;
  return false;
}

bool parsedTitleOverlapsAnime(const std::string& parsed_title, const anime::Details& item) {
  const std::string key = track::recognition::normalize(parsed_title);
  if (key.empty()) return false;
  if (normalizedTitlesOverlap(key, track::recognition::normalize(item.titles.romaji))) return true;
  if (normalizedTitlesOverlap(key, track::recognition::normalize(item.titles.english))) return true;
  if (normalizedTitlesOverlap(key, track::recognition::normalize(item.titles.japanese)))
    return true;
  for (const auto& syn : item.titles.synonyms) {
    if (normalizedTitlesOverlap(key, track::recognition::normalize(syn))) return true;
  }
  for (const auto& syn : track::recognition::syntheticTitleSynonyms(item)) {
    if (normalizedTitlesOverlap(key, track::recognition::normalize(syn))) return true;
  }
  if (item.id > 0) {
    for (const QString& alias : taiga::settings.animeRecognitionTitles(item.id)) {
      if (normalizedTitlesOverlap(key, track::recognition::normalize(alias.toStdString())))
        return true;
    }
  }
  return false;
}

std::optional<int> yearTokenInTitle(const QString& title) {
  static const QRegularExpression kYear(QStringLiteral(R"(\b((?:19|20)\d{2})\b)"));
  const auto m = kYear.match(title);
  if (!m.hasMatch()) return std::nullopt;
  bool ok = false;
  const int y = m.captured(1).toInt(&ok);
  if (!ok) return std::nullopt;
  return y;
}

/// When a search/download runs inside a specific anime context, drop RSS items that positively
/// identify as a *different* anime, look like the wrong type/year/season, or (when unrecognized)
/// do not overlap the target's titles. This prevents franchise movies / other cours from winning
/// autodl.
bool rssItemBelongsToAnimeContext(const rss::Item& it, const int context_anime_id) {
  if (context_anime_id <= 0) return true;
  const auto* item = anime::db.item(context_anime_id);
  if (!item) return true;

  track::Episode ep = track::recognition::parse(it.title);
  const QString title_full = QString::fromStdString(it.title);
  const int id = track::recognition::identify(ep);

  if (id != anime::kUnknownId && id != context_anime_id) return false;

  // Fansub titles often reuse the franchise base name; identify() can map S03E## to the S1 id.
  // Always honor an explicit season token in the release name vs the list entry's season.
  {
    int release_season = 0;
    const auto season_str = ep.element(anitomy::ElementKind::Season);
    if (!season_str.empty()) {
      release_season = QString::fromStdString(season_str).toInt();
    }
    if (release_season <= 0) {
      if (const auto n = seasonNumberFromTitleText(title_full)) release_season = *n;
    }
    if (release_season > 0 && release_season != expectedTorrentSeasonForAnime(*item)) {
      return false;
    }
  }

  // Cour-like context: reject movie/special packaging (old franchise BDs, etc.).
  // Movie/Special/OVA/Music entries must keep S00 / "Special"-tagged releases.
  const bool cour_like = item->type == anime::Type::Tv || item->type == anime::Type::Ona ||
                         item->type == anime::Type::Unknown;
  if (cour_like && isMovieOrSpecial(ep, title_full)) return false;

  if (const auto year = yearTokenInTitle(title_full)) {
    const int start_y = static_cast<int>(item->date_started.year());
    if (start_y > 0 && std::abs(*year - start_y) > 1) return false;
  }

  if (id == anime::kUnknownId) {
    const std::string parsed = ep.element(anitomy::ElementKind::Title);
    if (parsed.empty()) return false;
    return parsedTitleOverlapsAnime(parsed, *item);
  }
  return true;
}

std::optional<qint64> contextAnimeStartMs(const anime::Details& item) {
  if (item.date_started.empty()) return std::nullopt;
  const int y = static_cast<int>(item.date_started.year());
  int m = static_cast<int>(item.date_started.month());
  int d = static_cast<int>(item.date_started.day());
  if (y <= 0) return std::nullopt;
  if (m <= 0) m = 1;
  if (d <= 0) d = 1;
  const QDate date(y, m, d);
  if (!date.isValid()) return std::nullopt;
  // 1-day grace: fansubs often land the UTC day before AniList's calendar start (JST air).
  return QDateTime(date.addDays(-1), QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
}

std::optional<qint64> rssItemPublishedMs(const rss::Item& it) {
  const QString s = QString::fromStdString(it.pub_date).trimmed();
  if (s.isEmpty()) return std::nullopt;
  {
    const QDateTime dt = QDateTime::fromString(s, Qt::RFC2822Date);
    if (dt.isValid()) return dt.toMSecsSinceEpoch();
  }
  {
    const QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) return dt.toMSecsSinceEpoch();
  }
  return std::nullopt;
}

/// `context_anime_id > 0` restricts results to a single anime (see rssItemBelongsToAnimeContext);
/// pass 0 for free-text / catalog feeds that are not tied to one entry.
QList<const rss::Item*> filterRssItemsBySettings(const rss::Feed& feed,
                                                 const int context_anime_id = 0) {
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
          1,
          QString::fromStdString(ep.element(anitomy::ElementKind::ReleaseVersion, std::string{"1"}))
              .toInt());
      const qulonglong key =
          (static_cast<qulonglong>(anime_id) << 32) | static_cast<qulonglong>(ep_no);
      const int cur = max_version_for_key.value(key, 1);
      if (ver > cur) max_version_for_key.insert(key, ver);
    }
  }

  const std::optional<qint64> context_start_ms = [&]() -> std::optional<qint64> {
    if (context_anime_id <= 0) return std::nullopt;
    const auto* item = anime::db.item(context_anime_id);
    if (!item) return std::nullopt;
    // Always apply for anime-context fetches (manual + autodl) when start date is known —
    // blocks old franchise BD dumps whose pubDate predates the cour.
    return contextAnimeStartMs(*item);
  }();

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

    // Anime-scoped search/download: keep only releases that belong to the target entry.
    if (!rssItemBelongsToAnimeContext(it, context_anime_id)) continue;

    if (context_start_ms.has_value()) {
      const auto pub_ms = rssItemPublishedMs(it);
      if (pub_ms.has_value() && *pub_ms < *context_start_ms) continue;
    }

    if (hide_dropped || hide_not_in_list || hide_watched || hide_available || hide_older_versions) {
      track::Episode ep = track::recognition::parse(it.title);
      const int id = track::recognition::identify(ep);
      const ListEntry* entry = (id != anime::kUnknownId) ? anime::db.entry(id) : nullptr;
      const auto st = entry ? entry->status : anime::list::Status::NotInList;
      // Only apply "not in list" when the anime was positively identified.
      // Unrecognized items (id == kUnknownId) have no list membership data —
      // hiding them would silently discard Specials/OVAs that merely failed recognition.
      if (hide_not_in_list && id != anime::kUnknownId && st == anime::list::Status::NotInList)
        continue;
      if (hide_dropped && st == anime::list::Status::Dropped) continue;

      int ep_no = QString::fromStdString(ep.element(anitomy::ElementKind::Episode)).toInt();
      // S00 (season 0) is the Nyaa / AniDB convention for Specials/OVAs.
      // Their episode numbers live in a different namespace from the main series, so
      // comparing S00E01 against main-series watched_episodes gives false positives.
      // Skip episode-based checks entirely for season-0 releases.
      const auto season_val_str = ep.element(anitomy::ElementKind::Season);
      const bool is_season_zero =
          !season_val_str.empty() && QString::fromStdString(season_val_str).toInt() == 0;
      if (id != anime::kUnknownId && ep_no > 0 && !is_season_zero) {
        const int list_ep = track::toListEpisode(id, ep_no);
        if (list_ep > 0) ep_no = list_ep;
        if (hide_watched && entry && ep_no <= entry->watched_episodes) continue;
        if (hide_available && track::libraryHasLocalEpisode(id, ep_no)) continue;
        if (hide_older_versions && !max_version_for_key.isEmpty()) {
          const qulonglong key =
              (static_cast<qulonglong>(id) << 32) | static_cast<qulonglong>(ep_no);
          const int maxv = max_version_for_key.value(key, 1);
          const int v =
              std::max(1, QString::fromStdString(
                              ep.element(anitomy::ElementKind::ReleaseVersion, std::string{"1"}))
                              .toInt());
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
    QMessageBox::warning(
        &dlg, QObject::tr("Taiga"),
        QObject::tr("Could not create the folder:\n%1").arg(QDir::toNativeSeparators(base)));
  });

  QObject::connect(btnChoose, &QPushButton::clicked, &dlg, [&]() {
    const QString start =
        !base.isEmpty() ? base : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
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
  const bool custom_client =
      taiga::settings.torrentAppOpen() && taiga::settings.torrentAppMode() == 2;
  const QString exe = QString::fromStdString(taiga::settings.torrentAppExecutablePath());
  if (custom_client && !exe.isEmpty() && QFileInfo::exists(exe)) {
    if (QProcess::startDetached(exe, QStringList{url})) {
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(
            QCoreApplication::translate("TorrentFeedWidget", "Launched torrent client."), 2500);
      }
      return;
    }
    taiga::userFeedback(
        QCoreApplication::translate(
            "TorrentFeedWidget",
            "Could not start the torrent client executable. Using the default URL handler "
            "instead."),
        true);
  }
  if (!QDesktopServices::openUrl(QUrl::fromUserInput(url))) {
    taiga::userFeedback(QCoreApplication::translate("TorrentFeedWidget", "Could not open the URL."),
                        true);
  }
}
}  // namespace

QTreeView* TorrentFeedWidget::activeView() const {
  if (!m_tabs_) return nullptr;
  const int idx = m_tabs_->currentIndex();
  if (idx == 0) return m_view_eps_;
  if (idx == 1) return m_view_batches_;
  return m_view_eps_;
}

QTreeView* TorrentFeedWidget::otherView(const QTreeView* view) const {
  if (!view) return nullptr;
  if (view == m_view_eps_) return m_view_batches_;
  if (view == m_view_batches_) return m_view_eps_;
  return nullptr;
}

void TorrentFeedWidget::syncHeaderStateFrom(QTreeView* source) {
  if (m_syncing_header_state_) return;
  if (!source) return;
  auto* src_hdr = source->header();
  if (!src_hdr) return;
  m_hdr_sync_source_ = source;
  m_hdr_sync_state_ = src_hdr->saveState();
  // Do not push state to the other tab immediately: restoring header state while the user is
  // dragging a divider can make resizing feel blocked or unpredictable. We apply the stored
  // state when switching tabs (see tab-changed handler below).
  if (m_hdr_sync_timer_) m_hdr_sync_timer_->start(0);
}

TorrentFeedWidget::TorrentFeedWidget(QLineEdit* toolbar_query_edit, QWidget* parent)
    : QWidget(parent), m_query_edit_(toolbar_query_edit) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* hint = new QLabel(
      tr("Results load inside Taiga. Double-click opens the <b>primary</b> download link (magnet "
         "vs "
         ".torrent order follows the prefer-magnet checkbox in Settings → Library). When a "
         "<b>custom torrent client</b> is configured, that executable receives the link instead of "
         "the "
         "default OS handler. Column headers sort the table; their layout is remembered between "
         "sessions. <b>F5</b> fetches search RSS; <b>Ctrl+F5</b> refreshes the catalog feed. If "
         "enabled "
         "in Settings, the catalog RSS also refreshes periodically in the background. "
         "<b>Ctrl+C</b> "
         "copies the primary link for the current row. The filter text is remembered between "
         "sessions; "
         "<b>Esc</b> in the filter field clears it. Use the toolbar search field, then <b>Fetch "
         "RSS</b> "
         "or <b>Enter</b>."),
      this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto* row = new QHBoxLayout();
  m_btn_fetch_ = new QPushButton(tr("Fetch RSS"), this);
  m_btn_browser_ = new QPushButton(tr("Open in web browser…"), this);
  m_btn_catalog_ = new QPushButton(tr("Refresh catalog feed…"), this);
  m_btn_catalog_->setToolTip(tr("Uses the catalog RSS URL from Settings → Library."));
  row->addWidget(m_btn_fetch_);
  row->addWidget(m_btn_browser_);
  row->addWidget(m_btn_catalog_);
  m_btn_download_selected_ = new QPushButton(tr("Download selected"), this);
  m_btn_download_selected_->setToolTip(tr(
      "Save the selected .torrent files in sequence and open them in your configured torrent app "
      "(default handler or custom executable). Magnet-only rows are opened directly.\n"
      "Tip: use Ctrl/Shift to select multiple rows."));
  row->addWidget(m_btn_download_selected_);
  m_btn_download_best_ = new QPushButton(tr("⬇ Best match"), this);
  m_btn_download_best_->setToolTip(
      tr("Download the visible result with the highest download count (or the first visible result "
         "if download data is unavailable). Apply filters first for best results."));
  row->addWidget(m_btn_download_best_);
  m_btn_cancel_downloads_ = new QPushButton(tr("Cancel downloads"), this);
  m_btn_cancel_downloads_->setToolTip(
      tr("Cancel the active .torrent download and clear the queue."));
  m_btn_cancel_downloads_->setEnabled(false);
  row->addWidget(m_btn_cancel_downloads_);
  m_btn_clear_queue_ = new QPushButton(tr("Clear queue"), this);
  m_btn_clear_queue_->setToolTip(
      tr("Clear the queue list and pending items (does not delete files)."));
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

  m_tabs_ = new QTabWidget(this);
  m_tabs_->setDocumentMode(true);
  m_tabs_->setMovable(false);

  m_rss_model_ = new TorrentRssModel(this);
  m_proxy_eps_ = new TorrentRssProxyModel(this);
  m_proxy_eps_->setSourceModel(m_rss_model_);
  m_proxy_eps_->setShowBatches(false);
  m_proxy_batches_ = new TorrentRssProxyModel(this);
  m_proxy_batches_->setSourceModel(m_rss_model_);
  m_proxy_batches_->setShowBatches(true);

  m_view_eps_ = new QTreeView(m_tabs_);
  m_view_batches_ = new QTreeView(m_tabs_);

  const auto apply_header_policy = [](QTreeView* v) {
    if (!v) return;
    auto* hdr = v->header();
    if (!hdr) return;
    // Match Anime List / other tables: predictable hit-testing + user-resizable columns.
    hdr->setSectionsClickable(true);
    hdr->setSectionsMovable(false);
    hdr->setStretchLastSection(false);
    hdr->setCascadingSectionResizes(false);
    hdr->setTextElideMode(Qt::ElideRight);
    hdr->setSectionResizeMode(QHeaderView::Interactive);
  };

  const auto init_view = [apply_header_policy](QTreeView* v, QAbstractItemModel* model) {
    if (!v) return;
    v->setFrameShape(QFrame::Shape::NoFrame);
    v->setAlternatingRowColors(true);
    v->setRootIsDecorated(false);
    v->setItemsExpandable(false);
    v->setExpandsOnDoubleClick(false);
    v->setAllColumnsShowFocus(true);
    v->setMouseTracking(true);
    v->viewport()->setAttribute(Qt::WA_Hover, true);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    v->setSortingEnabled(true);
    v->setModel(model);
    gui::tables::applyDefaults(v);

    auto* hdr = v->header();
    apply_header_policy(v);
    hdr->setMouseTracking(true);
    if (hdr->viewport()) hdr->viewport()->setMouseTracking(true);

    // Column sizing is handled centrally via gui::tables::applyDefaults (fit-to-contents, capped).
  };

  init_view(m_view_eps_, m_proxy_eps_);
  init_view(m_view_batches_, m_proxy_batches_);

  m_tabs_->addTab(m_view_eps_, tr("Episodes"));
  m_tabs_->addTab(m_view_batches_, tr("Batches"));
  layout->addWidget(m_tabs_, 1);

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
    const bool ok_eps = m_view_eps_->header()->restoreState(hdr);
    const bool ok_batch = m_view_batches_->header()->restoreState(hdr);
    if (!ok_eps || !ok_batch) taiga::session.setTorrentRssTableHeaderState({});
    // `restoreState` can restore non-Interactive modes; re-apply our policy (Anime List does this
    // too).
    apply_header_policy(m_view_eps_);
    apply_header_policy(m_view_batches_);
  }

  const auto headerLooksSane = [](QTreeView* v) -> bool {
    if (!v || !v->header()) return true;
    auto* h = v->header();
    return h->sectionSize(TorrentRssModel::COLUMN_TITLE) >= 24 &&
           h->sectionSize(TorrentRssModel::COLUMN_PUBLISHED) >= 24 &&
           h->sectionSize(TorrentRssModel::COLUMN_PAGE) >= 24;
  };
  if (!headerLooksSane(m_view_eps_) || !headerLooksSane(m_view_batches_)) {
    taiga::session.setTorrentRssTableHeaderState({});
    // Reapply the init_view defaults.
    init_view(m_view_eps_, m_proxy_eps_);
    init_view(m_view_batches_, m_proxy_batches_);
  }

  // Seed the cross-tab sync state from the current header.
  if (m_view_eps_ && m_view_eps_->header()) {
    m_hdr_sync_state_ = m_view_eps_->header()->saveState();
    m_hdr_sync_source_ = m_view_eps_;
  }

  m_hdr_sync_timer_ = new QTimer(this);
  m_hdr_sync_timer_->setSingleShot(true);
  connect(m_hdr_sync_timer_, &QTimer::timeout, this, []() {
    // Intentionally no-op: we only store the most recent header state in m_hdr_sync_state_.
  });

  const auto wire_header_sync = [this](QTreeView* v) {
    auto* hdr = v->header();
    hdr->setCascadingSectionResizes(false);
    connect(hdr, &QHeaderView::sectionMoved, this,
            [this, v](int, int, int) { syncHeaderStateFrom(v); });
    // On resize, only store the state; do not apply cross-tab while dragging.
    connect(hdr, &QHeaderView::sectionResized, this,
            [this, v](int, int, int) { syncHeaderStateFrom(v); });
    connect(hdr, &QHeaderView::sortIndicatorChanged, this,
            [this, v](int, Qt::SortOrder) { syncHeaderStateFrom(v); });
  };
  wire_header_sync(m_view_eps_);
  wire_header_sync(m_view_batches_);

  // When switching tabs, sync the newly-shown header to the last-known header state so
  // both tabs stay aligned, without interfering with resize drags.
  connect(m_tabs_, &QTabWidget::currentChanged, this, [this, apply_header_policy](int) {
    QTreeView* view = activeView();
    if (!view) return;
    auto* hdr = view->header();
    if (!hdr) return;
    if (m_hdr_sync_state_.isEmpty()) return;
    m_syncing_header_state_ = true;
    hdr->restoreState(m_hdr_sync_state_);
    apply_header_policy(view);
    m_syncing_header_state_ = false;
  });

  {
    auto* sc_refresh = new QShortcut(QKeySequence::Refresh, this);
    sc_refresh->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_refresh, &QShortcut::activated, this, &TorrentFeedWidget::runSearch);
    auto* sc_cat = new QShortcut(QKeySequence{Qt::CTRL | Qt::Key_F5}, this);
    sc_cat->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_cat, &QShortcut::activated, this, &TorrentFeedWidget::refreshCatalogFeed);
    auto* sc_copy = new QShortcut(QKeySequence::Copy, this);
    sc_copy->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc_copy, &QShortcut::activated, this, [this]() {
      QTreeView* view = activeView();
      if (!view) return;
      const QModelIndex idx = view->currentIndex();
      if (!idx.isValid()) return;
      const QString url = primaryUrlForIndex(idx);
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
    QTreeView* view = activeView();
    if (!view) return;
    if (m_save_reply_ || !m_save_queue_.isEmpty()) {
      taiga::userFeedback(tr("A download queue is already running. Cancel it first if needed."),
                          true);
      return;
    }
    const auto rows =
        view->selectionModel() ? view->selectionModel()->selectedRows() : QModelIndexList{};
    if (rows.isEmpty()) {
      taiga::userFeedback(tr("Select one or more rows first."), true);
      return;
    }

    // Resolve URL + folder for each selected row.
    struct SelItem {
      QString url;
      QString folder;
      bool is_http_torrent;
    };
    QList<SelItem> items;
    for (const QModelIndex& idx : rows) {
      if (!idx.isValid()) continue;
      const QModelIndex idx0 = idx.sibling(idx.row(), TorrentRssModel::COLUMN_TITLE);
      const QString tor_u = idx0.data(TorrentRssModel::TorrentUrlRole).toString();
      const QString magnet_u = idx0.data(TorrentRssModel::MagnetUrlRole).toString();
      const QString anime_title = idx.sibling(idx.row(), TorrentRssModel::COLUMN_ANIME)
                                      .data(Qt::DisplayRole)
                                      .toString()
                                      .trimmed();
      const QString title_text = idx0.data(Qt::DisplayRole).toString();
      const QString raw_hint = anime_title.isEmpty() ? title_text : anime_title;
      const QString title = bestFolderNameForAnime(raw_hint);
      const bool has_http = httpUrlFromUserString(tor_u).has_value();
      // Prefer magnet → .torrent URL → page link.
      const QString url =
          !magnet_u.isEmpty() ? magnet_u : (!tor_u.isEmpty() ? tor_u : primaryUrlForIndex(idx));
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
        addTorrentViaQBitApi(it.url, save_path, [sent, total](bool ok, const QString& err) {
          if (!err.isEmpty()) taiga::userFeedback(QStringLiteral("qBit: ") + err, true);
          if (ok && ++(*sent) == total) {
            if (auto* mw = mainWindow())
              mw->statusBar()->showMessage(tr("Sent %1 torrent(s) to qBittorrent.").arg(total),
                                           6000);
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
      const QString save_dir =
          QString::fromStdString(taiga::settings.torrentFileSavePath()).trimmed();
      if (enqueued > 1 && (save_dir.isEmpty() || !QDir(save_dir).exists())) {
        const QString downloads =
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
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
    QTreeView* view = activeView();
    if (!view) return;
    if (m_save_reply_ || !m_save_queue_.isEmpty()) {
      taiga::userFeedback(tr("A download queue is already running. Cancel it first if needed."),
                          true);
      return;
    }

    // Build a virtual list from the filtered proxy model rows.
    struct VisibleRow {
      int episode = 0;  // parsed episode no (-1=batch, 0=unknown)
      qlonglong downloads = 0;
      QString magnet;
      QString tor_url;
      QString page_url;
      QString folder;  // per-row folder hint
    };

    QList<VisibleRow> rows;
    auto* m = view->model();
    if (!m) return;
    for (int r = 0; r < m->rowCount(); ++r) {
      const QModelIndex idx0 = m->index(r, TorrentRssModel::COLUMN_TITLE);
      if (!idx0.isValid()) continue;
      const QString tor_u = idx0.data(TorrentRssModel::TorrentUrlRole).toString();
      const QString mag_u = idx0.data(TorrentRssModel::MagnetUrlRole).toString();
      const QString page_u = idx0.data(TorrentRssModel::PageUrlRole).toString();
      const int ep_no = idx0.data(TorrentRssModel::EpisodeRole).toInt();
      const qlonglong dl = idx0.data(TorrentRssModel::DownloadsRole).toLongLong();
      const QString anime_title =
          m->index(r, TorrentRssModel::COLUMN_ANIME).data(Qt::DisplayRole).toString().trimmed();
      const QString title_text = idx0.data(Qt::DisplayRole).toString();
      const QString raw_hint = anime_title.isEmpty() ? title_text : anime_title;
      rows.append(
          {ep_no == 0 ? 0 : ep_no, dl, mag_u, tor_u, page_u, bestFolderNameForAnime(raw_hint)});
    }

    if (rows.isEmpty()) {
      taiga::userFeedback(tr("No results visible — try fetching the RSS feed first."), true);
      return;
    }

    // Group by episode: keep the best-downloaded row per unique episode number.
    QMap<int, int> best_idx_per_ep;  // episode → rows[] index
    QMap<int, qlonglong> best_downloads_per_ep;
    for (int i = 0; i < rows.size(); ++i) {
      const auto& row = rows[i];
      const auto it = best_downloads_per_ep.find(row.episode);
      if (it == best_downloads_per_ep.end() || row.downloads > it.value()) {
        best_downloads_per_ep[row.episode] = row.downloads;
        best_idx_per_ep[row.episode] = i;
      }
    }

    // Collect items to download (one per unique episode).
    struct Target {
      QString url;
      QString folder;
      int episode;
    };
    QList<Target> targets;
    for (auto it = best_idx_per_ep.begin(); it != best_idx_per_ep.end(); ++it) {
      const auto& row = rows[it.value()];
      const QString url = !row.magnet.isEmpty()
                              ? row.magnet
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
        addTorrentViaQBitApi(t.url, save_path, [t, sent, total](bool ok, const QString& err) {
          if (!err.isEmpty()) taiga::userFeedback(QStringLiteral("qBit: ") + err, true);
          if (ok) {
            ++(*sent);
            if (auto* mw = mainWindow())
              mw->statusBar()->showMessage(tr("Sent to qBittorrent: %1 ep%2 (%3/%4)")
                                               .arg(t.folder)
                                               .arg(t.episode)
                                               .arg(*sent)
                                               .arg(total),
                                           4000);
          }
        });
      }
    } else {
      int enqueued = 0;
      for (const auto& t : targets) {
        if (const auto u = httpUrlFromUserString(t.url)) {
          enqueueSaveTorrent(*u, t.folder);
          ++enqueued;
        } else {
          openPrimaryTorrentUrl(t.url);
        }
      }
      if (enqueued > 0) startNextQueuedSave();
      if (auto* mw = mainWindow())
        mw->statusBar()->showMessage(tr("Queued %1 torrent(s) — best per episode.").arg(total),
                                     5000);
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

  const auto wire_view = [this](QTreeView* view) {
    if (!view) return;

    connect(view, &QAbstractItemView::doubleClicked, this, [](const QModelIndex& idx) {
      const QString primary = TorrentFeedWidget::primaryUrlForIndex(idx);
      if (!primary.isEmpty()) openPrimaryTorrentUrl(primary);
    });

    connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint& pos) {
      const QModelIndex idx = view->indexAt(pos);
      if (!idx.isValid()) return;

      const QModelIndex idx0 = idx.sibling(idx.row(), TorrentRssModel::COLUMN_TITLE);
      const QString page_u = idx0.data(TorrentRssModel::PageUrlRole).toString();
      const QString tor_u = idx0.data(TorrentRssModel::TorrentUrlRole).toString();
      const QString magnet_u = idx0.data(TorrentRssModel::MagnetUrlRole).toString();
      const QString title_u = idx0.data(Qt::DisplayRole).toString();
      const QString primary = primaryUrlForIndex(idx0);

      auto* menu = new QMenu(this);

      const QModelIndexList selected_rows =
          view->selectionModel() ? view->selectionModel()->selectedRows() : QModelIndexList{};
      if (selected_rows.size() > 1 && selected_rows.contains(idx0)) {
        menu->addAction(tr("Download selected"), this, [this]() {
          if (m_btn_download_selected_) m_btn_download_selected_->click();
        });
        menu->addAction(
            tr("Discard selected titles (hide in future)"), this, [this, selected_rows]() {
              QStringList archive = taiga::settings.torrentFeedDiscardedTitleArchive();
              int added = 0;
              for (const QModelIndex& mi : selected_rows) {
                const QString t = mi.data(Qt::DisplayRole).toString().trimmed();
                if (t.isEmpty()) continue;
                if (!archive.contains(t)) {
                  archive.push_back(t);
                  ++added;
                }
              }
              if (added > 0) taiga::settings.setTorrentFeedDiscardedTitleArchive(archive);
              if (m_proxy_eps_) m_proxy_eps_->refresh();
              if (m_proxy_batches_) m_proxy_batches_->refresh();
              if (auto* mw = mainWindow()) {
                mw->statusBar()->showMessage(tr("Discarded %1 title(s).").arg(added), 4000);
              }
            });
        menu->addAction(tr("Cancel downloads"), this, [this]() {
          if (!m_save_reply_ && m_save_queue_.isEmpty()) return;
          cancelSaveTorrent();
        });
        menu->addAction(tr("Copy primary links (newline-separated)"), this, [selected_rows]() {
          QStringList lines;
          lines.reserve(selected_rows.size());
          for (const QModelIndex& mi : selected_rows) {
            const QString u = TorrentFeedWidget::primaryUrlForIndex(mi);
            if (!u.isEmpty()) lines.push_back(u);
          }
          if (!lines.isEmpty())
            QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
        });
        menu->addSeparator();
      }

      const QString client_dl = QString::fromStdString(taiga::settings.torrentClientDownloadPath());
      const QString torrent_save = QString::fromStdString(taiga::settings.torrentFileSavePath());
      const QString client_abs = client_dl.isEmpty() ? QString{} : QDir{client_dl}.absolutePath();
      const QString save_abs =
          torrent_save.isEmpty() ? QString{} : QDir{torrent_save}.absolutePath();
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
      if (!menu->actions().isEmpty()) menu->addSeparator();

      if (!primary.isEmpty()) {
        menu->addAction(tr("Open primary link"), this,
                        [primary]() { openPrimaryTorrentUrl(primary); });
        menu->addAction(tr("Copy primary link"), this,
                        [primary]() { QGuiApplication::clipboard()->setText(primary); });
      }

      if (!title_u.trimmed().isEmpty()) {
        menu->addAction(tr("Discard this title (hide in future)"), this, [this, title_u]() {
          QStringList archive = taiga::settings.torrentFeedDiscardedTitleArchive();
          const QString t = title_u.trimmed();
          if (!archive.contains(t)) {
            archive.push_back(t);
            taiga::settings.setTorrentFeedDiscardedTitleArchive(archive);
          }
          if (m_proxy_eps_) m_proxy_eps_->refresh();
          if (m_proxy_batches_) m_proxy_batches_->refresh();
          if (auto* mw = mainWindow()) {
            mw->statusBar()->showMessage(tr("Discarded \"%1\".").arg(t), 4000);
          }
        });
      }

      if (!magnet_u.isEmpty() && magnet_u != tor_u && !tor_u.isEmpty()) {
        menu->addAction(tr("Open .torrent URL"), this,
                        [tor_u]() { QDesktopServices::openUrl(QUrl::fromUserInput(tor_u)); });
        menu->addAction(tr("Copy .torrent URL"), this,
                        [tor_u]() { QGuiApplication::clipboard()->setText(tor_u); });
      }

      if (const auto tor_http = httpUrlFromUserString(tor_u)) {
        menu->addAction(
            tr("Save .torrent file…"), this,
            [this, url = *tor_http, title_hint = title_u]() { beginSaveTorrent(url, title_hint); });
      }

      if (taiga::settings.torrentAppOpen() && taiga::settings.torrentAppMode() == 2) {
        const QString exe = QString::fromStdString(taiga::settings.torrentAppExecutablePath());
        if (!exe.isEmpty() && QFileInfo::exists(exe)) {
          QString link_for_client = !magnet_u.isEmpty() ? magnet_u : tor_u;
          if (link_for_client.isEmpty()) link_for_client = primary;
          if (!link_for_client.isEmpty()) {
            menu->addAction(
                tr("Launch configured torrent client with this link"), this,
                [exe, link_for_client]() {
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
        menu->addAction(tr("Open info page in browser"), this,
                        [page_u]() { QDesktopServices::openUrl(QUrl::fromUserInput(page_u)); });
        menu->addAction(tr("Copy page URL"), this,
                        [page_u]() { QGuiApplication::clipboard()->setText(page_u); });
      }

      menu->addSeparator();
      menu->addAction(tr("Copy row (tab-separated)"), this, [idx0, tor_u, magnet_u]() {
        const QString title = idx0.data(Qt::DisplayRole).toString();
        const QString pub = idx0.sibling(idx0.row(), TorrentRssModel::COLUMN_PUBLISHED)
                                .data(Qt::DisplayRole)
                                .toString();
        const QString page =
            idx0.sibling(idx0.row(), TorrentRssModel::COLUMN_PAGE).data(Qt::DisplayRole).toString();
        const QString links = (!magnet_u.isEmpty() && !tor_u.isEmpty())
                                  ? (magnet_u + QStringLiteral("\t") + tor_u)
                                  : (!magnet_u.isEmpty() ? magnet_u : tor_u);
        const QString line =
            title + QLatin1Char('\t') + pub + QLatin1Char('\t') + page + QLatin1Char('\t') + links;
        QGuiApplication::clipboard()->setText(line);
      });

      menu->exec(view->viewport()->mapToGlobal(pos));
    });
  };

  wire_view(m_view_eps_);
  wire_view(m_view_batches_);
}

void TorrentFeedWidget::addTorrentViaQBitApi(const QString& torrent_url, const QString& save_path,
                                             std::function<void(bool ok, QString error)> on_done,
                                             const bool interactive) {
  // Serial queue: parallel adds overload Qt's per-host connection limit and hit the global
  // transfer timeout, so later episodes fail until the next auto-download retry.
  PendingQBitAdd job;
  job.torrent_url = torrent_url;
  job.save_path = save_path;
  job.on_done = std::move(on_done);
  job.interactive = interactive;
  m_qbit_add_queue_.enqueue(std::move(job));
  updateQBitCancelButton();
  startNextQBitAdd();
}

void TorrentFeedWidget::updateQBitCancelButton() {
  if (!m_btn_cancel_downloads_) return;
  const bool busy = m_qbit_add_active_ || !m_qbit_add_queue_.isEmpty() || m_save_reply_ ||
                    !m_save_queue_.isEmpty();
  m_btn_cancel_downloads_->setEnabled(busy);
}

void TorrentFeedWidget::startNextQBitAdd() {
  if (m_qbit_add_active_) return;
  if (m_qbit_add_queue_.isEmpty()) {
    updateQBitCancelButton();
    return;
  }
  m_qbit_add_active_ = true;
  updateQBitCancelButton();
  const PendingQBitAdd job = m_qbit_add_queue_.dequeue();
  performQBitAdd(job, m_qbit_add_generation_);
}

void TorrentFeedWidget::cancelPendingQBitAdds() {
  ++m_qbit_add_generation_;
  m_qbit_add_active_ = false;
  QQueue<PendingQBitAdd> pending = std::move(m_qbit_add_queue_);
  m_qbit_add_queue_.clear();
  while (!pending.isEmpty()) {
    const PendingQBitAdd job = pending.dequeue();
    if (job.on_done) job.on_done(false, tr("Cancelled."));
  }
  updateQBitCancelButton();
}

void TorrentFeedWidget::performQBitAdd(PendingQBitAdd job, const quint64 generation) {
  const QString torrent_url = job.torrent_url;
  const QString save_path = job.save_path;
  const bool interactive = job.interactive;
  // Move callback into a shared_ptr so cancel/finish can hand it off once.
  const auto on_done = std::make_shared<std::function<void(bool, QString)>>(std::move(job.on_done));
  const auto finished = std::make_shared<bool>(false);

  const auto finish = [this, generation, on_done, finished](const bool ok, const QString& err) {
    if (*finished) return;
    *finished = true;
    if (generation != m_qbit_add_generation_) {
      // Superseded by cancel: report failure for the in-flight job only.
      if (*on_done) (*on_done)(false, tr("Cancelled."));
      return;
    }
    m_qbit_add_active_ = false;
    if (*on_done) (*on_done)(ok, err);
    // Brief pause so qBittorrent can accept the next add after it starts resolving a magnet;
    // otherwise later /torrents/add calls hit the global 10s transfer timeout once qBit is busy.
    QTimer::singleShot(400, this, [this, generation]() {
      if (generation != m_qbit_add_generation_) return;
      startNextQBitAdd();
    });
  };

  const QString base_url =
      QString::fromStdString(taiga::settings.torrentQBitApiUrl()).trimmed().trimmed();

  // qBit may take well over the NAM's default 10s while resolving magnets / under load.
  constexpr int kQBitTransferTimeoutMs = 120000;

  // Keep attempt/retry alive across async network replies (no stack `[&]` captures).
  // Use weak_ptr in the body so the shared_ptr does not form a retain cycle with itself.
  using AttemptFn = std::function<void(const QString& user, const QString& pass, bool allow_retry)>;
  const auto attempt = std::make_shared<AttemptFn>();
  const std::weak_ptr<AttemptFn> attempt_weak = attempt;

  *attempt = [this, torrent_url, save_path, base_url, interactive, attempt_weak, finish,
              generation](const QString& user, const QString& pass, const bool allow_retry) {
    if (generation != m_qbit_add_generation_) {
      finish(false, tr("Cancelled."));
      return;
    }

    // Strong ref for the duration of this attempt's in-flight network work.
    const auto keep_alive = attempt_weak.lock();
    if (!keep_alive) {
      finish(false, tr("qBittorrent request was cancelled."));
      return;
    }

    const auto fail = [this, interactive, finish](const QString& err, const bool offer_guidance) {
      if (interactive && offer_guidance) {
        QMessageBox::warning(
            this, tr("Taiga"),
            tr("qBittorrent Web API request failed.\n\n"
               "Error: %1\n\n"
               "In qBittorrent: Tools → Preferences → Web UI → enable the Web UI.\n"
               "Then, under Authentication, check “Bypass authentication for clients on "
               "localhost”.")
                .arg(err.toHtmlEscaped()));
      }
      finish(false, err);
    };

    const auto maybeRetry = [this, attempt_weak, allow_retry, interactive,
                             fail](const QString& err) -> bool {
      // Interactive only: one credential prompt, then a single non-retrying attempt.
      if (!interactive || !allow_retry) return false;
      if (const auto creds = promptQBitCredentials(this, err)) {
        taiga::settings.setTorrentQBitApiUsername(creds->username.toStdString());
        taiga::settings.setTorrentQBitApiPassword(creds->password.toStdString());
        if (const auto locked = attempt_weak.lock()) {
          (*locked)(creds->username.trimmed(), creds->password, false);
        } else {
          fail(err, true);
        }
        return true;
      }
      fail(err, true);
      return true;  // handled (failed after cancel)
    };

    const auto do_add = [this, torrent_url, save_path, base_url, maybeRetry, fail, keep_alive,
                         finish, generation](const QString& cookie) {
      if (generation != m_qbit_add_generation_) {
        finish(false, tr("Cancelled."));
        return;
      }

      QNetworkRequest req(QUrl(base_url + QStringLiteral("/api/v2/torrents/add")));
      req.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/x-www-form-urlencoded"));
      req.setTransferTimeout(kQBitTransferTimeoutMs);
      if (!cookie.isEmpty()) req.setRawHeader("Cookie", cookie.toUtf8());

      QByteArray body = QByteArrayLiteral("urls=") + QUrl::toPercentEncoding(torrent_url);
      if (!save_path.isEmpty()) {
        QDir().mkpath(save_path);
        body += QByteArrayLiteral("&savepath=") + QUrl::toPercentEncoding(save_path);
      }

      auto* reply = taiga::network()->post(req, body);
      connect(reply, &QNetworkReply::finished, this, [=]() mutable {
        (void)keep_alive;
        reply->deleteLater();
        if (generation != m_qbit_add_generation_) {
          finish(false, tr("Cancelled."));
          return;
        }
        if (reply->error() != QNetworkReply::NoError) {
          const QString err = reply->errorString();
          if (maybeRetry(err)) return;
          fail(err, false);
          return;
        }

        const QString resp = QString::fromUtf8(reply->readAll()).trimmed();
        // Old qBittorrent API returns plain "Ok." / "Ok"; newer versions return a JSON object.
        // Accept the JSON form as success when failure_count == 0 and at least one torrent was
        // queued (success_count or pending_count > 0), which is what a fresh add looks like.
        const bool json_ok = [&]() -> bool {
          if (!resp.startsWith(QLatin1Char('{'))) return false;
          const QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
          if (!doc.isObject()) return false;
          const QJsonObject obj = doc.object();
          const int failure = obj.value(QStringLiteral("failure_count")).toInt(-1);
          const int success = obj.value(QStringLiteral("success_count")).toInt(0);
          const int pending = obj.value(QStringLiteral("pending_count")).toInt(0);
          return failure == 0 && (success > 0 || pending > 0);
        }();
        const bool ok = json_ok || resp.compare(QStringLiteral("Ok."), Qt::CaseInsensitive) == 0 ||
                        resp.startsWith(QStringLiteral("Ok"), Qt::CaseInsensitive);
        if (!ok) {
          const QString err = resp.isEmpty() ? tr("Unexpected response from qBittorrent.") : resp;
          if (maybeRetry(err)) return;
          fail(err, false);
          return;
        }

        finish(true, {});
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
    login_req.setTransferTimeout(kQBitTransferTimeoutMs);
    const QByteArray login_body = QByteArrayLiteral("username=") + user.toUtf8() +
                                  QByteArrayLiteral("&password=") + pass.toUtf8();

    auto* login_reply = taiga::network()->post(login_req, login_body);
    connect(login_reply, &QNetworkReply::finished, this, [=]() mutable {
      (void)keep_alive;
      login_reply->deleteLater();
      if (generation != m_qbit_add_generation_) {
        finish(false, tr("Cancelled."));
        return;
      }
      if (login_reply->error() != QNetworkReply::NoError) {
        const QString err = login_reply->errorString();
        if (maybeRetry(err)) return;
        fail(err, false);
        return;
      }

      QString cookie_str;
      const QVariant cv = login_reply->header(QNetworkRequest::SetCookieHeader);
      if (cv.isValid()) {
        for (const QNetworkCookie& c : cv.value<QList<QNetworkCookie>>()) {
          if (!cookie_str.isEmpty()) cookie_str += QStringLiteral("; ");
          cookie_str +=
              QString::fromUtf8(c.name()) + QStringLiteral("=") + QString::fromUtf8(c.value());
        }
      }
      do_add(cookie_str);
    });
  };

  const QString username =
      QString::fromStdString(taiga::settings.torrentQBitApiUsername()).trimmed();
  const QString password = QString::fromStdString(taiga::settings.torrentQBitApiPassword());
  // Interactive: allow one credential retry. Silent/auto: never prompt; just attempt once.
  (*attempt)(username, password, interactive);
}

void TorrentFeedWidget::saveSessionState() {
  if (!m_view_eps_ || !m_view_eps_->header()) return;
  taiga::session.setTorrentRssTableHeaderState(m_view_eps_->header()->saveState());
}

/// Generates RSS search title variants for manual search and auto-download (same list).
/// Order: season-qualified forms first (`… S01` / `… S02`), then bare English/romaji.
/// Callers may prepend a per-anime cache hit before this list.
static QStringList buildTitleVariants(const QString& english, const QString& romaji) {
  QStringList season_qualified;
  QStringList bare;
  const auto addIfNew = [](QStringList& list, const QString& s) {
    const QString t = s.trimmed();
    if (t.isEmpty() || list.contains(t, Qt::CaseInsensitive)) return;
    list.append(t);
  };

  const auto consider = [&](const QString& raw) {
    if (track::recognition::isFranchiseOnlySearchTitle(raw)) return;
    const QString t = raw.trimmed();
    if (t.isEmpty()) return;
    if (const QString sq = nyaaSeasonQualifiedTitle(t); !sq.isEmpty()) {
      addIfNew(season_qualified, sq);
    }
    addIfNew(bare, t);
    const QString stripped = stripTitleSubtitle(t);
    if (stripped.compare(t, Qt::CaseInsensitive) == 0) return;
    if (track::recognition::isFranchiseOnlySearchTitle(stripped)) return;
    if (const QString sq = nyaaSeasonQualifiedTitle(stripped); !sq.isEmpty()) {
      addIfNew(season_qualified, sq);
    }
    addIfNew(bare, stripped);
  };

  // Official titles / No.N+1 stripped forms, then explicit english + romaji.
  for (const QString& v :
       track::recognition::searchTitleVariantsFromOfficialTitles(english, romaji)) {
    consider(v);
  }
  if (!english.isEmpty()) consider(english);
  if (!romaji.isEmpty()) consider(romaji);

  QStringList result;
  result.reserve(season_qualified.size() + bare.size());
  for (const QString& s : season_qualified) addIfNew(result, s);
  for (const QString& s : bare) addIfNew(result, s);
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
    if (!cached.isEmpty() && !track::recognition::isFranchiseOnlySearchTitle(cached)) {
      variants.append(cached);
    }
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
    downloadBestMatchForTitle(title, folder_name,
                              [title, anime_id_cache, on_done, step_fn](bool found) {
                                if (found) {
                                  if (anime_id_cache > 0)
                                    if (!track::recognition::isFranchiseOnlySearchTitle(title)) {
                                      taiga::settings.setTorrentSearchTitleForAnime(anime_id_cache,
                                                                                    title);
                                    }
                                  if (on_done) on_done(true);
                                } else {
                                  (*step_fn)();
                                }
                              },
                              {} /* no internal fallback — we manage variants here */);
  };
  (*step_fn)();
}

/// Returns the best-downloaded RSS item per unique episode number found in `filtered`.
/// Key = episode number (1-based). Key = -1 means a batch/range item.
/// Key = 0 means the episode could not be parsed (rare; stored separately).
static QMap<int, const rss::Item*> selectBestPerEpisode(const QList<const rss::Item*>& filtered) {
  QMap<int, const rss::Item*> best;
  QMap<int, int> downloads_for;
  for (const rss::Item* it : filtered) {
    track::Episode ep = track::recognition::parse(it->title);
    const QString ep_str = QString::fromStdString(ep.element(anitomy::ElementKind::Episode));
    const QString title_full = QString::fromStdString(it->title);
    int ep_no = 0;
    if (ep_str.contains(QChar('-'))) {
      ep_no = -1;  // range → batch
    } else if (!ep_str.isEmpty()) {
      bool ok = false;
      ep_no = ep_str.toInt(&ok);
      if (!ok) ep_no = 0;
    }
    // If the episode token is missing entirely, treat it as a batch/pack unless it looks like a
    // Movie or Special. This improves auto-download behavior for Nyaa season packs that omit ep.
    if (ep_no == 0 && ep_str.isEmpty() && !isMovieOrSpecial(ep, title_full)) ep_no = -1;
    // Nyaa season packs often omit an episode token; anitomy leaves Episode empty while the
    // title still says "(Batch)" — treat those as a single multi-episode item (key -1).
    if (ep_no == 0 && isBatchLikeTitle(title_full)) ep_no = -1;
    const int downloads = downloadsForItem(*it);
    const auto existing = downloads_for.find(ep_no);
    if (existing == downloads_for.end() || downloads > existing.value()) {
      downloads_for[ep_no] = downloads;
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
    if (!cached.isEmpty() && !track::recognition::isFranchiseOnlySearchTitle(cached)) {
      variants.append(cached);
    }
  }
  for (const auto& v : buildTitleVariants(english_title, romaji_title)) {
    if (!variants.contains(v, Qt::CaseInsensitive)) variants.append(v);
  }
  if (variants.isEmpty()) {
    if (on_done) on_done(0);
    return;
  }

  const auto variantIdx = std::make_shared<int>(0);
  const auto try_fn = std::make_shared<std::function<void()>>();

  *try_fn = [this, variants, folder_name, on_done, variantIdx, try_fn, anime_id]() {
    const int idx = *variantIdx;
    if (idx >= variants.size()) {
      if (on_done) on_done(0);
      return;
    }
    ++(*variantIdx);
    const QString title = variants[idx];

    abortBackgroundRss();
    const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
    const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, title);
    if (!url.isValid()) {
      (*try_fn)();
      return;
    }

    QNetworkRequest req(url);
    taiga::applyCommonHeaders(req);
    m_bg_rss_op_ = BgRssOp::BatchEpisodes;
    m_bg_fetch_reply_ = taiga::network()->get(req);

    connect(m_bg_fetch_reply_, &QNetworkReply::finished, this,
            [this, title, folder_name, on_done, try_fn, anime_id]() {
              auto* reply = m_bg_fetch_reply_;
              m_bg_fetch_reply_ = nullptr;
              m_bg_rss_op_ = BgRssOp::None;
              if (!reply) {
                if (on_done) on_done(0);
                return;
              }
              reply->deleteLater();
              if (reply->error() != QNetworkReply::NoError) {
                (*try_fn)();
                return;
              }

              const rss::Feed feed =
                  gui::parseSyndicationFeed(reply->readAll()).value_or(rss::Feed{});
              const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed, anime_id);
              if (filtered.isEmpty()) {
                (*try_fn)();
                return;
              }

              // Determine which episodes are missing (list-relative numbers).
              const auto* item_db = anime::db.item(anime_id);
              const auto* entry_db = anime::db.entry(anime_id);
              QList<int> missing;
              if (item_db && entry_db) {
                // Use episode_count as fallback when last_aired_episode is not populated.
                const int raw_last = item_db->last_aired_episode > 0 ? item_db->last_aired_episode
                                                                     : item_db->episode_count;
                const int last_aired = track::toListLastAiredEpisode(*item_db, raw_last);
                const int watched = entry_db->watched_episodes;
                for (int ep = watched + 1; ep <= last_aired; ++ep) {
                  if (!track::libraryHasLocalEpisode(anime_id, ep)) missing.append(ep);
                }
              }
              if (missing.isEmpty()) {
                if (on_done) on_done(0);
                return;
              }

              // Build best-per-episode map from filtered feed (keys are release/file episode nos).
              const QMap<int, const rss::Item*> best_ep = selectBestPerEpisode(filtered);

              const int effective_last = item_db ? track::toListLastAiredEpisode(
                                                       *item_db, item_db->last_aired_episode > 0
                                                                     ? item_db->last_aired_episode
                                                                     : item_db->episode_count)
                                                 : 0;

              // Cache only after we actually queue a download (below).

              const auto enqueue_batch = [&](const rss::Item* batch) -> bool {
                if (!batch) return false;
                const QString batch_url = bestUrlForItem(batch);
                if (batch_url.isEmpty()) return false;
                if (anime_id > 0 && !track::recognition::isFranchiseOnlySearchTitle(title)) {
                  taiga::settings.setTorrentSearchTitleForAnime(anime_id, title);
                }
                const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(folder_name);
                if (taiga::settings.torrentQBitApiEnabled()) {
                  addTorrentViaQBitApi(
                      batch_url, save_path,
                      [on_done](bool ok, const QString& err) {
                        if (!err.isEmpty())
                          taiga::userFeedback(QStringLiteral("qBittorrent Web API error: ") + err,
                                              true);
                        if (on_done) on_done(ok ? 1 : 0);
                      },
                      /*interactive=*/false);
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

              // How many episodes of this cour are already on disk (any list ep)?
              int local_ep_count = 0;
              if (item_db && item_db->episode_count > 0) {
                for (int ep = 1; ep <= item_db->episode_count; ++ep) {
                  if (track::libraryHasLocalEpisode(anime_id, ep)) ++local_ep_count;
                }
              }

              // ── Batch preference: only when this cour has nothing local yet ─────
              // Otherwise a season pack re-downloads episodes the user already has.
              if (item_db && item_db->episode_count > 0 && local_ep_count == 0 &&
                  effective_last >= item_db->episode_count && missing.size() >= 3) {
                if (enqueue_batch(best_ep.value(-1, nullptr))) return;
              }

              // ── Individual episode downloads ──────────────────────────────────
              struct DownloadItem {
                int ep;
                QString url;
              };
              QList<DownloadItem> targets;
              for (const int list_ep : missing) {
                const int release_ep = track::toReleaseEpisode(anime_id, list_ep);
                if (const auto* best = best_ep.value(release_ep, nullptr)) {
                  const QString ep_url = bestUrlForItem(best);
                  if (!ep_url.isEmpty()) targets.append({list_ep, ep_url});
                }
              }
              if (targets.isEmpty()) {
                // Season packs often have no per-episode rows; only fall back to a batch when
                // nothing from this cour is on disk yet (avoids re-adding full packs).
                if (local_ep_count == 0 && enqueue_batch(best_ep.value(-1, nullptr))) return;
                (*try_fn)();
                return;
              }

              if (anime_id > 0 && !track::recognition::isFranchiseOnlySearchTitle(title)) {
                taiga::settings.setTorrentSearchTitleForAnime(anime_id, title);
              }

              if (taiga::settings.torrentQBitApiEnabled()) {
                const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(folder_name);
                const int total = targets.size();
                const auto downloaded = std::make_shared<int>(0);
                const auto done_count = std::make_shared<int>(0);
                const auto reported_err = std::make_shared<bool>(false);
                for (const auto& t : targets) {
                  addTorrentViaQBitApi(
                      t.url, save_path,
                      [downloaded, done_count, total, on_done, reported_err](bool ok,
                                                                             const QString& err) {
                        if (!err.isEmpty() && !*reported_err) {
                          *reported_err = true;
                          taiga::userFeedback(QStringLiteral("qBittorrent Web API error: ") + err,
                                              true);
                        }
                        if (ok) ++(*downloaded);
                        if (++(*done_count) >= total) {
                          if (on_done) on_done(*downloaded);
                        }
                      },
                      /*interactive=*/false);
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

static QString bestMatchCoalesceKey(const QUrl& url, const QString& folder_name,
                                    const QString& fallback_title) {
  return url.toString(QUrl::FullyEncoded) + QChar(0) + folder_name + QChar(0) + fallback_title;
}

void TorrentFeedWidget::downloadBestMatchForTitle(const QString& search_title,
                                                  const QString& folder_name,
                                                  std::function<void(bool found)> on_done,
                                                  const QString& fallback_title) {
  const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
  const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, search_title);
  if (!url.isValid()) {
    if (on_done) on_done(false);
    return;
  }

  const QString key = bestMatchCoalesceKey(url, folder_name, fallback_title);

  if (m_bg_fetch_reply_ != nullptr && m_bg_rss_op_ == BgRssOp::BestMatch &&
      m_bg_best_match_key_ == key) {
    m_bg_best_match_waiters_.push_back(
        BestMatchWaiter{std::move(on_done), folder_name, fallback_title});
    return;
  }

  abortBackgroundRss();

  m_bg_rss_op_ = BgRssOp::BestMatch;
  m_bg_best_match_key_ = key;
  m_bg_best_match_waiters_.clear();
  m_bg_best_match_waiters_.push_back(
      BestMatchWaiter{std::move(on_done), folder_name, fallback_title});

  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  m_bg_fetch_reply_ = taiga::network()->get(req);

  connect(m_bg_fetch_reply_, &QNetworkReply::finished, this, [this]() {
    auto* reply = m_bg_fetch_reply_;
    m_bg_fetch_reply_ = nullptr;
    m_bg_rss_op_ = BgRssOp::None;
    m_bg_best_match_key_.clear();
    QVector<BestMatchWaiter> waiters = std::move(m_bg_best_match_waiters_);
    m_bg_best_match_waiters_.clear();

    if (!reply) {
      for (const BestMatchWaiter& w : waiters) {
        if (w.on_done) w.on_done(false);
      }
      return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      for (const BestMatchWaiter& w : waiters) {
        if (w.on_done) w.on_done(false);
      }
      return;
    }

    const QByteArray data = reply->readAll();
    const rss::Feed feed = gui::parseSyndicationFeed(data).value_or(rss::Feed{});
    const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed);

    for (const BestMatchWaiter& w : waiters) {
      if (filtered.isEmpty() && !w.fallback_title.isEmpty()) {
        downloadBestMatchForTitle(w.fallback_title, w.folder_name, w.on_done, {});
      } else if (filtered.isEmpty()) {
        if (w.on_done) w.on_done(false);
      }
    }

    if (!filtered.isEmpty()) {
      if (waiters.size() == 1) {
        deliverBestMatchFromFiltered(filtered, waiters[0].folder_name, waiters[0].on_done);
      } else {
        deliverBestMatchFromFiltered(filtered, waiters[0].folder_name, [waiters](bool ok) {
          for (const BestMatchWaiter& w : waiters) {
            if (w.on_done) w.on_done(ok);
          }
        });
      }
    }
  });
}

void TorrentFeedWidget::abortBackgroundRss() {
  if (m_bg_fetch_reply_) {
    m_bg_fetch_reply_->disconnect();
    m_bg_fetch_reply_->abort();
    m_bg_fetch_reply_->deleteLater();
    m_bg_fetch_reply_ = nullptr;
  }
  for (const BestMatchWaiter& w : m_bg_best_match_waiters_) {
    if (w.on_done) w.on_done(false);
  }
  m_bg_best_match_waiters_.clear();
  m_bg_best_match_key_.clear();
  m_bg_rss_op_ = BgRssOp::None;
}

void TorrentFeedWidget::deliverBestMatchFromFiltered(const QList<const rss::Item*>& filtered,
                                                     const QString& folder_name,
                                                     std::function<void(bool)> on_done) {
  if (filtered.isEmpty()) {
    if (on_done) on_done(false);
    return;
  }

  const rss::Item* best = nullptr;
  int best_downloads = -1;
  for (const rss::Item* it : filtered) {
    const int downloads = downloadsForItem(*it);
    if (downloads > best_downloads) {
      best_downloads = downloads;
      best = it;
    }
  }
  if (!best) best = filtered.first();

  const QString effective_folder =
      folder_name.isEmpty() ? QString::fromStdString(best->title) : folder_name;

  if (taiga::settings.torrentQBitApiEnabled()) {
    // Auto/background best-match: never block on a folder-picker dialog.
    const QString base =
        QString::fromStdString(taiga::settings.torrentClientDownloadPath()).trimmed();
    if (base.isEmpty() || !QDir(base).exists()) {
      taiga::userFeedback(
          tr("qBittorrent Web API: torrent client download folder is missing or invalid."), true);
      if (on_done) on_done(false);
      return;
    }
    QString api_url;
    if (const auto m = best->namespace_elements.find(kTorrentFeedMagnetKey);
        m != best->namespace_elements.end()) {
      api_url = QString::fromStdString(m->second);
    }
    if (api_url.isEmpty()) {
      api_url = QString::fromStdString(best->enclosure.url);
    }
    if (api_url.isEmpty()) api_url = QString::fromStdString(best->link);
    if (api_url.isEmpty()) {
      if (on_done) on_done(false);
      return;
    }

    const QString save_path = resolvedTorrentDownloadDirForSavedTorrent(effective_folder);
    addTorrentViaQBitApi(
        api_url, save_path,
        [on_done](bool ok, const QString& err) {
          if (!err.isEmpty())
            taiga::userFeedback(QStringLiteral("qBittorrent Web API error: ") + err, true);
          if (on_done) on_done(ok);
        },
        /*interactive=*/false);
    return;
  }

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

  if (has_http) {
    if (const auto u = httpUrlFromUserString(tor_url)) {
      enqueueSaveTorrent(*u, effective_folder);
      startNextQueuedSave();
      if (on_done) on_done(true);
      return;
    }
  }

  const QString page_link = QString::fromStdString(best->link);
  if (!page_link.isEmpty()) {
    openPrimaryTorrentUrl(page_link);
    if (on_done) on_done(true);
  } else {
    if (on_done) on_done(false);
  }
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
  cancelPendingQBitAdds();
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
  updateQBitCancelButton();
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
    updateQBitCancelButton();
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
  updateQBitCancelButton();
  const PendingTorrentSave next = m_save_queue_.dequeue();
  if (next.ui_row >= 0) setQueueRowStatus(next.ui_row, QStringLiteral("downloading"), false);
  if (auto* mw = mainWindow()) {
    const int done = m_save_queue_total_ - m_save_queue_.size();
    mw->statusBar()->showMessage(
        tr("Downloading .torrent files… (%1/%2)").arg(done).arg(m_save_queue_total_), 3000);
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
    full_path = QFileDialog::getSaveFileName(
        this, tr("Save .torrent file"), def,
        tr("Torrent files") + u" (*.torrent);;" + tr("All files") + u" (*)");
  }
  if (full_path.isEmpty()) return;

  // Abort only an in-flight .torrent GET — do not clear the remaining save / qBit queues.
  if (m_save_reply_) {
    m_save_reply_->disconnect();
    m_save_reply_->abort();
    m_save_reply_->deleteLater();
    m_save_reply_ = nullptr;
  }
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
      taiga::userFeedback(tr("Could not download .torrent file: %1").arg(reply->errorString()),
                          true);
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
      const bool ok = s.contains(QStringLiteral("application/x-bittorrent")) ||
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
    const QString exe =
        QString::fromStdString(taiga::settings.torrentAppExecutablePath()).trimmed();
    if (!exe.isEmpty() && QFileInfo::exists(exe)) {
      // Only block when we would pass a save path to the client.
      if (!ensureClientDownloadBaseDir(this).has_value()) {
        done();
        return;
      }
      const QString dl_dir = resolvedTorrentDownloadDirForSavedTorrent(title_hint);
      const QStringList args = argsForTorrentClient(exe, full_path, dl_dir);
      if (!QProcess::startDetached(exe, args)) {
        taiga::userFeedback(
            tr("Could not start the torrent client executable. Check the path in Settings."), true);
      }
    }
    done();
  });
}

QStringList buildManualSearchTitleVariants(const Anime& item, const QString& primary_query) {
  QStringList result;
  const auto addIfNew = [&](const QString& s) {
    const QString t = s.trimmed();
    if (!t.isEmpty() && !result.contains(t, Qt::CaseInsensitive)) result.append(t);
  };

  // Season-qualified primary first: Nyaa RSS bare titles are flooded by newer seasons.
  if (const QString sq = nyaaSeasonQualifiedTitle(primary_query); !sq.isEmpty()) {
    addIfNew(sq);
  }
  addIfNew(primary_query);

  // Saved per-anime effective title (if any) — often already season-qualified after autodl.
  if (item.id > 0) {
    const QString cached = taiga::settings.torrentSearchTitleForAnime(item.id);
    addIfNew(cached);
  }

  const QString en = QString::fromStdString(item.titles.english);
  const QString romaji = QString::fromStdString(item.titles.romaji);
  const QString native = QString::fromStdString(item.titles.japanese);

  // Shared list (season-qualified first, then bare english/romaji) — same as auto-download.
  for (const auto& v : buildTitleVariants(en, romaji)) addIfNew(v);

  // Native title can help for some indexers / releases.
  if (!native.isEmpty()) {
    if (const QString sq = nyaaSeasonQualifiedTitle(native); !sq.isEmpty()) addIfNew(sq);
    addIfNew(native);
    const QString stripped = stripTitleSubtitle(native);
    if (stripped.compare(native, Qt::CaseInsensitive) != 0) addIfNew(stripped);
  }

  // Finally, add a capped number of synonyms (best-effort; can be noisy).
  int syn_added = 0;
  for (const std::string& s : item.titles.synonyms) {
    const QString syn = QString::fromStdString(s);
    if (const QString sq = nyaaSeasonQualifiedTitle(syn); !sq.isEmpty()) addIfNew(sq);
    addIfNew(syn);
    if (++syn_added >= kManualSearchSynonymCap) break;
  }

  return result;
}

std::optional<qint64> animeStartLowerBoundMs(const Anime& item) {
  if (item.date_started.empty()) return std::nullopt;
  const int y = static_cast<int>(item.date_started.year());
  int m = static_cast<int>(item.date_started.month());
  int d = static_cast<int>(item.date_started.day());
  if (y <= 0) return std::nullopt;
  if (m <= 0) m = 1;
  if (d <= 0) d = 1;
  const QDate date(y, m, d);
  if (!date.isValid()) return std::nullopt;
  // Match contextAnimeStartMs: allow pubs from the UTC day before listed start.
  return QDateTime(date.addDays(-1), QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
}

std::optional<qint64> publishedMsForRssItem(const rss::Item& it) {
  const QString s = QString::fromStdString(it.pub_date).trimmed();
  if (s.isEmpty()) return std::nullopt;
  // Most RSS feeds (Nyaa, Tokyo Tosho) use RFC2822.
  {
    const QDateTime dt = QDateTime::fromString(s, Qt::RFC2822Date);
    if (dt.isValid()) return dt.toMSecsSinceEpoch();
  }
  // Some feeds may return ISO8601 (Atom-style) or date-only strings.
  {
    const QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) return dt.toMSecsSinceEpoch();
  }
  {
    const QDate d = QDate::fromString(s, Qt::ISODate);
    if (d.isValid()) return QDateTime(d, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch();
  }
  return std::nullopt;
}

QString dedupeKeyForMergedFeedItem(const rss::Item& it) {
  // Prefer stable download identifiers when available.
  if (const auto m = it.namespace_elements.find(kTorrentFeedMagnetKey);
      m != it.namespace_elements.end() && !m->second.empty()) {
    return QStringLiteral("m:") + QString::fromStdString(m->second);
  }
  if (!it.enclosure.url.empty()) {
    return QStringLiteral("t:") + QString::fromStdString(it.enclosure.url);
  }
  if (!it.link.empty()) {
    return QStringLiteral("p:") + QString::fromStdString(it.link);
  }
  return QStringLiteral("x:") + fingerprintForItem(it);
}

void TorrentFeedWidget::setSearchFallback(const QString& fallback) {
  m_search_fallback_title_ = fallback.trimmed();
}

void TorrentFeedWidget::setManualSearchAnimeContext(const int anime_id) {
  m_manual_search_anime_id_ = std::max(0, anime_id);
}

void TorrentFeedWidget::runSearch() {
  if (!m_query_edit_) return;
  const QString q = m_query_edit_->text().trimmed();
  if (q.isEmpty()) {
    taiga::userFeedback(tr("Enter a title in the toolbar search field first."), true);
    return;
  }
  taiga::session.setTorrentPanelLastQuery(q);

  // Manual free-text search remains the existing single-query behavior unless an anime context is
  // provided (Option A from the design plan).
  if (m_manual_search_anime_id_ <= 0) {
    const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
    const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, q);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
      taiga::userFeedback(tr("Invalid torrent search URL in settings."), true);
      return;
    }
    startFetch(url, tr("Fetching torrent RSS…"), FetchKind::SearchRss);
    return;
  }

  const Anime* item = anime::db.item(m_manual_search_anime_id_);
  if (!item) {
    // Context is stale (service switched / DB missing). Fall back to the safe single query path.
    const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
    const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, q);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
      taiga::userFeedback(tr("Invalid torrent search URL in settings."), true);
      return;
    }
    startFetch(url, tr("Fetching torrent RSS…"), FetchKind::SearchRss);
    return;
  }

  // ── Multi-variant merged manual RSS search (sequential) ───────────────────
  QStringList variants = buildManualSearchTitleVariants(*item, q);
  // Preserve legacy manual behavior: if a fallback title was registered (e.g. from Media menu),
  // try it once at the end of the variant list.
  if (!m_search_fallback_title_.trimmed().isEmpty() &&
      !variants.contains(m_search_fallback_title_, Qt::CaseInsensitive)) {
    variants.append(m_search_fallback_title_.trimmed());
  }
  if (variants.isEmpty()) {
    taiga::userFeedback(tr("No valid title to search."), true);
    return;
  }

  cancelPending();
  m_active_fetch_ = FetchKind::SearchRss;
  const quint64 seq = ++m_manual_search_seq_;

  struct MergeState {
    QStringList variants;
    int idx = 0;
    rss::Feed merged;
    QSet<QString> seen;
    std::optional<qint64> cutoff_ms;
  };
  auto st = std::make_shared<MergeState>();
  st->variants = variants;
  st->seen.reserve(2000);

  if (taiga::settings.torrentFeedHideBeforeAnimeStartDate()) {
    st->cutoff_ms = animeStartLowerBoundMs(*item);
  }

  const auto finish = [this, seq, st]() {
    if (seq != m_manual_search_seq_) return;
    // Apply the existing filtering stack on the merged feed.
    rss::Feed out = st->merged;
    if (st->cutoff_ms.has_value()) {
      // Drop only when pubDate is parseable and older; keep items with missing/unparseable pubDate.
      std::vector<rss::Item> kept;
      kept.reserve(out.items.size());
      for (const rss::Item& it : out.items) {
        const auto pub_ms = publishedMsForRssItem(it);
        if (!pub_ms.has_value()) {
          kept.push_back(it);
          continue;
        }
        if (*pub_ms >= *st->cutoff_ms) kept.push_back(it);
      }
      out.items = std::move(kept);
    }

    // This branch only runs with a valid anime context (see runSearch above), so restrict results
    // to that entry — prevents wrong-season releases (e.g. S1 Part 2) from showing for a S3 search.
    populateTable(out, m_manual_search_anime_id_);
    if (auto* mw = mainWindow()) {
      mw->statusBar()->clearMessage();
    }
  };

  const auto step = std::make_shared<std::function<void()>>();
  *step = [this, st, step, seq, finish]() {
    if (seq != m_manual_search_seq_) return;
    if (st->idx >= st->variants.size()) {
      finish();
      return;
    }

    const int cur = st->idx++;
    const QString qv = st->variants[cur];
    const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
    const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, qv);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
      QTimer::singleShot(0, this, [step]() { (*step)(); });
      return;
    }

    if (auto* mw = mainWindow()) {
      mw->statusBar()->showMessage(
          tr("Fetching torrent RSS… (%1/%2)").arg(cur + 1).arg(st->variants.size()));
    }

    QNetworkRequest req{url};
    taiga::applyCommonHeaders(req);
    m_pending_ = taiga::network()->get(req);
    connect(m_pending_, &QNetworkReply::finished, this, [this, st, step, seq, finish]() {
      QNetworkReply* reply = m_pending_;
      m_pending_ = nullptr;
      if (seq != m_manual_search_seq_) {
        if (reply) reply->deleteLater();
        return;
      }
      if (!reply) {
        QTimer::singleShot(0, this, [step]() { (*step)(); });
        return;
      }
      reply->deleteLater();

      if (reply->error() != QNetworkReply::NoError) {
        QTimer::singleShot(0, this, [step]() { (*step)(); });
        return;
      }

      QString err;
      const QByteArray body = reply->readAll();
      const auto feed_opt = parseSyndicationFeed(body, &err);
      if (!feed_opt.has_value()) {
        QTimer::singleShot(0, this, [step]() { (*step)(); });
        return;
      }

      // Merge items (dedupe by magnet/torrent/page fallback).
      const rss::Feed& f = *feed_opt;
      if (st->merged.channel.title.empty()) st->merged.channel = f.channel;
      for (const rss::Item& it : f.items) {
        const QString key = dedupeKeyForMergedFeedItem(it);
        if (key.isEmpty() || st->seen.contains(key)) continue;
        st->seen.insert(key);
        st->merged.items.push_back(it);
      }

      // Inter-request delay to avoid hammering indexers.
      QTimer::singleShot(2000, this, [step]() { (*step)(); });
    });
  };

  (*step)();
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
  if (m_pending_ != nullptr && m_active_fetch_ == FetchKind::CatalogAutocheck) {
    return;
  }
  const QString url_key = url.toString(QUrl::FullyEncoded);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  constexpr qint64 kCatalogAutocheckMinIntervalMs = 10LL * 60LL * 1000LL;
  if (!m_catalog_autocheck_last_ok_url_.isEmpty() && url_key == m_catalog_autocheck_last_ok_url_ &&
      m_catalog_autocheck_last_ok_ms_ > 0 &&
      now - m_catalog_autocheck_last_ok_ms_ < kCatalogAutocheckMinIntervalMs) {
    return;
  }
  startFetch(url, {}, FetchKind::CatalogAutocheck);
}

void TorrentFeedWidget::startFetch(const QUrl& url, const QString& status_message,
                                   const FetchKind kind) {
  if (kind == FetchKind::SearchRss && m_pending_ != nullptr &&
      m_active_fetch_ == FetchKind::SearchRss) {
    const QString pending_key = m_pending_->request().url().toString(QUrl::FullyEncoded);
    const QString next_key = url.toString(QUrl::FullyEncoded);
    if (pending_key == next_key) {
      return;
    }
  }
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
      taiga::userFeedback(tr("Could not download feed: %1").arg(reply->errorString()), true);
    }
    return;
  }

  const QByteArray body = reply->readAll();
  {
    const QByteArray t = body.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      if (!silent) {
        taiga::userFeedback(tr("The server returned a web page, not an RSS feed. Check the URL in "
                               "Settings → Library."),
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
  if (kind == FetchKind::CatalogAutocheck) {
    m_catalog_autocheck_last_ok_url_ = reply->request().url().toString(QUrl::FullyEncoded);
    m_catalog_autocheck_last_ok_ms_ = QDateTime::currentMSecsSinceEpoch();
  }
  if (feed->items.empty()) {
    // Auto-retry with the fallback title (e.g. English title when romaji gave 0 results).
    if (kind == FetchKind::SearchRss && !m_search_fallback_title_.isEmpty() && m_query_edit_) {
      const QString fallback = m_search_fallback_title_;
      m_search_fallback_title_.clear();
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("No results for '%1', retrying with '%2'…")
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
          msg +=
              u" " + tr("Tip: try a shorter title — e.g. remove the subtitle after ':' or ' — '.");
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
      const int shown = (m_proxy_eps_ ? m_proxy_eps_->rowCount() : 0) +
                        (m_proxy_batches_ ? m_proxy_batches_->rowCount() : 0);
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
            !QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList())
                 .trimmed()
                 .isEmpty() ||
            !QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList())
                 .trimmed()
                 .isEmpty();
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

void TorrentFeedWidget::applyCatalogFingerprintState(const rss::Feed& feed,
                                                     const bool notify_if_new) {
  QStringList keys;
  const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed);
  const size_t n =
      std::min(static_cast<size_t>(filtered.size()), static_cast<size_t>(kCatalogFingerprintCap));
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
        const QString msg = tr("Catalog auto-check: %1 new item(s). Download-on-new is selected, "
                               "but no torrent save "
                               "folder is configured. Set one in Settings → Library → Torrents to "
                               "enable auto-queue.")
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

      // Enqueue only HTTP(S) .torrent enclosures/links. Magnet-only items are skipped (no safe
      // silent path).
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
        // Ensure the queue prefers the configured folder (prevents any Save-As dialog during
        // auto-check).
        m_save_queue_dir_ = save_dir;
      }

      const QString msg = tr("Catalog auto-check: queued %1 of %2 new item(s) for download.")
                              .arg(queued)
                              .arg(fresh);
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

void TorrentFeedWidget::populateTable(const rss::Feed& feed, const int context_anime_id) {
  const QList<const rss::Item*> filtered = filterRssItemsBySettings(feed, context_anime_id);

  size_t n = static_cast<size_t>(filtered.size());
  if (taiga::settings.torrentFeedFilterEnabled()) {
    const int cap = taiga::settings.torrentFeedArchiveMaxItems();
    if (cap > 0 && n > static_cast<size_t>(cap)) n = static_cast<size_t>(cap);
  }

  std::vector<TorrentRssRow> rows;
  rows.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const rss::Item* itp = filtered[static_cast<qsizetype>(i)];
    if (!itp) continue;
    const rss::Item& it = *itp;

    TorrentRssRow r{};
    r.title = QString::fromStdString(it.title);
    r.published_text = QString::fromStdString(it.pub_date);
    {
      const QDateTime dt = QDateTime::fromString(r.published_text, Qt::RFC2822Date);
      r.published_ms = dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
    }
    r.page_url = QUrl::fromUserInput(QString::fromStdString(it.link));
    r.is_batch = isBatchItem(it);

    // Links
    if (const auto m = it.namespace_elements.find(kTorrentFeedMagnetKey);
        m != it.namespace_elements.end()) {
      r.magnet_url = QString::fromStdString(m->second);
    }
    r.torrent_url = QString::fromStdString(it.enclosure.url);

    // Parsed metadata (best-effort; does not affect links).
    const track::Episode ep = track::recognition::parse(it.title);
    r.anime = QString::fromStdString(ep.element(anitomy::ElementKind::Title));
    const QString ep_no_raw = QString::fromStdString(ep.element(anitomy::ElementKind::Episode));
    if (!ep_no_raw.isEmpty()) {
      bool ok = false;
      const int e = ep_no_raw.toInt(&ok);
      if (ok) r.episode = e;
    }
    r.group = QString::fromStdString(ep.element(anitomy::ElementKind::ReleaseGroup));
    r.video = QString::fromStdString(ep.element(anitomy::ElementKind::VideoResolution));
    r.seeds = std::max(0, seedersForItem(it));
    r.downloads = std::max(0, downloadsForItem(it));

    rows.push_back(std::move(r));
  }

  if (m_rss_model_) m_rss_model_->setRows(std::move(rows));

  applyRssTableSortFromSettings();
  applyResultFilter();
}

void TorrentFeedWidget::applyRssTableSortFromSettings() {
  const std::string sb = taiga::settings.torrentRssSortBy();
  // Columns: Title=0, Published=1, Page=2, Anime=3, Ep=4, Group=5, Video=6, Seeds=7, Downloads=8
  int sort_col = 0;
  if (sb == "release_date") {
    sort_col = 1;
  } else if (sb == "episode_number") {
    // Sort by parsed episode number when available.
    sort_col = 4;
  }
  const bool desc = taiga::settings.torrentRssSortOrder() == std::string{"descending"};
  for (QTreeView* v : {m_view_eps_, m_view_batches_}) {
    if (!v) continue;
    v->sortByColumn(sort_col, desc ? Qt::DescendingOrder : Qt::AscendingOrder);
  }
}

void TorrentFeedWidget::resortRssTableFromSettings() {
  applyRssTableSortFromSettings();
}

void TorrentFeedWidget::applyResultFilter() {
  const QString needle = m_filter_edit_ ? m_filter_edit_->text() : QString{};
  if (m_proxy_eps_) m_proxy_eps_->setFilterText(needle);
  if (m_proxy_batches_) m_proxy_batches_->setFilterText(needle);
  applyRssTableSortFromSettings();
}

QString TorrentFeedWidget::primaryUrlForIndex(const QModelIndex& proxy_index) {
  if (!proxy_index.isValid()) return {};
  const QModelIndex idx0 = proxy_index.sibling(proxy_index.row(), TorrentRssModel::COLUMN_TITLE);
  const QString tor = idx0.data(TorrentRssModel::TorrentUrlRole).toString();
  const QString mag = idx0.data(TorrentRssModel::MagnetUrlRole).toString();
  const QString page = idx0.data(TorrentRssModel::PageUrlRole).toString();
  const bool prefer_magnet = taiga::settings.torrentDownloadUseMagnet();

  if (prefer_magnet) {
    if (!mag.isEmpty()) return mag;
    if (!tor.isEmpty()) return tor;
  } else {
    if (!tor.isEmpty() && !tor.startsWith(u"magnet:", Qt::CaseInsensitive)) return tor;
    if (!mag.isEmpty()) return mag;
    if (!tor.isEmpty()) return tor;
  }

  return page;
}

}  // namespace gui
