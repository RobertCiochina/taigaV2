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

#include "library_model.hpp"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <anitomy.hpp>
#include <anitomy/detail/keyword.hpp>  // don't try this at home
#include <ranges>

#include "base/string.hpp"
#include "gui/utils/ui_title.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/scanner.hpp"

namespace gui {

static QString preferredAnimeTitle(const anime::Details& item) {
  return gui::uiTitle(item);
}

LibraryModel::LibraryModel(QObject* parent) : QFileSystemModel(parent) {
  setNameFilters([]() {
    QStringList filters;
    for (const auto& [key, keyword] : anitomy::detail::keywords) {
      if (keyword.kind != anitomy::detail::KeywordKind::FileExtension) continue;
      filters.emplace_back(u"*.%1"_s.arg(QString::fromStdString(key.data())));
    }
    return filters;
  }());
  setNameFilterDisables(true);

  connect(this, &QFileSystemModel::directoryLoaded, this, &LibraryModel::parseDirectory);
  connect(this, &QFileSystemModel::directoryLoaded, this, [this](const QString& path) {
    invalidateDirChildrenCacheFor(path);
    // Also invalidate parent so its expander state reflects newly-seen children.
    invalidateDirChildrenCacheFor(QFileInfo(path).absolutePath());
  });

  // Restore manual overrides from the previous session.
  loadOverrides();
}

void LibraryModel::persistOverrides() const {
  QJsonArray arr;
  for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it) {
    if (it.value().id <= 0) continue;
    QJsonObject obj;
    obj[QStringLiteral("path")] = it.key();
    obj[QStringLiteral("id")] = it.value().id;
    obj[QStringLiteral("episode")] = it.value().episode;
    arr.append(obj);
  }
  taiga::settings.setLibraryManualOverridesJson(
      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void LibraryModel::loadOverrides() {
  const QString json = taiga::settings.libraryManualOverridesJson();
  if (json.isEmpty()) return;
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
  if (!doc.isArray()) return;
  for (const QJsonValue& val : doc.array()) {
    if (!val.isObject()) continue;
    const QJsonObject obj = val.toObject();
    const QString path = obj[QStringLiteral("path")].toString();
    const int id = obj[QStringLiteral("id")].toInt();
    const QString episode = obj[QStringLiteral("episode")].toString();
    if (path.isEmpty() || id <= 0) continue;
    QString title;
    if (const auto* item = anime::db.item(id)) title = preferredAnimeTitle(*item);
    m_overrides[path] = ParsedData{.title = title, .episode = episode, .id = id};
    bool ok = false;
    int ep_no = episode.toInt(&ok);
    if (!ok || ep_no < 1) ep_no = 1;
    track::addManualLibraryEpisode(id, ep_no);
  }
}

int LibraryModel::columnCount(const QModelIndex&) const {
  return NUM_COLUMNS;
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};

  switch (role) {
    case Qt::DisplayRole: {
      switch (index.column()) {
        case COLUMN_ANIME:
          if (isEnabled(index)) return getTitle(filePath(index));
          return {};
        case COLUMN_EPISODE:
          if (isEnabled(index)) return getEpisode(filePath(index));
          return {};
      }
      break;
    }

    case Qt::ForegroundRole: {
      switch (index.column()) {
        case COLUMN_NAME: {
          const auto info = fileInfo(index);
          if (info.isFile() && info.isExecutable()) {
            return QColorConstants::Red;  // potentially dangerous file
          }
          break;
        }
        case COLUMN_ANIME: {
          const auto disabledTextColor =
              qApp->palette().color(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Text);
          if (!getId(filePath(index))) return disabledTextColor;  // unidentified
          break;
        }
      }
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (index.column()) {
        case COLUMN_SIZE:
        case COLUMN_MODIFIED:
        case COLUMN_EPISODE:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
      }
      break;
    }
  }

  return QFileSystemModel::data(index, role);
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  switch (role) {
    case Qt::DisplayRole: {
      // clang-format off
      switch (section) {
        case COLUMN_NAME: return tr("Name");
        case COLUMN_SIZE: return tr("Size");
        case COLUMN_TYPE: return tr("Type");
        case COLUMN_ANIME: return tr("Anime");
        case COLUMN_EPISODE: return tr("Episode");
        case COLUMN_MODIFIED: return tr("Last modified");
      }
      // clang-format on
      break;
    }

    case Qt::TextAlignmentRole: {
      switch (section) {
        case COLUMN_NAME:
        case COLUMN_TYPE:
        case COLUMN_ANIME:
          return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        case COLUMN_SIZE:
        case COLUMN_MODIFIED:
        case COLUMN_EPISODE:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
      }
      break;
    }

    case Qt::InitialSortOrderRole: {
      switch (section) {
        case COLUMN_SIZE:
        case COLUMN_MODIFIED:
          return Qt::DescendingOrder;
        default:
          return Qt::AscendingOrder;
      }
      break;
    }
  }

  return QFileSystemModel::headerData(section, orientation, role);
}

bool LibraryModel::directoryHasAnyEntries(const QString& dir_path) const {
  if (dir_path.isEmpty()) return false;
  const QDir d(dir_path);
  if (!d.exists()) return false;

  // Directories should always count as children, even if the current nameFilters are file-based.
  // Many libraries are laid out as Root/Title/Season/Episode.mkv, so Title folders would look
  // "empty" if we applied "*.mkv" filters to directories.
  {
    QDirIterator it(dir_path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    if (it.hasNext()) return true;
  }

  // Files: respect the model's name filters so the expander matches visible files.
  {
    QDirIterator it(dir_path, nameFilters(), QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::NoIteratorFlags);
    if (it.hasNext()) return true;
  }

  return false;
}

void LibraryModel::invalidateDirChildrenCacheFor(const QString& dir_path) {
  if (dir_path.isEmpty()) return;
  m_dir_has_children_cache.remove(QDir::cleanPath(QDir::fromNativeSeparators(dir_path)));
}

bool LibraryModel::hasChildren(const QModelIndex& parent) const {
  if (!parent.isValid()) return QFileSystemModel::hasChildren(parent);
  const QFileInfo info = fileInfo(parent);
  if (!info.isDir()) return false;

  // If Qt already loaded the directory, rowCount is authoritative.
  const int known = rowCount(parent);
  if (known > 0) return true;

  const QString path = QDir::cleanPath(QDir::fromNativeSeparators(info.absoluteFilePath()));
  if (auto it = m_dir_has_children_cache.constFind(path); it != m_dir_has_children_cache.constEnd()) {
    return it.value();
  }

  const bool any = directoryHasAnyEntries(path);
  m_dir_has_children_cache.insert(path, any);
  return any;
}

bool LibraryModel::isEnabled(const QModelIndex& index) const {
  return index.flags() & Qt::ItemIsEnabled;
}

QString LibraryModel::getTitle(const QString& path) const {
  // Override takes priority over auto-recognition.
  if (m_overrides.contains(path)) {
    const int oid = m_overrides[path].id;
    if (oid > 0) {
      if (const auto* item = anime::db.item(oid)) return preferredAnimeTitle(*item);
    }
    return m_overrides[path].title;
  }
  if (const int id = m_parsed.value(path).id) {
    if (const auto* item = anime::db.item(id)) return preferredAnimeTitle(*item);
  }
  return m_parsed.value(path).title;
}

QString LibraryModel::getEpisode(const QString& path) const {
  if (m_overrides.contains(path)) return m_overrides[path].episode;
  return m_parsed[path].episode;
}

int LibraryModel::getId(const QString& path) const {
  if (m_overrides.contains(path)) return m_overrides[path].id;
  return m_parsed[path].id;
}

void LibraryModel::setOverride(const QString& path, const int id, const QString& episode) {
  if (id <= 0) {
    // Remove old manual episode tracking if there was an override.
    if (m_overrides.contains(path)) {
      const int old_id = m_overrides[path].id;
      if (old_id > 0) track::removeManualLibraryEpisode(old_id);
    }
    m_overrides.remove(path);
  } else {
    QString title;
    if (const auto* item = anime::db.item(id)) title = preferredAnimeTitle(*item);
    m_overrides[path] = ParsedData{.title = title, .episode = episode, .id = id};
    // Register with the scanner so libraryHasLocalEpisode() reflects this override.
    bool ok = false;
    int ep_no = episode.toInt(&ok);
    if (!ok || ep_no < 1) ep_no = 1;  // OVA/Special → episode 1
    track::addManualLibraryEpisode(id, ep_no);
  }
  // Emit dataChanged for all columns of this file so the view refreshes.
  const QModelIndex name_idx = index(path);
  if (name_idx.isValid()) {
    const QModelIndex last = name_idx.siblingAtColumn(NUM_COLUMNS - 1);
    emit dataChanged(name_idx, last);
  }
  // Persist so the assignment survives app restarts.
  persistOverrides();
  // Signal the rest of the app that library availability changed.
  emit libraryOverrideChanged();
}

void LibraryModel::parseDirectory(const QString& path) {
  const auto parent = index(path);

  if (!parent.isValid()) return;

  for (int i = 0; i < rowCount(parent); ++i) {
    const auto child = index(i, 0, parent);
    if (!child.isValid()) continue;
    if (!isEnabled(child)) continue;
    const auto info = fileInfo(child);
    if (!info.isFile()) continue;
    parseFileInfo(info);
  }
}

void LibraryModel::parseFileInfo(const QFileInfo& info) {
  const auto path = info.filePath();

  if (m_parsed.contains(path)) return;

  auto episode = track::recognition::parseFileInfo(
      info, {}, taiga::settings.libraryScanLookupParentDirectories());
  const auto anime_id = track::recognition::identify(episode);

  m_parsed[path] = ParsedData{
      .title = QString::fromStdString(episode.element(anitomy::ElementKind::Title)),
      .episode = QString::fromStdString(episode.element(anitomy::ElementKind::Episode)),
      .id = anime_id,
  };
}

}  // namespace gui
