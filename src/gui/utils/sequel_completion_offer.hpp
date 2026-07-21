/**
 * Taiga
 */

#pragma once

class QWidget;

namespace gui {

/// After a title transitions to Completed, optionally offer the next AniList Sequel via a
/// persistent status-bar strip (settings-gated, AniList-only).
void maybeOfferNextSequelAfterCompletion(int completedAnimeId);

/// Modal to set list status for a single sequel. Returns true if the user applied a change.
bool showSequelStatusDialog(QWidget* parent, int sequelAnimeId);

}  // namespace gui
