/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include "media/anime_list.hpp"

class QWidget;

namespace gui {

/// Updates the local list row, then optionally uploads to the active service (Taiga v1 `enablesync`,
/// `account/update/asktoconfirm`). `context_widget` is the parent for confirmation dialogs.
void commitListEntryLocalAndMaybeRemote(const ListEntry& entry, QWidget* context_widget = nullptr);

}  // namespace gui
