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
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

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
  void populatePlaceholderPage(const QString& parent, const QString& child);
  void buildDownloadsPage();
  void saveDownloadsPage();

  Ui::SettingsDialog* ui_ = nullptr;

  QLineEdit* m_kitsu_email_ = nullptr;
  QLineEdit* m_kitsu_username_ = nullptr;
  QLineEdit* m_kitsu_password_ = nullptr;
  QLineEdit* m_mal_username_ = nullptr;
  QLineEdit* m_mal_access_ = nullptr;
  QLineEdit* m_mal_refresh_ = nullptr;

  // Torrents → Downloads page: RSS sources (moved from Library page).
  QLineEdit* m_rss_search_url_ = nullptr;
  QLineEdit* m_rss_feed_url_ = nullptr;
  QCheckBox* m_rss_autocheck_ = nullptr;
  QSpinBox*  m_rss_autocheck_mins_ = nullptr;
  QComboBox* m_rss_sort_by_ = nullptr;
  QComboBox* m_rss_sort_order_ = nullptr;

  // Torrents → Downloads page widgets (programmatic page, saves on accept()).
  QCheckBox* m_dl_use_magnet_ = nullptr;
  QLineEdit* m_dl_client_path_ = nullptr;
  QLineEdit* m_dl_file_save_path_ = nullptr;
  QCheckBox* m_dl_use_anime_folder_ = nullptr;
  QCheckBox* m_dl_fallback_client_ = nullptr;
  QCheckBox* m_dl_create_subfolder_ = nullptr;
  QCheckBox* m_dl_app_open_ = nullptr;
  QRadioButton* m_dl_app_default_ = nullptr;
  QRadioButton* m_dl_app_custom_ = nullptr;
  QLineEdit* m_dl_app_exe_ = nullptr;
  QCheckBox* m_dl_autodl_skip_failed_twice_today_ = nullptr;
  QSpinBox* m_dl_autodl_release_delay_mins_ = nullptr;
  QCheckBox* m_dl_autodl_cleanup_unrecognized_ = nullptr;
  // qBittorrent Web API
  QCheckBox* m_dl_qbit_api_enabled_ = nullptr;
  QLineEdit* m_dl_qbit_api_url_ = nullptr;
  QLineEdit* m_dl_qbit_api_user_ = nullptr;
  QLineEdit* m_dl_qbit_api_pass_ = nullptr;

  std::vector<std::pair<std::string, QCheckBox*>> streaming_provider_checks_;
};

}  // namespace gui
