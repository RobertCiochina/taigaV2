/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "movie_torrent_watch.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <anitomy.hpp>

#include "gui/utils/rss_feed_parser.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"
#include "taiga/torrent_discovery.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/scanner.hpp"

namespace gui {
namespace {

constexpr qint64 kFirstDelaySecs = 2 * 60;
constexpr qint64 kRequestGapSecs = 10;
constexpr qint64 kErrorRetrySecs = 30 * 60;
constexpr qint64 kAfterHitSecs = 24 * 3600;

qint64 missBackoffSecs(const int missStreak) {
  switch (missStreak) {
    case 0:
    case 1:
      return 12 * 3600;
    case 2:
      return 24 * 3600;
    case 3:
      return 48 * 3600;
    default:
      return 72 * 3600;
  }
}

QString foldTitle(QString s) {
  s = s.toLower();
  s.replace(QChar(0x2019), QLatin1Char('\''));
  s.remove(QRegularExpression(QStringLiteral("\\b(the|movie|film|gekijouban|gekijoban)\\b"),
                              QRegularExpression::CaseInsensitiveOption));
  s.remove(QRegularExpression(QStringLiteral("[^a-z0-9]+")));
  return s;
}

bool releaseLooksLikeSeriesEpisode(const QString& title) {
  static const QRegularExpression kMovieToken(
      QStringLiteral(R"(\b(movie|film|gekijouban|gekijōban|gekijoban)\b)"),
      QRegularExpression::CaseInsensitiveOption);
  if (kMovieToken.match(title).hasMatch()) return false;

  static const QRegularExpression kBatch(
      QStringLiteral(R"(\b(batch|complete\s+collection|bd\s*batch)\b)"),
      QRegularExpression::CaseInsensitiveOption);
  if (kBatch.match(title).hasMatch()) return true;

  static const QRegularExpression kSeasonEpisode(QStringLiteral(R"(\bS\d{1,2}E\d{1,3}\b)"),
                                                 QRegularExpression::CaseInsensitiveOption);
  if (kSeasonEpisode.match(title).hasMatch()) return true;

  const track::Episode parsed = track::recognition::parse(title.toStdString());
  const QString episode =
      QString::fromStdString(parsed.element(anitomy::ElementKind::Episode)).trimmed();
  if (episode == QStringLiteral("480") || episode == QStringLiteral("720") ||
      episode == QStringLiteral("1080") || episode == QStringLiteral("2160")) {
    return false;
  }
  return !episode.isEmpty();
}

bool titleOverlaps(const QString& releaseTitle, const QStringList& titles) {
  const track::Episode parsed = track::recognition::parse(releaseTitle.toStdString());
  const QString parsedTitle =
      foldTitle(QString::fromStdString(parsed.element(anitomy::ElementKind::Title)));
  const QString foldedRelease = foldTitle(releaseTitle);
  for (const QString& raw : titles) {
    const QString folded = foldTitle(raw);
    if (folded.size() < 3) continue;
    if (foldedRelease.contains(folded)) return true;
    if (!parsedTitle.isEmpty() && (parsedTitle.contains(folded) || folded.contains(parsedTitle))) {
      return true;
    }
  }
  return false;
}

bool passesRegexFilters(const QString& title, const QList<QRegularExpression>& include,
                        const QList<QRegularExpression>& exclude) {
  if (!include.isEmpty()) {
    const bool any = std::any_of(
        include.cbegin(), include.cend(),
        [&](const QRegularExpression& re) { return re.isValid() && re.match(title).hasMatch(); });
    if (!any) return false;
  }
  return std::none_of(exclude.cbegin(), exclude.cend(), [&](const QRegularExpression& re) {
    return re.isValid() && re.match(title).hasMatch();
  });
}

int itemSeeders(const rss::Item& it) {
  for (const auto& [key, value] : it.namespace_elements) {
    const QString k = QString::fromStdString(key);
    if (k.endsWith(QStringLiteral("seeders"), Qt::CaseInsensitive)) {
      bool ok = false;
      const int n = QString::fromStdString(value).toInt(&ok);
      if (ok) return n;
    }
  }
  return 0;
}

QString addableLink(const rss::Item& it) {
  const auto usable = [](const QString& url) {
    const QString u = url.trimmed();
    return u.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive) ||
           u.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive);
  };
  for (const auto& [key, value] : it.namespace_elements) {
    if (key == gui::kTorrentFeedMagnetKey) {
      const QString u = QString::fromStdString(value).trimmed();
      if (usable(u)) return u;
    }
  }
  const QString enclosure = QString::fromStdString(it.enclosure.url).trimmed();
  if (usable(enclosure)) return enclosure;
  const QString link = QString::fromStdString(it.link).trimmed();
  if (usable(link)) return link;
  return {};
}

QString anyLink(const rss::Item& it) {
  const QString addable = addableLink(it);
  if (!addable.isEmpty()) return addable;
  const QString enclosure = QString::fromStdString(it.enclosure.url).trimmed();
  if (!enclosure.isEmpty()) return enclosure;
  return QString::fromStdString(it.link).trimmed();
}

bool movieFileOnDisk(const anime::Details& item) {
  const int total = item.episode_count > 0 ? item.episode_count : 1;
  for (int ep = 1; ep <= total; ++ep) {
    if (!track::libraryHasLocalEpisode(item.id, ep)) return false;
  }
  return true;
}

QString stripSearchSubtitle(QString q) {
  const int dashIdx = q.indexOf(QStringLiteral(" - "));
  if (dashIdx > 0) q = q.left(dashIdx).trimmed();
  return q.trimmed();
}

QStringList overlapTitles(const anime::Details& item) {
  QStringList out;
  const auto add = [&](const std::string& s) {
    const QString q = QString::fromStdString(s).trimmed();
    if (q.isEmpty()) return;
    if (!out.contains(q, Qt::CaseInsensitive)) out.push_back(q);
  };
  add(item.titles.romaji);
  add(item.titles.english);
  add(item.titles.japanese);
  for (const std::string& syn : item.titles.synonyms) add(syn);
  return out;
}

QList<QRegularExpression> compileLines(const std::string& text) {
  QList<QRegularExpression> out;
  const QStringList lines = QString::fromStdString(text).split(QLatin1Char('\n'));
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
    QRegularExpression re(line, QRegularExpression::CaseInsensitiveOption);
    if (re.isValid()) out.push_back(std::move(re));
  }
  return out;
}

}  // namespace

MovieTorrentMatch matchMovieTorrentFeed(const rss::Feed& feed, const QStringList& titles,
                                        const QList<QRegularExpression>& include,
                                        const QList<QRegularExpression>& exclude) {
  MovieTorrentMatch match;
  int bestSeeders = -1;
  for (const rss::Item& it : feed.items) {
    const QString title = QString::fromStdString(it.title).trimmed();
    if (title.isEmpty()) continue;
    if (releaseLooksLikeSeriesEpisode(title)) continue;
    if (!titleOverlaps(title, titles)) continue;
    if (!passesRegexFilters(title, include, exclude)) continue;
    ++match.count;
    const int seeders = itemSeeders(it);
    const QString link = anyLink(it);
    const bool canAdd = !addableLink(it).isEmpty();
    const bool better = match.bestTitle.isEmpty() || seeders > bestSeeders ||
                        (seeders == bestSeeders && canAdd && !match.bestLinkCanAdd);
    if (!better) continue;
    bestSeeders = seeders;
    match.bestTitle = title;
    match.bestLink = canAdd ? addableLink(it) : link;
    match.bestLinkCanAdd = canAdd;
  }
  return match;
}

MovieTorrentWatch* MovieTorrentWatch::instance() {
  static auto* watch = new MovieTorrentWatch(qApp);
  return watch;
}

MovieTorrentWatch::MovieTorrentWatch(QObject* parent) : QObject(parent) {}

void MovieTorrentWatch::start() {
  if (m_started) return;
  m_started = true;
  load();
  armEligible();
  m_timer = new QTimer(this);
  m_timer->setInterval(static_cast<int>(kRequestGapSecs * 1000));
  connect(m_timer, &QTimer::timeout, this, &MovieTorrentWatch::pump);
  m_timer->start();

  connect(&anime::db, &anime::Database::entryUpdated, this, [this](int) { armEligible(); });
  connect(&anime::db, &anime::Database::itemUpdated, this, [this](int) { armEligible(); });
  QTimer::singleShot(0, this, &MovieTorrentWatch::pump);
}

void MovieTorrentWatch::load() {
  const QByteArray raw = taiga::settings.movieTorrentWatchState().toUtf8();
  if (raw.isEmpty()) return;
  const QJsonObject root = QJsonDocument::fromJson(raw).object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    bool ok = false;
    const int id = it.key().toInt(&ok);
    if (!ok || !it.value().isObject()) continue;
    const QJsonObject o = it.value().toObject();
    State s;
    s.nextDue = static_cast<qint64>(o.value(QStringLiteral("next")).toDouble());
    s.lastCheck = static_cast<qint64>(o.value(QStringLiteral("checked")).toDouble());
    s.missStreak = o.value(QStringLiteral("miss")).toInt();
    s.hitCount = o.value(QStringLiteral("hits")).toInt();
    s.bestTitle = o.value(QStringLiteral("title")).toString();
    s.bestLink = o.value(QStringLiteral("link")).toString();
    s.addedLink = o.value(QStringLiteral("added")).toString();
    m_state.insert(id, std::move(s));
  }
}

void MovieTorrentWatch::save() const {
  QJsonObject root;
  for (auto it = m_state.cbegin(); it != m_state.cend(); ++it) {
    const State& s = it.value();
    QJsonObject o;
    o.insert(QStringLiteral("next"), static_cast<double>(s.nextDue));
    o.insert(QStringLiteral("checked"), static_cast<double>(s.lastCheck));
    o.insert(QStringLiteral("miss"), s.missStreak);
    o.insert(QStringLiteral("hits"), s.hitCount);
    o.insert(QStringLiteral("title"), s.bestTitle);
    o.insert(QStringLiteral("link"), s.bestLink);
    o.insert(QStringLiteral("added"), s.addedLink);
    root.insert(QString::number(it.key()), o);
  }
  taiga::settings.setMovieTorrentWatchState(
      QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

bool MovieTorrentWatch::isPollTarget(const int animeId) const {
  const anime::Details* item = anime::db.item(animeId);
  const ListEntry* entry = anime::db.entry(animeId);
  if (!item || !entry) return false;
  if (item->type != anime::Type::Movie) return false;
  if (entry->status != anime::list::Status::Watching) return false;
  if (!taiga::settings.listShowMatureContent() && anime::isNsfw(*item)) return false;
  const int total = item->episode_count > 0 ? item->episode_count : 1;
  if (entry->watched_episodes >= total) return false;
  if (movieFileOnDisk(*item)) return false;
  return true;
}

void MovieTorrentWatch::armEligible() {
  if (!m_started) return;
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  bool changed = false;
  for (const ListEntry& entry : anime::db.entries()) {
    if (!isPollTarget(entry.anime_id)) continue;
    if (m_state.contains(entry.anime_id)) continue;
    State s;
    s.nextDue = now + kFirstDelaySecs;
    m_state.insert(entry.anime_id, s);
    changed = true;
    emit updated(entry.anime_id);
  }
  if (changed) save();
}

MovieTorrentSnapshot MovieTorrentWatch::snapshot(const int animeId) const {
  MovieTorrentSnapshot snap;
  const anime::Details* item = anime::db.item(animeId);
  const ListEntry* entry = anime::db.entry(animeId);
  if (!item || item->type != anime::Type::Movie) return snap;
  if (entry && entry->status == anime::list::Status::Watching) snap.watching = true;
  if (movieFileOnDisk(*item)) snap.onDisk = true;
  const auto it = m_state.constFind(animeId);
  if (it != m_state.cend()) {
    snap.hitCount = it->hitCount;
    snap.lastCheckSecs = it->lastCheck;
    snap.bestTitle = it->bestTitle;
  }
  snap.checking = m_activeId == animeId && m_reply != nullptr;
  return snap;
}

void MovieTorrentWatch::checkNow(const int animeId) {
  if (!isPollTarget(animeId)) return;
  State& s = m_state[animeId];
  s.nextDue = QDateTime::currentSecsSinceEpoch();
  m_nextRequestNotBefore = 0;
  save();
  pump();
}

void MovieTorrentWatch::noteAutoAdded(const int animeId, const QString& link) {
  State& s = m_state[animeId];
  s.addedLink = link;
  save();
}

void MovieTorrentWatch::pump() {
  if (!m_started || m_reply) return;
  if (!taiga::settings.movieTorrentWatchEnabled()) return;
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (now < m_nextRequestNotBefore) return;

  int dueId = 0;
  qint64 earliest = 0;
  for (auto it = m_state.cbegin(); it != m_state.cend(); ++it) {
    if (!isPollTarget(it.key())) continue;
    if (it->nextDue > now) continue;
    if (dueId == 0 || it->nextDue < earliest) {
      dueId = it.key();
      earliest = it->nextDue;
    }
  }
  if (dueId == 0) return;

  const anime::Details* item = anime::db.item(dueId);
  if (!item) return;
  const QString romaji = stripSearchSubtitle(QString::fromStdString(item->titles.romaji));
  const QString english = stripSearchSubtitle(QString::fromStdString(item->titles.english));
  const QString primary = romaji.isEmpty() ? english : romaji;
  if (primary.isEmpty()) return;
  beginFetch(dueId, primary, false);
}

void MovieTorrentWatch::beginFetch(const int animeId, const QString& query,
                                   const bool tryingFallback) {
  const QString tmpl = QString::fromStdString(taiga::settings.torrentDiscoverySearchUrl());
  const QUrl url = taiga::torrentDiscoveryFeedFetchUrl(tmpl, query);
  if (!url.isValid()) return;

  QNetworkRequest req(url);
  taiga::applyCommonHeaders(req);
  req.setTransferTimeout(30000);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  m_activeId = animeId;
  m_tryingFallback = tryingFallback;
  m_reply = taiga::network()->get(req);
  emit updated(animeId);
  connect(m_reply, &QNetworkReply::finished, this, [this, animeId, query]() {
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    const int active = m_activeId;
    m_activeId = 0;
    m_nextRequestNotBefore = QDateTime::currentSecsSinceEpoch() + kRequestGapSecs;
    if (!reply) return;
    reply->deleteLater();
    if (active != animeId) return;

    const bool transportError = reply->error() != QNetworkReply::NoError;
    MovieTorrentMatch match;
    if (!transportError) {
      QString err;
      const auto feed = parseSyndicationFeed(reply->readAll(), &err);
      if (feed) {
        const anime::Details* item = anime::db.item(animeId);
        const QStringList titles = item ? overlapTitles(*item) : QStringList{query};
        match = matchMovieTorrentFeed(*feed, titles,
                                      compileLines(taiga::settings.torrentFeedIncludeRegexList()),
                                      compileLines(taiga::settings.torrentFeedExcludeRegexList()));
      }
    }

    if (match.count == 0 && !transportError && !m_tryingFallback) {
      const anime::Details* item = anime::db.item(animeId);
      if (item) {
        const QString romaji = stripSearchSubtitle(QString::fromStdString(item->titles.romaji));
        const QString english = stripSearchSubtitle(QString::fromStdString(item->titles.english));
        const QString fallback = romaji.isEmpty() ? QString{} : english;
        if (!fallback.isEmpty() && fallback.compare(query, Qt::CaseInsensitive) != 0) {
          m_nextRequestNotBefore = 0;
          beginFetch(animeId, fallback, true);
          return;
        }
      }
    }
    finishFetch(animeId, match, transportError);
  });
}

void MovieTorrentWatch::finishFetch(const int animeId, const MovieTorrentMatch& match,
                                    const bool hadTransportError) {
  State& s = m_state[animeId];
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (hadTransportError) {
    s.nextDue = now + kErrorRetrySecs;
    save();
    emit updated(animeId);
    return;
  }
  s.lastCheck = now;
  s.hitCount = match.count;
  s.bestTitle = match.bestTitle;
  s.bestLink = match.bestLink;
  if (match.count > 0) {
    s.missStreak = 0;
    s.nextDue = now + kAfterHitSecs;
  } else {
    ++s.missStreak;
    s.nextDue = now + missBackoffSecs(s.missStreak);
  }
  save();
  emit updated(animeId);

  if (match.bestLinkCanAdd && taiga::settings.movieTorrentWatchAutoAdd() &&
      taiga::settings.torrentQBitApiEnabled() && match.bestLink != s.addedLink) {
    const anime::Details* item = anime::db.item(animeId);
    QString folder;
    if (item) {
      folder = QString::fromStdString(item->titles.english).trimmed();
      if (folder.isEmpty()) folder = QString::fromStdString(item->titles.romaji).trimmed();
    }
    emit addRequested(animeId, match.bestLink, folder);
  }
}

}  // namespace gui
