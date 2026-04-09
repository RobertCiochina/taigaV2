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
#include <QTimer>
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
#include "media/anime_db.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_season.hpp"
#include "sync/anilist.hpp"
#include "sync/service.hpp"
#include "taiga/session.hpp"
#include "taiga/user_feedback.hpp"

namespace gui {

namespace {

void setComboToData(QComboBox* combo, const int value) {
  if (!combo) return;
  for (int i = 0; i < combo->count(); ++i) {
    if (combo->itemData(i).toInt() == value) {
      combo->setCurrentIndex(i);
      return;
    }
  }
}

}  // namespace

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
  // Search should be fast and predictable: keep the natural database order (by anime id) and
  // avoid proxy sorting costs on large result sets (e.g. after Reset filters).
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
      setComboToData(m_comboYear, *m_proxyModel->filters().year);
    }
    connect(m_comboYear, &QComboBox::currentIndexChanged, this,
            [this](int index) {
              m_proxyModel->setYearFilter(filterValue(m_comboYear, index));
              if (!m_applying_defaults_) taiga::session.setSearchListSeasonYearCustomized(true);
            });
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
      setComboToData(m_comboSeason, *m_proxyModel->filters().season);
    }
    connect(m_comboSeason, &QComboBox::currentIndexChanged, this, [this](int index) {
      m_proxyModel->setSeasonFilter(filterValue(m_comboSeason, index));
      if (!m_applying_defaults_) taiga::session.setSearchListSeasonYearCustomized(true);
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
      setComboToData(m_comboType, *m_proxyModel->filters().type);
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
      setComboToData(m_comboStatus, *m_proxyModel->filters().status);
    }
    connect(m_comboStatus, &QComboBox::currentIndexChanged, this, [this](int index) {
      m_proxyModel->setStatusFilter(filterValue(m_comboStatus, index));
    });
    filtersLayout->addWidget(m_comboStatus);
  }

  {
    auto* load_all = new QPushButton(tr("Load all"), this);
    load_all->setToolTip(
        tr("Download the full year+season catalog from the active service into the local database."));
    connect(load_all, &QPushButton::clicked, this, [this]() {
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
    filtersLayout->addWidget(load_all);

    auto* load_my_list = new QPushButton(tr("Load my list"), this);
    load_my_list->setToolTip(
        tr("Refresh details for anime already on your list that match the selected year+season.\n"
           "This avoids loading the full seasonal catalog."));
    connect(load_my_list, &QPushButton::clicked, this, [this]() {
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

      // Collect list anime ids that match the selected season/year.
      QList<int> ids;
      for (const auto& [id, entry] : anime::db.entries().asKeyValueRange()) {
        (void)entry;
        const auto* item = anime::db.item(id);
        if (!item) continue;
        if (item->date_started.year() != y) continue;
        const anime::Season s{item->date_started};
        if (static_cast<int>(s.name) != static_cast<int>(season)) continue;
        ids.push_back(id);
      }

      if (ids.isEmpty()) {
        taiga::userFeedback(tr("No anime on your list match that year+season."), false);
        return;
      }

      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Refreshing %1 list title(s)…").arg(ids.size()), 4000);
      }

      // Best-effort: queue per-anime refreshes. Only AniList exposes completion signals in this build.
      if (sync::currentServiceId() == sync::ServiceId::AniList) {
        auto* svc = sync::anilist::Service::instance();
        const int total = ids.size();
        auto remaining = std::make_shared<int>(total);
        auto ok_count = std::make_shared<int>(0);
        auto wanted = std::make_shared<QSet<int>>();
        for (int id : ids) wanted->insert(id);

        QPointer<SearchWidget> guard(this);
        QMetaObject::Connection conn;
        conn = connect(svc, &sync::anilist::Service::mediaFetchFinished, this,
                       [guard, remaining, ok_count, wanted, total, &conn](int id, bool success) mutable {
                         if (!guard) {
                           disconnect(conn);
                           return;
                         }
                         if (!wanted->contains(id)) return;
                         wanted->remove(id);
                         if (success) ++(*ok_count);
                         if (*remaining > 0) --(*remaining);
                         if (*remaining <= 0) {
                           disconnect(conn);
                           if (auto* mw = mainWindow()) {
                             mw->statusBar()->showMessage(
                                 tr("Refreshed %1/%2 title(s).").arg(*ok_count).arg(total),
                                 6000);
                           }
                           guard->reloadAnimeList();
                           if (auto* mw = mainWindow()) {
                             if (mw->navigation()) mw->navigation()->refresh();
                           }
                         }
                       });

        for (int id : ids) {
          sync::fetchAnime(id);
        }
      } else {
        for (int id : ids) {
          sync::fetchAnime(id);
        }
        // No completion tracking for MAL/Kitsu in this build; just refresh locally shortly after queueing.
        QTimer::singleShot(1500, this, [this]() {
          reloadAnimeList();
          if (auto* mw = mainWindow()) {
            if (mw->navigation()) mw->navigation()->refresh();
          }
        });
      }
    });
    filtersLayout->addWidget(load_my_list);

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
      // Batch structured-filter clear into a single proxy invalidation (performance).
      auto f = m_proxyModel->filters();
      f.year.reset();
      f.season.reset();
      f.type.reset();
      f.status.reset();
      m_proxyModel->setFilters(f);
      // Clearing season/year is an explicit user choice — don't re-apply defaults next time.
      taiga::session.setSearchListSeasonYearCustomized(true);
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Search filters cleared."), 3000);
      }
    });
    filtersLayout->addWidget(reset_filters);
  }

  initToolbar();
  connect(m_viewMenu, &QMenu::aboutToShow, this, &SearchWidget::initViewMenu);
  connect(m_moreMenu, &QMenu::aboutToShow, this, &SearchWidget::initMoreMenu);
  setViewMode(taiga::session.searchListViewMode());

  applyDefaultSeasonYearIfNeeded();

  // Some Qt widget/layout initialization can reset combo selection back to -1 after we set it.
  // Sync once on the next event-loop tick so the visible UI always matches active filters.
  QTimer::singleShot(0, this, [this]() { syncSeasonYearCombosFromFilters(); });
}

void SearchWidget::syncSeasonYearCombosFromFilters() {
  if (!m_comboYear || !m_comboSeason) return;
  const auto f = m_proxyModel->filters();

  m_applying_defaults_ = true;
  const QSignalBlocker by(m_comboYear);
  const QSignalBlocker bs(m_comboSeason);

  if (f.year.has_value()) {
    setComboToData(m_comboYear, *f.year);
  }
  if (f.season.has_value()) {
    setComboToData(m_comboSeason, *f.season);
  }

  // If the filters are present but the combo still couldn't select (unexpected), fall back to "now".
  if (m_comboYear->currentIndex() < 0 || m_comboSeason->currentIndex() < 0) {
    const anime::Season cur{QDate::currentDate().toStdSysDays()};
    const int year = static_cast<int>(cur.year);
    const auto season = cur.name;
    if (year > 0 && season != anime::SeasonName::Unknown) {
      if (m_comboYear->currentIndex() < 0) setComboToData(m_comboYear, year);
      if (m_comboSeason->currentIndex() < 0)
        setComboToData(m_comboSeason, static_cast<int>(season));
      m_proxyModel->setYearFilter(year);
      m_proxyModel->setSeasonFilter(static_cast<int>(season));
    }
  }

  m_applying_defaults_ = false;
}

void SearchWidget::applyDefaultSeasonYearIfNeeded() {
  if (taiga::session.searchListSeasonYearCustomized()) return;

  const anime::Season cur{QDate::currentDate().toStdSysDays()};
  const int year = static_cast<int>(cur.year);
  const auto season = cur.name;
  if (year <= 0 || season == anime::SeasonName::Unknown) return;

  // If the UI already has a selection, keep it (even if session filters are empty).
  // This avoids fighting any future behavior that sets defaults earlier in the ctor.
  const bool year_selected = m_comboYear && m_comboYear->currentIndex() >= 0;
  const bool season_selected = m_comboSeason && m_comboSeason->currentIndex() >= 0;
  if (year_selected && season_selected) {
    maybeAutoLoadDefaultSeason();
    return;
  }

  m_applying_defaults_ = true;
  {
    const QSignalBlocker by(m_comboYear);
    const QSignalBlocker bs(m_comboSeason);

    // Select by data (more robust than by text).
    if (!year_selected) setComboToData(m_comboYear, year);
    if (!season_selected) setComboToData(m_comboSeason, static_cast<int>(season));
    m_proxyModel->setYearFilter(year);
    m_proxyModel->setSeasonFilter(static_cast<int>(season));
  }
  m_applying_defaults_ = false;

  // Pre-populate results by auto-loading the seasonal catalog once (best-effort).
  maybeAutoLoadDefaultSeason();
}

void SearchWidget::maybeAutoLoadDefaultSeason() {
  // Only when we have a valid selected season/year.
  const auto f = m_proxyModel->filters();
  if (!f.year.has_value() || !f.season.has_value()) return;

  const int y = *f.year;
  const auto season = static_cast<anime::SeasonName>(*f.season);
  if (y <= 0 || season == anime::SeasonName::Unknown) return;

  // If the current filters already yield results, assume the season is cached locally
  // and avoid an external call.
  if (m_proxyModel->rowCount() > 0) return;

  // Avoid repeating the same auto-load too often.
  const QString key = QStringLiteral("%1:%2").arg(y).arg(static_cast<int>(season));
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const qint64 last = taiga::session.searchListAutoLoadedSeasonAtSecs();
  constexpr qint64 kMinIntervalSecs = 12 * 60 * 60;  // 12 hours
  if (taiga::session.searchListAutoLoadedSeasonKey() == key && last > 0 &&
      (now - last) < kMinIntervalSecs) {
    return;
  }

  // If sync isn't configured, don't spam an error on every Search open.
  if (sync::currentServiceId() == sync::ServiceId::Unknown) return;
  if (!sync::remoteListAccessConfigured()) return;

  taiga::session.setSearchListAutoLoadedSeasonKey(key);
  taiga::session.setSearchListAutoLoadedSeasonAtSecs(now);

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
        taiga::userFeedback(msg.isEmpty() ? QStringLiteral("Season request failed.") : msg, true);
      }
    }
  });
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
      // Disable interactive sorting on Search to keep resets/snappy filters fast.
      m_listView = new ListView(this, m_model, m_proxyModel, /*enableSorting=*/false);
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
  const auto actionView = new QAction(theme.getIcon("grid_view"), tr("View"), this);
  const auto actionMore = new QAction(theme.getIcon("more_horiz"), tr("More"), this);

  m_toolbar->addAction(actionView);
  m_toolbar->addAction(actionMore);

  const auto viewButton = static_cast<QToolButton*>(m_toolbar->widgetForAction(actionView));
  viewButton->setPopupMode(QToolButton::InstantPopup);
  viewButton->setMenu(m_viewMenu);

  const auto moreButton = static_cast<QToolButton*>(m_toolbar->widgetForAction(actionMore));
  moreButton->setPopupMode(QToolButton::InstantPopup);
  moreButton->setMenu(m_moreMenu);
}

// Sorting is intentionally disabled on the Search page to keep large-result-set operations (like
// Reset filters) responsive. The Anime List page uses a fixed last-updated order (no sort UI).

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
