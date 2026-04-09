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

#include "anime_history.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <format>

#include "compat/history.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"

namespace anime {

namespace {

QString historyJsonPath() {
  const QString base = QString::fromStdString(taiga::get_data_path());
  const QString dir = QDir(base).filePath(QStringLiteral("cache"));
  QDir().mkpath(dir);
  return QDir(dir).filePath(QStringLiteral("history_items.json"));
}

QList<HistoryItem> loadJsonHistory() {
  QFile f(historyJsonPath());
  if (!f.exists()) return {};
  if (!f.open(QIODevice::ReadOnly)) return {};
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isArray()) return {};
  QList<HistoryItem> out;
  out.reserve(doc.array().size());
  for (const QJsonValue& v : doc.array()) {
    if (!v.isObject()) continue;
    const QJsonObject o = v.toObject();
    out.append(HistoryItem{
        .anime_id = o.value(QStringLiteral("anime_id")).toInt(),
        .episode = o.value(QStringLiteral("episode")).toInt(),
        .time = o.value(QStringLiteral("time")).toString().toStdString(),
    });
  }
  return out;
}

void saveJsonHistory(const QList<HistoryItem>& items) {
  QJsonArray arr;
  for (const auto& h : items) {
    QJsonObject o;
    o.insert(QStringLiteral("anime_id"), h.anime_id);
    o.insert(QStringLiteral("episode"), h.episode);
    o.insert(QStringLiteral("time"), QString::fromStdString(h.time));
    arr.append(o);
  }
  QSaveFile out(historyJsonPath());
  if (!out.open(QIODevice::WriteOnly)) return;
  out.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
  out.commit();
}

}  // namespace

History& history() {
  static History* h = new History(qApp);
  return *h;
}

void History::init() {
  const auto path = []() {
    const auto service = taiga::settings.service();
    return std::format("{}/v1/user/{}@{}/history.xml", taiga::get_data_path(),
                       taiga::accounts.serviceUsername(service), service);
  }();

  items_ = compat::v1::readHistory(path);

  // Load persistent history recorded by this Qt build (separate file).
  const auto recorded = loadJsonHistory();
  if (!recorded.isEmpty()) {
    // Newest-first by time string (ISO sorts lexicographically).
    items_.append(recorded);
  }
}

const QList<HistoryItem>& History::items() const {
  return items_;
}

void History::recordEpisode(const int anime_id, const int episode) {
  if (anime_id <= 0 || episode <= 0) return;

  // De-dupe: media polling can re-emit the same episode; only record when it changes.
  if (!items_.isEmpty()) {
    const auto& last = items_.front();
    if (last.anime_id == anime_id && last.episode == episode) return;
  }

  const QString now = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
  items_.push_front(
      HistoryItem{.anime_id = anime_id, .episode = episode, .time = now.toStdString()});

  constexpr int kMax = 500;
  while (items_.size() > kMax) items_.pop_back();

  // Persist only non-imported rows (our recorded episodes).
  QList<HistoryItem> recorded;
  recorded.reserve(items_.size());
  for (const auto& h : items_) {
    const auto p = anime::kHistoryItemQueuedImportPrefix;
    const bool is_queue =
        h.time.size() >= p.size() && std::string_view{h.time}.substr(0, p.size()) == p;
    if (!is_queue) recorded.push_back(h);
  }
  saveJsonHistory(recorded);

  emit itemsChanged();
}

}  // namespace anime
