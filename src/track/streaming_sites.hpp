/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * Taiga v1 `track::recognition` streaming URL/title handling (`media_stream.cpp`) — URL regex,
 * title cleanup, and browser title normalization for web player detection.
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

/// Fixed provider list (order matches v1 `recognition/streaming/providers/*` keys).
const std::vector<ProviderUiEntry>& providerUiEntries();

/// Returns slug when `url` matches a known streaming provider pattern (HTTP[S] host/path).
std::optional<std::string_view> matchProviderSlugByUrl(std::string_view url);

/// Strips common non-video browser tab titles / noise (v1 `NormalizeWebBrowserTitle`).
void normalizeBrowserTitle(std::string_view url, std::string& title_utf8);

/// Applies v1-style title regex extraction + per-site cleanup when `slug` matches `matchProviderSlugByUrl`.
bool refineTitleForProvider(std::string_view slug, std::string& title_utf8);

}  // namespace track::streaming
