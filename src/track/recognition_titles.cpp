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

/// `A: B` → `B: A` when both sides are real titles. Fansubs swap Steel Ball Run's colon order.
QString swapColonTitleHalves(const QString& title) {
  const int idx = title.indexOf(QStringLiteral(": "));
  if (idx <= 0) return {};
  const QString left = title.left(idx).trimmed();
  const QString right = title.mid(idx + 2).trimmed();
  if (left.isEmpty() || right.isEmpty() || right.contains(QStringLiteral(": "))) return {};
  if (significantTokenCount(left) < 2 || significantTokenCount(right) < 2) return {};
  return right + QStringLiteral(": ") + left;
}

}  // namespace

QString stripCourStageSuffix(const QString& title) {
  const QString t = title.trimmed();
  if (t.isEmpty()) return {};
  // AniList cours: "… Steel Ball Run - 1st STAGE", "… - 2nd & 3rd STAGE".
  static const QRegularExpression re(
      QStringLiteral(
          R"((?:\s+-\s+|\s+)((?:\d+(?:st|nd|rd|th)\s*(?:&|and|-)\s*)*\d+(?:st|nd|rd|th)\s+stage)\s*$)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto m = re.match(t);
  if (!m.hasMatch()) return {};
  const QString base = t.left(m.capturedStart()).trimmed();
  if (base.size() < 4) return {};
  return base;
}

QString courStageKey(const QString& title) {
  const QString t = title.trimmed();
  if (t.isEmpty()) return {};
  static const QRegularExpression re(
      QStringLiteral(
          R"(\b((?:\d+(?:st|nd|rd|th)\s*(?:&|and|-)\s*)*\d+(?:st|nd|rd|th))\s+stage\b)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto m = re.match(t);
  if (!m.hasMatch()) return {};
  static const QRegularExpression ord(QStringLiteral(R"(\d+)"));
  QStringList nums;
  auto it = ord.globalMatch(m.captured(1));
  while (it.hasNext()) nums.push_back(it.next().captured(0));
  return nums.join(QLatin1Char('-'));
}

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

  // "… - 2nd & 3rd STAGE" releases are often filed under the shared series name, and some
  // groups swap the colon halves (`Steel Ball Run: JoJo no Kimyou na Bouken`).
  const auto deriveCourBase = [&](const std::string& title) {
    if (title.empty()) return;
    const QString base = stripCourStageSuffix(QString::fromStdString(title));
    if (base.isEmpty()) return;
    addUnique(out, seen, base);
    if (const QString swapped = swapColonTitleHalves(base); !swapped.isEmpty()) {
      addUnique(out, seen, swapped);
    }
  };
  deriveCourBase(item.titles.romaji);
  deriveCourBase(item.titles.english);
  for (const auto& syn : item.titles.synonyms) deriveCourBase(syn);

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

QString foldTorrentSearchPunctuation(QString title) {
  title.replace(QChar(0x2018), QLatin1Char('\''));
  title.replace(QChar(0x2019), QLatin1Char('\''));
  title.replace(QChar(0x201B), QLatin1Char('\''));
  title.replace(QChar(0x2032), QLatin1Char('\''));
  title.replace(QChar(0x201C), QLatin1Char('"'));
  title.replace(QChar(0x201D), QLatin1Char('"'));
  title.replace(QChar(0x2013), QLatin1Char('-'));
  title.replace(QChar(0x2014), QLatin1Char('-'));
  return title;
}

QStringList torrentSearchPunctuationVariants(const QString& title) {
  QStringList out;
  addUniqueQ(out, title);
  const QString folded = foldTorrentSearchPunctuation(title);
  addUniqueQ(out, folded);
  QString stripped = folded;
  stripped.remove(QLatin1Char('\''));
  addUniqueQ(out, stripped.simplified());
  return out;
}

QStringList searchTitleVariantsFromOfficialTitles(const QString& english, const QString& romaji) {
  QStringList out;
  for (const QString& src : {english, romaji}) {
    if (src.trimmed().isEmpty()) continue;
    for (const QString& v : torrentSearchPunctuationVariants(src)) addUniqueQ(out, v);
    const QString stripped = stripEpisodeNumberMarkers(src);
    for (const QString& v : torrentSearchPunctuationVariants(stripped)) addUniqueQ(out, v);
    QString no_colon = stripped;
    no_colon.replace(QLatin1Char(':'), QLatin1Char(' '));
    for (const QString& v : torrentSearchPunctuationVariants(no_colon.simplified())) {
      addUniqueQ(out, v);
    }
  }
  return out;
}

}  // namespace track::recognition
