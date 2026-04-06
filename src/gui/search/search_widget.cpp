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

#include "search_widget.hpp"

#include <optional>

#include "base/string.hpp"

#include <QActionGroup>
#include <QDate>
#include <QDateTime>
#include <QFileDialog>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include "gui/common/anime_list_view.hpp"
#include "gui/common/anime_list_view_cards.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_season.hpp"
#include "sync/service.hpp"
#include "taiga/session.hpp"
#include "taiga/user_feedback.hpp"

namespace gui {

SearchWidget::SearchWidget(QWidget* parent)
    : PageWidget(parent),
      m_model(new AnimeListModel(this)),
      m_proxyModel(new AnimeListProxyModel(this)),
      m_comboYear(new ComboBox(this)),
      m_comboSeason(new ComboBox(this)),
      m_comboType(new ComboBox(this)),
      m_comboStatus(new ComboBox(this)),
      m_sortMenu(new QMenu(this)),
      m_viewMenu(new QMenu(this)),
      m_moreMenu(new QMenu(this)) {
  m_proxyModel->sort(taiga::session.searchListSortColumn(), taiga::session.searchListSortOrder());
  m_proxyModel->setSecondarySort(taiga::session.searchListSortColumnSecondary(),
                                 taiga::session.searchListSortOrderSecondary());
  m_proxyModel->setFilters(taiga::session.searchListFilters());

  static const auto filterValue = [](QComboBox* combo, int index) {
    return index > -1 ? std::optional<int>{combo->itemData(index).toInt()} : std::nullopt;
  };

  auto filtersLayout = new QHBoxLayout(this);
  filtersLayout->setSpacing(4);
  m_toolbarLayout->insertLayout(0, filtersLayout);

  // Year
  {
    m_comboYear->setPlaceholderText(tr("Year"));
    for (int year = QDate::currentDate().year() + 1; year >= 1940; --year) {
      m_comboYear->addItem(QString::number(year), year);
    }
    if (m_proxyModel->filters().year) {
      m_comboYear->setCurrentText(QString::number(*m_proxyModel->filters().year));
    }
    connect(m_comboYear, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_proxyModel->setYearFilter(filterValue(m_comboYear, index)); });
    filtersLayout->addWidget(m_comboYear);
  }

  // Season
  {
    m_comboSeason->setPlaceholderText(tr("Season"));
    const auto seasons = {
        anime::SeasonName::Winter,
        anime::SeasonName::Spring,
        anime::SeasonName::Summer,
        anime::SeasonName::Fall,
    };
    for (const auto season : seasons) {
      m_comboSeason->addItem(formatSeasonName(season), static_cast<int>(season));
    }
    if (m_proxyModel->filters().season) {
      m_comboSeason->setCurrentText(
          formatSeasonName(static_cast<anime::SeasonName>(*m_proxyModel->filters().season)));
    }
    connect(m_comboSeason, &QComboBox::currentIndexChanged, this, [this](int index) {
      m_proxyModel->setSeasonFilter(filterValue(m_comboSeason, index));
    });
    filtersLayout->addWidget(m_comboSeason);
  }

  // Type
  {
    m_comboType->setPlaceholderText(tr("Type"));
    for (const auto type : anime::kTypes) {
      m_comboType->addItem(formatType(type), static_cast<int>(type));
    }
    if (m_proxyModel->filters().type) {
      m_comboType->setCurrentText(
          formatType(static_cast<anime::Type>(*m_proxyModel->filters().type)));
    }
    connect(m_comboType, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_proxyModel->setTypeFilter(filterValue(m_comboType, index)); });
    filtersLayout->addWidget(m_comboType);
  }

  // Status
  {
    m_comboStatus->setPlaceholderText(tr("Status"));
    for (const auto status : anime::kStatuses) {
      m_comboStatus->addItem(formatStatus(status), static_cast<int>(status));
    }
    if (m_proxyModel->filters().status) {
      m_comboStatus->setCurrentText(
          formatStatus(static_cast<anime::Status>(*m_proxyModel->filters().status)));
    }
    connect(m_comboStatus, &QComboBox::currentIndexChanged, this, [this](int index) {
      m_proxyModel->setStatusFilter(filterValue(m_comboStatus, index));
    });
    filtersLayout->addWidget(m_comboStatus);
  }

  {
    auto* load_season = new QPushButton(tr("Load season"), this);
    load_season->setToolTip(
        tr("Download this season’s catalog from the active service into the local database."));
    connect(load_season, &QPushButton::clicked, this, [this]() {
      const int yi = m_comboYear->currentIndex();
      const int si = m_comboSeason->currentIndex();
      if (yi < 0 || si < 0) {
        taiga::userFeedback(tr("Select a year and season first."), true);
        return;
      }
      const int y = m_comboYear->itemData(yi).toInt();
      const auto season = static_cast<anime::SeasonName>(m_comboSeason->itemData(si).toInt());
      if (y <= 0 || season == anime::SeasonName::Unknown) {
        taiga::userFeedback(tr("Select a valid year and season."), true);
        return;
      }
      QPointer<SearchWidget> guard(this);
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Loading seasonal catalog…"));
      }
      sync::fetchSeasonBrowse(season, y, [guard](const bool ok, const QString& msg) {
        if (!guard) return;
        if (auto* mw = mainWindow()) {
          mw->statusBar()->clearMessage();
          if (ok) {
            guard->reloadAnimeList();
            if (mw->navigation()) mw->navigation()->refresh();
            mw->statusBar()->showMessage(msg.isEmpty() ? tr("Season loaded.") : msg, 6000);
          } else {
            taiga::userFeedback(msg.isEmpty() ? QStringLiteral("Season request failed.") : msg,
                                true);
          }
        }
      });
    });
    filtersLayout->addWidget(load_season);

    auto* reset_filters = new QPushButton(tr("Reset filters"), this);
    reset_filters->setToolTip(
        tr("Clear year, season, format, and airing status filters. The main toolbar search is "
           "unchanged.\nTip: middle-click or right-click a single filter to clear only that one."));
    connect(reset_filters, &QPushButton::clicked, this, [this]() {
      const QSignalBlocker by(m_comboYear);
      const QSignalBlocker bs(m_comboSeason);
      const QSignalBlocker bt(m_comboType);
      const QSignalBlocker bu(m_comboStatus);
      m_comboYear->setCurrentIndex(-1);
      m_comboSeason->setCurrentIndex(-1);
      m_comboType->setCurrentIndex(-1);
      m_comboStatus->setCurrentIndex(-1);
      m_proxyModel->setYearFilter(std::nullopt);
      m_proxyModel->setSeasonFilter(std::nullopt);
      m_proxyModel->setTypeFilter(std::nullopt);
      m_proxyModel->setStatusFilter(std::nullopt);
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Search filters cleared."), 3000);
      }
    });
    filtersLayout->addWidget(reset_filters);
  }

  initToolbar();
  connect(m_sortMenu, &QMenu::aboutToShow, this, &SearchWidget::initSortMenu);
  connect(m_viewMenu, &QMenu::aboutToShow, this, &SearchWidget::initViewMenu);
  connect(m_moreMenu, &QMenu::aboutToShow, this, &SearchWidget::initMoreMenu);
  setViewMode(taiga::session.searchListViewMode());
}

void SearchWidget::setViewMode(ListViewMode mode) {
  if (m_listView) {
    layout()->removeWidget(m_listView);
    m_listView->deleteLater();
    m_listView = nullptr;
  }
  if (m_listViewCards) {
    layout()->removeWidget(m_listViewCards);
    m_listViewCards->deleteLater();
    m_listViewCards = nullptr;
  }

  m_viewMode = mode;

  switch (mode) {
    case ListViewMode::List:
      m_listView = new ListView(this, m_model, m_proxyModel);
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

void SearchWidget::initToolbar() {
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

void SearchWidget::initSortMenu() {
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

void SearchWidget::initViewMenu() {
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

void SearchWidget::initMoreMenu() {
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

void SearchWidget::saveState() {
  taiga::session.setSearchListFilters(m_proxyModel->filters());
  taiga::session.setSearchListSortColumn(m_proxyModel->sortColumn());
  taiga::session.setSearchListSortOrder(m_proxyModel->sortOrder());
  taiga::session.setSearchListSortColumnSecondary(m_proxyModel->secondarySortColumn());
  taiga::session.setSearchListSortOrderSecondary(m_proxyModel->secondarySortOrder());
  taiga::session.setSearchListViewMode(m_viewMode);
}

void SearchWidget::reloadAnimeList() {
  m_model->reloadFromDatabase();
}

void SearchWidget::refreshListTitleDisplay() {
  m_model->emitTitleColumnDataChanged();
  m_proxyModel->invalidate();
}

void SearchWidget::refreshProgressColumnDisplay() {
  m_model->emitProgressColumnDataChanged();
}

void SearchWidget::refreshNewEpisodeHighlightDisplay() {
  m_model->emitNewEpisodeHighlightDataChanged();
  const int col = m_proxyModel->sortColumn();
  if (col >= 0) m_proxyModel->sort(col, m_proxyModel->sortOrder());
}

void SearchWidget::applyToolbarTextFilter(const QString& text) {
  m_proxyModel->setTextFilter(text);
}

}  // namespace gui
