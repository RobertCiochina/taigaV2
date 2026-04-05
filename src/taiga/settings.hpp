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

#include <chrono>
#include <string>
#include <vector>

#include "base/settings.hpp"

namespace taiga {

class Settings final : public base::Settings {
public:
  void init() const;

  Qt::ColorScheme appColorScheme() const;
  std::string service() const;
  std::vector<std::string> libraryFolders() const;
  std::chrono::milliseconds mediaDetectionInterval() const;

  std::string proxyHost() const;
  std::string proxyUsername() const;
  std::string proxyPassword() const;

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

  /// Taiga v1: program/general/enablerecognition
  bool mediaDetectionEnabled() const;
  /// Taiga v1: program/general/enablesharing
  bool sharingEnabled() const;
  /// Taiga v1: program/general/enablesync — when false, manual/auto list sync is skipped.
  bool listSynchronizationEnabled() const;

  void setAppColorScheme(const Qt::ColorScheme scheme) const;
  void setService(const std::string& service) const;
  void setLibraryFolders(std::vector<std::string> folders) const;
  void setMediaDetectionInterval(const std::chrono::milliseconds interval) const;
  void setProxyHost(const std::string& host) const;
  void setProxyUsername(const std::string& username) const;
  void setProxyPassword(const std::string& password) const;
  void setSyncAutoOnStart(bool enabled) const;
  void setSyncOnWindowFocus(bool enabled) const;
  void setSyncOnWindowFocusMinutes(int minutes) const;
  void setWelcomeSetupPromptDismissed(bool dismissed) const;
  void setCheckForUpdatesOnStartup(bool enabled) const;
  void setScanLibraryOnStartup(bool enabled) const;
  void setMediaDetectionEnabled(bool enabled) const;
  void setSharingEnabled(bool enabled) const;
  void setListSynchronizationEnabled(bool enabled) const;

private:
  QString fileName() const override;
};

inline Settings settings;

}  // namespace taiga
