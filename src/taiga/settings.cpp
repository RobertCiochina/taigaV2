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

#include <QFile>
#include <QJsonArray>
#include <QString>
#include <algorithm>
#include <cmath>
#include <ranges>

#include "base/string.hpp"
#include "compat/settings.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"
#include "taiga/version.hpp"

namespace taiga {

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

bool Settings::startMinimized() const {
  return value("app.startup.startMinimized", false).toBool();
}

bool Settings::mediaDetectionEnabled() const {
  return value("track.detection.enabled", true).toBool();
}

bool Settings::sharingEnabled() const {
  return value("app.features.sharingEnabled", true).toBool();
}

bool Settings::listSynchronizationEnabled() const {
  return value("sync.listUpdates.enabled", true).toBool();
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

void Settings::setMediaDetectionEnabled(const bool enabled) const {
  setValue("track.detection.enabled", enabled);
}

void Settings::setSharingEnabled(const bool enabled) const {
  setValue("app.features.sharingEnabled", enabled);
}

void Settings::setListSynchronizationEnabled(const bool enabled) const {
  setValue("sync.listUpdates.enabled", enabled);
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

}  // namespace taiga
