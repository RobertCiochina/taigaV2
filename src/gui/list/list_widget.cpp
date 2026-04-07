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

#include "list_widget.hpp"

#include <QAbstractItemView>
#include <QActionGroup>
#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QListView>
#include <QMenu>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <format>

#include "base/string.hpp"
#include "gui/common/anime_list_view.hpp"
#include "gui/common/anime_list_view_cards.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/navigation_item_delegate.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_export.hpp"
#include "taiga/session.hpp"
#include "taiga/user_feedback.hpp"

namespace {

void applyPendingV1ListColumnLayout(QTreeView* view) {
  if (!view) return;
  const QString raw = taiga::session.takePendingV1ListColumnLayout();
  if (raw.isEmpty()) return;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isArray()) return;
  QHeaderView* const h = view->header();
  for (const QJsonValue& v : doc.array()) {
    const QJsonObject o = v.toObject();
    const int col = o[QStringLiteral("c")].toInt(-1);
    if (col < 0 || col >= gui::AnimeListModel::NUM_COLUMNS) continue;
    if (o.contains(QStringLiteral("w"))) {
      const int w = o[QStringLiteral("w")].toInt();
      if (w > 0) h->resizeSection(col, w);
    }
    if (o.contains(QStringLiteral("v"))) {
      h->setSectionHidden(col, !o[QStringLiteral("v")].toBool());
    }
  }
}

}  // namespace

namespace gui {

ListWidget::ListWidget(QWidget* parent)
    : PageWidget(parent),
      m_model(new AnimeListModel(this)),
      m_proxyModel(new AnimeListProxyModel(this)),
      m_sortMenu(new QMenu(this)),
      m_viewMenu(new QMenu(this)),
      m_moreMenu(new QMenu(this)) {
  m_proxyModel->sort(taiga::session.animeListSortColumn(), taiga::session.animeListSortOrder());
  m_proxyModel->setSecondarySort(taiga::session.animeListSortColumnSecondary(),
                                taiga::session.animeListSortOrderSecondary());

  initToolbar();
  initStatusTabBar();
  setViewMode(taiga::session.animeListViewMode());

  connect(m_sortMenu, &QMenu::aboutToShow, this, &ListWidget::initSortMenu);
  connect(m_viewMenu, &QMenu::aboutToShow, this, &ListWidget::initViewMenu);
  connect(m_moreMenu, &QMenu::aboutToShow, this, &ListWidget::initMoreMenu);

  // Keep tab bar in sync with model changes (counts update after sync, import, etc.)
  connect(m_model, &AnimeListModel::modelReset, this, &ListWidget::refreshStatusTabCounts);
  connect(m_model, &AnimeListModel::rowsInserted, this, &ListWidget::refreshStatusTabCounts);
  connect(m_model, &AnimeListModel::rowsRemoved, this, &ListWidget::refreshStatusTabCounts);
  connect(m_model, &AnimeListModel::dataChanged, this, &ListWidget::refreshStatusTabCounts);

  // Sync sidebar → tab bar
  connect(mainWindow()->navigation(), &NavigationWidget::currentListStatusChanged, this,
          [this](anime::list::Status status) {
            const QSignalBlocker b(m_statusTabBar);
            m_suppressTabSync = true;
            selectStatusTab(status);
            m_suppressTabSync = false;
            m_proxyModel->setListStatusFilter({
                .status = static_cast<int>(status),
                .anyStatus = !static_cast<int>(status),
            });
          });
}

ListViewMode ListWidget::viewMode() const {
  return m_viewMode;
}

void ListWidget::setViewMode(ListViewMode mode) {
  if (m_listView) {
    layout()->removeWidget(m_listView);
    m_listView->deleteLater();
    m_listView = nullptr;
  };
  if (m_listViewCards) {
    layout()->removeWidget(m_listViewCards);
    m_listViewCards->deleteLater();
    m_listViewCards = nullptr;
  };

  m_viewMode = mode;

  switch (mode) {
    case ListViewMode::List:
      m_listView = new ListView(this, m_model, m_proxyModel);
      if (const QByteArray header_state = taiga::session.animeListHeaderState();
          !header_state.isEmpty()) {
        m_listView->header()->restoreState(header_state);
      } else {
        applyPendingV1ListColumnLayout(m_listView);
      }
      layout()->addWidget(m_listView);
      m_listView->show();
      break;

    case ListViewMode::Cards:
      m_listViewCards = new ListViewCards(this, m_model, m_proxyModel);
      layout()->addWidget(m_listViewCards);
      m_listViewCards->show();
      break;
  }
}

void ListWidget::saveState() {
  taiga::session.setAnimeListSortColumn(m_proxyModel->sortColumn());
  taiga::session.setAnimeListSortOrder(m_proxyModel->sortOrder());
  taiga::session.setAnimeListSortColumnSecondary(m_proxyModel->secondarySortColumn());
  taiga::session.setAnimeListSortOrderSecondary(m_proxyModel->secondarySortOrder());
  taiga::session.setAnimeListViewMode(m_viewMode);
  if (m_listView) {
    taiga::session.setAnimeListHeaderState(m_listView->header()->saveState());
  }
}

void ListWidget::initStatusTabBar() {
  m_statusTabBar = new QTabBar(this);
  m_statusTabBar->setObjectName("statusTabBar");
  m_statusTabBar->setExpanding(false);
  m_statusTabBar->setDrawBase(false);
  m_statusTabBar->setDocumentMode(true);

  // Tab index 0 = "All", indices 1-N = each status in kStatuses order
  m_statusTabBar->addTab(tr("All"));
  m_statusTabBar->setTabData(0, QVariant::fromValue(static_cast<int>(anime::list::Status::NotInList)));
  for (const auto status : anime::list::kStatuses) {
    const int idx = m_statusTabBar->addTab(formatListStatus(status));
    m_statusTabBar->setTabData(idx, QVariant::fromValue(static_cast<int>(status)));
  }

  refreshStatusTabCounts();

  // Tab click → update proxy filter + sync sidebar
  connect(m_statusTabBar, &QTabBar::currentChanged, this, [this](int idx) {
    if (m_suppressTabSync) return;
    const int raw = m_statusTabBar->tabData(idx).toInt();
    const bool any = (raw == static_cast<int>(anime::list::Status::NotInList));
    m_proxyModel->setListStatusFilter({.status = raw, .anyStatus = any});

    // Sync sidebar navigation to match
    if (!any && mainWindow()) {
      const auto status = static_cast<anime::list::Status>(raw);
      if (auto* nav = mainWindow()->navigation()) {
        if (auto* item = nav->findItemByPage(MainWindowPage::List)) {
          for (int c = 0; c < item->childCount(); ++c) {
            const auto childStatus = static_cast<anime::list::Status>(
                item->child(c)->data(0, static_cast<int>(NavigationItemDataRole::ListStatus)).toInt());
            if (childStatus == status) {
              const QSignalBlocker b(nav);
              nav->setCurrentItem(item->child(c));
              break;
            }
          }
        }
      }
    }
  });

  // Insert the tab bar between toolbar row and list content
  if (auto* vl = qobject_cast<QVBoxLayout*>(layout())) {
    vl->insertWidget(1, m_statusTabBar);
  }
}

void ListWidget::refreshStatusTabCounts() {
  if (!m_statusTabBar) return;
  // Count entries per status
  QMap<anime::list::Status, int> counts;
  for (const auto& entry : anime::db.entries()) {
    counts[entry.status] += 1;
  }
  const int total = static_cast<int>(anime::db.entries().size());

  // Tab 0 = All
  m_statusTabBar->setTabText(0, total > 0 ? tr("All (%1)").arg(total) : tr("All"));

  for (int i = 1; i < m_statusTabBar->count(); ++i) {
    const auto status = static_cast<anime::list::Status>(m_statusTabBar->tabData(i).toInt());
    const int n = counts.value(status, 0);
    m_statusTabBar->setTabText(i, n > 0 ? tr("%1 (%2)").arg(formatListStatus(status)).arg(n)
                                        : formatListStatus(status));
  }
}

void ListWidget::selectStatusTab(const anime::list::Status status) {
  if (!m_statusTabBar) return;
  for (int i = 0; i < m_statusTabBar->count(); ++i) {
    const auto s = static_cast<anime::list::Status>(m_statusTabBar->tabData(i).toInt());
    if (s == status || (i == 0 && !static_cast<int>(status))) {
      m_statusTabBar->setCurrentIndex(i);
      return;
    }
  }
}

void ListWidget::refreshStatusTabCountsNow() {
  refreshStatusTabCounts();
}

void ListWidget::initToolbar() {
  const auto actionSort = new QAction(theme.getIcon("sort"), tr("Sort"), this);
  const auto actionView = new QAction(theme.getIcon("grid_view"), tr("View"), this);
  const auto actionMore = new QAction(theme.getIcon("more_horiz"), tr("More"), this);

  m_toolbar->addAction(actionSort);
  m_toolbar->addAction(actionView);
  m_toolbar->addAction(actionMore);

  const auto sortButton = static_cast<QToolButton*>(m_toolbar->widgetForAction(actionSort));
  sortButton->setPopupMode(QToolButton::InstantPopup);
  sortButton->setMenu(m_sortMenu);

  const auto viewButton = static_cast<QToolButton*>(m_toolbar->widgetForAction(actionView));
  viewButton->setPopupMode(QToolButton::InstantPopup);
  viewButton->setMenu(m_viewMenu);

  const auto moreButton = static_cast<QToolButton*>(m_toolbar->widgetForAction(actionMore));
  moreButton->setPopupMode(QToolButton::InstantPopup);
  moreButton->setMenu(m_moreMenu);
}

void ListWidget::initSortMenu() {
  using Qt::SortOrder::AscendingOrder;
  using Qt::SortOrder::DescendingOrder;

  static const QList<QPair<AnimeListModel::Column, Qt::SortOrder>> items{
      {AnimeListModel::COLUMN_TITLE, AscendingOrder},
      {AnimeListModel::COLUMN_PROGRESS, DescendingOrder},
      {AnimeListModel::COLUMN_DURATION, DescendingOrder},
      {AnimeListModel::COLUMN_REWATCHES, DescendingOrder},
      {AnimeListModel::COLUMN_SCORE, DescendingOrder},
      {AnimeListModel::COLUMN_AVERAGE, DescendingOrder},
      {AnimeListModel::COLUMN_TYPE, AscendingOrder},
      {AnimeListModel::COLUMN_SEASON, DescendingOrder},
      {AnimeListModel::COLUMN_STARTED, DescendingOrder},
      {AnimeListModel::COLUMN_COMPLETED, DescendingOrder},
      {AnimeListModel::COLUMN_LAST_UPDATED, DescendingOrder},
      {AnimeListModel::COLUMN_NOTES, AscendingOrder},
  };

  const auto actionGroup = new QActionGroup(this);
  const auto secondaryGroup = new QActionGroup(this);

  m_sortMenu->clear();

  for (const auto& [column, order] : items) {
    const auto headerData =
        m_model->headerData(column, Qt::Orientation::Horizontal, Qt::DisplayRole);

    const auto action = m_sortMenu->addAction(headerData.toString(), this, [this, column, order]() {
      if (m_listView) {
        // Sorting the proxy model doesn't update the sort indicator on the header.
        m_listView->sortByColumn(column, order);
      } else {
        m_proxyModel->sort(column, order);
      }
    });

    action->setCheckable(true);
    action->setChecked(column == m_proxyModel->sortColumn() && order == m_proxyModel->sortOrder());
    actionGroup->addAction(action);
  }

  m_sortMenu->addSeparator();
  auto* secondaryMenu = m_sortMenu->addMenu(tr("Secondary sort"));

  const auto secondaryNone = secondaryMenu->addAction(tr("None"), this, [this]() {
    m_proxyModel->setSecondarySort(std::nullopt, Qt::AscendingOrder);
  });
  secondaryNone->setCheckable(true);
  secondaryNone->setChecked(!m_proxyModel->secondarySortColumn().has_value());
  secondaryGroup->addAction(secondaryNone);

  secondaryMenu->addSeparator();

  for (const auto& [column, order] : items) {
    const auto headerData =
        m_model->headerData(column, Qt::Orientation::Horizontal, Qt::DisplayRole);

    const auto action =
        secondaryMenu->addAction(headerData.toString(), this, [this, column, order]() {
          m_proxyModel->setSecondarySort(column, order);
        });
    action->setCheckable(true);
    action->setChecked(m_proxyModel->secondarySortColumn().value_or(-1) == column &&
                       m_proxyModel->secondarySortOrder() == order);
    secondaryGroup->addAction(action);
  }
}

void ListWidget::initViewMenu() {
  static const QList<QPair<QString, ListViewMode>> items{
      {"List", ListViewMode::List},
      {"Cards", ListViewMode::Cards},
  };

  const auto actionGroup = new QActionGroup(this);

  m_viewMenu->clear();

  for (const auto& [text, mode] : items) {
    const auto action = m_viewMenu->addAction(text, this, [this, mode]() { setViewMode(mode); });
    action->setCheckable(true);
    action->setChecked(mode == m_viewMode);
    actionGroup->addAction(action);
  }
}

void ListWidget::initMoreMenu() {
  m_moreMenu->clear();

  m_moreMenu->addAction(tr("Synchronize list from service…"), mainWindow(),
                        &MainWindow::startListSynchronization);
  m_moreMenu->addSeparator();

  constexpr auto export_as = [](QWidget* parent, const QString& extension, auto export_function) {
    const auto directory = QFileDialog::getExistingDirectory(
        parent, tr("Select Export Location"), {},
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly);

    if (directory.isEmpty()) return;

    const auto timestamp = QDateTime::currentDateTime().toSecsSinceEpoch();
    const auto path = u"{}/animelist_{}.{}"_s.arg(directory).arg(timestamp).arg(extension);
    if (export_function(path.toStdString())) {
      taiga::userFeedback(tr("Exported list to %1").arg(path), false);
    } else {
      taiga::userFeedback(tr("Could not write the export file."), true);
    }
  };

  m_moreMenu->addAction(tr("Export as Markdown..."), this,
                        [this]() { export_as(this, "md", &anime::list::exportAsMarkdown); });

  m_moreMenu->addAction(tr("Export as XML..."), this,
                        [this]() { export_as(this, "xml", &anime::list::exportAsXml); });
  m_moreMenu->addAction(tr("Export as CSV..."), this,
                        [this]() { export_as(this, "csv", &anime::list::exportAsCsv); });
  m_moreMenu->addSeparator();
  m_moreMenu->addAction(tr("Import from MyAnimeList XML..."), mainWindow(),
                        &MainWindow::importAnimeListMalXml);
}

void ListWidget::reloadAnimeList() {
  m_model->reloadFromDatabase();
}

void ListWidget::refreshListTitleDisplay() {
  m_model->emitTitleColumnDataChanged();
  m_proxyModel->invalidate();
}

void ListWidget::refreshProgressColumnDisplay() {
  m_model->emitProgressColumnDataChanged();
}

void ListWidget::refreshNewEpisodeHighlightDisplay() {
  m_model->emitNewEpisodeHighlightDataChanged();
  const int col = m_proxyModel->sortColumn();
  if (col >= 0) m_proxyModel->sort(col, m_proxyModel->sortOrder());
}

void ListWidget::applyToolbarTextFilter(const QString& text) {
  m_proxyModel->setTextFilter(text);
}

std::optional<int> ListWidget::selectedAnimeId() const {
  const auto fromView = [this](const QAbstractItemView* view) -> std::optional<int> {
    if (!view) return std::nullopt;
    const auto rows = view->selectionModel()->selectedRows();
    if (rows.isEmpty()) return std::nullopt;
    const auto src = m_proxyModel->mapToSource(rows.front());
    if (const auto* anime = m_model->getAnime(src)) return anime->id;
    return std::nullopt;
  };
  if (m_listView) return fromView(m_listView);
  if (m_listViewCards) return fromView(m_listViewCards);
  return std::nullopt;
}

}  // namespace gui
