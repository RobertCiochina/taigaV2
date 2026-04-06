/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include "media/anime.hpp"
#include "media/anime_list.hpp"

class QWidget;

namespace gui {

/// Updates the local list row, then optionally uploads to the active service (Taiga v1 `enablesync`,
/// `account/update/asktoconfirm`). `context_widget` is the parent for confirmation dialogs.
void commitListEntryLocalAndMaybeRemote(const ListEntry& entry, QWidget* context_widget = nullptr);

/// Taiga v1 "series complete" prompt: if watched_episodes >= known total (and total > 0), and the
/// entry is still Watching/PlanToWatch, shows a dialog asking to move to Completed.  Updates
/// `entry` in-place (status + date_completed) and returns true when the user accepted.  Call this
/// *before* commitListEntryLocalAndMaybeRemote so the single commit includes the status change.
bool maybePromptCompletion(QWidget* parent, const Anime& item, ListEntry& entry);

}  // namespace gui
