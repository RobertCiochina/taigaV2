/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
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

#include "settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <algorithm>
#include <cmath>
#include <ranges>

#include "base/string.hpp"
#include "compat/settings.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/config.h"
#include "taiga/path.hpp"
#include "taiga/version.hpp"

namespace {

#ifdef Q_OS_WIN
void applyWindowsAutoStartRunKey(const bool enable) {
  QSettings reg(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)",
                QSettings::NativeFormat);
  const QString key = QString::fromUtf8(TAIGA_APP_NAME);
  if (enable) {
    reg.setValue(key, QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
  } else {
    reg.remove(key);
  }
}
#endif

}  // namespace

namespace taiga {

void Settings::ensureWindowsAutoStartFromSettings() const {
#ifdef Q_OS_WIN
  if (startWithWindows()) applyWindowsAutoStartRunKey(true);
#endif
}

void Settings::init() const {
  const auto appVersion = taiga::version().to_string();

  // v1 to v2
  if (!QFile::exists(fileName())) {
    compat::v1::readSettings(std::format("{}/v1/settings.xml", get_data_path()), *this, accounts);
    setValue("meta.version", appVersion);
    return;
  }

  // v2.x
  const auto fileVersion = value("meta.version").toString().toStdString();
  if (fileVersion != appVersion) {
    setValue("meta.version", appVersion);
  }
}

QString Settings::fileName() const {
  return u"%1/settings.json"_s.arg(QString::fromStdString(get_data_path()));
}

////////////////////////////////////////////////////////////////////////////////

Qt::ColorScheme Settings::appColorScheme() const {
  return value("app.colorScheme", static_cast<int>(Qt::ColorScheme::Unknown))
      .value<Qt::ColorScheme>();
}

std::string Settings::service() const {
  return value("v1.service", sync::serviceSlug(sync::ServiceId::AniList)).toString().toStdString();
}

std::vector<std::string> Settings::libraryFolders() const {
  return value("library.folders").toJsonArray().toVariantList() |
         std::views::transform([](const QVariant& v) { return v.toString().toStdString(); }) |
         std::ranges::to<std::vector>();
}

bool Settings::libraryWatchFoldersEnabled() const {
  return value("library.watch.enabled", true).toBool();
}

qint64 Settings::libraryScanMinFileSizeBytes() const {
  const QVariant v = value("library.scan.minFileSizeBytes", 0);
  bool ok = false;
  const qlonglong n = v.toLongLong(&ok);
  if (!ok || n < 0) return 0;
  return static_cast<qint64>(n);
}

bool Settings::libraryScanLookupParentDirectories() const {
  return value("library.scan.lookupParentDirectories", true).toBool();
}

std::string Settings::mediaPlayerExecutablePath() const {
  return value("recognition.mediaPlayer.executablePath").toString().toStdString();
}

std::chrono::milliseconds Settings::mediaDetectionInterval() const {
  using rep = std::chrono::milliseconds::rep;
  const rep ms = static_cast<rep>(value("track.detection.interval", 3000).toInt());
  return std::chrono::milliseconds{std::clamp(ms, rep{1000}, rep{120000})};
}

std::string Settings::proxyHost() const {
  return value("program.proxy.host").toString().toStdString();
}

std::string Settings::proxyUsername() const {
  return value("program.proxy.username").toString().toStdString();
}

std::string Settings::proxyPassword() const {
  return value("program.proxy.password").toString().toStdString();
}

bool Settings::networkRelaxedTls() const {
  return value("program.network.relaxedTls", false).toBool();
}

bool Settings::syncAutoOnStart() const {
  // v2 key; fallback matches legacy v1 flat key name (account/myanimelist/login was reused for this bool).
  return value("sync.autoOnStart", value("account/myanimelist/login", false)).toBool();
}

bool Settings::syncOnWindowFocus() const {
  return value("sync.onWindowFocus", false).toBool();
}

int Settings::syncOnWindowFocusMinutes() const {
  const int m = value("sync.onWindowFocusMinutes", 15).toInt();
  return std::clamp(m, 1, 24 * 60);
}

bool Settings::welcomeSetupPromptDismissed() const {
  return value("app.welcomeSetupPromptDismissed", false).toBool();
}

bool Settings::checkForUpdatesOnStartup() const {
  return value("app.startup.checkForUpdates", true).toBool();
}

bool Settings::scanLibraryOnStartup() const {
  return value("app.startup.scanLibrary", false).toBool();
}

bool Settings::cacheDiagnosticsEnabled() const {
  return value("app.debug.cacheDiagnosticsEnabled", false).toBool();
}

void Settings::setCacheDiagnosticsEnabled(const bool enabled) const {
  setValue("app.debug.cacheDiagnosticsEnabled", enabled);
}

bool Settings::startMinimized() const {
  return value("app.startup.startMinimized", false).toBool();
}

bool Settings::startWithWindows() const {
  return value("app.startup.withWindows", false).toBool();
}

bool Settings::mediaDetectionEnabled() const {
  return value("track.detection.enabled", true).toBool();
}

bool Settings::mediaDetectionPlayersEnabled() const {
  return value("track.detection.playersEnabled", true).toBool();
}

bool Settings::mediaDetectionStreamingEnabled() const {
  return value("track.detection.streamingEnabled", false).toBool();
}

bool Settings::mediaDetectionPollingActive() const {
  return mediaDetectionEnabled() &&
         (mediaDetectionPlayersEnabled() || mediaDetectionStreamingEnabled());
}

bool Settings::recognitionAutoUpdateList() const {
  return value("recognition.listUpdate.auto", true).toBool();
}

bool Settings::recognitionDeleteAfterWatched() const {
  return value("recognition.listUpdate.deleteAfterWatched", false).toBool();
}

void Settings::setRecognitionDeleteAfterWatched(bool enabled) const {
  setValue("recognition.listUpdate.deleteAfterWatched", enabled);
}

int Settings::recognitionUpdateDelaySeconds() const {
  return std::clamp(value("recognition.listUpdate.delaySeconds", 120).toInt(), 1, 3600);
}

bool Settings::recognitionUpdateOutOfRange() const {
  return value("recognition.listUpdate.outOfRange", true).toBool();
}

std::string Settings::recognitionIgnoredSubstrings() const {
  return value("recognition.anitomy.ignoredSubstrings").toString().toStdString();
}

bool Settings::streamProviderEnabled(const std::string& slug) const {
  const QJsonObject o = value("recognition.streaming.providers").toJsonObject();
  const QString k = QString::fromStdString(slug);
  if (!o.contains(k)) return true;
  return o.value(k).toBool(true);
}

bool Settings::mediaNotifyRecognizedBalloon() const {
  return value("track.notifications.balloonRecognized", true).toBool();
}

bool Settings::mediaNotifyUnrecognizedBalloon() const {
  return value("track.notifications.balloonUnrecognized", true).toBool();
}

std::string Settings::mediaNotifyBalloonFormatRecognized() const {
  static const QString kDefault =
      QStringLiteral("%title%\nEpisode %episode% / %total%");
  const QString v = value("track.notifications.balloonFormatRecognized", kDefault).toString();
  const QString trimmed = v.trimmed();
  return (trimmed.isEmpty() ? kDefault : trimmed).toStdString();
}

std::string Settings::mediaNotifyBalloonFormatUnrecognized() const {
  static const QString kDefault = QStringLiteral("Could not match: %name%");
  const QString v = value("track.notifications.balloonFormatUnrecognized", kDefault).toString();
  const QString trimmed = v.trimmed();
  return (trimmed.isEmpty() ? kDefault : trimmed).toStdString();
}

bool Settings::mediaNotifyBalloonUnrecognizedAppendHint() const {
  return value("track.notifications.balloonUnrecognizedAppendHint", true).toBool();
}

bool Settings::listSynchronizationEnabled() const {
  return value("sync.listUpdates.enabled", true).toBool();
}

int Settings::syncListUpdateDelaySeconds() const {
  const int d = value("sync.listUpdates.apiDelaySeconds", 0).toInt();
  return std::clamp(d, 0, 86400);
}

bool Settings::syncListPushAskConfirm() const {
  return value("sync.listPush.askConfirm", true).toBool();
}

bool Settings::closeToTray() const {
  return value("app.window.closeToTray", false).toBool();
}

bool Settings::minimizeToTray() const {
  return value("app.window.minimizeToTray", false).toBool();
}

bool Settings::navigationSidebarVisible() const {
  return value("app.window.navigationSidebarVisible", true).toBool();
}

anime::TitleLanguage Settings::listTitleLanguage() const {
  const QString v = value("list.displayTitleLanguage", QStringLiteral("romaji")).toString();
  if (v.compare(u"english", Qt::CaseInsensitive) == 0) return anime::TitleLanguage::English;
  if (v.compare(u"native", Qt::CaseInsensitive) == 0) return anime::TitleLanguage::Native;
  return anime::TitleLanguage::Romaji;
}

ListRowAction Settings::listDoubleClickAction() const {
  const int v = value("list.action.doubleClick", static_cast<int>(ListRowAction::ShowDetails)).toInt();
  return static_cast<ListRowAction>(std::clamp(v, 0, 5));
}

ListRowAction Settings::listMiddleClickAction() const {
  const int v = value("list.action.middleClick", static_cast<int>(ListRowAction::PlayNext)).toInt();
  return static_cast<ListRowAction>(std::clamp(v, 0, 5));
}

bool Settings::listProgressShowAired() const {
  return value("list.progress.showAired", true).toBool();
}

bool Settings::listProgressShowAvailable() const {
  return value("list.progress.showAvailable", true).toBool();
}

bool Settings::listHighlightNextEpisodeOnDisk() const {
  return value("list.highlightNextEpisodeOnDisk", true).toBool();
}

bool Settings::listHighlightAvailableOnTop() const {
  return value("list.highlightAvailableOnTop", false).toBool();
}

std::string Settings::torrentDiscoverySearchUrl() const {
  return value("torrent.discovery.searchUrl").toString().toStdString();
}

std::string Settings::torrentDiscoveryFeedSourceUrl() const {
  return value("torrent.discovery.feedSourceUrl").toString().toStdString();
}

bool Settings::torrentDiscoveryAutoCheckEnabled() const {
  return value("torrent.discovery.autoCheck", true).toBool();
}

int Settings::torrentDiscoveryAutoCheckIntervalMinutes() const {
  const int m = value("torrent.discovery.autoCheckIntervalMinutes", 60).toInt();
  return std::clamp(m, 5, 24 * 60);
}

TorrentDiscoveryNewCatalogAction Settings::torrentDiscoveryNewCatalogAction() const {
  const int v = value("torrent.discovery.newCatalogAction", 1).toInt();
  if (v == static_cast<int>(TorrentDiscoveryNewCatalogAction::Download)) {
    return TorrentDiscoveryNewCatalogAction::Download;
  }
  return TorrentDiscoveryNewCatalogAction::Notify;
}

std::string Settings::torrentRssSortBy() const {
  const QString v = value("torrent.rss.sortBy", QStringLiteral("episode_number")).toString();
  if (v.compare(u"release_date", Qt::CaseInsensitive) == 0) return "release_date";
  return "episode_number";
}

std::string Settings::torrentRssSortOrder() const {
  const QString v = value("torrent.rss.sortOrder", QStringLiteral("ascending")).toString();
  if (v.compare(u"descending", Qt::CaseInsensitive) == 0) return "descending";
  return "ascending";
}

bool Settings::torrentFeedFilterEnabled() const {
  return value("torrent.feed.filterEnabled", true).toBool();
}

int Settings::torrentFeedArchiveMaxItems() const {
  const int v = value("torrent.feed.archiveMaxItems", 1000).toInt();
  if (v <= 0) return 1000;
  return std::clamp(v, 100, 50000);
}

std::string Settings::torrentFeedIncludeRegexList() const {
  return value("torrent.feed.includeRegex", QString{}).toString().toStdString();
}

std::string Settings::torrentFeedExcludeRegexList() const {
  return value("torrent.feed.excludeRegex", QString{}).toString().toStdString();
}

bool Settings::torrentFeedHideDropped() const {
  return value("torrent.feed.hideDropped", false).toBool();
}

bool Settings::torrentFeedHideNotInList() const {
  return value("torrent.feed.hideNotInList", false).toBool();
}

bool Settings::torrentFeedHideWatchedEpisodes() const {
  return value("torrent.feed.hideWatchedEpisodes", false).toBool();
}

bool Settings::torrentFeedHideAvailableEpisodes() const {
  return value("torrent.feed.hideAvailableEpisodes", false).toBool();
}

bool Settings::torrentFeedHideOlderVersionsWhenNewerExists() const {
  return value("torrent.feed.hideOlderVersionsWhenNewerExists", false).toBool();
}

QStringList Settings::torrentFeedDiscardedTitleArchive() const {
  const QString raw = value("torrent.feed.discardedTitleArchive", QString{}).toString();
  if (raw.isEmpty()) return {};
  const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
  if (!doc.isArray()) return {};
  QStringList out;
  out.reserve(doc.array().size());
  for (const QJsonValue& v : doc.array()) {
    if (v.isString()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) out.push_back(s);
    }
  }
  out.removeDuplicates();
  return out;
}

bool Settings::torrentDownloadUseMagnet() const {
  return value("torrent.download.useMagnet", false).toBool();
}

std::string Settings::torrentClientDownloadPath() const {
  return value("torrent.paths.clientDownload").toString().toStdString();
}

std::string Settings::torrentFileSavePath() const {
  return value("torrent.paths.torrentFileSave").toString().toStdString();
}

bool Settings::torrentDownloadUseAnimeFolder() const {
  return value("torrent.options.useAnimeFolder", true).toBool();
}

bool Settings::torrentDownloadFallbackOnClientPath() const {
  return value("torrent.options.fallbackOnClientPath", false).toBool();
}

bool Settings::torrentDownloadCreateSubfolder() const {
  return value("torrent.options.createSubfolder", false).toBool();
}

bool Settings::torrentAppOpen() const {
  return value("torrent.app.open", true).toBool();
}

int Settings::torrentAppMode() const {
  const int m = value("torrent.app.mode", 1).toInt();
  return m == 2 ? 2 : 1;
}

std::string Settings::torrentAppExecutablePath() const {
  return value("torrent.app.executablePath").toString().toStdString();
}

QString Settings::torrentSearchTitleForAnime(const int anime_id) const {
  return value(QStringLiteral("torrent.searchTitleCache.%1").arg(anime_id)).toString();
}
void Settings::setTorrentSearchTitleForAnime(const int anime_id, const QString& title) const {
  setValue(QStringLiteral("torrent.searchTitleCache.%1").arg(anime_id), title);
}

bool Settings::torrentQBitApiEnabled() const {
  return value("torrent.qbit.apiEnabled", true).toBool();
}
std::string Settings::torrentQBitApiUrl() const {
  return value("torrent.qbit.apiUrl", QStringLiteral("http://localhost:8080")).toString().toStdString();
}
std::string Settings::torrentQBitApiUsername() const {
  return value("torrent.qbit.apiUsername", QStringLiteral("admin")).toString().toStdString();
}
std::string Settings::torrentQBitApiPassword() const {
  return value("torrent.qbit.apiPassword").toString().toStdString();
}
void Settings::setTorrentQBitApiEnabled(const bool enabled) const {
  setValue("torrent.qbit.apiEnabled", enabled);
}
void Settings::setTorrentQBitApiUrl(const std::string& url) const {
  setValue("torrent.qbit.apiUrl", QString::fromStdString(url));
}
void Settings::setTorrentQBitApiUsername(const std::string& username) const {
  setValue("torrent.qbit.apiUsername", QString::fromStdString(username));
}
void Settings::setTorrentQBitApiPassword(const std::string& password) const {
  setValue("torrent.qbit.apiPassword", QString::fromStdString(password));
}

std::string Settings::announceV1MigrationJson() const {
  return value("compat.v1.announceMigration").toString().toStdString();
}

////////////////////////////////////////////////////////////////////////////////

void Settings::setAppColorScheme(const Qt::ColorScheme scheme) const {
  setValue("app.colorScheme", static_cast<int>(scheme));
}

void Settings::setService(const std::string& service) const {
  setValue("v1.service", service);
}

void Settings::setLibraryFolders(std::vector<std::string> folders) const {
  const auto list =
      folders |
      std::views::transform([](const std::string& s) { return QString::fromStdString(s); }) |
      std::ranges::to<QList>();
  setValue("library.folders", QJsonArray::fromStringList(list));
}

void Settings::setLibraryWatchFoldersEnabled(const bool enabled) const {
  setValue("library.watch.enabled", enabled);
}

void Settings::setLibraryScanMinFileSizeBytes(const qint64 bytes) const {
  const qlonglong n = bytes < 0 ? 0 : static_cast<qlonglong>(bytes);
  setValue("library.scan.minFileSizeBytes", QVariant{n});
}

void Settings::setLibraryScanLookupParentDirectories(const bool enabled) const {
  setValue("library.scan.lookupParentDirectories", enabled);
}

void Settings::setMediaPlayerExecutablePath(const std::string& path) const {
  setValue("recognition.mediaPlayer.executablePath", QString::fromStdString(path));
}

void Settings::setMediaDetectionInterval(const std::chrono::milliseconds interval) const {
  using rep = std::chrono::milliseconds::rep;
  const rep c = interval.count();
  const rep cl = std::clamp(c, rep{1000}, rep{120000});
  setValue("track.detection.interval", static_cast<int>(cl));
}

void Settings::setProxyHost(const std::string& host) const {
  setValue("program.proxy.host", host);
}

void Settings::setProxyUsername(const std::string& username) const {
  setValue("program.proxy.username", username);
}

void Settings::setProxyPassword(const std::string& password) const {
  setValue("program.proxy.password", password);
}

void Settings::setNetworkRelaxedTls(const bool enabled) const {
  setValue("program.network.relaxedTls", enabled);
}

void Settings::setSyncAutoOnStart(const bool enabled) const {
  setValue("sync.autoOnStart", enabled);
}

void Settings::setSyncOnWindowFocus(const bool enabled) const {
  setValue("sync.onWindowFocus", enabled);
}

void Settings::setSyncOnWindowFocusMinutes(const int minutes) const {
  setValue("sync.onWindowFocusMinutes", std::clamp(minutes, 1, 24 * 60));
}

void Settings::setWelcomeSetupPromptDismissed(const bool dismissed) const {
  setValue("app.welcomeSetupPromptDismissed", dismissed);
}

void Settings::setCheckForUpdatesOnStartup(const bool enabled) const {
  setValue("app.startup.checkForUpdates", enabled);
}

void Settings::setScanLibraryOnStartup(const bool enabled) const {
  setValue("app.startup.scanLibrary", enabled);
}

void Settings::setStartMinimized(const bool enabled) const {
  setValue("app.startup.startMinimized", enabled);
}

void Settings::setStartWithWindows(const bool enabled) const {
  setValue("app.startup.withWindows", enabled);
#ifdef Q_OS_WIN
  applyWindowsAutoStartRunKey(enabled);
#endif
}

void Settings::setMediaDetectionEnabled(const bool enabled) const {
  setValue("track.detection.enabled", enabled);
}

void Settings::setMediaDetectionPlayersEnabled(const bool enabled) const {
  setValue("track.detection.playersEnabled", enabled);
}

void Settings::setMediaDetectionStreamingEnabled(const bool enabled) const {
  setValue("track.detection.streamingEnabled", enabled);
}

void Settings::setRecognitionAutoUpdateList(const bool enabled) const {
  setValue("recognition.listUpdate.auto", enabled);
}

void Settings::setRecognitionUpdateDelaySeconds(const int seconds) const {
  setValue("recognition.listUpdate.delaySeconds", std::clamp(seconds, 1, 3600));
}

void Settings::setRecognitionUpdateOutOfRange(const bool enabled) const {
  setValue("recognition.listUpdate.outOfRange", enabled);
}

void Settings::setRecognitionIgnoredSubstrings(const std::string& text) const {
  setValue("recognition.anitomy.ignoredSubstrings", QString::fromStdString(text));
}

void Settings::setStreamProviderEnabled(const std::string& slug, const bool enabled) const {
  QJsonObject o = value("recognition.streaming.providers").toJsonObject();
  o[QString::fromStdString(slug)] = enabled;
  setValue("recognition.streaming.providers", o);
}

void Settings::setMediaNotifyRecognizedBalloon(const bool enabled) const {
  setValue("track.notifications.balloonRecognized", enabled);
}

void Settings::setMediaNotifyUnrecognizedBalloon(const bool enabled) const {
  setValue("track.notifications.balloonUnrecognized", enabled);
}

void Settings::setMediaNotifyBalloonFormatRecognized(const std::string& format) const {
  setValue("track.notifications.balloonFormatRecognized", QString::fromStdString(format));
}

void Settings::setMediaNotifyBalloonFormatUnrecognized(const std::string& format) const {
  setValue("track.notifications.balloonFormatUnrecognized", QString::fromStdString(format));
}

void Settings::setMediaNotifyBalloonUnrecognizedAppendHint(const bool enabled) const {
  setValue("track.notifications.balloonUnrecognizedAppendHint", enabled);
}

void Settings::setListSynchronizationEnabled(const bool enabled) const {
  setValue("sync.listUpdates.enabled", enabled);
}

void Settings::setSyncListUpdateDelaySeconds(const int seconds) const {
  setValue("sync.listUpdates.apiDelaySeconds", std::clamp(seconds, 0, 86400));
}

void Settings::setSyncListPushAskConfirm(const bool enabled) const {
  setValue("sync.listPush.askConfirm", enabled);
}

void Settings::setCloseToTray(const bool enabled) const {
  setValue("app.window.closeToTray", enabled);
}

void Settings::setMinimizeToTray(const bool enabled) const {
  setValue("app.window.minimizeToTray", enabled);
}

void Settings::setNavigationSidebarVisible(const bool visible) const {
  setValue("app.window.navigationSidebarVisible", visible);
}

void Settings::setListTitleLanguage(const anime::TitleLanguage language) const {
  QString slug = QStringLiteral("romaji");
  switch (language) {
    case anime::TitleLanguage::English:
      slug = QStringLiteral("english");
      break;
    case anime::TitleLanguage::Native:
      slug = QStringLiteral("native");
      break;
    case anime::TitleLanguage::Romaji:
    default:
      break;
  }
  setValue("list.displayTitleLanguage", slug);
}

void Settings::setListDoubleClickAction(const ListRowAction action) const {
  setValue("list.action.doubleClick", static_cast<int>(action));
}

void Settings::setListMiddleClickAction(const ListRowAction action) const {
  setValue("list.action.middleClick", static_cast<int>(action));
}

void Settings::setListProgressShowAired(const bool enabled) const {
  setValue("list.progress.showAired", enabled);
}

void Settings::setListProgressShowAvailable(const bool enabled) const {
  setValue("list.progress.showAvailable", enabled);
}

void Settings::setListHighlightNextEpisodeOnDisk(const bool enabled) const {
  setValue("list.highlightNextEpisodeOnDisk", enabled);
}

void Settings::setListHighlightAvailableOnTop(const bool enabled) const {
  setValue("list.highlightAvailableOnTop", enabled);
}

void Settings::setTorrentDiscoverySearchUrl(const std::string& url) const {
  setValue("torrent.discovery.searchUrl", QString::fromStdString(url));
}

void Settings::setTorrentDiscoveryFeedSourceUrl(const std::string& url) const {
  setValue("torrent.discovery.feedSourceUrl", QString::fromStdString(url));
}

void Settings::setTorrentDiscoveryAutoCheckEnabled(const bool enabled) const {
  setValue("torrent.discovery.autoCheck", enabled);
}

void Settings::setTorrentDiscoveryAutoCheckIntervalMinutes(const int minutes) const {
  setValue("torrent.discovery.autoCheckIntervalMinutes", std::clamp(minutes, 5, 24 * 60));
}

void Settings::setTorrentDiscoveryNewCatalogAction(const TorrentDiscoveryNewCatalogAction action) const {
  const int v = action == TorrentDiscoveryNewCatalogAction::Download
                    ? static_cast<int>(TorrentDiscoveryNewCatalogAction::Download)
                    : static_cast<int>(TorrentDiscoveryNewCatalogAction::Notify);
  setValue("torrent.discovery.newCatalogAction", v);
}

void Settings::setTorrentRssSortBy(const std::string& value) const {
  QString s = QString::fromStdString(value).trimmed();
  if (s.compare(u"release_date", Qt::CaseInsensitive) == 0) {
    setValue("torrent.rss.sortBy", QStringLiteral("release_date"));
  } else {
    setValue("torrent.rss.sortBy", QStringLiteral("episode_number"));
  }
}

void Settings::setTorrentRssSortOrder(const std::string& value) const {
  QString s = QString::fromStdString(value).trimmed();
  if (s.compare(u"descending", Qt::CaseInsensitive) == 0) {
    setValue("torrent.rss.sortOrder", QStringLiteral("descending"));
  } else {
    setValue("torrent.rss.sortOrder", QStringLiteral("ascending"));
  }
}

void Settings::setTorrentFeedFilterEnabled(const bool enabled) const {
  setValue("torrent.feed.filterEnabled", enabled);
}

void Settings::setTorrentFeedArchiveMaxItems(const int count) const {
  setValue("torrent.feed.archiveMaxItems", std::clamp(count, 100, 50000));
}

void Settings::setTorrentFeedIncludeRegexList(const std::string& text) const {
  setValue("torrent.feed.includeRegex", QString::fromStdString(text));
}

void Settings::setTorrentFeedExcludeRegexList(const std::string& text) const {
  setValue("torrent.feed.excludeRegex", QString::fromStdString(text));
}

void Settings::setTorrentFeedHideDropped(const bool enabled) const {
  setValue("torrent.feed.hideDropped", enabled);
}

void Settings::setTorrentFeedHideNotInList(const bool enabled) const {
  setValue("torrent.feed.hideNotInList", enabled);
}

void Settings::setTorrentFeedHideWatchedEpisodes(const bool enabled) const {
  setValue("torrent.feed.hideWatchedEpisodes", enabled);
}

void Settings::setTorrentFeedHideAvailableEpisodes(const bool enabled) const {
  setValue("torrent.feed.hideAvailableEpisodes", enabled);
}

void Settings::setTorrentFeedHideOlderVersionsWhenNewerExists(const bool enabled) const {
  setValue("torrent.feed.hideOlderVersionsWhenNewerExists", enabled);
}

void Settings::setTorrentFeedDiscardedTitleArchive(const QStringList& titles) const {
  QStringList t = titles;
  for (QString& s : t) s = s.trimmed();
  t.removeAll(QString{});
  t.removeDuplicates();
  constexpr int kMax = 5000;  // safety cap; v1 stored on disk, here we store in settings.json
  if (t.size() > kMax) {
    t = t.mid(t.size() - kMax);
  }
  QJsonArray arr;
  for (const QString& s : t) arr.push_back(s);
  setValue("torrent.feed.discardedTitleArchive", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void Settings::setTorrentDownloadUseMagnet(const bool enabled) const {
  setValue("torrent.download.useMagnet", enabled);
}

void Settings::setTorrentClientDownloadPath(const std::string& path) const {
  setValue("torrent.paths.clientDownload", QString::fromStdString(path));
}

void Settings::setTorrentFileSavePath(const std::string& path) const {
  setValue("torrent.paths.torrentFileSave", QString::fromStdString(path));
}

void Settings::setTorrentDownloadUseAnimeFolder(const bool enabled) const {
  setValue("torrent.options.useAnimeFolder", enabled);
}

void Settings::setTorrentDownloadFallbackOnClientPath(const bool enabled) const {
  setValue("torrent.options.fallbackOnClientPath", enabled);
}

void Settings::setTorrentDownloadCreateSubfolder(const bool enabled) const {
  setValue("torrent.options.createSubfolder", enabled);
}

void Settings::setTorrentAppOpen(const bool enabled) const {
  setValue("torrent.app.open", enabled);
}

void Settings::setTorrentAppMode(const int mode) const {
  setValue("torrent.app.mode", mode == 2 ? 2 : 1);
}

void Settings::setTorrentAppExecutablePath(const std::string& path) const {
  setValue("torrent.app.executablePath", QString::fromStdString(path));
}

void Settings::setAnnounceV1MigrationJson(const std::string& json) const {
  setValue("compat.v1.announceMigration", QString::fromStdString(json));
}

QString Settings::libraryManualOverridesJson() const {
  return value("library.manualOverrides", QString{}).toString();
}

void Settings::setLibraryManualOverridesJson(const QString& json) const {
  setValue("library.manualOverrides", json);
}

}  // namespace taiga
