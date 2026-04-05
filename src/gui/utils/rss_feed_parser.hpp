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

/// Best-effort RSS 2.0 parser (namespace-agnostic element names). Returns nullopt on hard failure.
std::optional<rss::Feed> parseRss2Feed(const QByteArray& xml_utf8, QString* error_message = nullptr,
                                       int max_items = 400);

}  // namespace gui
