/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
 */

#include "anime_list_import.hpp"

#include <QFile>
#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>
#include <ctime>
#include <optional>

#include "media/anime_db.hpp"
#include "media/anime_list.hpp"

namespace anime::list {

namespace {

std::optional<anime::list::Status> parseMalMyStatus(const QString& s) {
  const QString t = s.trimmed();
  if (t.compare(u"Watching", Qt::CaseInsensitive) == 0) return anime::list::Status::Watching;
  if (t.compare(u"Completed", Qt::CaseInsensitive) == 0) return anime::list::Status::Completed;
  if (t.compare(u"On-Hold", Qt::CaseInsensitive) == 0) return anime::list::Status::OnHold;
  if (t.compare(u"Dropped", Qt::CaseInsensitive) == 0) return anime::list::Status::Dropped;
  if (t.compare(u"Plan to Watch", Qt::CaseInsensitive) == 0)
    return anime::list::Status::PlanToWatch;
  return {};
}

bool malDateLooksEmpty(const QString& s) {
  const QString t = s.trimmed();
  return t.isEmpty() || t.startsWith(u"0000-00-00") || t == u"0-0-0";
}

}  // namespace

MalXmlImportResult importFromMyAnimeListXml(const std::string& path) {
  MalXmlImportResult out;

  QFile file(QString::fromStdString(path));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    out.error = QStringLiteral("Could not open the file.");
    return out;
  }

  QXmlStreamReader xml(&file);
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) continue;
    if (xml.name() != u"anime") continue;

    QHash<QString, QString> el;
    while (!xml.atEnd()) {
      xml.readNext();
      if (xml.isEndElement() && xml.name() == u"anime") break;
      if (!xml.isStartElement()) continue;
      const QString name = xml.name().toString();
      el[name] = xml.readElementText();
    }

    bool ok_id = false;
    const int mal_anime_id = el.value(QStringLiteral("series_animedb_id")).trimmed().toInt(&ok_id);
    if (!ok_id || mal_anime_id <= 0) {
      ++out.skipped_invalid_row;
      continue;
    }
    if (!anime::db.item(mal_anime_id)) {
      ++out.skipped_unknown_anime;
      continue;
    }

    const auto st = parseMalMyStatus(el.value(QStringLiteral("my_status")));
    if (!st) {
      ++out.skipped_invalid_row;
      continue;
    }

    ListEntry row;
    if (const ListEntry* ex = anime::db.entry(mal_anime_id)) {
      row = *ex;
    } else {
      bool ok_lid = false;
      const qint64 lid = el.value(QStringLiteral("my_id")).trimmed().toLongLong(&ok_lid);
      if (!ok_lid || lid <= 0) {
        ++out.skipped_invalid_row;
        continue;
      }
      row.id = lid;
      row.anime_id = mal_anime_id;
    }

    row.status = *st;

    bool ok_ep = false;
    const int watched = el.value(QStringLiteral("my_watched_episodes")).trimmed().toInt(&ok_ep);
    if (ok_ep && watched >= 0) row.watched_episodes = watched;

    bool ok_sc = false;
    const int mal_score = el.value(QStringLiteral("my_score")).trimmed().toInt(&ok_sc);
    if (ok_sc) row.score = std::clamp(mal_score, 0, 10) * 10;

    const QString ds = el.value(QStringLiteral("my_start_date")).trimmed();
    if (!malDateLooksEmpty(ds)) row.date_started = FuzzyDate(ds.toStdString());

    const QString df = el.value(QStringLiteral("my_finish_date")).trimmed();
    if (!malDateLooksEmpty(df)) row.date_completed = FuzzyDate(df.toStdString());

    row.notes = el.value(QStringLiteral("my_comments")).trimmed().toStdString();

    bool ok_rw = false;
    const int ntw = el.value(QStringLiteral("my_times_watched")).trimmed().toInt(&ok_rw);
    if (ok_rw && ntw >= 0) row.rewatched_times = ntw;

    row.rewatching = el.value(QStringLiteral("my_rewatching")).trimmed().toInt() != 0;

    bool ok_rwe = false;
    const int rwe = el.value(QStringLiteral("my_rewatching_ep")).trimmed().toInt(&ok_rwe);
    if (ok_rwe && rwe >= 0) row.rewatching_ep = rwe;

    row.last_updated = static_cast<std::time_t>(std::time(nullptr));

    anime::db.updateEntry(row);
    ++out.updated;
  }

  if (xml.hasError()) {
    out.error = xml.errorString();
    return out;
  }

  return out;
}

}  // namespace anime::list
