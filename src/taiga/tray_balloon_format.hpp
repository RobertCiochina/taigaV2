/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

class QString;

namespace anime {
struct Details;
}

namespace track {
class Episode;
}

namespace taiga::tray_balloon {

/// Replaces `%tokens%` for tray / taskbar messages. Supports literal `\n` as newline.
/// When `anime` is null (unrecognized), only filename / parse tokens are meaningful.
QString formatTemplate(QString tmpl, const track::Episode& episode, const anime::Details* anime);

}  // namespace taiga::tray_balloon
