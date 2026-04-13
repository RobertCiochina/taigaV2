/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <functional>

#include <QString>
class QWidget;

namespace taiga {

/// Queries taiga.moe/update.php (same endpoint as Taiga 1.x). If \a silent, failures are ignored and
/// “up to date” is not announced; a newer build still opens an optional download prompt.
void checkForUpdates(QWidget* parent_context, bool silent);

struct UpdateCheckResult {
  bool ok = false;
  bool has_newer = false;
  QString latest;
  QString link;
  QString error;
};

/// Performs an update check but never shows UI. Callback is invoked on the calling thread's event loop.
void checkForUpdatesSilent(std::function<void(UpdateCheckResult)> callback);

/// Shows the “newer version available” prompt for a previously-detected update.
void promptUpdateAvailable(QWidget* parent_context, const QString& latest, const QString& link);

}  // namespace taiga
