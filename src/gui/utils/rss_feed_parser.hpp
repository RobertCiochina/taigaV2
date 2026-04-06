/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <optional>

#include <QByteArray>
#include <QString>

#include "base/rss.hpp"

namespace gui {

/// Populated by `parseSyndicationFeed` in `rss::Item::namespace_elements` when a magnet URI is found in
/// HTML (e.g. Tokyo Toshokan descriptions).
inline constexpr const char kTorrentFeedMagnetKey[] = "_taiga_magnet";

/// RSS 2.0, RSS 1.0 / RDF (item scan), then Atom 1.0. Applies Taiga-specific normalization for feeds
/// like Tokyo Tosho (page link vs .torrent, magnet in HTML description).
std::optional<rss::Feed> parseSyndicationFeed(const QByteArray& xml_utf8, QString* error_message = nullptr,
                                              int max_items = 400);

}  // namespace gui
