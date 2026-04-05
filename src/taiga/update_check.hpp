/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

class QWidget;

namespace taiga {

/// Queries taiga.moe/update.php (same endpoint as Taiga 1.x). If \a silent, failures are ignored and
/// “up to date” is not announced; a newer build still opens an optional download prompt.
void checkForUpdates(QWidget* parent_context, bool silent);

}  // namespace taiga
