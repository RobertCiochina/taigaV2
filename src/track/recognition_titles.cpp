/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
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

#include "recognition_titles.hpp"

#include <QRegularExpression>
#include <QSet>
#include <QString>

#include "media/anime.hpp"
#include "track/recognition_normalize.hpp"

namespace track::recognition {
namespace {

QString stripEpisodeNumberMarkers(QString title) {
  // "Boku no Hero Academia No. 170+1: More" → "Boku no Hero Academia: More"
  static const QRegularExpression kNoMarker(QStringLiteral(R"(\bNo\.?\s*\d+(?:\s*\+\s*\d+)?\s*:?)"),
                                            QRegularExpression::CaseInsensitiveOption);
  title.replace(kNoMarker, QStringLiteral(" "));
  // Bare "170+1" left behind in odd layouts.
  static const QRegularExpression kBarePlus(QStringLiteral(R"(\b\d+\s*\+\s*\d+\b)"));
  title.replace(kBarePlus, QStringLiteral(" "));
  return title.simplified();
}

void addUnique(std::vector<std::string>& out, QSet<QString>& seen, const QString& raw) {
  const QString t = raw.trimmed();
  if (t.isEmpty()) return;
  const QString key = QString::fromStdString(normalize(t.toStdString()));
  if (key.isEmpty() || seen.contains(key)) return;
  seen.insert(key);
  out.push_back(t.toStdString());
}

void addUniqueQ(QStringList& out, const QString& raw) {
  const QString t = raw.trimmed();
  if (t.isEmpty()) return;
  if (!out.contains(t, Qt::CaseInsensitive)) out.append(t);
}

int significantTokenCount(const QString& title) {
  static const QRegularExpression kSplit(QStringLiteral(R"([^\w]+)"));
  int n = 0;
  for (const QString& part : title.split(kSplit, Qt::SkipEmptyParts)) {
    if (part.size() >= 2) ++n;
  }
  return n;
}

}  // namespace

std::vector<std::string> syntheticTitleSynonyms(const anime::Details& item) {
  std::vector<std::string> out;
  QSet<QString> seen;

  // Don't re-add the raw official titles as "synthetic" — only derivations.
  seen.insert(QString::fromStdString(normalize(item.titles.romaji)));
  seen.insert(QString::fromStdString(normalize(item.titles.english)));
  seen.insert(QString::fromStdString(normalize(item.titles.japanese)));
  for (const auto& syn : item.titles.synonyms) {
    seen.insert(QString::fromStdString(normalize(syn)));
  }

  const auto derive = [&](const std::string& title) {
    if (title.empty()) return;
    const QString q = QString::fromStdString(title);
    const QString stripped = stripEpisodeNumberMarkers(q);
    if (stripped.compare(q, Qt::CaseInsensitive) == 0) return;
    addUnique(out, seen, stripped);
    QString no_colon = stripped;
    no_colon.replace(QLatin1Char(':'), QLatin1Char(' '));
    addUnique(out, seen, no_colon.simplified());
  };

  derive(item.titles.romaji);
  derive(item.titles.english);
  for (const auto& syn : item.titles.synonyms) derive(syn);

  return out;
}

std::string stripSeasonNoiseFromNormalized(std::string normalized) {
  QString q = QString::fromStdString(normalized);
  // Normalized titles have no spaces/punctuation — operate on glued tokens.
  q.replace(QStringLiteral("finalseason"), QString());
  static const QRegularExpression kSeasonDigit(QStringLiteral(R"(season\d+)"));
  q.replace(kSeasonDigit, QString());
  static const QRegularExpression kOrdSeason(QStringLiteral(R"(\d+(?:st|nd|rd|th)season)"));
  q.replace(kOrdSeason, QString());
  return q.toStdString();
}

bool isFranchiseOnlySearchTitle(const QString& title) {
  const QString t = title.trimmed();
  if (t.isEmpty()) return true;
  // Single token / very short queries like "BLEACH" or "Naruto" are too broad for autodl.
  if (significantTokenCount(t) <= 1) return true;
  if (t.size() <= 6) return true;
  return false;
}

QStringList searchTitleVariantsFromOfficialTitles(const QString& english, const QString& romaji) {
  QStringList out;
  for (const QString& src : {english, romaji}) {
    if (src.trimmed().isEmpty()) continue;
    addUniqueQ(out, src);
    const QString stripped = stripEpisodeNumberMarkers(src);
    addUniqueQ(out, stripped);
    QString no_colon = stripped;
    no_colon.replace(QLatin1Char(':'), QLatin1Char(' '));
    addUniqueQ(out, no_colon.simplified());
  }
  return out;
}

}  // namespace track::recognition
