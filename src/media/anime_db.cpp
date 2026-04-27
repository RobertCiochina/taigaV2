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

#include "anime_db.hpp"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlRecord>
#include <QSqlResult>
#include <format>

#include "base/file.hpp"
#include "base/string.hpp"
#include "compat/anime.hpp"
#include "compat/list.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"
#include "taiga/version.hpp"

namespace anime {

Database::Database() : QObject{} {}

namespace {

bool tableHasColumn(QSqlDatabase& db, const QString& table, const QString& column) {
  if (!db.isOpen() && !db.open()) return false;
  QSqlQuery q{db};
  q.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(table));
  if (!q.exec()) return false;
  while (q.next()) {
    if (q.value(QStringLiteral("name")).toString() == column) return true;
  }
  return false;
}

void ensureAnimeTableHasRelationsJson(QSqlDatabase& db) {
  if (!db.isOpen() && !db.open()) return;
  if (tableHasColumn(db, QStringLiteral("anime"), QStringLiteral("relations_json"))) return;
  QSqlQuery q{db};
  q.exec(QStringLiteral("ALTER TABLE anime ADD COLUMN relations_json TEXT"));
}

}  // namespace

void Database::init() {
  db_ = QSqlDatabase::addDatabase("QSQLITE");
  db_.setDatabaseName(fileName());

  if (!QFile::exists(fileName())) {
    createTables();
    migrateItemsFromV1();
    migrateListEntriesFromV1();
    return;
  }

  // Schema upgrades for older existing databases.
  // (Do this before reading items so in-memory cache matches disk schema.)
  if (db_.open()) {
    ensureAnimeTableHasRelationsJson(db_);
    db_.close();
  }

  readItems();
  readEntries();
}

const Anime* Database::item(const int id) const {
  const auto it = items_.find(id);
  return it != items_.end() ? &(*it) : nullptr;
}

const ListEntry* Database::entry(const int id) const {
  const auto it = entries_.find(id);
  return it != entries_.end() ? &(*it) : nullptr;
}

const QMap<int, Anime>& Database::items() const {
  return items_;
}

const QMap<int, ListEntry>& Database::entries() const {
  return entries_;
}

void Database::updateItem(const Anime& item) {
  // Some API endpoints (e.g. list sync / seasonal browse) do not include relations.
  // Preserve any already-cached relation edges so features like Announced releases
  // remain stable across restarts/syncs.
  Anime merged = item;
  if (merged.id > 0 && merged.relations.empty()) {
    if (const auto it = items_.find(merged.id); it != items_.end() && !it->relations.empty()) {
      merged.relations = it->relations;
    }
  }

  // In batch mode the DB is already open and a transaction is active.
  const bool opened_here = !m_batch_mode_ && !db_.isOpen() && db_.open();
  if (!db_.isOpen()) return;

  QSqlQuery q{db_};
  if (q.prepare(sql("insertAnime"))) {
    bindItemToQuery(merged, q);
    q.exec();
  }

  if (opened_here) db_.close();

  items_[merged.id] = merged;

  if (!m_batch_mode_) emit itemUpdated(merged.id);
}

void Database::updateEntry(const ListEntry& entry) {
  const bool opened_here = !m_batch_mode_ && !db_.isOpen() && db_.open();
  if (!db_.isOpen()) return;

  QSqlQuery q{db_};
  if (q.prepare(sql("insertAnimeList"))) {
    bindEntryToQuery(entry, q);
    q.exec();
  }

  if (opened_here) db_.close();

  entries_[entry.anime_id] = entry;

  if (!m_batch_mode_) emit entryUpdated(entry.anime_id);
}

void Database::beginBatch() {
  m_batch_mode_ = true;
  if (db_.open()) {
    db_.transaction();
  }
}

void Database::endBatch() {
  if (db_.isOpen()) {
    db_.commit();
    db_.close();
  }
  m_batch_mode_ = false;
  emit batchFinished();
}

void Database::deleteEntry(const int anime_id) {
  const bool opened_here = !m_batch_mode_ && !db_.isOpen() && db_.open();
  if (!db_.isOpen()) return;

  QSqlQuery q{db_};
  if (q.prepare(sql("deleteAnimeList"))) {
    q.bindValue(":media_id", anime_id);
    q.exec();
  }

  if (opened_here) db_.close();

  entries_.remove(anime_id);

  if (!m_batch_mode_) emit entryUpdated(anime_id);
}

QString Database::fileName() const {
  return u"%1/media.sqlite"_s.arg(QString::fromStdString(taiga::get_data_path()));
}

QString Database::sql(const QString& name) const {
  return base::readFile(u":/sql/%1.sql"_s.arg(name));
}

void Database::createTables() {
  if (!db_.open()) return;

  const auto tables = db_.tables();

  db_.transaction();

  if (!tables.contains("meta")) {
    QSqlQuery q{db_};
    q.exec(sql("createMeta"));
    q.prepare("INSERT INTO meta(name, value) VALUES(:name, :value)");
    q.bindValue(":name", "version");
    q.bindValue(":value", QString::fromStdString(taiga::version().to_string()));
    q.exec();
  }

  if (!tables.contains("anime")) {
    QSqlQuery q{db_};
    q.exec(sql("createAnime"));
  }

  if (!tables.contains("anime_list")) {
    QSqlQuery q{db_};
    q.exec(sql("createAnimeList"));
  }

  db_.commit();
  db_.close();
}

QString Database::currentVersion() {
  if (!db_.open()) return {};

  QSqlQuery q{db_};

  if (!q.prepare("SELECT value FROM meta WHERE name = :name")) return {};

  q.bindValue(":name", "version");
  q.exec();
  const QString version = q.value(0).toString();

  db_.close();

  return version;
}

void Database::readItems() {
  if (!db_.open()) return;

  QSqlQuery q{db_};
  if (!q.exec("SELECT * FROM anime")) return;

  while (q.next()) {
    const int id = q.value("id").toInt();
    items_[id] = itemFromQuery(q);
  }

  db_.close();
}

void Database::readEntries() {
  if (!db_.open()) return;

  QSqlQuery q{db_};
  if (!q.exec("SELECT * FROM anime_list")) return;

  while (q.next()) {
    const int id = q.value("media_id").toInt();
    entries_[id] = entryFromQuery(q);
  }

  db_.close();
}

void Database::bindItemToQuery(const Anime& item, QSqlQuery& q) const {
  q.bindValue(":id", item.id);
  q.bindValue(":title", QString::fromStdString(item.titles.romaji));
  q.bindValue(":english", QString::fromStdString(item.titles.english));
  q.bindValue(":japanese", QString::fromStdString(item.titles.japanese));
  q.bindValue(":synonym", joinStrings(item.titles.synonyms, ""));
  q.bindValue(":type", static_cast<int>(item.type));
  q.bindValue(":status", static_cast<int>(item.status));
  q.bindValue(":episode_count", item.episode_count);
  q.bindValue(":episode_length", item.episode_length);
  q.bindValue(":date_start", QString::fromStdString(item.date_started.to_string()));
  q.bindValue(":date_end", QString::fromStdString(item.date_finished.to_string()));
  q.bindValue(":image", QString::fromStdString(item.image_url));
  q.bindValue(":trailer_id", QString::fromStdString(item.trailer_id));
  q.bindValue(":age_rating", static_cast<int>(item.age_rating));
  q.bindValue(":genres", joinStrings(item.genres, ""));
  q.bindValue(":tags", joinStrings(item.tags, ""));
  q.bindValue(":producers", joinStrings(item.producers, ""));
  q.bindValue(":studios", joinStrings(item.studios, ""));
  q.bindValue(":score", QString::number(item.score));
  q.bindValue(":popularity", item.popularity_rank);
  q.bindValue(":synopsis", QString::fromStdString(item.synopsis));
  q.bindValue(":last_aired_episode", item.last_aired_episode);
  q.bindValue(":next_episode_time", QString::number(item.next_episode_time));
  // Persist relations as compact JSON array: [{"id":123,"t":2}, ...]
  if (item.relations.empty()) {
    q.bindValue(":relations_json", QString{});
  } else {
    QJsonArray arr;
    for (const auto& e : item.relations) {
      if (e.related_id <= 0) continue;
      QJsonObject o;
      o.insert(QStringLiteral("id"), e.related_id);
      o.insert(QStringLiteral("t"), static_cast<int>(e.type));
      arr.push_back(o);
    }
    q.bindValue(":relations_json",
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
  }
  q.bindValue(":modified", QString::number(item.last_modified));
}

void Database::bindEntryToQuery(const ListEntry& entry, QSqlQuery& q) const {
  q.bindValue(":id", entry.id);
  q.bindValue(":media_id", entry.anime_id);
  q.bindValue(":progress", entry.watched_episodes);
  q.bindValue(":date_start", QString::fromStdString(entry.date_started.to_string()));
  q.bindValue(":date_end", QString::fromStdString(entry.date_completed.to_string()));
  q.bindValue(":score", entry.score);
  q.bindValue(":status", static_cast<int>(entry.status));
  q.bindValue(":private", entry.is_private);
  q.bindValue(":rewatched_times", entry.rewatched_times);
  q.bindValue(":rewatching", entry.rewatching);
  q.bindValue(":rewatching_ep", entry.rewatching_ep);
  q.bindValue(":notes", QString::fromStdString(entry.notes));
  q.bindValue(":last_updated", QString::number(entry.last_updated));
}

Anime Database::itemFromQuery(const QSqlQuery& q) const {
  static const auto splitToVector = [](const QVariant& variant) {
    return toVector(variant.toString().split(", ", Qt::SkipEmptyParts));
  };
  Anime a{
      .id = q.value("id").toInt(),
      .last_modified = q.value("modified").toInt(),
      .episode_count = q.value("episode_count").toInt(),
      .episode_length = q.value("episode_length").toInt(),
      .age_rating = q.value("age_rating").value<anime::AgeRating>(),
      .status = q.value("status").value<anime::Status>(),
      .type = q.value("type").value<anime::Type>(),
      .date_started = FuzzyDate(q.value("date_start").toString().toStdString()),
      .date_finished = FuzzyDate(q.value("date_end").toString().toStdString()),
      .score = q.value("score").toFloat(),
      .popularity_rank = q.value("popularity").toInt(),
      .image_url = q.value("image").toString().toStdString(),
      .synopsis = q.value("synopsis").toString().toStdString(),
      .trailer_id = q.value("trailer_id").toString().toStdString(),
      .titles{
          .romaji = q.value("title").toString().toStdString(),
          .english = q.value("english").toString().toStdString(),
          .japanese = q.value("japanese").toString().toStdString(),
          .synonyms = splitToVector(q.value("synonym")),
      },
      .genres = splitToVector(q.value("genres")),
      .producers = splitToVector(q.value("producers")),
      .studios = splitToVector(q.value("studios")),
      .tags = splitToVector(q.value("tags")),
      .last_aired_episode = q.value("last_aired_episode").toInt(),
      .next_episode_time = q.value("next_episode_time").toInt(),
  };
  const QString rel = q.value("relations_json").toString().trimmed();
  if (!rel.isEmpty()) {
    const QJsonDocument doc = QJsonDocument::fromJson(rel.toUtf8());
    if (doc.isArray()) {
      for (const QJsonValue& v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const int id = o.value(QStringLiteral("id")).toInt();
        const int t = o.value(QStringLiteral("t")).toInt(0);
        if (id > 0) {
          a.relations.push_back({.related_id = id, .type = static_cast<anime::RelationType>(t)});
        }
      }
    }
  }
  return a;
}

ListEntry Database::entryFromQuery(const QSqlQuery& q) const {
  return {
      .id = q.value("id").toLongLong(),
      .anime_id = q.value("media_id").toInt(),
      .watched_episodes = q.value("progress").toInt(),
      .score = q.value("score").toInt(),
      .status = q.value("status").value<anime::list::Status>(),
      .is_private = q.value("private").toBool(),
      .rewatched_times = q.value("rewatched_times").toInt(),
      .rewatching = q.value("rewatching").toBool(),
      .rewatching_ep = q.value("rewatching_ep").toInt(),
      .date_started = FuzzyDate(q.value("date_start").toString().toStdString()),
      .date_completed = FuzzyDate(q.value("date_end").toString().toStdString()),
      .last_updated = q.value("last_updated").toInt(),
      .notes = q.value("notes").toString().toStdString(),
  };
}

void Database::migrateItemsFromV1() {
  if (!db_.open()) return;

  QSqlQuery q{db_};
  if (!q.prepare(sql("insertAnime"))) return;

  const auto path = std::format("{}/v1/db/anime.xml", taiga::get_data_path());

  db_.transaction();

  for (const auto& item : compat::v1::readAnimeDatabase(path)) {
    items_[item.id] = item;
    bindItemToQuery(item, q);
    q.exec();
  }

  db_.commit();
  db_.close();
}

void Database::migrateListEntriesFromV1() {
  if (!db_.open()) return;

  QSqlQuery q{db_};
  if (!q.prepare(sql("insertAnimeList"))) return;

  const auto path = []() {
    const auto service = taiga::settings.service();
    return std::format("{}/v1/user/{}@{}/anime.xml", taiga::get_data_path(),
                       taiga::accounts.serviceUsername(service), service);
  }();

  db_.transaction();

  for (const auto& entry : compat::v1::readListEntries(path)) {
    if (!items_.contains(entry.anime_id)) continue;
    entries_[entry.anime_id] = entry;
    bindEntryToQuery(entry, q);
    q.exec();
  }

  db_.commit();
  db_.close();
}

}  // namespace anime
