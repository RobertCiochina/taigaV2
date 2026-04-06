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

#pragma once

#include <QtGlobal>

#include <chrono>
#include <string>
#include <vector>

#include "base/settings.hpp"
#include "media/anime.hpp"
#include "taiga/list_row_action.hpp"

namespace taiga {

/// Taiga v1 `track::TorrentAction` for `rss/torrent/options/newaction`.
enum class TorrentDiscoveryNewCatalogAction {
  Notify = 1,
  Download = 2,
};

class Settings final : public base::Settings {
public:
  void init() const;
  /// When autostart is enabled, refresh the Windows **Run** value to the current executable path.
  void ensureWindowsAutoStartFromSettings() const;

  Qt::ColorScheme appColorScheme() const;
  std::string service() const;
  std::vector<std::string> libraryFolders() const;
  /// Taiga v1: `anime/folders/watch/enabled` — watch library roots and trigger a debounced rescan.
  bool libraryWatchFoldersEnabled() const;
  /// Taiga v1: anime/folders/scan @minfilesize (bytes). If positive, library scan skips smaller videos.
  qint64 libraryScanMinFileSizeBytes() const;
  /// Taiga v1: `recognition/general/lookup_parent_directories` — use parent folder name as title hint when
  /// the filename alone has no parseable title.
  bool libraryScanLookupParentDirectories() const;
  /// Taiga v1: `recognition/mediaplayers/launchpath` — optional player executable for **Play** / open file.
  std::string mediaPlayerExecutablePath() const;
  std::chrono::milliseconds mediaDetectionInterval() const;

  std::string proxyHost() const;
  std::string proxyUsername() const;
  std::string proxyPassword() const;
  /// Taiga v1: `program/general/sslnorevoke` — relax TLS peer verification (implementation uses VerifyNone).
  bool networkRelaxedTls() const;

  /// When true, fetches the remote anime list once after the main window is shown (Taiga v1
  /// behavior).
  bool syncAutoOnStart() const;

  /// When true, synchronizes after the window regains focus if idle for at least
  /// syncOnWindowFocusMinutes().
  bool syncOnWindowFocus() const;
  int syncOnWindowFocusMinutes() const;

  bool welcomeSetupPromptDismissed() const;

  /// Matches Taiga v1 default (program/startup/checkversion).
  bool checkForUpdatesOnStartup() const;
  /// Matches Taiga v1 optional scan (program/startup/checkeps); off by default.
  bool scanLibraryOnStartup() const;
  /// Taiga v1: program/startup/minimize — start with the window minimized (or hidden to tray if
  /// minimizeToTray() is also on).
  bool startMinimized() const;
  /// Taiga v1: `program/general/autostart` — register in Windows **Run** (Windows only).
  bool startWithWindows() const;

  /// Taiga v1: program/general/enablerecognition
  bool mediaDetectionEnabled() const;
  /// Taiga v1: `account/update/auto` — when true, automatically update watched episode count on recognition.
  bool recognitionAutoUpdateList() const;
  /// Taiga v1: `account/update/delay` — seconds to wait after recognition before committing the list update.
  int recognitionUpdateDelaySeconds() const;
  /// Taiga v1: `account/update/outofrange` — skip auto-update if the detected episode exceeds the anime's
  /// total episode count.
  bool recognitionUpdateOutOfRange() const;
  /// Taiga v1: `recognition/mediaplayers/enabled` — when false, desktop player polling is off.
  bool mediaDetectionPlayersEnabled() const;
  /// Taiga v1: `recognition/streaming/enabled` — include web browsers in media detection (Windows).
  bool mediaDetectionStreamingEnabled() const;
  /// True when master recognition is on and at least one of desktop players or streaming detection is on.
  bool mediaDetectionPollingActive() const;
  /// Taiga v1 `recognition/anitomy/ignored_strings` (removed from modern Anitomy): substrings stripped from
  /// filenames/titles before parsing (newline, comma, or semicolon separated).
  std::string recognitionIgnoredSubstrings() const;
  /// Taiga v1: `recognition/streaming/providers/<slug>` — when the URL matches a known provider, that
  /// provider must be enabled or the detection pass is skipped (default: all **on**).
  bool streamProviderEnabled(const std::string& slug) const;
  /// Taiga v1: `program/notifications/balloon/recognized` — tray message when a playing title is matched.
  bool mediaNotifyRecognizedBalloon() const;
  /// Taiga v1: `program/notifications/balloon/notrecognized` — tray message for unmatched media.
  bool mediaNotifyUnrecognizedBalloon() const;
  /// Taiga v1: `program/notifications/balloon/format` — body text for recognized tray messages (`%title%`, `%episode%`, …).
  std::string mediaNotifyBalloonFormatRecognized() const;
  /// Body template when the parser could not match a list entry (typically `%name%`).
  std::string mediaNotifyBalloonFormatUnrecognized() const;
  /// Extra line appended to unrecognized balloons (v1 suggested similar titles); can be disabled.
  bool mediaNotifyBalloonUnrecognizedAppendHint() const;
  /// Taiga v1: program/general/enablesync — when false, manual/auto list sync is skipped.
  bool listSynchronizationEnabled() const;
  /// Taiga v1: `account/update/delay` — seconds before pushing the same title after a local change
  /// (debounced `sync::saveListEntry`). **0** = immediate.
  int syncListUpdateDelaySeconds() const;
  /// Taiga v1: `account/update/asktoconfirm` — confirm before each upload from the list editor / dialogs.
  bool syncListPushAskConfirm() const;

  /// Taiga v1: program/general/close — window close keeps the app running in the tray.
  bool closeToTray() const;
  /// Taiga v1: program/general/minimize — minimize sends the window to the tray.
  bool minimizeToTray() const;
  /// Taiga v1: inverse of program/general/hidesidebar — left navigation pane visibility.
  bool navigationSidebarVisible() const;
  /// Taiga v1: program/list/action/titlelang (romaji | english | native).
  anime::TitleLanguage listTitleLanguage() const;
  /// Taiga v1: program/list/action/doubleclick (int 0–5).
  ListRowAction listDoubleClickAction() const;
  /// Taiga v1: program/list/action/middleclick (int 0–5).
  ListRowAction listMiddleClickAction() const;
  /// Taiga v1: program/list/progress/showaired
  bool listProgressShowAired() const;
  /// Taiga v1: program/list/progress/showavailable (uses episode index from last library scan).
  bool listProgressShowAvailable() const;
  /// Taiga v1: program/list/filter/episodes/highlight — accent title when next episode is on disk.
  bool listHighlightNextEpisodeOnDisk() const;
  /// Taiga v1: program/list/filter/episodes/highlightedontop — sort those rows first (with active sort as tiebreaker).
  bool listHighlightAvailableOnTop() const;

  /// Taiga v1: `rss/torrent/search/address` — URL with `%title%` replaced by the URL-encoded query (HTTP GET).
  std::string torrentDiscoverySearchUrl() const;
  /// Taiga v1: `rss/torrent/source/address` — catalog RSS (fetched in-app on Torrents page).
  std::string torrentDiscoveryFeedSourceUrl() const;
  /// Taiga v1: `rss/torrent/options/autocheck` — periodic catalog RSS fetch while Taiga runs.
  bool torrentDiscoveryAutoCheckEnabled() const;
  /// Taiga v1: `rss/torrent/options/checkinterval` — minutes between automatic catalog checks.
  int torrentDiscoveryAutoCheckIntervalMinutes() const;
  /// Taiga v1: `rss/torrent/options/newaction` — notify (1) vs intended auto-download (2); download queue not ported yet.
  TorrentDiscoveryNewCatalogAction torrentDiscoveryNewCatalogAction() const;
  /// Taiga v1: `rss/torrent/options/downloadsortby` — episode_number | release_date (RSS table: title | date column).
  std::string torrentRssSortBy() const;
  /// Taiga v1: `rss/torrent/options/downloadsortorder` — ascending | descending.
  std::string torrentRssSortOrder() const;
  /// Taiga v1: `rss/torrent/filter/enabled` — when true, cap how many RSS items are shown (archive limit).
  bool torrentFeedFilterEnabled() const;
  /// Taiga v1: `rss/torrent/filter/archive_maxcount` — max feed items in the Torrents table when filter is on.
  int torrentFeedArchiveMaxItems() const;
  /// Qt port: simple in-app RSS filtering (regex, one per line).
  /// If non-empty, at least one include regex must match the row text for it to appear.
  std::string torrentFeedIncludeRegexList() const;
  /// Qt port: simple in-app RSS filtering (regex, one per line).
  /// If any exclude regex matches the row text, the row is hidden.
  std::string torrentFeedExcludeRegexList() const;
  /// Qt port: torrent RSS filter — hide items that match anime on your Dropped list.
  bool torrentFeedHideDropped() const;
  /// Qt port: torrent RSS filter — hide items that do not match any anime on your list.
  bool torrentFeedHideNotInList() const;
  /// Taiga v1 default filter: discard items at or below your watched progress.
  bool torrentFeedHideWatchedEpisodes() const;
  /// Taiga v1 default filter: discard items already available locally.
  bool torrentFeedHideAvailableEpisodes() const;
  /// Taiga v1 preset: prefer new versions (v2+). When enabled, older versions of the same episode are hidden
  /// if a newer version exists in the current RSS view.
  bool torrentFeedHideOlderVersionsWhenNewerExists() const;
  /// Taiga v1: torrent archive (discarded items). Exact-match title list applied on RSS fill.
  /// Stored in settings for persistence across runs.
  QStringList torrentFeedDiscardedTitleArchive() const;
  /// Taiga v1: `rss/torrent/options/downloadusemagnet` — when true, prefer magnet over HTTP .torrent when both exist.
  bool torrentDownloadUseMagnet() const;
  /// Taiga v1: `rss/torrent/options/downloadpath` — default directory passed to the torrent client (when supported).
  std::string torrentClientDownloadPath() const;
  /// Taiga v1: `rss/torrent/options/filedownloadpath` — where Taiga saves `.torrent` files (when implemented).
  std::string torrentFileSavePath() const;
  /// Taiga v1: `rss/torrent/options/autosetfolder` — prefer per-title library folder when passing paths to client.
  bool torrentDownloadUseAnimeFolder() const;
  /// Taiga v1: `rss/torrent/options/autousefolder` — fall back to client download path when no anime folder.
  bool torrentDownloadFallbackOnClientPath() const;
  /// Taiga v1: `rss/torrent/options/autocreatefolder` — create subfolder by title under client download path.
  bool torrentDownloadCreateSubfolder() const;
  /// Taiga v1: `rss/torrent/application/open` — launch torrent client after handling a torrent.
  bool torrentAppOpen() const;
  /// Taiga v1: `rss/torrent/application/mode` — 1 = default handler, 2 = custom executable path.
  int torrentAppMode() const;
  std::string torrentAppExecutablePath() const;

  /// JSON object from migrated v1 announce block (for future announce parity).
  std::string announceV1MigrationJson() const;

  void setAppColorScheme(const Qt::ColorScheme scheme) const;
  void setService(const std::string& service) const;
  void setLibraryFolders(std::vector<std::string> folders) const;
  void setLibraryWatchFoldersEnabled(bool enabled) const;
  void setLibraryScanMinFileSizeBytes(qint64 bytes) const;
  void setLibraryScanLookupParentDirectories(bool enabled) const;
  void setMediaPlayerExecutablePath(const std::string& path) const;
  void setMediaDetectionInterval(const std::chrono::milliseconds interval) const;
  void setProxyHost(const std::string& host) const;
  void setProxyUsername(const std::string& username) const;
  void setProxyPassword(const std::string& password) const;
  void setNetworkRelaxedTls(bool enabled) const;
  void setSyncAutoOnStart(bool enabled) const;
  void setSyncOnWindowFocus(bool enabled) const;
  void setSyncOnWindowFocusMinutes(int minutes) const;
  void setWelcomeSetupPromptDismissed(bool dismissed) const;
  void setCheckForUpdatesOnStartup(bool enabled) const;
  void setScanLibraryOnStartup(bool enabled) const;
  void setStartMinimized(bool enabled) const;
  void setStartWithWindows(bool enabled) const;
  void setMediaDetectionEnabled(bool enabled) const;
  void setRecognitionAutoUpdateList(bool enabled) const;
  void setRecognitionUpdateDelaySeconds(int seconds) const;
  void setRecognitionUpdateOutOfRange(bool enabled) const;
  void setMediaDetectionPlayersEnabled(bool enabled) const;
  void setMediaDetectionStreamingEnabled(bool enabled) const;
  void setRecognitionIgnoredSubstrings(const std::string& text) const;
  void setStreamProviderEnabled(const std::string& slug, bool enabled) const;
  void setMediaNotifyRecognizedBalloon(bool enabled) const;
  void setMediaNotifyUnrecognizedBalloon(bool enabled) const;
  void setMediaNotifyBalloonFormatRecognized(const std::string& format) const;
  void setMediaNotifyBalloonFormatUnrecognized(const std::string& format) const;
  void setMediaNotifyBalloonUnrecognizedAppendHint(bool enabled) const;
  void setListSynchronizationEnabled(bool enabled) const;
  void setSyncListUpdateDelaySeconds(int seconds) const;
  void setSyncListPushAskConfirm(bool enabled) const;
  void setCloseToTray(bool enabled) const;
  void setMinimizeToTray(bool enabled) const;
  void setNavigationSidebarVisible(bool visible) const;
  void setListTitleLanguage(anime::TitleLanguage language) const;
  void setListDoubleClickAction(ListRowAction action) const;
  void setListMiddleClickAction(ListRowAction action) const;
  void setListProgressShowAired(bool enabled) const;
  void setListProgressShowAvailable(bool enabled) const;
  void setListHighlightNextEpisodeOnDisk(bool enabled) const;
  void setListHighlightAvailableOnTop(bool enabled) const;
  void setTorrentDiscoverySearchUrl(const std::string& url) const;
  void setTorrentDiscoveryFeedSourceUrl(const std::string& url) const;
  void setTorrentDiscoveryAutoCheckEnabled(bool enabled) const;
  void setTorrentDiscoveryAutoCheckIntervalMinutes(int minutes) const;
  void setTorrentDiscoveryNewCatalogAction(TorrentDiscoveryNewCatalogAction action) const;
  void setTorrentRssSortBy(const std::string& value) const;
  void setTorrentRssSortOrder(const std::string& value) const;
  void setTorrentFeedFilterEnabled(bool enabled) const;
  void setTorrentFeedArchiveMaxItems(int count) const;
  void setTorrentFeedIncludeRegexList(const std::string& text) const;
  void setTorrentFeedExcludeRegexList(const std::string& text) const;
  void setTorrentFeedHideDropped(bool enabled) const;
  void setTorrentFeedHideNotInList(bool enabled) const;
  void setTorrentFeedHideWatchedEpisodes(bool enabled) const;
  void setTorrentFeedHideAvailableEpisodes(bool enabled) const;
  void setTorrentFeedHideOlderVersionsWhenNewerExists(bool enabled) const;
  void setTorrentFeedDiscardedTitleArchive(const QStringList& titles) const;
  void setTorrentDownloadUseMagnet(bool enabled) const;
  void setTorrentClientDownloadPath(const std::string& path) const;
  void setTorrentFileSavePath(const std::string& path) const;
  void setTorrentDownloadUseAnimeFolder(bool enabled) const;
  void setTorrentDownloadFallbackOnClientPath(bool enabled) const;
  void setTorrentDownloadCreateSubfolder(bool enabled) const;
  void setTorrentAppOpen(bool enabled) const;
  void setTorrentAppMode(int mode) const;
  void setTorrentAppExecutablePath(const std::string& path) const;
  void setAnnounceV1MigrationJson(const std::string& json) const;

private:
  QString fileName() const override;
};

inline Settings settings;

}  // namespace taiga
