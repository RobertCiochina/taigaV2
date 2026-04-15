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

#include "library_widget.hpp"

#include <QAbstractProxyModel>
#include <QDesktopServices>
#include <QHeaderView>
#include <QLayout>
#include <QUrl>
#include <QDir>
#include <algorithm>

#include "gui/library/library_menu.hpp"
#include "gui/main/main_window.hpp"
#include "gui/models/library_model.hpp"
#include "gui/utils/table_view_defaults.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/ui_strings.hpp"
#include "taiga/settings.hpp"
#include "ui_main_window.h"

namespace gui {

namespace {

QString chooseInitialRoot(const std::vector<std::string>& folders) {
  if (folders.empty()) return {};
  const QString first = QString::fromStdString(folders.front());
  // Prefer the first configured folder (stable default); if it is empty, fall back to the first
  // non-empty entry (defensive against bad settings).
  if (!first.trimmed().isEmpty()) return first;
  const auto it = std::find_if(folders.begin(), folders.end(),
                               [](const std::string& s) { return !s.empty(); });
  return it == folders.end() ? QString{} : QString::fromStdString(*it);
}

QString normalizeRootPath(QString path) {
  path = path.trimmed();
  if (path.isEmpty()) return {};
  // QFileSystemModel accepts forward slashes on Windows, but normalize anyway so comparisons in the
  // view/model are stable.
  path = QDir::fromNativeSeparators(path);
  path = QDir::cleanPath(path);
  return path;
}

QModelIndex mapSourceIndexToViewModel(QAbstractItemModel* view_model, QAbstractItemModel* source_model,
                                     QModelIndex idx) {
  if (!idx.isValid()) return {};
  if (!view_model || !source_model) return {};
  if (view_model == source_model) return idx;

  QList<QAbstractProxyModel*> proxies;
  QAbstractItemModel* m = view_model;
  while (auto* p = qobject_cast<QAbstractProxyModel*>(m)) {
    proxies.push_back(p);
    m = p->sourceModel();
  }
  if (m != source_model) return {};

  // Map from source outward to the view model.
  for (auto it = proxies.crbegin(); it != proxies.crend(); ++it) {
    idx = (*it)->mapFromSource(idx);
    if (!idx.isValid()) return {};
  }
  return idx;
}

}  // namespace

LibraryWidget::LibraryWidget(QWidget* parent)
    : PageWidget{parent},
      m_model(new LibraryModel(parent)),
      m_comboRoot(new ComboBox(this)),
      m_view(new QTreeView(parent)) {
  const auto libraryFolders = taiga::settings.libraryFolders();
  const QString rootPath = normalizeRootPath(chooseInitialRoot(libraryFolders));

  auto filtersLayout = new QHBoxLayout(this);
  filtersLayout->setSpacing(4);
  m_toolbarLayout->insertLayout(0, filtersLayout);

  // Root
  {
    m_comboRoot->setPlaceholderText("Location");
    m_comboRoot->setDisabled(rootPath.isEmpty());
    // This is a "current location" selector, not a filter: clearing should not be possible via
    // Esc/RMB/MMB (those are useful for search filters elsewhere).
    m_comboRoot->setProperty("taiga.clearOnEscape", false);
    m_comboRoot->setProperty("taiga.clearOnChordClicks", false);
    for (const auto& folder : libraryFolders) {
      m_comboRoot->addItem(QString::fromStdString(folder));
    }
    // Keep selection stable even if settings contain odd/empty values.
    const int initial_index = std::max(0, m_comboRoot->findText(rootPath));
    if (m_comboRoot->count() > 0) m_comboRoot->setCurrentIndex(initial_index);
    connect(m_comboRoot, &QComboBox::currentIndexChanged, this,
            [this](int index) {
              if (index < 0 || index >= m_comboRoot->count()) return;
              const QString path = normalizeRootPath(m_comboRoot->itemText(index));
              const QModelIndex src_idx = m_model->setRootPath(path);
              const QModelIndex view_idx = mapSourceIndexToViewModel(m_view->model(), m_model, src_idx);
              if (view_idx.isValid()) m_view->setRootIndex(view_idx);
            });
    filtersLayout->addWidget(m_comboRoot);
  }

  // Toolbar: only the open-folder action — play/random/more are in the main toolbar already
  {
    const auto actionOpenFolder =
        new QAction(theme.getIcon("folder_open"), libraryOpenFolderActionLabel(), this);
    actionOpenFolder->setToolTip(libraryOpenFolderForTitleToolTip());
    connect(actionOpenFolder, &QAction::triggered, this, []() {
      if (auto* mw = mainWindow()) mw->openDataFolder();
    });
    m_toolbar->addAction(actionOpenFolder);
  }

  m_view->setObjectName("libraryView");
  m_view->setFrameShape(QFrame::Shape::NoFrame);
  m_view->setModel(m_model);
  // Root is applied above via setRootPath() return index, to avoid invalid indexes briefly
  // showing the "drives" view on Windows.
  m_view->setAlternatingRowColors(true);
  m_view->setAllColumnsShowFocus(true);
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  m_view->setUniformRowHeights(true);

  gui::tables::applyDefaults(m_view);

  // Apply root after applyDefaults(): applyDefaults may wrap the view model in proxies.
  const auto applyRoot = [this](QString path) {
    path = normalizeRootPath(path);
    const QModelIndex src_idx = m_model->setRootPath(path);
    const QModelIndex view_idx = mapSourceIndexToViewModel(m_view->model(), m_model, src_idx);
    if (view_idx.isValid()) m_view->setRootIndex(view_idx);
  };
  applyRoot(rootPath);

  m_view->header()->setSectionsMovable(false);
  m_view->header()->setStretchLastSection(false);
  m_view->header()->setTextElideMode(Qt::ElideRight);
  m_view->header()->hideSection(LibraryModel::COLUMN_TYPE);
  // Use Interactive so manual resizing is stable (ResizeToContents fights user widths).
  m_view->header()->setSectionResizeMode(QHeaderView::Interactive);
  // Keep all visible columns user-resizable.
  m_view->header()->setSectionResizeMode(LibraryModel::COLUMN_NAME, QHeaderView::Interactive);
  m_view->header()->setSectionResizeMode(LibraryModel::COLUMN_ANIME, QHeaderView::Interactive);
  m_view->header()->moveSection(LibraryModel::COLUMN_ANIME, 1);
  m_view->header()->moveSection(LibraryModel::COLUMN_EPISODE, 2);

  m_view->sortByColumn(LibraryModel::COLUMN_NAME, Qt::SortOrder::AscendingOrder);
  m_view->setSortingEnabled(true);

  layout()->addWidget(m_view);

  connect(m_model, &QFileSystemModel::rootPathChanged, this,
          [this](const QString& newPath) {
            const QString p = normalizeRootPath(newPath);
            const QModelIndex src_idx = m_model->index(p);
            const QModelIndex view_idx = mapSourceIndexToViewModel(m_view->model(), m_model, src_idx);
            if (view_idx.isValid()) m_view->setRootIndex(view_idx);
          });

  connect(m_view, &QWidget::customContextMenuRequested, this, &LibraryWidget::showContextMenu);

  connect(m_view, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
    if (!index.isValid()) return;
    if (!(index.flags() & Qt::ItemIsEnabled)) return;
    const auto info = m_model->fileInfo(index);
    if (!info.isFile()) return;
    if (info.isExecutable()) return;  // avoid running potentially dangerous files
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_model->filePath(index)));
  });

  // When a manual override is applied, refresh the home dashboard and list colors.
  connect(m_model, &LibraryModel::libraryOverrideChanged, this, []() {
    if (auto* mw = mainWindow()) {
      mw->refreshHomeDashboard();
      mw->refreshListColors();
    }
  });
}

void LibraryWidget::refreshRootsFromSettings() {
  const auto folders = taiga::settings.libraryFolders();
  m_comboRoot->clear();
  m_comboRoot->setDisabled(folders.empty());
  for (const auto& folder : folders) {
    m_comboRoot->addItem(QString::fromStdString(folder));
  }
  if (folders.empty()) {
    m_model->setRootPath({});
    return;
  }
  const QString root = normalizeRootPath(chooseInitialRoot(folders));
  const int idx = std::max(0, m_comboRoot->findText(root));
  m_comboRoot->setCurrentIndex(idx);
  const QModelIndex src_idx = m_model->setRootPath(normalizeRootPath(m_comboRoot->itemText(idx)));
  const QModelIndex view_idx = mapSourceIndexToViewModel(m_view->model(), m_model, src_idx);
  if (view_idx.isValid()) m_view->setRootIndex(view_idx);
}

std::optional<int> LibraryWidget::selectedRecognizedAnimeId() const {
  const auto index = m_view->currentIndex();
  if (!index.isValid()) return std::nullopt;
  const auto info = m_model->fileInfo(index);
  if (!info.isFile()) return std::nullopt;
  const int id = m_model->getId(info.filePath());
  if (id <= 0) return std::nullopt;
  return id;
}

void LibraryWidget::showContextMenu() const {
  const auto index = m_view->currentIndex();

  if (!index.isValid()) return;

  const QString path = m_model->fileInfo(index).filePath();

  auto* menu = new LibraryMenu(m_view, path, m_model->getId(path), m_model);
  menu->popup();
}

}  // namespace gui
