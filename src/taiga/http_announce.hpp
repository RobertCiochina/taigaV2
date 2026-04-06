/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

namespace track {
class Episode;
}

namespace taiga::http_announce {

/// Taiga v1 `announce/http` — POST body after token replacement when sharing + HTTP announce are on.
void postRecognizedEpisodeIfConfigured(const track::Episode& episode);

}  // namespace taiga::http_announce
