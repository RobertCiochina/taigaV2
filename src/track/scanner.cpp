/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "scanner.hpp"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <optional>
#include <shared_mutex>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"


namespace track {

namespace {

std::unordered_map<int, std::unordered_set<int>> g_library_episodes;
// Episode file paths seen during the last scan (same authoritative source as g_library_episodes).
// Keyed by anime id -> episode number -> absolute file path.
std::unordered_map<int, std::unordered_map<int, QString>> g_library_episode_paths;
// Manual overrides from Library UI — not cleared by scanLibraryFolders.
std::unordered_map<int, std::unordered_set<int>> g_manual_episodes;
bool g_has_scan_results = false;
std::shared_mutex g_index_mu;
QString g_cache_last_error;
QString g_cache_last_info;
QStringList g_cache_log;
int g_best_saved_series = -1;
int g_best_saved_eps = -1;

void loadManualLibraryOverridesOnce() {
  static bool loaded = false;
  if (loaded) return;
  loaded = true;

  const QString json = taiga::settings.libraryManualOverridesJson();
  if (json.isEmpty()) return;

  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
  if (!doc.isArray()) return;

  for (const QJsonValue& val : doc.array()) {
    if (!val.isObject()) continue;
    const QJsonObject obj = val.toObject();
    const int id = obj[QStringLiteral("id")].toInt();
    const QString episode = obj[QStringLiteral("episode")].toString();
    if (id <= 0) continue;
    bool ok = false;
    int ep_no = episode.toInt(&ok);
    if (!ok || ep_no < 1) ep_no = 1;
    g_manual_episodes[id].insert(ep_no);
  }
}

void logCacheEvent(const QString& msg) {
  const QString line =
      QStringLiteral("%1  %2").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), msg);
  g_cache_log.push_back(line);
  constexpr int kMax = 250;
  while (g_cache_log.size() > kMax) g_cache_log.pop_front();
}

std::pair<int, int> snapshotIndexSizeLocked() {
  std::shared_lock lk(g_index_mu);
  const int series = static_cast<int>(g_library_episodes.size());
  int eps = 0;
  for (const auto& [_, set] : g_library_episodes) eps += static_cast<int>(set.size());
  return {series, eps};
}

QString libraryIndexCachePath() {
  const QString base = QString::fromStdString(taiga::get_data_path());
  const QString dir = QDir(base).filePath(QStringLiteral("cache"));
  QDir().mkpath(dir);
  return QDir(dir).filePath(QStringLiteral("library_episode_index.dat"));
}

QString libraryIndexCacheSignature() {
  // If library folders or scan options change, treat the cache as invalid.
  QString sig;
  sig += QString::fromStdString(
      std::format("minBytes={};", taiga::settings.libraryScanMinFileSizeBytes()));
  sig += QString::fromStdString(std::format(
      "lookupParent={};", taiga::settings.libraryScanLookupParentDirectories() ? 1 : 0));
  const auto folders = taiga::settings.libraryFolders();
  sig += QStringLiteral("folders=");
  for (const auto& f : folders) {
    sig += QString::fromStdString(f);
    sig += QLatin1Char('|');
  }
  return sig;
}

enum class CacheSaveResult { Saved, Skipped, Failed };

CacheSaveResult saveLibraryEpisodeIndexCacheLocked(const bool allow_regress,
                                                   const QString& source) {
  const auto [series, eps] = snapshotIndexSizeLocked();
  if (g_best_saved_series >= 0) {
    const bool worse_series = series < g_best_saved_series;
    const bool worse_eps = eps < g_best_saved_eps;
    if (!allow_regress && (worse_series || worse_eps)) {
      logCacheEvent(
          QStringLiteral("cache: save skipped (source=%1, worse index: %2 series/%3 eps < %4/%5)")
              .arg(source)
              .arg(series)
              .arg(eps)
              .arg(g_best_saved_series)
              .arg(g_best_saved_eps));
      return CacheSaveResult::Skipped;
    }
  }

  QSaveFile out(libraryIndexCachePath());
  if (!out.open(QIODevice::WriteOnly)) return CacheSaveResult::Failed;

  QDataStream ds(&out);
  ds.setVersion(QDataStream::Qt_6_0);
  ds << quint32(0x54474941);  // 'TGIA' magic
  ds << quint16(1);           // version
  ds << libraryIndexCacheSignature();

  ds << quint32(series);
  {
    std::shared_lock lk(g_index_mu);
    for (const auto& [aid, set] : g_library_episodes) {
      ds << qint32(aid);
      ds << quint32(set.size());
      for (const int ep : set) ds << qint32(ep);
    }
  }

  if (!out.commit()) return CacheSaveResult::Failed;
  g_best_saved_series = series;
  g_best_saved_eps = eps;
  return CacheSaveResult::Saved;
}

/// Map S00Exx-style absolute numbers onto 1..N for short specials when xx > N (common with
/// TVDB-style numbering). Skips unknown or non-season-0 releases.
int storageEpisodeNumber(const int anime_id, const track::Episode& episode) {
  int ep = QString::fromStdString(episode.element(anitomy::ElementKind::Episode, {})).toInt();
  if (ep < 1) return 0;
  const std::string season_str = episode.element(anitomy::ElementKind::Season, {});
  const bool is_s0 = season_str == "0" || season_str == "00";
  const Anime* item = anime::db.item(anime_id);
  if (!item || !is_s0 || item->episode_count < 1) return ep;
  if (ep > item->episode_count && item->episode_count <= 12) {
    return ((ep - 1) % item->episode_count) + 1;
  }
  return ep;
}

QStringList configuredLibraryRootsClean() {
  QStringList roots;
  roots.reserve(static_cast<int>(taiga::settings.libraryFolders().size()));
  for (const auto& r : taiga::settings.libraryFolders()) {
    const QString rp = QDir(QString::fromStdString(r)).absolutePath();
    if (!rp.isEmpty()) roots << QDir::cleanPath(rp);
  }
  return roots;
}

bool isUnderRootPath(const QString& dir, const QString& root) {
  const QString d = QDir::cleanPath(dir);
  const QString r = QDir::cleanPath(root);
  if (d.isEmpty() || r.isEmpty()) return false;
  if (d.compare(r, Qt::CaseInsensitive) == 0) return true;
  return d.startsWith(r + QDir::separator(), Qt::CaseInsensitive);
}

bool isIgnorableLibraryJunkEntry(const QString& name) {
  // Common OS metadata files that can remain after deleting the last episode.
  // We treat these as ignorable so "empty folder cleanup" still works.
  static const QSet<QString> kExact{
      QStringLiteral("desktop.ini"),
      QStringLiteral("thumbs.db"),
      QStringLiteral(".ds_store"),
  };
  const QString lower = name.trimmed().toLower();
  if (lower.isEmpty()) return true;
  if (kExact.contains(lower)) return true;
  return false;
}

void removeEmptyDirsUpToRoot(QString start_dir, const QString& root) {
  QString cur = QDir::cleanPath(start_dir);
  const QString root_clean = QDir::cleanPath(root);
  if (cur.isEmpty() || root_clean.isEmpty()) return;
  if (!isUnderRootPath(cur, root_clean)) return;

  // Never delete the root itself.
  while (!cur.isEmpty() && cur.compare(root_clean, Qt::CaseInsensitive) != 0) {
    QDir d(cur);
    const QFileInfoList entries =
        d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    bool has_non_junk = false;
    for (const QFileInfo& fi : entries) {
      const QString name = fi.fileName();
      // Only ignore known junk files; never ignore subdirectories.
      if (fi.isDir()) {
        has_non_junk = true;
        break;
      }
      if (!isIgnorableLibraryJunkEntry(name)) {
        has_non_junk = true;
        break;
      }
    }
    if (has_non_junk) break;

    const QString parent = QFileInfo(cur).absoluteDir().absolutePath();
    if (!QDir().rmdir(cur)) break;
    cur = parent;
  }
}

}  // namespace

void appendLibraryEpisodeIndexCacheDebugLine(const QString& msg) {
  logCacheEvent(msg);
}

void saveLibraryEpisodeIndexCacheAfterScan(const QString& source, const bool allow_regress) {
  g_cache_last_error.clear();
  g_cache_last_info.clear();
  logCacheEvent(QStringLiteral("cache: save requested (source=%1, allowRegress=%2)")
                    .arg(source)
                    .arg(allow_regress ? 1 : 0));
  const CacheSaveResult r = saveLibraryEpisodeIndexCacheLocked(allow_regress, source);
  if (r == CacheSaveResult::Saved) {
    g_cache_last_info =
        QStringLiteral("saved %1 series").arg(static_cast<int>(g_library_episodes.size()));
    logCacheEvent(QStringLiteral("cache: save ok (%1)").arg(g_cache_last_info));
  } else if (r == CacheSaveResult::Failed) {
    g_cache_last_error = QStringLiteral("write failed");
    logCacheEvent(
        QStringLiteral("cache: save failed (source=%1, %2)").arg(source).arg(g_cache_last_error));
  }
}

const std::unordered_map<int, std::unordered_set<int>>& libraryEpisodeAvailability() {
  return g_library_episodes;
}

bool libraryScanHasResults() {
  return g_has_scan_results;
}

bool loadLibraryEpisodeIndexCache() {
  g_cache_last_error.clear();
  g_cache_last_info.clear();
  logCacheEvent(QStringLiteral("cache: load requested"));
  QFile f(libraryIndexCachePath());
  if (!f.exists()) {
    g_cache_last_error = QStringLiteral("cache file missing");
    logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
    return false;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    g_cache_last_error = QStringLiteral("open failed");
    logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
    return false;
  }

  QDataStream ds(&f);
  ds.setVersion(QDataStream::Qt_6_0);

  quint32 magic = 0;
  quint16 ver = 0;
  QString sig;
  ds >> magic >> ver >> sig;
  if (ds.status() != QDataStream::Ok) {
    g_cache_last_error = QStringLiteral("header read failed");
    logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
    return false;
  }
  if (magic != 0x54474941 || ver != 1) {
    g_cache_last_error = QStringLiteral("bad magic/version");
    logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
    return false;
  }
  // Best-effort: do not hard-fail on signature mismatch. Folder strings can differ by slash style,
  // case, or trailing separators across runs, and an imperfect cache is still better than none.

  quint32 n = 0;
  ds >> n;
  if (ds.status() != QDataStream::Ok) {
    g_cache_last_error = QStringLiteral("count read failed");
    logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
    return false;
  }

  std::unordered_map<int, std::unordered_set<int>> loaded;
  loaded.reserve(static_cast<size_t>(n));
  for (quint32 i = 0; i < n; ++i) {
    qint32 aid = 0;
    quint32 ec = 0;
    ds >> aid >> ec;
    if (ds.status() != QDataStream::Ok) {
      g_cache_last_error = QStringLiteral("row read failed");
      logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
      return false;
    }
    auto& set = loaded[static_cast<int>(aid)];
    for (quint32 j = 0; j < ec; ++j) {
      qint32 ep = 0;
      ds >> ep;
      if (ds.status() != QDataStream::Ok) {
        g_cache_last_error = QStringLiteral("episode read failed");
        logCacheEvent(QStringLiteral("cache: load failed (%1)").arg(g_cache_last_error));
        return false;
      }
      set.insert(static_cast<int>(ep));
    }
  }

  int total_eps = 0;
  for (const auto& [_, eps] : loaded) total_eps += static_cast<int>(eps.size());

  {
    std::unique_lock lk(g_index_mu);
    g_library_episodes = std::move(loaded);
    g_has_scan_results = true;
  }
  g_cache_last_info = QStringLiteral("loaded %1 series, %2 episode(s)")
                          .arg(static_cast<int>(g_library_episodes.size()))
                          .arg(total_eps);
  logCacheEvent(QStringLiteral("cache: load ok (%1)").arg(g_cache_last_info));
  g_best_saved_series = static_cast<int>(g_library_episodes.size());
  g_best_saved_eps = total_eps;
  return true;
}

void saveLibraryEpisodeIndexCache() {
  g_cache_last_error.clear();
  g_cache_last_info.clear();
  // Non-scan save (exit-to-tray, etc.): avoid overwriting the cache with a smaller index that may
  // be caused by transient startup state (pre-sync recognition DB) rather than real file removal.
  const QString source = QStringLiteral("exit-or-manual-save");
  const CacheSaveResult r = saveLibraryEpisodeIndexCacheLocked(/*allow_regress=*/false, source);
  if (r == CacheSaveResult::Saved) {
    g_cache_last_info =
        QStringLiteral("saved %1 series").arg(static_cast<int>(g_library_episodes.size()));
    logCacheEvent(QStringLiteral("cache: save ok (%1)").arg(g_cache_last_info));
  } else if (r == CacheSaveResult::Failed) {
    g_cache_last_error = QStringLiteral("write failed");
    logCacheEvent(
        QStringLiteral("cache: save failed (source=%1, %2)").arg(source).arg(g_cache_last_error));
  }
}

QString libraryEpisodeIndexCacheLastError() {
  return g_cache_last_error;
}

QString libraryEpisodeIndexCacheLastInfo() {
  return g_cache_last_info;
}

QString libraryEpisodeIndexCacheDebugLog() {
  return g_cache_log.join(QLatin1Char('\n'));
}

bool libraryHasLocalEpisode(const int anime_id, const int episode_number) {
  if (episode_number < 1) return false;
  loadManualLibraryOverridesOnce();
  {
    std::shared_lock lk(g_index_mu);
    const auto it = g_library_episodes.find(anime_id);
    if (it != g_library_episodes.end() && it->second.contains(episode_number)) return true;
  }
  const auto it2 = g_manual_episodes.find(anime_id);
  return it2 != g_manual_episodes.end() && it2->second.contains(episode_number);
}

void removeLibraryEpisode(const int anime_id, const int episode_number) {
  if (episode_number < 1) return;
  std::unique_lock lk(g_index_mu);
  const auto it = g_library_episodes.find(anime_id);
  if (it == g_library_episodes.end()) return;
  it->second.erase(episode_number);
  if (it->second.empty()) {
    g_library_episodes.erase(it);
  }
  // Keep cached episode file paths in sync with availability.
  const auto itp = g_library_episode_paths.find(anime_id);
  if (itp != g_library_episode_paths.end()) {
    itp->second.erase(episode_number);
    if (itp->second.empty()) {
      g_library_episode_paths.erase(itp);
    }
  }
}

void addManualLibraryEpisode(const int anime_id, int episode) {
  if (episode < 1) episode = 1;
  g_manual_episodes[anime_id].insert(episode);
}

void removeManualLibraryEpisode(const int anime_id) {
  g_manual_episodes.erase(anime_id);
}

bool nextEpisodeIsOnDisk(const int anime_id, const anime::Details* anime,
                         const anime::list::Entry* entry) {
  if (!anime || !entry) return false;
  const int next = entry->watched_episodes + 1;
  if (next < 1) return false;
  if (anime->episode_count > 0 && next > anime->episode_count) return false;
  return libraryHasLocalEpisode(anime_id, next);
}

LibraryScanSummary scanLibraryFolders(const std::vector<std::string>& folders,
                                      const int max_entries, const bool allow_regress_apply) {
  LibraryScanSummary s;
  if (max_entries <= 0) return s;
  loadManualLibraryOverridesOnce();

  std::unordered_map<int, std::unordered_set<int>> local;
  std::unordered_map<int, std::unordered_map<int, QString>> local_paths;
  const bool diag = taiga::settings.cacheDiagnosticsEnabled();
  int unknown_files = 0;
  int diag_unknown_logged = 0;
  constexpr int kDiagUnknownLimit = 18;

  const qint64 min_bytes = taiga::settings.libraryScanMinFileSizeBytes();

  static const QSet<QString> kVideoExt{
      QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"),
      QStringLiteral("wmv"), QStringLiteral("mov"), QStringLiteral("webm"),
      QStringLiteral("m4v"), QStringLiteral("ogm"), QStringLiteral("ts"),
  };

  for (const auto& folder : folders) {
    const QDir root{QString::fromStdString(folder)};
    if (!root.exists()) continue;

    QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      if (s.entries_visited >= max_entries) return s;
      const QFileInfo fi(it.next());
      ++s.entries_visited;

      if (!kVideoExt.contains(fi.suffix().toLower())) continue;
      if (min_bytes > 0 && fi.size() < min_bytes) continue;
      ++s.video_files;

      auto episode =
          recognition::parseFileInfo(fi, {}, taiga::settings.libraryScanLookupParentDirectories());
      const int aid = recognition::identify(episode);

      // Targeted diagnostics (requested): help debug intermittent recognition of specific titles.
      // Logged only when cache diagnostics are enabled.
      const bool target_diag =
          diag &&
          (fi.filePath().contains(QStringLiteral("Witch Hat Atelier"), Qt::CaseInsensitive) ||
           fi.dir().dirName().contains(QStringLiteral("Witch Hat Atelier"), Qt::CaseInsensitive));
      if (target_diag) {
        const QString t = QString::fromStdString(episode.element(anitomy::ElementKind::Title, {}));
        const QString s0 =
            QString::fromStdString(episode.element(anitomy::ElementKind::Season, {}));
        const QString e0 =
            QString::fromStdString(episode.element(anitomy::ElementKind::Episode, {}));
        logCacheEvent(QStringLiteral("scan: target: title='%1' S='%2' E='%3' parent='%4' file='%5'")
                          .arg(t.left(120))
                          .arg(s0)
                          .arg(e0)
                          .arg(fi.dir().dirName().left(120))
                          .arg(fi.fileName().left(160)));
        logCacheEvent(QStringLiteral("scan: target: %1")
                          .arg(recognition::debugIdentifySummary(episode).left(260)));
      }

      if (aid != anime::kUnknownId) {
        ++s.recognized;
        const int ep = storageEpisodeNumber(aid, episode);
        if (ep > 0) {
          local[aid].insert(ep);
          // Record absolute file path for playback (best-effort). If multiple files map to the same
          // anime+episode, prefer the shorter path as a stable heuristic.
          const QString fp = fi.filePath();
          auto& m = local_paths[aid];
          const auto itp = m.find(ep);
          if (itp == m.end() || itp->second.isEmpty() ||
              (fp.size() > 0 && fp.size() < itp->second.size())) {
            m[ep] = fp;
          }
        } else {
          // No episode number in filename (movie, batch, or single-file release).
          // Treat as episode 1 so the entry appears in "Up next" and library lookups.
          local[aid].insert(1);
          const QString fp = fi.filePath();
          auto& m = local_paths[aid];
          const auto itp = m.find(1);
          if (itp == m.end() || itp->second.isEmpty() ||
              (fp.size() > 0 && fp.size() < itp->second.size())) {
            m[1] = fp;
          }
        }
        if (target_diag) {
          const Anime* item = anime::db.item(aid);
          QString name = QStringLiteral("<unknown>");
          if (item) {
            if (!item->titles.english.empty()) {
              name = QString::fromStdString(item->titles.english);
            } else {
              name = QString::fromStdString(item->titles.romaji);
            }
          }
          logCacheEvent(QStringLiteral("scan: target: recognized aid=%1 '%2' storedEp=%3")
                            .arg(aid)
                            .arg(name.left(120))
                            .arg(ep > 0 ? ep : 1));
        }
      } else {
        ++unknown_files;
        if (target_diag) {
          logCacheEvent(QStringLiteral("scan: target: NOT recognized (aid=unknown)"));
        }
        // For startup-pre-sync diagnostics: sample a few unknown matches so we can see what the
        // parser produced before the post-sync scan fixes them.
        if (diag && !allow_regress_apply && diag_unknown_logged < kDiagUnknownLimit) {
          ++diag_unknown_logged;
          const QString t =
              QString::fromStdString(episode.element(anitomy::ElementKind::Title, {}));
          const QString s0 =
              QString::fromStdString(episode.element(anitomy::ElementKind::Season, {}));
          const QString e0 =
              QString::fromStdString(episode.element(anitomy::ElementKind::Episode, {}));
          const QString parent = fi.dir().dirName();
          logCacheEvent(
              QStringLiteral(
                  "scan: unknown file #%1: title='%2' S='%3' E='%4' parent='%5' file='%6'")
                  .arg(diag_unknown_logged)
                  .arg(t.left(80))
                  .arg(s0)
                  .arg(e0)
                  .arg(parent.left(80))
                  .arg(fi.fileName().left(120)));
          logCacheEvent(QStringLiteral("scan: unknown file #%1: %2")
                            .arg(diag_unknown_logged)
                            .arg(recognition::debugIdentifySummary(episode).left(240)));
        }
      }
    }
  }

  s.series_with_local_episodes = static_cast<int>(local.size());
  int local_eps = 0;
  for (const auto& [_, set] : local) local_eps += static_cast<int>(set.size());
  {
    std::unique_lock lk(g_index_mu);
    const int cur_series = static_cast<int>(g_library_episodes.size());
    int cur_eps = 0;
    for (const auto& [_, set] : g_library_episodes) cur_eps += static_cast<int>(set.size());

    // If this scan is known to be potentially incomplete (startup-pre-sync), don't let it wipe out
    // a better index we just loaded from cache.
    const bool would_regress = static_cast<int>(local.size()) < cur_series || (local_eps < cur_eps);
    if (!allow_regress_apply && would_regress) {
      logCacheEvent(
          QStringLiteral("scan: apply skipped (worse than current: %1 series/%2 eps -> %3/%4)")
              .arg(cur_series)
              .arg(cur_eps)
              .arg(static_cast<int>(local.size()))
              .arg(local_eps));
      if (diag) {
        // Show which anime ids would disappear (present in current index but missing in scan).
        int shown = 0;
        constexpr int kMaxShown = 12;
        for (const auto& [aid, _] : g_library_episodes) {
          if (local.contains(aid)) continue;
          const Anime* item = anime::db.item(aid);
          QString name = QStringLiteral("<unknown>");
          if (item) {
            if (!item->titles.english.empty()) {
              name = QString::fromStdString(item->titles.english);
            } else {
              name = QString::fromStdString(item->titles.romaji);
            }
          }
          logCacheEvent(
              QStringLiteral("scan: missing vs current: aid=%1 '%2'").arg(aid).arg(name.left(120)));
          if (++shown >= kMaxShown) break;
        }
        if (unknown_files > 0) {
          logCacheEvent(QStringLiteral("scan: unknown totals: %1/%2 video file(s) not identified")
                            .arg(unknown_files)
                            .arg(s.video_files));
        }
      }
    } else {
      g_library_episodes = std::move(local);
      g_library_episode_paths = std::move(local_paths);
      g_has_scan_results = true;
      logCacheEvent(QStringLiteral("scan: apply ok (%1 series, %2 eps)")
                        .arg(static_cast<int>(g_library_episodes.size()))
                        .arg(local_eps));
    }
  }
  return s;
}

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number) {
  QElapsedTimer t;
  t.start();
  int visited = 0;
  const bool diag = taiga::settings.cacheDiagnosticsEnabled();
  QDirIterator it{path, QDir::Files, QDirIterator::Subdirectories};
  const qint64 min_bytes = taiga::settings.libraryScanMinFileSizeBytes();

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();
    ++visited;

    if (!info.isFile()) continue;
    if (min_bytes > 0 && info.size() < min_bytes) continue;

    auto episode =
        recognition::parseFileInfo(info, {}, taiga::settings.libraryScanLookupParentDirectories());

    if (recognition::identify(episode) != anime_id) continue;

    if (storageEpisodeNumber(anime_id, episode) != episode_number) continue;

    const qint64 ms = t.elapsed();
    if (diag && ms > 200) {
      qDebug() << "findEpisode:" << ms << "ms visited=" << visited << "root=" << path
               << "animeId=" << anime_id << "ep=" << episode_number;
    }
    return info.filePath();
  }

  const qint64 ms = t.elapsed();
  if (diag && ms > 200) {
    qDebug() << "findEpisode: miss" << ms << "ms visited=" << visited << "root=" << path
             << "animeId=" << anime_id << "ep=" << episode_number;
  }
  return std::nullopt;
}

std::optional<QString> libraryEpisodePath(const int anime_id, const int episode_number) {
  if (anime_id <= 0 || episode_number < 1) return std::nullopt;
  std::shared_lock lk(g_index_mu);
  const auto it = g_library_episode_paths.find(anime_id);
  if (it == g_library_episode_paths.end()) return std::nullopt;
  const auto it2 = it->second.find(episode_number);
  if (it2 == it->second.end()) return std::nullopt;
  if (it2->second.isEmpty()) return std::nullopt;
  return it2->second;
}

void removeLibraryEpisodePath(const int anime_id, const int episode_number) {
  if (anime_id <= 0 || episode_number < 1) return;
  std::unique_lock lk(g_index_mu);
  const auto it = g_library_episode_paths.find(anime_id);
  if (it == g_library_episode_paths.end()) return;
  it->second.erase(episode_number);
  if (it->second.empty()) {
    g_library_episode_paths.erase(it);
  }
}

std::optional<QString> findFolder(const QString& path, const int anime_id) {
  QDirIterator it{path, QDir::Dirs, QDirIterator::Subdirectories};

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isDir()) continue;

    auto episode =
        recognition::parseFileInfo(info, {}, taiga::settings.libraryScanLookupParentDirectories());

    if (track::recognition::identify(episode) != anime_id) continue;

    return info.filePath();
  }

  return std::nullopt;
}

void cleanupEmptyLibraryDirectoriesFromPath(const QString& dir_or_file_path) {
  if (dir_or_file_path.trimmed().isEmpty()) return;
  const QFileInfo info(dir_or_file_path);
  const QString start_dir =
      info.isDir() ? info.absoluteFilePath() : info.absoluteDir().absolutePath();
  if (start_dir.isEmpty()) return;

  const QString start_clean = QDir::cleanPath(start_dir);
  const QStringList roots = configuredLibraryRootsClean();
  for (const QString& root : roots) {
    if (isUnderRootPath(start_clean, root)) {
      removeEmptyDirsUpToRoot(start_clean, root);
      return;
    }
  }

  // Fallback for paths not under any configured library root (e.g. a torrent download folder).
  // Attempt to remove just the immediate parent directory if it is empty/junk-only.
  // Using the parent as the root causes removeEmptyDirsUpToRoot to stop after one level.
  const QString parent_clean = QDir::cleanPath(QFileInfo(start_clean).absoluteDir().absolutePath());
  if (!parent_clean.isEmpty() && parent_clean != start_clean) {
    removeEmptyDirsUpToRoot(start_clean, parent_clean);
  }
}

void cleanupEmptyLibraryDirectoriesForAnime(const int anime_id) {
  if (anime_id <= 0) return;
  const QStringList roots = configuredLibraryRootsClean();
  for (const QString& root : roots) {
    const auto folder = findFolder(root, anime_id);
    if (!folder.has_value()) continue;
    cleanupEmptyLibraryDirectoriesFromPath(*folder);
  }
}

}  // namespace track
