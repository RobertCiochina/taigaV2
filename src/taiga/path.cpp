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

#include "path.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <format>

#include "taiga/config.h"

namespace taiga {

namespace {

bool copyRecursively(const QString& src, const QString& dst) {
  const QFileInfo srcInfo(src);
  if (!srcInfo.exists()) return false;

  if (srcInfo.isFile()) {
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFileInfo::exists(dst)) return true;  // do not clobber
    return QFile::copy(src, dst);
  }

  // Directory
  QDir srcDir(src);
  if (!srcDir.exists()) return false;
  QDir().mkpath(dst);

  const QFileInfoList entries =
      srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::DirsFirst);
  bool ok = true;
  for (const QFileInfo& e : entries) {
    const QString nextSrc = e.filePath();
    const QString nextDst = QDir(dst).filePath(e.fileName());
    if (!copyRecursively(nextSrc, nextDst)) ok = false;
  }
  return ok;
}

}  // namespace

// Returns current path in portable mode, AppData location otherwise.
// Note: when switching from portable->AppData (common during dev rebuilds),
// migrate existing portable data once so updates/rebuilds retain history/settings.
std::string get_data_path() {
#ifdef TAIGA_PORTABLE
  return std::format("{}/data", QCoreApplication::applicationDirPath().toStdString());
#else
  const QString appDataBase = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString appDataData = QDir(appDataBase).filePath(QStringLiteral("data"));

  // Migrate from legacy portable location if AppData is empty/new.
  const QString portableData =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
  if (!QDir(appDataData).exists() && QDir(portableData).exists()) {
    copyRecursively(portableData, appDataData);
  }

  QDir().mkpath(appDataData);
  return appDataData.toStdString();
#endif
}

}  // namespace taiga
