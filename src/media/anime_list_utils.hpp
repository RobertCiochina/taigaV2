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

#pragma once

namespace anime {
struct Details;
}

namespace anime::list {

struct Entry;

float getProgressRatio(const Details* item, const Entry* entry);

/// Last aired episode number for list progress visuals.
int lastAiredEpisodeForProgress(const Details& item, const Entry* entry);

/// Ratios in 0–1 for the list progress bar: aired vs. total, watched vs. total.
void getProgressBarRatios(const Details* item, const Entry* entry, float& ratio_aired,
                          float& ratio_watched);

}  // namespace anime::list
