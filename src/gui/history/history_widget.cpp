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

#include "history_widget.hpp"

#include <QDesktopServices>
#include <QHeaderView>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QUrl>

#include "gui/main/main_window.hpp"
#include "gui/media/media_dialog.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "gui/models/history_model.hpp"
#include "gui/utils/theme.hpp"
#include "taiga/settings.hpp"

namespace gui {

HistoryWidget::HistoryWidget(QWidget* parent)
    : PageWidget{parent}, m_model(new HistoryModel(parent)), m_view(new QTreeView(parent)) {
  m_view->setObjectName("historyView");
  m_view->setFrameShape(QFrame::Shape::NoFrame);
  m_view->setModel(m_model);
  m_view->setAlternatingRowColors(true);
  m_view->setAllColumnsShowFocus(true);
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  m_view->setRootIsDecorated(false);
  m_view->setUniformRowHeights(true);

  m_view->header()->setSectionsClickable(false);
  m_view->header()->setSectionsMovable(false);
  m_view->header()->setStretchLastSection(false);
  m_view->header()->setTextElideMode(Qt::ElideRight);
  m_view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  m_view->header()->setSectionResizeMode(HistoryModel::COLUMN_TITLE, QHeaderView::Stretch);
  m_view->header()->setSectionResizeMode(HistoryModel::COLUMN_DETAILS, QHeaderView::Stretch);

  m_view->sortByColumn(HistoryModel::COLUMN_MODIFIED, Qt::SortOrder::DescendingOrder);
  m_view->setSortingEnabled(true);

  layout()->addWidget(m_view);

  connect(m_view, &QWidget::customContextMenuRequested, this, &HistoryWidget::showContextMenu);
}

void HistoryWidget::showContextMenu() const {
  const auto index = m_view->currentIndex();

  if (!index.isValid()) return;

  const int anime_id = index.data(HistoryModel::AnimeIdRole).toInt();
  if (anime_id <= 0) return;

  QMenu menu;
  menu.addAction(tr("View details..."), [anime_id]() {
    const auto* item = anime::db.item(anime_id);
    if (!item) return;
    const auto* entry = anime::db.entry(anime_id);
    MediaDialog::show(mainWindow(), MediaDialogPage::Details, *item,
                      entry ? std::optional<ListEntry>{*entry} : std::nullopt);
  });
  menu.addAction(tr("Go to anime list"), [anime_id]() {
    mainWindow()->navigateTo(MainWindowPage::List);
    if (const auto* item = anime::db.item(anime_id)) {
      mainWindow()->searchBox()->setText(QString::fromStdString(item->titles.romaji));
    }
  });
  menu.exec(QCursor::pos());
}

}  // namespace gui
