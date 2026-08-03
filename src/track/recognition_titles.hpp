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

#pragma once

#include <QStringList>
#include <string>
#include <vector>

namespace anime {
struct Details;
}

namespace track::recognition {

/// Bounded synthetic synonyms derived from AniList titles (e.g. strip `No. 170+1:`).
std::vector<std::string> syntheticTitleSynonyms(const anime::Details& item);

/// Remove season-noise tokens from an already-normalized title key so
/// `…finalseasonmore` can match `…more` (Erai "Final Season - More" vs AniList "…: More").
std::string stripSeasonNoiseFromNormalized(std::string normalized);

/// True when a stripped subtitle-only / franchise-only search variant is too weak to use.
bool isFranchiseOnlySearchTitle(const QString& title);

/// Generate extra torrent search variants from AniList-style `No. N+1` titles.
QStringList searchTitleVariantsFromOfficialTitles(const QString& english, const QString& romaji);

}  // namespace track::recognition
