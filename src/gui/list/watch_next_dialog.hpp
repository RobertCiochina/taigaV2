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

#pragma once

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

#include "media/anime.hpp"
#include "media/anime_list.hpp"

class QCloseEvent;
class QComboBox;
class QFrame;
class QKeyEvent;
class QLabel;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace gui {

enum class WatchNextLayoutMode : int {
  HorizontalTimeline = 0,
  MasterDetail = 1,
};

class WatchNextDialog final : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(WatchNextDialog)

public:
  explicit WatchNextDialog(QWidget* parent = nullptr);
  ~WatchNextDialog() override;

  bool didChangeList() const;

  /// Sidebar / modal flow: call before `exec()` to pick a random Planning seed and titles.
  void runModalRandomPlanningSession();

  /// Non-modal guide for a specific list entry (user can keep using the main window). Always uses
  /// list + detail layout (no layout switcher).
  void presentModelessGuideForAnime(int anime_id);

  /// Embedded panel below the anime list: horizontal timeline only, no window frame, no toolbar
  /// (Randomize / add-all / Planning combo) or AniList fetch strip — compact legend + cards.
  void presentEmbeddedGuideForAnime(int anime_id);

  /// Select a new random seed from Planning-only entries and (re)load relations.
  void randomizeFromPlanning();

signals:
  /// Emitted when a modeless guide closes after list edits (sync handled by `MainWindow`).
  void listChangeCommitted();

private:
  enum class SessionKind { ModalRandomPlanning, ModelessGuide, EmbeddedGuide };
  struct RelationLabel {
    int related_id = 0;
    anime::RelationType type = anime::RelationType::Unknown;
  };

  QWidget* buildCard(int id, bool compact = false, const QString& relation_badge = {});
  QWidget* buildRowWithAlternatives(int main_id, const QVector<int>& alt_ids,
                                    const QString& main_badge);
  QWidget* buildMainTimelineRow(const QVector<int>& flow, const QSet<int>& idSet,
                                const QSet<int>& flowSet,
                                const std::function<QString(int)>& badge_for_spine);
  QWidget* buildMasterDetailSection(const QVector<int>& flow, const QVector<int>& orphans,
                                    const QSet<int>& idSet, const QSet<int>& flowSet,
                                    const std::function<QString(int)>& badge_for_spine);
  void fillWatchNextDetailPane(QWidget* detail_host, int sel_id, const QSet<int>& id_set,
                                const QHash<int, QString>& badge_by_id, const QSet<int>& flow_set);
  void appendAlternativesPanel(QWidget* outer, QVBoxLayout* root_lay, const QSet<int>& id_set);
  /// Scrollable column of full-width “Related” rows (separate from the main watch-order tab).
  /// When `has_more` is true, a button expands to the full orphan list (see `m_relatedShowAll`).
  QWidget* buildRelatedTitlesTabContent(const QVector<int>& orphans_slice, int total_orphan_count,
                                        bool has_more);
  void rebuildCards();
  void setSeed(int id, bool skip_closure_loading_gate = false);
  void repopulatePlanningCombo();
  void ensureFetched(int id);
  void recomputeClosure();
  void scheduleRecomputeAndRebuild();
  void updateSubHeaderHint();

  void commitSetStatus(int id, anime::list::Status status, bool force_rewatch);
  void commitAddAllWatching();
  void commitAddAllPlanning();
  void updateBulkActionButtons();
  void updateRandomizeEnabled();
  void applyRandomizeEnableIfSettled();
  void clearAnilistFetchRows();
  void refreshAnilistFetchRow(int id);
  QString formatAnilistFetchRowHtml(int id) const;
  void applyPendingScrollRestore();
  int readTimelineHorizontalScrollValue() const;
  void restoreTimelineHorizontalScroll(int px);
  void syncCardsHostGeometry();
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void onAnilistMediaFetchQueued(int id);
  void onAnilistMediaFetchStarted(int id);
  void onAnilistMediaFetchFinished(int id, bool ok);
  void applyEmbeddedPanelChrome();
  /// Modal "What to watch next" shows the layout combo + persisted variant; embedded pin is always
  /// horizontal timeline; modeless guide is always list+detail (no combo, no session writes).
  void syncLayoutSwitcherChrome();
  bool seedAnilistFetchSettled() const;
  bool watchClosureAnilistBusy() const;
  bool shouldShowWatchNextLoadingScreen() const;
  void tryFinishWatchNextLoadingGate();
  QWidget* buildWatchNextLoadingWidget();

  int m_seedId = 0;
  bool m_didChange = false;

  QLabel* m_header = nullptr;
  QLabel* m_subHeader = nullptr;
  QWidget* m_layoutSwitcherHost = nullptr;
  QComboBox* m_watchLayoutCombo = nullptr;
  QFrame* m_toolFrame = nullptr;
  QWidget* m_buttonLegendHost = nullptr;
  QPushButton* m_randomBtn = nullptr;
  QPushButton* m_addAllWatchingBtn = nullptr;
  QPushButton* m_addAllPlanningBtn = nullptr;
  QPushButton* m_closeBtn = nullptr;
  QComboBox* m_planningCombo = nullptr;
  SessionKind m_sessionKind = SessionKind::ModalRandomPlanning;
  /// Delays re-enabling Randomize so brief gaps between sequential AniList completions don’t flash.
  QTimer* m_randomizeSettleTimer = nullptr;
  QScrollArea* m_cardsOuterScroll = nullptr;
  QWidget* m_cardsHost = nullptr;
  QVBoxLayout* m_cardsLayout = nullptr;
  /// Horizontal franchise timeline strip (recreated on each `rebuildCards()`).
  QPointer<QScrollArea> m_timelineStripScroll;

  QScrollArea* m_fetchStripScroll = nullptr;
  QLabel* m_fetchStripTitle = nullptr;
  QVBoxLayout* m_fetchRowsLayout = nullptr;
  QLabel* m_fetchStripPlaceholder = nullptr;
  QHash<int, QLabel*> m_anilistFetchLabels;
  QVector<int> m_anilistFetchOrder;
  enum class AnilistRowState { Queued, Loading, Done, Failed };
  QMap<int, AnilistRowState> m_anilistRowState;

  QVector<int> m_displayIds;
  QMap<int, anime::RelationType> m_relationTypeFromSeed;
  QSet<int> m_fetchedIds;
  QMap<int, qint64> m_lastFetchAttemptMs;
  QMap<int, int> m_fetchRetryCount;
  /// Every id we have called `ensureFetched` for this session — used so `itemUpdated` is not
  /// ignored when a fetch completes before that id was added to `m_displayIds`.
  QSet<int> m_watchClosureIds;
  /// `ensureFetched` emits AniList `mediaFetchQueued` synchronously; `onAnilistMediaFetchQueued`
  /// must not call `rebuildCards()` while `recomputeClosure()` is still building `m_displayIds`.
  bool m_recomputingClosure = false;
  /// Flow card id whose alternative versions are shown below the timeline (only one at a time).
  int m_alternativesOpenForId = 0;
  /// Related tab: user expanded past the initial cap (`kRelatedTabInitialCap` ids).
  bool m_relatedShowAll = false;
  WatchNextLayoutMode m_layoutMode = WatchNextLayoutMode::HorizontalTimeline;
  int m_masterDetailSelectedId = 0;
  QTimer* m_rebuildTimer = nullptr;
  /// Modal/modeless: false until AniList work for the current seed + debounced recompute settles.
  bool m_watchNextClosureReady = true;
  /// Suppress per-row `listChangeCommitted` while running add-all bulk commits (embedded panel).
  bool m_bulkListCommit = false;
  bool m_embeddedChromeApplied = false;
};

}  // namespace gui
