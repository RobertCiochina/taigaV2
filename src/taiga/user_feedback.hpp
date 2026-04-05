/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <functional>

#include <QString>

namespace taiga {

/// Optional UI hook (registered from MainWindow). Safe from any thread; delivery is queued to the GUI.
void setUserFeedbackHandler(std::function<void(const QString& message, bool error)> handler);

void userFeedback(const QString& message, bool error = false);

}  // namespace taiga
