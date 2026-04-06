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

#include <QDialog>

#include <string>
#include <utility>
#include <vector>

namespace Ui {
class SettingsDialog;
}

class QCheckBox;
class QLineEdit;

namespace gui {

class SettingsDialog final : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SettingsDialog)

public:
  SettingsDialog(QWidget* parent);
  ~SettingsDialog() = default;

  static void show(QWidget* parent);
  /// Opens Settings with the Accounts page selected (sign-in, MAL/Kitsu fields).
  static void showAccounts(QWidget* parent);

protected:
  void accept() override;

private:
  void selectStackPageByRole(int stack_index);

  Ui::SettingsDialog* ui_ = nullptr;

  QLineEdit* m_kitsu_email_ = nullptr;
  QLineEdit* m_kitsu_username_ = nullptr;
  QLineEdit* m_kitsu_password_ = nullptr;
  QLineEdit* m_mal_username_ = nullptr;
  QLineEdit* m_mal_access_ = nullptr;
  QLineEdit* m_mal_refresh_ = nullptr;

  std::vector<std::pair<std::string, QCheckBox*>> streaming_provider_checks_;
};

}  // namespace gui
