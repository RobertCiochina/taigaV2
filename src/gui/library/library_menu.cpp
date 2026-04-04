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

#include "library_menu.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QUrl>

#include "gui/media/media_dialog.hpp"
#include "gui/media/media_menu.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"

namespace gui {

LibraryMenu::LibraryMenu(QWidget* parent, const QString& path, int anime_id)
    : QMenu(parent), m_path(path), m_anime_id(anime_id) {
  setAttribute(Qt::WA_DeleteOnClose);
}

void LibraryMenu::popup() {
  if (m_path.isEmpty()) return;

  addAction(QIcon(m_path), tr("Open"), tr("Enter"), this, &LibraryMenu::open);
  addSeparator();
  addAction(theme.getIcon("delete"), tr("Delete"), tr("Del"), this, &LibraryMenu::remove);
  addAction(theme.getIcon("edit"), tr("Rename"), tr("F2"), this, &LibraryMenu::rename);

  if (const auto item = anime::db.item(m_anime_id)) {
    addSeparator();
    addAction(theme.getIcon("info"), tr("Details"), this, &LibraryMenu::viewDetails);
  }

  QMenu::popup(QCursor::pos());
}

void LibraryMenu::open() const {
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
}

void LibraryMenu::remove() const {
  const QFileInfo info(m_path);
  const auto reply =
      QMessageBox::question(parentWidget(), tr("Delete folder"),
                            tr("Move \"%1\" to the Recycle Bin?").arg(info.fileName()),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  QFile file(m_path);
  if (file.moveToTrash()) return;

  QDir dir(m_path);
  if (!dir.removeRecursively()) {
    QMessageBox::warning(parentWidget(), tr("Delete folder"),
                         tr("Could not delete \"%1\".").arg(info.fileName()));
  }
}

void LibraryMenu::rename() const {
  const QFileInfo info(m_path);
  bool ok = false;
  const QString name =
      QInputDialog::getText(parentWidget(), tr("Rename folder"), tr("New name:"), QLineEdit::Normal,
                            info.fileName(), &ok);
  if (!ok || name.isEmpty() || name == info.fileName()) return;

  QDir parent(info.absolutePath());
  if (!parent.rename(info.fileName(), name)) {
    QMessageBox::warning(parentWidget(), tr("Rename folder"), tr("Could not rename the folder."));
  }
}

void LibraryMenu::viewDetails() const {
  const auto item = anime::db.item(m_anime_id);
  const auto entry = anime::db.entry(m_anime_id);

  if (!item) return;

  MediaDialog::show(parentWidget(), MediaDialogPage::Details, *item,
                    entry ? std::optional<ListEntry>{*entry} : std::nullopt);
}

}  // namespace gui
