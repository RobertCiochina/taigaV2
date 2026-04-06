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

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "gui/media/media_dialog.hpp"
#include "gui/media/media_menu.hpp"
#include "gui/models/library_model.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"

namespace gui {

LibraryMenu::LibraryMenu(QWidget* parent, const QString& path, int anime_id, LibraryModel* model)
    : QMenu(parent), m_path(path), m_anime_id(anime_id), m_model(model) {
  setAttribute(Qt::WA_DeleteOnClose);
}

void LibraryMenu::popup() {
  if (m_path.isEmpty()) return;

  addAction(QIcon(m_path), tr("Open"), tr("Enter"), this, &LibraryMenu::open);
  if (QFileInfo info(m_path); info.isFile()) {
    addAction(theme.getIcon("folder"), tr("Open containing folder"), this,
              &LibraryMenu::openContainingFolder);
  }
  addSeparator();
  addAction(theme.getIcon("delete"), tr("Delete"), tr("Del"), this, &LibraryMenu::remove);
  addAction(theme.getIcon("edit"), tr("Rename"), tr("F2"), this, &LibraryMenu::rename);

  if (const auto item = anime::db.item(m_anime_id)) {
    addSeparator();
    addAction(theme.getIcon("info"), tr("Details"), this, &LibraryMenu::viewDetails);
  }

  // Allow manual assignment for files (identified or not).
  if (m_model && QFileInfo(m_path).isFile()) {
    if (!anime::db.item(m_anime_id)) addSeparator();
    addAction(theme.getIcon("edit"), tr("Assign anime && episode…"), this,
              &LibraryMenu::assignAnime);
  }

  QMenu::popup(QCursor::pos());
}

void LibraryMenu::open() const {
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
}

void LibraryMenu::openContainingFolder() const {
  const QFileInfo info(m_path);
  const QString dir = info.isFile() ? info.absolutePath() : info.absoluteFilePath();
  if (dir.isEmpty()) return;
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void LibraryMenu::remove() const {
  const QFileInfo info(m_path);
  const bool is_file = info.isFile();
  const auto reply = QMessageBox::question(
      parentWidget(), is_file ? tr("Delete file") : tr("Delete folder"),
      is_file ? tr("Move \"%1\" to the Recycle Bin?").arg(info.fileName())
              : tr("Move folder \"%1\" to the Recycle Bin?").arg(info.fileName()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  QFile file(m_path);
  if (file.moveToTrash()) return;

  QDir dir(m_path);
  if (!dir.removeRecursively()) {
    QMessageBox::warning(parentWidget(), is_file ? tr("Delete file") : tr("Delete folder"),
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

void LibraryMenu::assignAnime() const {
  if (!m_model) return;

  // Collect all known anime sorted by display title.
  struct AnimeEntry {
    int id;
    QString display;
  };
  QList<AnimeEntry> list;
  for (const auto& [id, item] : anime::db.items().asKeyValueRange()) {
    const QString en = QString::fromStdString(item.titles.english);
    const QString ro = QString::fromStdString(item.titles.romaji);
    const QString display = en.isEmpty() ? ro : QStringLiteral("%1  (%2)").arg(en, ro);
    if (!display.trimmed().isEmpty()) list.append({id, display});
  }
  std::sort(list.begin(), list.end(),
            [](const AnimeEntry& a, const AnimeEntry& b) { return a.display < b.display; });

  auto* dlg = new QDialog(parentWidget());
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle(tr("Assign anime & episode"));
  dlg->setMinimumWidth(520);

  auto* layout = new QVBoxLayout(dlg);

  // Show filename for context.
  auto* fileLabel = new QLabel(
      QStringLiteral("<b>File:</b> %1").arg(QFileInfo(m_path).fileName().toHtmlEscaped()), dlg);
  fileLabel->setWordWrap(true);
  layout->addWidget(fileLabel);

  auto* form = new QFormLayout();

  // Search box to filter combo.
  auto* searchEdit = new QLineEdit(dlg);
  searchEdit->setPlaceholderText(tr("Type to filter anime…"));
  form->addRow(tr("Filter:"), searchEdit);

  auto* combo = new QComboBox(dlg);
  combo->setEditable(false);
  combo->setMaxVisibleItems(20);
  for (const auto& e : list) combo->addItem(e.display, e.id);
  // Pre-select the currently assigned anime if any.
  if (m_anime_id > 0) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toInt() == m_anime_id) { combo->setCurrentIndex(i); break; }
    }
  }
  form->addRow(tr("Anime:"), combo);

  auto* spinEp = new QSpinBox(dlg);
  spinEp->setRange(0, 9999);
  spinEp->setValue(1);
  spinEp->setSpecialValueText(tr("— (unspecified)"));
  // Pre-fill from current recognized episode.
  const QString cur_ep = m_model->getEpisode(m_path);
  if (!cur_ep.isEmpty()) {
    bool ok = false;
    const int ep = cur_ep.toInt(&ok);
    if (ok && ep >= 0) spinEp->setValue(ep);
  }
  form->addRow(tr("Episode:"), spinEp);
  layout->addLayout(form);

  // Filter combo when search text changes (list captured by value for safety).
  connect(searchEdit, &QLineEdit::textChanged, dlg, [combo, list](const QString& text) {
    const QString lower = text.trimmed().toLower();
    combo->clear();
    for (const auto& e : list) {
      if (lower.isEmpty() || e.display.toLower().contains(lower)) {
        combo->addItem(e.display, e.id);
      }
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
  auto* clearBtn = buttons->addButton(tr("Clear assignment"), QDialogButtonBox::ResetRole);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
  // Clear → apply immediately then close (reject skips the normal apply-override path below).
  connect(clearBtn, &QPushButton::clicked, dlg, [this, dlg]() {
    m_model->setOverride(m_path, 0, {});
    dlg->reject();
  });

  if (dlg->exec() != QDialog::Accepted) return;
  const int sel_id = combo->currentData().toInt();
  if (sel_id > 0) {
    const QString ep_str = spinEp->value() > 0 ? QString::number(spinEp->value()) : QString{};
    m_model->setOverride(m_path, sel_id, ep_str);
  }
}

}  // namespace gui
