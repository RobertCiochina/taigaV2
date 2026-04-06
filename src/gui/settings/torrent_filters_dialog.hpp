/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#pragma once

#include <QDialog>

namespace gui {

class TorrentFiltersDialog final : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentFiltersDialog)

public:
  explicit TorrentFiltersDialog(QWidget* parent = nullptr);
  ~TorrentFiltersDialog() override = default;
};

}  // namespace gui

