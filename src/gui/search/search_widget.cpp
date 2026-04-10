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
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QGuiApplication>
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
#include "media/anime_season.hpp"
#include "sync/anilist.hpp"
#include "sync/service.hpp"
#include "taiga/season_browse_cache.hpp"
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
      m_viewMenu(new QMenu(this)) {
  // Search should be fast and predictable: keep the natural database order (by anime id) and
  // avoid proxy sorting costs on large result sets (e.g. after Reset filters).
  m_proxyModel->setFilters(taiga::session.searchListFilters());
  // Default behavior: only show titles already on the user's list.
  // This is intentionally not persisted (Load all is a temporary mode).
  m_proxyModel->setListStatusFilter({/*status=*/0, /*anyStatus=*/true});

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
    m_btnLoadAll = new QPushButton(tr("Load all"), this);
    m_btnLoadAll->setToolTip(
        tr("Download the full year+season catalog from the active service into the local database.\n"
           "If that season was already loaded before, this will use the local database.\n"
           "Tip: hold Shift while clicking to force refresh from the service."));
    connect(m_btnLoadAll, &QPushButton::clicked, this, [this]() {
      if (m_seasonBrowseInFlight) {
        if (auto* mw = mainWindow()) {
          mw->statusBar()->showMessage(tr("Please wait for the current refresh to finish."), 3500);
        }
        return;
      }
      const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
      if (m_lastNetworkOpMs > 0 && (now_ms - m_lastNetworkOpMs) < 1200) {
        if (auto* mw = mainWindow()) {
          mw->statusBar()->showMessage(tr("Please wait a moment and try again."), 2500);
        }
        return;
      }

      // Temporarily show all results (not just titles in the user's list).
      m_proxyModel->setListStatusFilter({});
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

      const bool force_refresh =
          (QGuiApplication::keyboardModifiers() & Qt::KeyboardModifier::ShiftModifier) != 0;
      const QString key = taiga::seasonBrowseCacheKey(sync::currentServiceId(), y, season);
      const QStringList loaded_keys = taiga::session.searchListSeasonBrowseLoadedKeys();
      if (!taiga::shouldFetchSeasonBrowse(loaded_keys, key, force_refresh)) {
        reloadAnimeList();
        if (auto* mw = mainWindow()) {
          if (mw->navigation()) mw->navigation()->refresh();
          mw->statusBar()->showMessage(
              tr("Season already loaded. Hold Shift and click Load all to refresh."), 5000);
        }
        return;
      }

      QPointer<SearchWidget> guard(this);
      if (auto* mw = mainWindow()) {
        mw->statusBar()->showMessage(tr("Loading seasonal catalog…"));
      }
      m_seasonBrowseInFlight = true;
      m_lastNetworkOpMs = now_ms;
      if (m_btnLoadAll) m_btnLoadAll->setEnabled(false);
      if (m_btnLoadMyList) m_btnLoadMyList->setEnabled(false);

      sync::fetchSeasonBrowse(season, y, [guard, key](const bool ok, const QString& msg) {
        if (!guard) return;
        guard->m_seasonBrowseInFlight = false;
        if (guard->m_btnLoadAll) guard->m_btnLoadAll->setEnabled(true);
        if (guard->m_btnLoadMyList) guard->m_btnLoadMyList->setEnabled(true);
        if (auto* mw = mainWindow()) {
          mw->statusBar()->clearMessage();
          if (ok) {
            taiga::session.setSearchListSeasonBrowseLoadedKeys(taiga::seasonBrowseCacheAdd(
                taiga::session.searchListSeasonBrowseLoadedKeys(), key));
            guard->reloadAnimeList();
            if (mw->navigation()) mw->navigation()->refresh();
            mw->statusBar()->showMessage(msg.isEmpty() ? tr("Season loaded.") : msg, 6000);
          } else {
            // Avoid a modal network error popup on rapid repeated actions; status bar is enough.
            taiga::userFeedback(msg.isEmpty() ? QStringLiteral("Season request failed.") : msg,
                                false);
          }
        }
      });
    });
    filtersLayout->addWidget(m_btnLoadAll);

    m_btnLoadMyList = new QPushButton(tr("Load my list"), this);
    m_btnLoadMyList->setToolTip(
        tr("Show only titles already on your list.\n"
           "This is instant and does not make network calls."));
    connect(m_btnLoadMyList, &QPushButton::clicked, this, [this]() {
      // Switch back to "my list only" mode.
      m_proxyModel->setListStatusFilter({/*status=*/0, /*anyStatus=*/true});
      reloadAnimeList();
      if (auto* mw = mainWindow()) {
        if (mw->navigation()) mw->navigation()->refresh();
        mw->statusBar()->showMessage(tr("Showing only titles in your list."), 3000);
      }
    });
    filtersLayout->addWidget(m_btnLoadMyList);

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

  m_toolbar->addAction(actionView);

  const auto viewButton = static_cast<QToolButton*>(m_toolbar->widgetForAction(actionView));
  viewButton->setPopupMode(QToolButton::InstantPopup);
  viewButton->setMenu(m_viewMenu);
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

void SearchWidget::saveState() {
  // Persist search filters, but keep "my list only" as the default next time
  // (Load all is a temporary mode).
  auto f = m_proxyModel->filters();
  f.listStatus = {};
  taiga::session.setSearchListFilters(f);
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
  m_model->refreshNewEpisodeHighlightDisplay();
  const int col = m_proxyModel->sortColumn();
  if (col >= 0) m_proxyModel->sort(col, m_proxyModel->sortOrder());
}

void SearchWidget::applyToolbarTextFilter(const QString& text) {
  m_proxyModel->setTextFilter(text);
}

}  // namespace gui
