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

#include "anilist_utils.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringView>
#include <QUrl>
#include <QUrlQuery>

#include "base/chrono.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "media/anime_season.hpp"

namespace sync::anilist {

QJsonObject fromFuzzyDate(const FuzzyDate& date) {
  return {
      {"year", date.year()},
      {"month", date.month()},
      {"day", date.day()},
  };
}

QString fromListStatus(const anime::list::Status value) {
  // clang-format off
  switch (value) {
    case anime::list::Status::Watching: return "CURRENT";
    case anime::list::Status::Completed: return "COMPLETED";
    case anime::list::Status::OnHold: return "PAUSED";
    case anime::list::Status::Dropped: return "DROPPED";
    case anime::list::Status::PlanToWatch: return "PLANNING";
  }
  // clang-format on
  return "";
}

float fromScore(float value) {
  return value * 10.0f;
}

QString fromSeasonName(const anime::SeasonName name) {
  // clang-format off
  switch (name) {
    case anime::SeasonName::Unknown: return "";
    case anime::SeasonName::Winter: return "WINTER";
    case anime::SeasonName::Spring: return "SPRING";
    case anime::SeasonName::Summer: return "SUMMER";
    case anime::SeasonName::Fall: return "FALL";
  }
  // clang-format on
  return "";
}

////////////////////////////////////////////////////////////////////////////////

std::string animePageUrl(const int id) {
  return std::format("https://anilist.co/anime/{}", id);
}

std::string requestTokenUrl() {
  constexpr auto kTaigaClientId = 161;
  QUrl url{"https://anilist.co/api/v2/oauth/authorize"};
  url.setQuery({
      {"client_id", QString::number(kTaigaClientId)},
      {"response_type", "token"},
  });
  return url.toString().toStdString();
}

namespace {

QString extractAccessTokenFromAmpersandPairs(QStringView fragment_or_query) {
  const QString str = fragment_or_query.toString();
  for (const QString& part : str.split(u'&', Qt::SkipEmptyParts)) {
    const int eq = part.indexOf(u'=');
    if (eq <= 0) continue;
    const QString key = part.left(eq).trimmed();
    if (key.compare(u"access_token", Qt::CaseInsensitive) != 0) continue;
    QString val = part.mid(eq + 1).trimmed();
    const int amp = val.indexOf(u'&');
    if (amp >= 0) val = val.left(amp);
    val = QUrl::fromPercentEncoding(val.toUtf8());
    if (!val.isEmpty()) return val;
  }
  return {};
}

}  // namespace

std::optional<std::string> extractAnilistAccessToken(const QString& raw) {
  const QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) return std::nullopt;

  const auto try_jwt = [](const QString& s) -> std::optional<std::string> {
    if (s.contains(u' ') || s.contains(u'\n') || s.contains(u'\t')) return std::nullopt;
    if (s.count(u'.') != 2) return std::nullopt;
    if (s.size() < 32) return std::nullopt;
    return s.toStdString();
  };

  for (QString line : trimmed.split(u'\n', Qt::SkipEmptyParts)) {
    line = line.trimmed();
    if (line.isEmpty()) continue;

    const int hash = line.indexOf(u'#');
    if (hash >= 0) {
      if (const QString t = extractAccessTokenFromAmpersandPairs(QStringView{line}.mid(hash + 1));
          !t.isEmpty()) {
        return t.toStdString();
      }
    }

    if (line.contains(u"access_token=", Qt::CaseInsensitive)) {
      int pos = line.indexOf(u"access_token=", 0, Qt::CaseInsensitive);
      pos += QStringLiteral("access_token=").size();
      QString rest = line.mid(pos);
      const int amp = rest.indexOf(u'&');
      if (amp >= 0) rest = rest.left(amp);
      rest = QUrl::fromPercentEncoding(rest.trimmed().toUtf8());
      if (!rest.isEmpty()) return rest.toStdString();
    }

    {
      const QUrl u = QUrl::fromUserInput(line);
      if (u.isValid() && !u.fragment().isEmpty()) {
        if (const QString t = extractAccessTokenFromAmpersandPairs(u.fragment()); !t.isEmpty()) {
          return t.toStdString();
        }
      }
      const QString q = u.query(QUrl::FullyDecoded);
      if (!q.isEmpty()) {
        if (const QString t = extractAccessTokenFromAmpersandPairs(q); !t.isEmpty()) {
          return t.toStdString();
        }
      }
    }

    if (const auto j = try_jwt(line)) return j;
  }

  if (const auto j = try_jwt(trimmed)) return j;

  return std::nullopt;
}

}  // namespace sync::anilist
