/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * Row actions for anime list / search views.
 */

#pragma once

namespace taiga {

enum class ListRowAction : int {
  Nothing = 0,
  EditListEntry = 1,
  OpenFolder = 2,
  PlayNext = 3,
  ShowDetails = 4,
  OpenAnimePage = 5,
};

}  // namespace taiga
