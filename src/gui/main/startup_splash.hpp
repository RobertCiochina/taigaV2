/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QProgressBar;

namespace gui {

class StartupSplash final : public QDialog {
  Q_OBJECT
public:
  explicit StartupSplash(QWidget* parent = nullptr);

  void setStepText(const QString& text);
  void appendLine(const QString& line);

private:
  QLabel* m_stepLabel = nullptr;
  QProgressBar* m_bar = nullptr;
  QPlainTextEdit* m_log = nullptr;
};

}  // namespace gui

