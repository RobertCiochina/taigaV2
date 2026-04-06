/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
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

#include <QMenu>
#include <QString>

namespace gui {

class LibraryModel;

class LibraryMenu final : public QMenu {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LibraryMenu)

public:
  /// `model` may be null (menu will skip the assignment action in that case).
  LibraryMenu(QWidget* parent, const QString& path, int anime_id, LibraryModel* model = nullptr);
  ~LibraryMenu() = default;

  void popup();

private slots:
  void open() const;
  void openContainingFolder() const;
  void remove() const;
  void rename() const;
  void viewDetails() const;
  void assignAnime() const;

private:
  int m_anime_id = 0;
  QString m_path;
  LibraryModel* m_model = nullptr;
};

}  // namespace gui
