/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * Streaming URL/title handling — URL regex, title cleanup, and browser title normalization for web
 * player detection.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace track::streaming {

struct ProviderUiEntry {
  std::string_view slug;
  /// English label for the settings grid (user-visible).
  const char* label;
};

/// Fixed provider list (order matches stored provider keys).
const std::vector<ProviderUiEntry>& providerUiEntries();

/// Returns slug when `url` matches a known streaming provider pattern (HTTP[S] host/path).
std::optional<std::string_view> matchProviderSlugByUrl(std::string_view url);

/// Strips common non-video browser tab titles / noise.
void normalizeBrowserTitle(std::string_view url, std::string& title_utf8);

/// Applies title regex extraction + per-site cleanup when `slug` matches `matchProviderSlugByUrl`.
bool refineTitleForProvider(std::string_view slug, std::string& title_utf8);

}  // namespace track::streaming
