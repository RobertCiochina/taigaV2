/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
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

#include <QComboBox>
#include <QPointer>

class QFrame;
class QListView;

namespace gui {

/// Custom popup (Search filters, etc.). Optional dynamic `QWidget` properties:
/// `taiga.clearOnEscape` (default true: Esc clears selection), `taiga.clearOnChordClicks` (default
/// true: MMB/RMB clear).
class ComboBox : public QComboBox {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ComboBox)

public:
  ComboBox(QWidget* parent);
  ~ComboBox() = default;

protected:
  void showPopup() override;
  void hidePopup() override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  void ensurePopup();
  void positionAndShowPopup();

  QPointer<QFrame> popup_frame_;
  QListView* popup_list_ = nullptr;
};

}  // namespace gui
