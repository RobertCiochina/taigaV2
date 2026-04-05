/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <QDialog>

class QWidget;

namespace gui {

/// OAuth implicit grant: user authorizes in the browser, then pastes the redirect URL or token.
class AnilistAuthDialog final : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AnilistAuthDialog)

public:
  explicit AnilistAuthDialog(QWidget* parent = nullptr);

  /// Runs the dialog; returns true if a token was saved and verified with the Viewer query.
  static bool signIn(QWidget* parent);
};

}  // namespace gui
