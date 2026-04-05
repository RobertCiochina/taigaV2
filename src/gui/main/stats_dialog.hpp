/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

class QWidget;

namespace gui {

/// List / database statistics (Taiga v1 “Statistics” dialog, Qt port).
class StatsDialog {
public:
  static void show(QWidget* parent);
};

}  // namespace gui
