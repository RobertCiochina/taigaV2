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
#include "gui/utils/ui_strings.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"

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
    addAction(theme.getIcon("info"), mediaViewDetailsActionLabel(), this, &LibraryMenu::viewDetails);
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

  // Build a full list of all anime sorted by display title.
  struct AnimeEntry {
    int id;
    QString display;
  };
  QList<AnimeEntry> allAnime;
  for (const auto& [id, item] : anime::db.items().asKeyValueRange()) {
    const QString en = QString::fromStdString(item.titles.english);
    const QString ro = QString::fromStdString(item.titles.romaji);
    const QString display = en.isEmpty() ? ro : QStringLiteral("%1  (%2)").arg(en, ro);
    if (!display.trimmed().isEmpty()) allAnime.append({id, display});
  }
  std::sort(allAnime.begin(), allAnime.end(),
            [](const AnimeEntry& a, const AnimeEntry& b) { return a.display < b.display; });

  // Helper: return anime filtered by list status (Status::NotInList = All).
  using Status = anime::list::Status;
  const auto filteredByStatus = [&](Status st) {
    QList<AnimeEntry> result;
    for (const auto& e : allAnime) {
      if (st == Status::NotInList) {
        result.append(e);
      } else {
        const auto* entry = anime::db.entry(e.id);
        if (entry && entry->status == st) result.append(e);
      }
    }
    return result;
  };

  auto* dlg = new QDialog(parentWidget());
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle(tr("Assign anime & episode"));
  dlg->setMinimumWidth(540);

  auto* layout = new QVBoxLayout(dlg);

  auto* fileLabel = new QLabel(
      QStringLiteral("<b>File:</b> %1").arg(QFileInfo(m_path).fileName().toHtmlEscaped()), dlg);
  fileLabel->setWordWrap(true);
  layout->addWidget(fileLabel);

  auto* form = new QFormLayout();

  // Status filter combo (default: Watching).
  auto* statusCombo = new QComboBox(dlg);
  statusCombo->addItem(tr("Watching"),    static_cast<int>(Status::Watching));
  statusCombo->addItem(tr("Completed"),   static_cast<int>(Status::Completed));
  statusCombo->addItem(tr("Plan to Watch"), static_cast<int>(Status::PlanToWatch));
  statusCombo->addItem(tr("On Hold"),     static_cast<int>(Status::OnHold));
  statusCombo->addItem(tr("Dropped"),     static_cast<int>(Status::Dropped));
  statusCombo->addItem(tr("All anime"),   static_cast<int>(Status::NotInList));
  form->addRow(tr("Show:"), statusCombo);

  // Search / filter box.
  auto* searchEdit = new QLineEdit(dlg);
  searchEdit->setPlaceholderText(tr("Type to filter…"));
  form->addRow(tr("Filter:"), searchEdit);

  auto* combo = new QComboBox(dlg);
  combo->setEditable(false);
  combo->setMaxVisibleItems(20);
  form->addRow(tr("Anime:"), combo);

  // Populate combo from status + search filter.
  const auto rebuildCombo = [&, combo, statusCombo, searchEdit, allAnime]() {
    const auto st = static_cast<Status>(statusCombo->currentData().toInt());
    const QList<AnimeEntry> statusList = filteredByStatus(st);
    const QString lower = searchEdit->text().trimmed().toLower();
    const int prev_id = combo->currentData().toInt();
    combo->clear();
    for (const auto& e : statusList) {
      if (lower.isEmpty() || e.display.toLower().contains(lower))
        combo->addItem(e.display, e.id);
    }
    // Try to restore selection.
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toInt() == prev_id) { combo->setCurrentIndex(i); break; }
    }
  };

  // Initial population (Watching).
  {
    const auto st = static_cast<Status>(statusCombo->currentData().toInt());
    for (const auto& e : filteredByStatus(st)) combo->addItem(e.display, e.id);
    // Pre-select the currently assigned anime if any.
    if (m_anime_id > 0) {
      for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toInt() == m_anime_id) { combo->setCurrentIndex(i); break; }
      }
    }
  }

  auto* spinEp = new QSpinBox(dlg);
  spinEp->setRange(0, 9999);
  spinEp->setValue(1);
  spinEp->setSpecialValueText(tr("— (unspecified)"));
  const QString cur_ep = m_model->getEpisode(m_path);
  if (!cur_ep.isEmpty()) {
    bool ok = false;
    if (const int ep = cur_ep.toInt(&ok); ok && ep >= 0) spinEp->setValue(ep);
  }
  form->addRow(tr("Episode:"), spinEp);
  layout->addLayout(form);

  connect(statusCombo, &QComboBox::currentIndexChanged, dlg,
          [rebuildCombo](int) { rebuildCombo(); });
  connect(searchEdit, &QLineEdit::textChanged, dlg,
          [rebuildCombo](const QString&) { rebuildCombo(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
  auto* clearBtn = buttons->addButton(tr("Clear assignment"), QDialogButtonBox::ResetRole);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
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
