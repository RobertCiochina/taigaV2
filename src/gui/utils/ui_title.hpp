#pragma once

#include <QString>

#include "media/anime.hpp"

namespace gui {

/// UI-wide title display rule: English-first with safe fallback.
inline QString uiTitle(const anime::Details& a) {
  return QString::fromStdString(anime::preferredListTitleString(a, anime::TitleLanguage::English));
}

}  // namespace gui

