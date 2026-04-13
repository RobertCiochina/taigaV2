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

class QAbstractItemView;
class QHeaderView;
class QWidget;

namespace gui::tables {

struct Defaults {
  // 0 = do not enforce a minimum (tight rows by default).
  int min_row_height_px = 0;
  int min_section_width_px = 42;
  int min_text_column_width_px = 140;
  int min_first_visible_column_width_px = 220;
  int max_auto_column_width_px = 420;
  bool wrap_cell_text = true;
  bool auto_size_columns_to_contents = true;
  bool alternating_row_colors = true;
};

/// Applies consistent, readable defaults to a table/tree view.
/// Safe to call multiple times.
void applyDefaults(QAbstractItemView* view, const Defaults& d = {});

/// Applies header defaults (minimum section size, elide).
/// Safe to call multiple times.
void applyHeaderDefaults(QHeaderView* header, const Defaults& d = {});

/// Forces an immediate one-time "initial sizing" + relayout pass, using the same logic as
/// `applyDefaults`. Useful to pre-warm expensive layout work (e.g. at startup) so the first time
/// a page is shown doesn't hitch.
void warmupSizingNow(QAbstractItemView* view);

}  // namespace gui::tables

