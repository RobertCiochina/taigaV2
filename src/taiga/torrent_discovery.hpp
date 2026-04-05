/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <QString>
#include <QUrl>

namespace taiga {

/// Taiga v1 default (`rss/torrent/search/address`): Nyaa RSS with `%title%` placeholder.
QString defaultTorrentDiscoverySearchUrl();
/// Taiga v1 default (`rss/torrent/source/address`): catalog RSS (no `%title%` substitution).
QString defaultTorrentDiscoveryFeedSourceUrl();

/// Same substitution as v1 `CheckFeed` / torrent search — use for **HTTP GET** (RSS, not HTML).
QUrl torrentDiscoveryFeedFetchUrl(const QString& template_with_placeholders, const QString& title);

/// Taiga v1 `rss/torrent/source/address` — full GET URL (no `%title%` substitution). Empty uses Tokyo
/// Tosho default.
QUrl torrentDiscoveryCatalogFeedUrl(const QString& source_url_or_empty);

/// Substitutes `%title%` (case-insensitive) with the URL-encoded title; may rewrite Nyaa RSS URLs
/// to the HTML search page so the browser shows results instead of raw XML.
QUrl torrentDiscoveryResolvedUrl(const QString& template_with_placeholders, const QString& title);

/// Opens the resolved URL in the default browser. Reports errors via `userFeedback`.
bool openTorrentDiscoverySearch(const QString& title);

}  // namespace taiga
