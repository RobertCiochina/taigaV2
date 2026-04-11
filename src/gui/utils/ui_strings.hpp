/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QCoreApplication>
#include <QString>

namespace gui {

/// Opens the media (anime) dialog on the Details tab — menus, settings, and list actions.
inline QString mediaViewDetailsActionLabel() {
  return QCoreApplication::translate("TaigaGui", "View details…");
}

/// Open the on-disk library folder for one title (anime list, media menu, library browser).
inline QString libraryOpenFolderActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Open folder");
}

inline QString libraryOpenFolderMessageTitle() {
  return QCoreApplication::translate("TaigaGui", "Open folder");
}

inline QString libraryOpenFolderForTitleToolTip() {
  return QCoreApplication::translate(
      "TaigaGui", "Open the library folder that contains this title, in Explorer");
}

inline QString libraryNoFolderForTitleMessage() {
  return QCoreApplication::translate("TaigaGui",
                                       "Could not find a library folder for this title.");
}

inline QString libraryNoFolderForNamedTitleMessage(const QString& title) {
  return QCoreApplication::translate("TaigaGui",
                                       "Could not find a library folder for \"%1\".")
      .arg(title);
}

inline QString playNextEpisodeActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Play next episode");
}

/// Shown when Play next cannot resolve a file for the current title.
inline QString playNextEpisodeNotFoundMessage() {
  return QCoreApplication::translate(
      "TaigaGui", "Could not find the next episode in your library folders for this title.");
}

inline QString playingNextEpisodeStatusMessage() {
  return QCoreApplication::translate("TaigaGui", "Playing next episode…");
}

inline QString openAnimePageInBrowserActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Open anime page in browser");
}

/// Main menu: first configured library folder, or app data if none (see MainWindow::openDataFolder).
inline QString openPrimaryLibraryOrDataFolderActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Open library folder");
}

inline QString openPrimaryLibraryOrDataFolderToolTip() {
  return QCoreApplication::translate(
      "TaigaGui",
      "Opens your first configured library folder in Explorer, or the app data folder if none is "
      "set.");
}

inline QString noLibraryFolderConfiguredBody() {
  return QCoreApplication::translate(
      "TaigaGui", "No library folder is configured.\nAdd one in Settings → Library.");
}

inline QString openPrimaryFolderCreateFailedMessage() {
  return QCoreApplication::translate("TaigaGui", "Could not create or access the folder.");
}

inline QString openPrimaryFolderLaunchFailedMessage() {
  return QCoreApplication::translate("TaigaGui", "Could not open the folder.");
}

inline QString openPrimaryFolderOpenedStatus(const QString& path) {
  return QCoreApplication::translate("TaigaGui", "Opened folder: %1").arg(path);
}

inline QString listExportSucceededMessage(const QString& path) {
  return QCoreApplication::translate("TaigaGui", "Exported list to %1").arg(path);
}

inline QString listExportWriteFailedMessage() {
  return QCoreApplication::translate("TaigaGui", "Could not write the export file.");
}

inline QString editListEntryActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Edit list entry");
}

inline QString synchronizeActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Synchronize");
}

inline QString synchronizeWithServiceToolTip(const QString& serviceName) {
  return QCoreApplication::translate("TaigaGui", "Synchronize with %1").arg(serviceName);
}

inline QString synchronizeDownloadListStatusTip(const QString& serviceName) {
  return QCoreApplication::translate("TaigaGui", "Download your list from %1 (F5 or Ctrl+S).")
      .arg(serviceName);
}

inline QString synchronizingWithServiceStatus(const QString& serviceName) {
  return QCoreApplication::translate("TaigaGui", "Synchronizing with %1…").arg(serviceName);
}

inline QString synchronizedDoneStatus() {
  return QCoreApplication::translate("TaigaGui", "Synchronized.");
}

inline QString synchronizationFailedStatus(const QString& message) {
  return QCoreApplication::translate("TaigaGui", "Synchronization failed: %1").arg(message);
}

inline QString synchronizationDisabledStatusHint() {
  return QCoreApplication::translate(
      "TaigaGui", "Synchronization is disabled (enable it in Settings → Anime List).");
}

inline QString settingsActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Settings");
}

inline QString settingsActionToolTipWithShortcut(const QString& nativePreferencesKey) {
  return QCoreApplication::translate("TaigaGui", "Preferences (%1)").arg(nativePreferencesKey);
}

inline QString copyTitleActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Copy title");
}

inline QString copiedTitleToClipboardStatus() {
  return QCoreApplication::translate("TaigaGui", "Copied title to clipboard.");
}

inline QString copyEnglishTitleActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Copy English title");
}

inline QString copiedEnglishTitleToClipboardStatus() {
  return QCoreApplication::translate("TaigaGui", "Copied English title to clipboard.");
}

inline QString copyNativeTitleActionLabel() {
  return QCoreApplication::translate("TaigaGui", "Copy native title");
}

inline QString copiedNativeTitleToClipboardStatus() {
  return QCoreApplication::translate("TaigaGui", "Copied native title to clipboard.");
}

}  // namespace gui
