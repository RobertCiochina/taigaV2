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

#include "watch_next_dialog.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "base/string.hpp"
#include "gui/common/clickable_label.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "sync/anilist.hpp"
#include "sync/service.hpp"

namespace {

QString relationTypeLabel(const anime::RelationType t) {
  using anime::RelationType;
  switch (t) {
    case RelationType::Prequel:
      return QApplication::translate("WatchNext", "Prequel");
    case RelationType::Sequel:
      return QApplication::translate("WatchNext", "Sequel");
    case RelationType::Alternative:
      return QApplication::translate("WatchNext", "Alternative");
    case RelationType::SideStory:
      return QApplication::translate("WatchNext", "Side story");
    case RelationType::Parent:
      return QApplication::translate("WatchNext", "Parent");
    case RelationType::SpinOff:
      return QApplication::translate("WatchNext", "Spin-off");
    case RelationType::Summary:
      return QApplication::translate("WatchNext", "Summary");
    case RelationType::Character:
      return QApplication::translate("WatchNext", "Character");
    case RelationType::Other:
      return QApplication::translate("WatchNext", "Other");
    case RelationType::Unknown:
    default:
      return {};
  }
}

QString formatLabel(const anime::Type t) {
  using anime::Type;
  switch (t) {
    case Type::Tv:
      return QApplication::translate("WatchNext", "TV");
    case Type::Movie:
      return QApplication::translate("WatchNext", "Movie");
    case Type::Ova:
      return QApplication::translate("WatchNext", "OVA");
    case Type::Ona:
      return QApplication::translate("WatchNext", "ONA");
    case Type::Special:
      return QApplication::translate("WatchNext", "Special");
    case Type::Music:
      return QApplication::translate("WatchNext", "Music");
    case Type::Unknown:
    default:
      return {};
  }
}

/// Prefer English title for display; fall back to romaji, then “#id”.
QString animeDisplayTitle(const Anime* a, const int id) {
  if (!a) return QApplication::translate("WatchNext", "#%1").arg(id);
  if (!a->titles.english.empty()) return QString::fromStdString(a->titles.english);
  if (!a->titles.romaji.empty()) return QString::fromStdString(a->titles.romaji);
  return QApplication::translate("WatchNext", "#%1").arg(id);
}

QWidget* makeLegendItem(QWidget* parent, const QIcon& icon, const QString& text) {
  auto* w = new QWidget(parent);
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(6);
  auto* ic = new QLabel(w);
  ic->setFixedSize(22, 22);
  ic->setPixmap(icon.pixmap(22, 22));
  ic->setScaledContents(true);
  auto* tx = new QLabel(text, w);
  tx->setWordWrap(true);
  tx->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText); font-size: 11px;}"));
  h->addWidget(ic, 0, Qt::AlignTop);
  h->addWidget(tx, 1);
  return w;
}

/// Single-line legend row for the embedded panel: same icons as on cards, shorter copy.
QWidget* makeCompactLegendItem(QWidget* parent, const QIcon& icon, const QString& text) {
  auto* w = new QWidget(parent);
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(4);
  auto* ic = new QLabel(w);
  constexpr int k = 18;
  ic->setFixedSize(k, k);
  ic->setPixmap(icon.pixmap(k, k));
  ic->setScaledContents(true);
  auto* tx = new QLabel(text, w);
  tx->setWordWrap(false);
  tx->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText); font-size: 10px;}"));
  h->addWidget(ic, 0, Qt::AlignVCenter);
  h->addWidget(tx, 0, Qt::AlignVCenter);
  return w;
}

QString listStatusLabel(const anime::list::Status s) {
  using anime::list::Status;
  switch (s) {
    case Status::Watching:
      return QApplication::translate("WatchNext", "Watching");
    case Status::Completed:
      return QApplication::translate("WatchNext", "Completed");
    case Status::OnHold:
      return QApplication::translate("WatchNext", "On hold");
    case Status::Dropped:
      return QApplication::translate("WatchNext", "Dropped");
    case Status::PlanToWatch:
      return QApplication::translate("WatchNext", "Planning");
    case Status::NotInList:
    default:
      return QApplication::translate("WatchNext", "Not in list");
  }
}

}  // namespace

namespace {

qint64 chronologyKey(const Anime* a) {
  if (!a || a->date_started.empty()) return 0;
  const int y = static_cast<int>(a->date_started.year());
  const int m = static_cast<int>(a->date_started.month());
  const int d = static_cast<int>(a->date_started.day());
  return (static_cast<qint64>(y) * 10000) + (m * 100) + d;
}

void sortIdsByStartDate(QVector<int>& ids) {
  std::sort(ids.begin(), ids.end(), [](int lhs, int rhs) {
    const Anime* a = anime::db.item(lhs);
    const Anime* b = anime::db.item(rhs);
    const qint64 ka = chronologyKey(a);
    const qint64 kb = chronologyKey(b);
    if (ka != kb) return ka < kb;
    return lhs < rhs;
  });
}

void sortIdsByChronologyAsc(QVector<int>& ids) {
  std::sort(ids.begin(), ids.end(), [](int a, int b) {
    const qint64 ka = chronologyKey(anime::db.item(a));
    const qint64 kb = chronologyKey(anime::db.item(b));
    if (ka == 0 && kb != 0) return false;
    if (kb == 0 && ka != 0) return true;
    if (ka != kb) return ka < kb;
    return a < b;
  });
}

/// Edges used to expand the fetched closure (BFS). **Parent** is intentionally excluded so TV /
/// source links do not pull unrelated entries; use prequel/sequel (and alternatives for fetch).
bool isClosureExpansionRelation(const anime::RelationType t) {
  using anime::RelationType;
  switch (t) {
    case RelationType::Prequel:
    case RelationType::Sequel:
    case RelationType::Alternative:
      return true;
    default:
      return false;
  }
}

/// Main timeline: prequel / sequel only. Alternatives are fetched into the closure but do not
/// stitch the flow row; they are shown on demand per card.
bool isFlowTimelineRelation(const anime::RelationType t) {
  using anime::RelationType;
  switch (t) {
    case RelationType::Prequel:
    case RelationType::Sequel:
      return true;
    default:
      return false;
  }
}

void addUndirected(QHash<int, QSet<int>>& adj, const int u, const int v) {
  if (u <= 0 || v <= 0 || u == v) return;
  adj[u].insert(v);
  adj[v].insert(u);
}

void buildFranchiseAdjacency(const QSet<int>& ids, QHash<int, QSet<int>>& adj) {
  adj.clear();
  for (const int id : ids) {
    const Anime* a = anime::db.item(id);
    if (!a) continue;
    for (const auto& rel : a->relations) {
      if (rel.related_id <= 0) continue;
      if (!ids.contains(rel.related_id)) continue;
      if (!isFlowTimelineRelation(rel.type)) continue;
      addUndirected(adj, id, rel.related_id);
    }
  }
}

/// Main flow: all titles in `ids` connected to `seed` via franchise links, sorted by start date.
QVector<int> buildSeriesFlow(const QSet<int>& ids, const int seed) {
  if (seed <= 0 || !ids.contains(seed)) return {};

  QHash<int, QSet<int>> adj;
  buildFranchiseAdjacency(ids, adj);

  QSet<int> seen;
  QVector<int> q;
  q.push_back(seed);
  seen.insert(seed);

  for (int i = 0; i < q.size(); ++i) {
    const int cur = q[i];
    for (const int nid : adj.value(cur)) {
      if (nid <= 0) continue;
      if (seen.contains(nid)) continue;
      seen.insert(nid);
      q.push_back(nid);
    }
  }

  QVector<int> flow = QVector<int>(seen.cbegin(), seen.cend());
  sortIdsByChronologyAsc(flow);
  return flow;
}

QVector<int> alternativesFromAnchor(const int anchor, const QSet<int>& ids) {
  QVector<int> out;
  const Anime* a = anime::db.item(anchor);
  if (!a) return out;
  for (const auto& rel : a->relations) {
    if (rel.type != anime::RelationType::Alternative) continue;
    if (!ids.contains(rel.related_id)) continue;
    out.push_back(rel.related_id);
  }
  // Reverse direction: another entry may list this anchor as its alternative.
  for (const int oid : ids) {
    if (oid == anchor) continue;
    const Anime* o = anime::db.item(oid);
    if (!o) continue;
    for (const auto& rel : o->relations) {
      if (rel.type != anime::RelationType::Alternative) continue;
      if (rel.related_id != anchor) continue;
      if (!out.contains(oid)) out.push_back(oid);
    }
  }
  sortIdsByStartDate(out);
  return out;
}

bool isAlternativeNeighborOfFlow(const int id, const QSet<int>& flowSet, const QSet<int>& idSet) {
  if (flowSet.contains(id)) return false;
  for (const int fid : flowSet) {
    for (const int aid : alternativesFromAnchor(fid, idSet)) {
      if (aid == id) return true;
    }
  }
  return false;
}

void clearVBoxLayoutWidgets(QVBoxLayout* const lay) {
  if (!lay) return;
  while (QLayoutItem* it = lay->takeAt(0)) {
    if (QWidget* w = it->widget()) w->deleteLater();
    delete it;
  }
}

}  // namespace

namespace gui {

WatchNextDialog::WatchNextDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("What to watch next"));
  setModal(true);
  resize(1000, 980);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(8);

  m_header = new QLabel(this);
  m_header->setTextFormat(Qt::RichText);
  m_header->setText(tr("<b>What to watch next</b>"));
  m_header->setWordWrap(true);
  root->addWidget(m_header);

  m_subHeader = new QLabel(this);
  m_subHeader->setStyleSheet(
      QStringLiteral("QLabel{color: palette(placeholderText); font-size:12px;}"));
  m_subHeader->setWordWrap(true);
  m_subHeader->setMaximumHeight(m_subHeader->fontMetrics().lineSpacing() * 2 + 6);
  m_subHeader->setText(
      tr("Pick a random title from Planning, then review all related seasons/releases."));
  root->addWidget(m_subHeader);

  m_toolFrame = new QFrame(this);
  m_toolFrame->setObjectName(QStringLiteral("watchNextToolFrame"));
  m_toolFrame->setStyleSheet(QStringLiteral(
      "QFrame#watchNextToolFrame{border:1px solid palette(mid); border-radius:8px; background: "
      "palette(alternate-base);}"));
  auto* toolRoot = new QVBoxLayout(m_toolFrame);
  toolRoot->setContentsMargins(10, 10, 10, 10);
  toolRoot->setSpacing(10);

  auto* primaryRow = new QHBoxLayout();
  primaryRow->setSpacing(8);
  m_randomBtn = new QPushButton(theme.getIcon("shuffle"), tr("Randomize"), this);
  m_randomBtn->setToolTip(tr("Pick another random title from Planning."));
  m_addAllWatchingBtn = new QPushButton(theme.getIcon("lists"), tr("Add all to Watching"), this);
  m_addAllPlanningBtn = new QPushButton(theme.getIcon("add_box"), tr("Add all to Planning"), this);
  m_addAllPlanningBtn->setToolTip(
      tr("Put every related title on Planning (skips titles already on Planning)."));
  m_closeBtn = new QPushButton(tr("Close"), this);
  m_closeBtn->setDefault(true);
  primaryRow->addWidget(m_randomBtn);
  primaryRow->addWidget(m_addAllWatchingBtn);
  primaryRow->addWidget(m_addAllPlanningBtn);
  primaryRow->addStretch(1);
  primaryRow->addWidget(m_closeBtn);
  toolRoot->addLayout(primaryRow);

  m_planningCombo = new QComboBox(this);
  m_planningCombo->setMinimumWidth(260);
  m_planningCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_planningCombo->setToolTip(tr("Jump to any title on your Planning list."));
  connect(m_planningCombo, QOverload<int>::of(&QComboBox::activated), this, [this](const int idx) {
    const int id = m_planningCombo->itemData(idx).toInt();
    if (id > 0 && id != m_seedId) setSeed(id);
  });
  auto* pickForm = new QFormLayout();
  pickForm->setContentsMargins(0, 0, 0, 0);
  pickForm->setHorizontalSpacing(12);
  pickForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  pickForm->addRow(tr("Start from Planning"), m_planningCombo);
  toolRoot->addLayout(pickForm);

  root->addWidget(m_toolFrame);

  // Cards live in a plain widget (no outer QScrollArea): a tall viewport was leaving a large empty
  // alternate-base band above the legend. Horizontal scrolling stays on the timeline strip only.
  m_cardsHost = new QWidget(this);
  m_cardsHost->setObjectName(QStringLiteral("watchNextCardsHost"));
  m_cardsHost->setStyleSheet(QStringLiteral("QWidget#watchNextCardsHost{background:transparent;}"));
  m_cardsHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_cardsLayout = new QVBoxLayout(m_cardsHost);
  m_cardsLayout->setContentsMargins(0, 0, 0, 0);
  m_cardsLayout->setSpacing(7);
  root->addWidget(m_cardsHost);

  m_buttonLegendHost = new QWidget(this);
  auto* legOuter = new QVBoxLayout(m_buttonLegendHost);
  legOuter->setContentsMargins(0, 6, 0, 0);
  legOuter->setSpacing(3);
  auto* legHeading = new QLabel(tr("On each card"), m_buttonLegendHost);
  legHeading->setStyleSheet(
      QStringLiteral("QLabel{font-weight:600; font-size:11px; color: palette(placeholderText);}"));
  legOuter->addWidget(legHeading);
  legOuter->addWidget(makeLegendItem(m_buttonLegendHost, theme.getIcon("skip_next"),
                                     tr("Watching — start or continue this title.")));
  legOuter->addWidget(makeLegendItem(m_buttonLegendHost, theme.getIcon("history"),
                                     tr("Rewatch — reset progress (completed titles).")));
  legOuter->addWidget(makeLegendItem(m_buttonLegendHost, theme.getIcon("add_box"),
                                     tr("Planning — add a related title to Planning.")));
  root->addWidget(m_buttonLegendHost);

  m_fetchStripScroll = new QScrollArea(this);
  m_fetchStripScroll->setMaximumHeight(120);
  m_fetchStripScroll->setWidgetResizable(true);
  m_fetchStripScroll->setFrameShape(QFrame::StyledPanel);
  m_fetchStripScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* fetchOuter = new QWidget(m_fetchStripScroll);
  auto* fetchRootLay = new QVBoxLayout(fetchOuter);
  fetchRootLay->setContentsMargins(8, 6, 8, 6);
  fetchRootLay->setSpacing(4);
  m_fetchStripTitle = new QLabel(tr("AniList media requests (this dialog)"), fetchOuter);
  m_fetchStripTitle->setStyleSheet(QStringLiteral("QLabel{font-weight:600; font-size:11px;}"));
  fetchRootLay->addWidget(m_fetchStripTitle);
  m_fetchStripPlaceholder = new QLabel(
      tr("No requests yet. Related titles will appear here as they are fetched."), fetchOuter);
  m_fetchStripPlaceholder->setStyleSheet(
      QStringLiteral("QLabel{color: palette(placeholderText); font-size:11px;}"));
  m_fetchStripPlaceholder->setWordWrap(true);
  fetchRootLay->addWidget(m_fetchStripPlaceholder);
  m_fetchRowsLayout = new QVBoxLayout();
  m_fetchRowsLayout->setContentsMargins(0, 0, 0, 0);
  m_fetchRowsLayout->setSpacing(2);
  fetchRootLay->addLayout(m_fetchRowsLayout);
  m_fetchStripScroll->setWidget(fetchOuter);
  root->addWidget(m_fetchStripScroll);

  connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
    if (isModal())
      accept();
    else
      close();
  });
  connect(m_randomBtn, &QPushButton::clicked, this, &WatchNextDialog::randomizeFromPlanning);
  connect(m_addAllWatchingBtn, &QPushButton::clicked, this, &WatchNextDialog::commitAddAllWatching);
  connect(m_addAllPlanningBtn, &QPushButton::clicked, this, &WatchNextDialog::commitAddAllPlanning);

  m_randomizeSettleTimer = new QTimer(this);
  m_randomizeSettleTimer->setSingleShot(true);
  m_randomizeSettleTimer->setInterval(450);
  connect(m_randomizeSettleTimer, &QTimer::timeout, this,
          &WatchNextDialog::applyRandomizeEnableIfSettled);

  m_rebuildTimer = new QTimer(this);
  m_rebuildTimer->setSingleShot(true);
  m_rebuildTimer->setInterval(120);
  connect(m_rebuildTimer, &QTimer::timeout, this, [this]() {
    const QSet<int> before(m_displayIds.cbegin(), m_displayIds.cend());
    recomputeClosure();
    const QSet<int> after(m_displayIds.cbegin(), m_displayIds.cend());
    // Prevent flicker: while relations are still loading, never shrink the visible set.
    // (It’s OK to show a partial set that only grows.)
    bool after_covers_before = true;
    for (const int x : before) {
      if (!after.contains(x)) {
        after_covers_before = false;
        break;
      }
    }
    if (!after_covers_before) {
      m_displayIds = QVector<int>(before.cbegin(), before.cend());
    }
    rebuildCards();
  });

  connect(&anime::db, &anime::Database::itemUpdated, this, [this](const int id) {
    if (m_seedId <= 0) return;
    if (!m_watchClosureIds.contains(id)) return;
    scheduleRecomputeAndRebuild();
  });
  connect(&anime::db, &anime::Database::entryUpdated, this, [this](const int id) {
    if (m_seedId <= 0) return;
    if (!m_watchClosureIds.contains(id)) return;
    scheduleRecomputeAndRebuild();
  });
  connect(&imageProvider, &ImageProvider::posterChanged, this, [this](const int id) {
    if (m_seedId <= 0) return;
    if (!m_watchClosureIds.contains(id)) return;
    scheduleRecomputeAndRebuild();
  });

  auto* anilist = sync::anilist::Service::instance();
  connect(anilist, &sync::anilist::Service::mediaFetchQueued, this,
          [this](const int id) { onAnilistMediaFetchQueued(id); });
  connect(anilist, &sync::anilist::Service::mediaFetchStarted, this,
          [this](const int id) { onAnilistMediaFetchStarted(id); });
  connect(anilist, &sync::anilist::Service::mediaFetchFinished, this,
          [this](const int id, const bool ok) { onAnilistMediaFetchFinished(id, ok); });
}

void WatchNextDialog::runModalRandomPlanningSession() {
  m_sessionKind = SessionKind::ModalRandomPlanning;
  setModal(true);
  setWindowTitle(tr("What to watch next"));
  randomizeFromPlanning();
  updateRandomizeEnabled();
}

void WatchNextDialog::presentModelessGuideForAnime(const int anime_id) {
  m_sessionKind = SessionKind::ModelessGuide;
  setModal(false);
  setWindowFlag(Qt::Window, true);
  setWindowTitle(tr("Watch order guide"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  if (m_closeBtn) m_closeBtn->show();
  if (m_fetchStripScroll) {
    m_fetchStripScroll->show();
    m_fetchStripScroll->setMaximumHeight(120);
    m_fetchStripScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  }
  if (anime_id > 0) setSeed(anime_id);
}

void WatchNextDialog::applyEmbeddedPanelChrome() {
  if (m_embeddedChromeApplied) return;
  m_embeddedChromeApplied = true;

  if (m_fetchStripScroll) {
    m_fetchStripScroll->hide();
    m_fetchStripScroll->setMaximumHeight(0);
    m_fetchStripScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  }

  if (m_toolFrame) {
    m_toolFrame->hide();
    m_toolFrame->setMaximumHeight(0);
    m_toolFrame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  }

  if (m_subHeader) {
    m_subHeader->setMaximumHeight(QWIDGETSIZE_MAX);
    m_subHeader->setWordWrap(true);
    m_subHeader->setText(
        tr("Franchise watch order for the pinned list entry. Related seasons appear below in "
           "suggested order."));
  }

  auto* root = qobject_cast<QVBoxLayout*>(layout());
  if (root && m_toolFrame && m_cardsHost && m_buttonLegendHost) {
    root->removeWidget(m_cardsHost);
    root->removeWidget(m_buttonLegendHost);
    const int ti = root->indexOf(m_toolFrame);
    if (ti >= 0) {
      root->insertWidget(ti + 1, m_buttonLegendHost);
      root->insertWidget(ti + 2, m_cardsHost);
    } else {
      root->addWidget(m_buttonLegendHost);
      root->addWidget(m_cardsHost);
    }
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
  }

  if (m_buttonLegendHost) {
    if (QLayout* oldLay = m_buttonLegendHost->layout()) {
      QLayoutItem* it = nullptr;
      while ((it = oldLay->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
      }
      delete oldLay;
    }
    auto* hl = new QHBoxLayout(m_buttonLegendHost);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);
    hl->addWidget(makeCompactLegendItem(m_buttonLegendHost, theme.getIcon("skip_next"),
                                        tr("Start or continue watching.")),
                  0, Qt::AlignLeft);
    hl->addWidget(makeCompactLegendItem(m_buttonLegendHost, theme.getIcon("history"),
                                        tr("Rewatch completed titles.")),
                  0, Qt::AlignLeft);
    hl->addWidget(makeCompactLegendItem(m_buttonLegendHost, theme.getIcon("add_box"),
                                        tr("Add related title to Planning.")),
                  0, Qt::AlignLeft);
    hl->addStretch(1);
  }
}

void WatchNextDialog::presentEmbeddedGuideForAnime(const int anime_id) {
  m_sessionKind = SessionKind::EmbeddedGuide;
  setModal(false);
  setWindowFlags(Qt::Widget);
  setWindowTitle(tr("Watch order"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  if (m_closeBtn) m_closeBtn->hide();
  applyEmbeddedPanelChrome();
  setMinimumHeight(200);
  resize(800, 420);
  setSeed(anime_id);
}

bool WatchNextDialog::didChangeList() const {
  return m_didChange;
}

void WatchNextDialog::randomizeFromPlanning() {
  QVector<int> candidates;
  candidates.reserve(anime::db.entries().size());

  for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
    const ListEntry& e = it.value();
    if (e.status == anime::list::Status::PlanToWatch) candidates.push_back(e.anime_id);
  }

  if (candidates.isEmpty()) {
    m_seedId = 0;
    m_alternativesOpenForId = 0;
    m_lastFetchAttemptMs.clear();
    m_fetchRetryCount.clear();
    clearAnilistFetchRows();
    m_header->setText(tr("<b>What to watch next</b>"));
    m_subHeader->setText(tr("No titles in Planning yet."));
    m_displayIds.clear();
    rebuildCards();
    repopulatePlanningCombo();
    return;
  }

  // Try a few times to avoid picking the same seed again.
  int pick = candidates.at(QRandomGenerator::global()->bounded(candidates.size()));
  if (candidates.size() > 1) {
    for (int i = 0; i < 4 && pick == m_seedId; ++i) {
      pick = candidates.at(QRandomGenerator::global()->bounded(candidates.size()));
    }
  }

  setSeed(pick);
}

void WatchNextDialog::updateRandomizeEnabled() {
  if (!m_randomBtn) return;
  if (m_sessionKind == SessionKind::EmbeddedGuide) return;
  if (m_seedId <= 0) {
    if (m_randomizeSettleTimer) m_randomizeSettleTimer->stop();
    m_randomBtn->setEnabled(true);
    m_randomBtn->setToolTip({});
    m_randomBtn->setText(tr("Randomize"));
    m_randomBtn->setIcon(theme.getIcon("shuffle"));
    return;
  }
  for (const int id : m_anilistFetchOrder) {
    const auto it = m_anilistRowState.constFind(id);
    if (it == m_anilistRowState.cend() || it.value() == AnilistRowState::Queued ||
        it.value() == AnilistRowState::Loading) {
      if (m_randomizeSettleTimer) m_randomizeSettleTimer->stop();
      m_randomBtn->setEnabled(false);
      m_randomBtn->setToolTip(tr("Wait until AniList finishes loading related titles."));
      m_randomBtn->setText(tr("Randomize…"));
      m_randomBtn->setIcon(theme.getIcon("shuffle"));
      return;
    }
  }
  // Every tracked request is Done/Failed — wait briefly so a follow-up queued fetch (same tick)
  // does not flash the button on then off.
  if (m_anilistFetchOrder.isEmpty()) {
    if (m_randomizeSettleTimer) m_randomizeSettleTimer->stop();
    m_randomBtn->setEnabled(true);
    m_randomBtn->setToolTip({});
    m_randomBtn->setText(tr("Randomize"));
    m_randomBtn->setIcon(theme.getIcon("shuffle"));
    return;
  }
  if (m_randomizeSettleTimer) m_randomizeSettleTimer->start();
}

void WatchNextDialog::applyRandomizeEnableIfSettled() {
  if (!m_randomBtn || m_seedId <= 0) return;
  if (m_sessionKind == SessionKind::EmbeddedGuide) return;
  for (const int id : m_anilistFetchOrder) {
    const auto it = m_anilistRowState.constFind(id);
    if (it == m_anilistRowState.cend() || it.value() == AnilistRowState::Queued ||
        it.value() == AnilistRowState::Loading) {
      m_randomBtn->setEnabled(false);
      m_randomBtn->setToolTip(tr("Wait until AniList finishes loading related titles."));
      m_randomBtn->setText(tr("Randomize…"));
      m_randomBtn->setIcon(theme.getIcon("shuffle"));
      return;
    }
  }
  m_randomBtn->setEnabled(true);
  m_randomBtn->setToolTip({});
  m_randomBtn->setText(tr("Randomize"));
  m_randomBtn->setIcon(theme.getIcon("shuffle"));
}

void WatchNextDialog::clearAnilistFetchRows() {
  m_anilistFetchOrder.clear();
  m_anilistFetchLabels.clear();
  m_anilistRowState.clear();
  if (!m_fetchRowsLayout) return;
  while (m_fetchRowsLayout->count() > 0) {
    QLayoutItem* it = m_fetchRowsLayout->takeAt(0);
    if (QWidget* w = it->widget()) w->deleteLater();
    delete it;
  }
  if (m_fetchStripPlaceholder) m_fetchStripPlaceholder->setVisible(true);
  if (m_randomizeSettleTimer) m_randomizeSettleTimer->stop();
  updateRandomizeEnabled();
}

void WatchNextDialog::refreshAnilistFetchRow(const int id) {
  if (id <= 0 || !m_fetchRowsLayout || !m_fetchStripTitle) return;
  const auto stIt = m_anilistRowState.constFind(id);
  if (stIt == m_anilistRowState.cend()) return;

  QString statusHtml;
  switch (stIt.value()) {
    case AnilistRowState::Queued:
      statusHtml = QStringLiteral("<span style=\"color:#b8860b;\">%1</span>")
                       .arg(tr("Queued").toHtmlEscaped());
      break;
    case AnilistRowState::Loading:
      statusHtml = QStringLiteral("<span style=\"color:#1565c0;\">%1</span>")
                       .arg(tr("Loading…").toHtmlEscaped());
      break;
    case AnilistRowState::Done:
      statusHtml = QStringLiteral("<span style=\"color:#2e7d32;\">%1</span>")
                       .arg(tr("Done").toHtmlEscaped());
      break;
    case AnilistRowState::Failed:
      statusHtml = QStringLiteral("<span style=\"color:#c62828;\">%1</span>")
                       .arg(tr("Failed").toHtmlEscaped());
      break;
  }

  const QString title = animeDisplayTitle(anime::db.item(id), id).toHtmlEscaped();
  const QString html = QStringLiteral("#%1 %2 — %3").arg(id).arg(title).arg(statusHtml);

  QLabel* lab = m_anilistFetchLabels.value(id);
  if (!lab) {
    QWidget* parentW = m_fetchStripTitle->parentWidget();
    lab = new QLabel(parentW);
    lab->setTextFormat(Qt::RichText);
    lab->setWordWrap(true);
    lab->setStyleSheet(QStringLiteral("QLabel{font-size:11px;}"));
    const int pos = m_anilistFetchOrder.indexOf(id);
    m_fetchRowsLayout->insertWidget(qMax(0, pos), lab);
    m_anilistFetchLabels.insert(id, lab);
    if (m_fetchStripPlaceholder) m_fetchStripPlaceholder->setVisible(false);
  }
  lab->setText(html);
}

void WatchNextDialog::applyPendingScrollRestore() {
  if (!m_cardsHost) return;
  if (QWidget* fw = QApplication::focusWidget(); fw && m_cardsHost->isAncestorOf(fw)) {
    m_cardsHost->setFocus(Qt::OtherFocusReason);
  }
}

void WatchNextDialog::syncCardsHostGeometry() {
  if (!m_cardsHost) return;
  m_cardsHost->updateGeometry();
}

void WatchNextDialog::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  QTimer::singleShot(0, this, [this]() { syncCardsHostGeometry(); });
}

void WatchNextDialog::closeEvent(QCloseEvent* event) {
  if (m_sessionKind == SessionKind::EmbeddedGuide) {
    event->ignore();
    return;
  }
  if (!isModal() && m_didChange) {
    m_didChange = false;
    emit listChangeCommitted();
  }
  QDialog::closeEvent(event);
}

void WatchNextDialog::keyPressEvent(QKeyEvent* event) {
  if (m_sessionKind == SessionKind::EmbeddedGuide && event->key() == Qt::Key_Escape) {
    event->ignore();
    return;
  }
  QDialog::keyPressEvent(event);
}

void WatchNextDialog::repopulatePlanningCombo() {
  if (!m_planningCombo) return;
  if (m_sessionKind == SessionKind::EmbeddedGuide) return;
  const QSignalBlocker blocker(m_planningCombo);
  m_planningCombo->clear();
  m_planningCombo->addItem(tr("(pick a Planning title)"), 0);

  struct Row {
    QString title;
    int id = 0;
  };
  QVector<Row> rows;
  rows.reserve(static_cast<int>(anime::db.entries().size()));
  for (auto it = anime::db.entries().cbegin(); it != anime::db.entries().cend(); ++it) {
    const ListEntry& e = it.value();
    if (e.status != anime::list::Status::PlanToWatch) continue;
    const int aid = e.anime_id;
    if (const Anime* a = anime::db.item(aid)) {
      rows.push_back({animeDisplayTitle(a, aid), aid});
    } else {
      rows.push_back({tr("#%1").arg(aid), aid});
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
  });
  for (const Row& r : rows) {
    m_planningCombo->addItem(r.title, r.id);
  }

  if (m_seedId > 0) {
    const int ix = m_planningCombo->findData(m_seedId);
    m_planningCombo->setCurrentIndex(ix >= 0 ? ix : 0);
  } else {
    m_planningCombo->setCurrentIndex(0);
  }
}

void WatchNextDialog::onAnilistMediaFetchQueued(const int id) {
  if (m_seedId <= 0 || !m_watchClosureIds.contains(id)) return;
  if (!m_anilistFetchOrder.contains(id)) m_anilistFetchOrder.push_back(id);
  m_anilistRowState[id] = AnilistRowState::Queued;
  refreshAnilistFetchRow(id);
  updateRandomizeEnabled();
}

void WatchNextDialog::onAnilistMediaFetchStarted(const int id) {
  if (m_seedId <= 0 || !m_watchClosureIds.contains(id)) return;
  m_anilistRowState[id] = AnilistRowState::Loading;
  refreshAnilistFetchRow(id);
  updateRandomizeEnabled();
}

void WatchNextDialog::onAnilistMediaFetchFinished(const int id, const bool ok) {
  if (m_seedId <= 0 || !m_watchClosureIds.contains(id)) return;
  m_anilistRowState[id] = ok ? AnilistRowState::Done : AnilistRowState::Failed;
  refreshAnilistFetchRow(id);
  updateRandomizeEnabled();
}

void WatchNextDialog::setSeed(const int id) {
  if (id <= 0) {
    m_seedId = 0;
    m_relationTypeFromSeed.clear();
    m_fetchedIds.clear();
    m_lastFetchAttemptMs.clear();
    m_fetchRetryCount.clear();
    m_watchClosureIds.clear();
    m_alternativesOpenForId = 0;
    clearAnilistFetchRows();
    m_displayIds.clear();
    if (m_sessionKind == SessionKind::EmbeddedGuide) {
      m_header->setText(tr("<b>Watch order</b>"));
      m_subHeader->setText(
          tr("Pin a title from the anime list toolbar to show its watch order graph here."));
    } else {
      m_header->setText(tr("<b>What to watch next</b>"));
    }
    rebuildCards();
    scheduleRecomputeAndRebuild();
    if (m_sessionKind != SessionKind::EmbeddedGuide) repopulatePlanningCombo();
    updateBulkActionButtons();
    return;
  }

  m_seedId = id;
  m_relationTypeFromSeed.clear();
  m_fetchedIds.clear();
  m_lastFetchAttemptMs.clear();
  m_fetchRetryCount.clear();
  m_watchClosureIds.clear();
  m_alternativesOpenForId = 0;
  clearAnilistFetchRows();

  if (const Anime* a = anime::db.item(m_seedId)) {
    const QString title = animeDisplayTitle(a, m_seedId);
    if (m_sessionKind == SessionKind::ModelessGuide ||
        m_sessionKind == SessionKind::EmbeddedGuide) {
      m_header->setText(tr("<b>Watch order:</b> %1").arg(title.toHtmlEscaped()));
    } else {
      m_header->setText(tr("<b>Recommended:</b> %1").arg(title.toHtmlEscaped()));
    }
  } else {
    m_header->setText(tr("<b>Recommended:</b> #%1").arg(id));
  }

  // Show a stable initial view immediately, then expand as relations load.
  m_displayIds = {m_seedId};
  rebuildCards();
  scheduleRecomputeAndRebuild();
  if (m_sessionKind != SessionKind::EmbeddedGuide) repopulatePlanningCombo();
}

void WatchNextDialog::ensureFetched(const int id) {
  if (id <= 0) return;
  m_watchClosureIds.insert(id);

  const Anime* a = anime::db.item(id);
  const bool hasRelations = a && !a->relations.empty();

  if (m_fetchedIds.contains(id)) {
    if (hasRelations) return;
    // First request ran but relations still empty (rate limit, error, or race) — retry slowly.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastFetchAttemptMs.value(id, 0) < 5000) return;
    if (m_fetchRetryCount.value(id, 0) >= 8) return;
    m_fetchRetryCount[id] = m_fetchRetryCount.value(id, 0) + 1;
    m_lastFetchAttemptMs[id] = now;
    sync::anilist::Service::instance()->fetchAnime(id);
    imageProvider.fetchPoster(id);
    return;
  }

  m_fetchedIds.insert(id);
  m_lastFetchAttemptMs[id] = QDateTime::currentMSecsSinceEpoch();
  sync::anilist::Service::instance()->fetchAnime(id);
  imageProvider.fetchPoster(id);
}

void WatchNextDialog::scheduleRecomputeAndRebuild() {
  if (!m_rebuildTimer) return;
  m_rebuildTimer->start();
}

void WatchNextDialog::recomputeClosure() {
  m_displayIds.clear();
  if (m_seedId <= 0) return;

  // BFS over all franchise-related edges (no cap): every reachable id is fetched so prequel/sequel
  // chains are complete once AniList data is present.
  QSet<int> seen;
  QVector<int> q;
  q.push_back(m_seedId);
  seen.insert(m_seedId);

  while (!q.isEmpty()) {
    const int curId = q.front();
    q.pop_front();

    m_displayIds.push_back(curId);
    ensureFetched(curId);

    const Anime* a = anime::db.item(curId);
    if (!a) continue;

    for (const auto& rel : a->relations) {
      if (rel.related_id <= 0) continue;
      if (!isClosureExpansionRelation(rel.type)) continue;
      if (seen.contains(rel.related_id)) continue;
      seen.insert(rel.related_id);
      q.push_back(rel.related_id);

      if (curId == m_seedId) {
        m_relationTypeFromSeed.insert(rel.related_id, rel.type);
      }
    }
  }
}

void WatchNextDialog::rebuildCards() {
  if (!m_cardsHost || !m_cardsLayout) return;

  m_cardsHost->setUpdatesEnabled(false);
  clearVBoxLayoutWidgets(m_cardsLayout);

  if (m_seedId <= 0) {
    const QString emptyText =
        m_sessionKind == SessionKind::EmbeddedGuide
            ? tr("Select a title in the list, then use “Pin watch order graph” on the toolbar.")
            : tr("Add some titles to Planning to use this feature.");
    auto* empty = new QLabel(emptyText, m_cardsHost);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    empty->setWordWrap(true);
    m_cardsLayout->addWidget(empty);
    m_cardsHost->setUpdatesEnabled(true);
    QTimer::singleShot(0, this, [this]() {
      syncCardsHostGeometry();
      applyPendingScrollRestore();
    });
    if (m_fetchStripScroll && m_sessionKind != SessionKind::EmbeddedGuide) {
      m_fetchStripScroll->setVisible(false);
    }
    updateSubHeaderHint();
    updateBulkActionButtons();
    return;
  }

  const QSet<int> idSet(m_displayIds.cbegin(), m_displayIds.cend());
  const QVector<int> flow = buildSeriesFlow(idSet, m_seedId);
  const QSet<int> flowSet(flow.cbegin(), flow.cend());
  if (m_alternativesOpenForId != 0 && !flowSet.contains(m_alternativesOpenForId)) {
    m_alternativesOpenForId = 0;
  }

  const QString seedFlowBadge =
      (m_sessionKind == SessionKind::ModelessGuide || m_sessionKind == SessionKind::EmbeddedGuide)
          ? tr("Starting point")
          : tr("Picked from Planning");
  const auto badgeForSpine = [&](int mid) -> QString {
    if (mid == m_seedId) return seedFlowBadge;
    const int si = flow.indexOf(m_seedId);
    const int ii = flow.indexOf(mid);
    if (ii < 0 || si < 0) return {};
    if (ii < si) return tr("Prequel");
    if (ii > si) return tr("Sequel");
    return {};
  };

  m_cardsLayout->addWidget(buildMainTimelineRow(flow, idSet, flowSet, badgeForSpine));

  QVector<int> orphans;
  orphans.reserve(m_displayIds.size());
  for (const int id : m_displayIds) {
    if (flowSet.contains(id)) continue;
    if (isAlternativeNeighborOfFlow(id, flowSet, idSet)) continue;
    orphans.push_back(id);
  }
  sortIdsByStartDate(orphans);
  for (const int oid : orphans) {
    m_cardsLayout->addWidget(buildRowWithAlternatives(oid, {}, tr("Related")));
  }

  m_cardsHost->setUpdatesEnabled(true);
  QTimer::singleShot(0, this, [this]() {
    syncCardsHostGeometry();
    applyPendingScrollRestore();
  });

  if (m_fetchStripScroll && m_sessionKind != SessionKind::EmbeddedGuide) {
    m_fetchStripScroll->setVisible(m_seedId > 0);
  }

  updateSubHeaderHint();
  updateRandomizeEnabled();
  updateBulkActionButtons();
}

void WatchNextDialog::updateSubHeaderHint() {
  if (m_seedId <= 0) return;
  if (m_displayIds.size() <= 1) {
    const Anime* a = anime::db.item(m_seedId);
    if (a && a->relations.empty()) {
      m_subHeader->setText(tr("No related titles found on AniList for this entry."));
    } else {
      m_subHeader->setText(tr("Loading related titles from AniList…"));
    }
  } else {
    if (m_sessionKind == SessionKind::ModelessGuide) {
      m_subHeader->setText(
          tr("Suggested order uses AniList prequel/sequel links and start dates. "
             "Use Start from Planning below to switch the starting title, or Randomize for another "
             "pick."));
    } else if (m_sessionKind == SessionKind::EmbeddedGuide) {
      m_subHeader->setText(
          tr("Suggested order uses AniList prequel/sequel links and start dates. "
             "Pin another title from the anime list to change the starting entry."));
    } else {
      m_subHeader->setText(
          tr("Pick a random title from Planning, then review all related seasons/releases."));
    }
  }
}

QWidget* WatchNextDialog::buildRowWithAlternatives(const int main_id, const QVector<int>& alt_ids,
                                                   const QString& main_badge) {
  if (alt_ids.isEmpty()) {
    return buildCard(main_id, false, main_badge);
  }

  auto* row = new QWidget(m_cardsHost);
  auto* hl = new QHBoxLayout(row);
  hl->setContentsMargins(0, 0, 0, 0);
  hl->setSpacing(10);

  hl->addWidget(buildCard(main_id, false, main_badge), 1);

  auto* altCol = new QWidget(row);
  auto* vl = new QVBoxLayout(altCol);
  vl->setContentsMargins(0, 0, 0, 0);
  vl->setSpacing(8);
  auto* altHeader = new QLabel(tr("Alternatives"), altCol);
  altHeader->setStyleSheet(
      QStringLiteral("QLabel{color: palette(placeholderText); font-weight:600;}"));
  vl->addWidget(altHeader);
  for (const int aid : alt_ids) {
    vl->addWidget(buildCard(aid, true, tr("Alternative")));
  }
  vl->addStretch(1);
  hl->addWidget(altCol, 0, Qt::AlignTop);

  return row;
}

QWidget* WatchNextDialog::buildMainTimelineRow(const QVector<int>& flow, const QSet<int>& idSet,
                                               const QSet<int>& flowSet,
                                               const std::function<QString(int)>& badge_for_spine) {
  auto* outer = new QWidget(m_cardsHost);
  auto* rootLay = new QVBoxLayout(outer);
  rootLay->setContentsMargins(0, 0, 0, 0);
  rootLay->setSpacing(4);

  auto* scroll = new QScrollArea(outer);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidgetResizable(false);

  auto* inner = new QWidget(scroll);
  inner->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
  auto* hl = new QHBoxLayout(inner);
  hl->setContentsMargins(2, 4, 2, 4);
  hl->setSpacing(10);

  for (int i = 0; i < flow.size(); ++i) {
    const int mainId = flow[i];

    auto* cell = new QWidget(inner);
    cell->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* cellLay = new QHBoxLayout(cell);
    cellLay->setContentsMargins(0, 0, 0, 0);
    cellLay->setSpacing(6);
    cellLay->addWidget(buildCard(mainId, false, badge_for_spine(mainId)), 1);

    const QVector<int> altsForCard = alternativesFromAnchor(mainId, idSet);
    if (!altsForCard.isEmpty()) {
      auto* altBtn = new QToolButton(cell);
      altBtn->setIcon(theme.getIcon("arrow_drop_down"));
      altBtn->setToolTip(tr("Show or hide alternative versions for this title"));
      altBtn->setCheckable(true);
      altBtn->setChecked(m_alternativesOpenForId == mainId);
      connect(altBtn, &QToolButton::clicked, this, [this, mainId]() {
        if (m_alternativesOpenForId == mainId) {
          m_alternativesOpenForId = 0;
        } else {
          m_alternativesOpenForId = mainId;
        }
        rebuildCards();
      });
      cellLay->addWidget(altBtn, 0, Qt::AlignVCenter);
    }

    hl->addWidget(cell, 0, Qt::AlignVCenter);
    if (i + 1 < flow.size()) {
      auto* ar = new QLabel(QStringLiteral("→"), inner);
      ar->setStyleSheet(
          QStringLiteral("QLabel{color: palette(placeholderText); font-size: 15pt;}"));
      ar->setAlignment(Qt::AlignCenter);
      ar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
      hl->addWidget(ar, 0, Qt::AlignVCenter);
    }
  }
  scroll->setWidget(inner);
  inner->adjustSize();
  {
    const int h = inner->sizeHint().height();
    const int stripH = std::max(h, 96);
    scroll->setMinimumHeight(stripH);
    scroll->setMaximumHeight(stripH);
  }
  rootLay->addWidget(scroll);

  if (m_alternativesOpenForId != 0) {
    const QVector<int> panelAlts = alternativesFromAnchor(m_alternativesOpenForId, idSet);
    if (!panelAlts.isEmpty()) {
      auto* panel = new QFrame(outer);
      panel->setObjectName(QStringLiteral("watchNextAltPanel"));
      panel->setStyleSheet(QStringLiteral(
          "QFrame#watchNextAltPanel{border:1px solid palette(mid); border-radius:8px; background: "
          "palette(alternate-base);}"));
      auto* pl = new QVBoxLayout(panel);
      pl->setContentsMargins(8, 6, 8, 6);
      pl->setSpacing(4);
      QString t =
          animeDisplayTitle(anime::db.item(m_alternativesOpenForId), m_alternativesOpenForId);
      auto* hdr = new QLabel(panel);
      hdr->setStyleSheet(QStringLiteral("QLabel{font-weight:600; font-size:12px;}"));
      hdr->setWordWrap(false);
      hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      {
        const QFontMetrics fm(hdr->font());
        const int dlgW = std::max(0, width());
        const int hdrMaxW = std::max(160, dlgW - 80);
        const QString full = tr("Alternatives: %1").arg(t);
        hdr->setText(fm.elidedText(full, Qt::ElideRight, hdrMaxW));
        hdr->setFixedHeight(fm.height() + 2);
        hdr->setToolTip(full);
      }
      pl->addWidget(hdr);
      auto* ph = new QHBoxLayout();
      ph->setContentsMargins(0, 0, 0, 0);
      ph->setSpacing(8);
      for (const int aid : panelAlts) {
        ph->addWidget(buildCard(aid, true, tr("Alternative")));
      }
      ph->addStretch(1);
      pl->addLayout(ph);
      rootLay->addWidget(panel);
    }
  }

  return outer;
}

QWidget* WatchNextDialog::buildCard(const int id, const bool compact,
                                    const QString& relation_badge) {
  auto* card = new QFrame(m_cardsHost);
  card->setObjectName("watchNextCard");
  if (compact) {
    card->setFixedWidth(228);
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
  } else {
    card->setMinimumWidth(200);
  }
  card->setStyleSheet(
      QStringLiteral("QFrame#watchNextCard{border:1px solid palette(mid); border-radius:10px; "
                     "background: palette(base);}"
                     "QFrame#watchNextCard:hover{border-color: palette(highlight);}"));

  auto* hl = new QHBoxLayout(card);
  hl->setContentsMargins(compact ? 6 : 8, compact ? 6 : 8, compact ? 6 : 8, compact ? 6 : 8);
  hl->setSpacing(compact ? 6 : 8);

  const int pw = compact ? 40 : 56;
  const int ph = compact ? 60 : 78;

  auto* poster = new ClickableLabel(card);
  poster->setFixedSize(pw, ph);
  poster->setScaledContents(true);
  poster->setStyleSheet(
      QStringLiteral("ClickableLabel{border-radius:6px; background: palette(alternate-base);}"));
  poster->setCursor(Qt::PointingHandCursor);
  poster->setToolTip(tr("Open AniList page"));

  if (const QPixmap* px = imageProvider.loadPoster(id)) {
    poster->setPixmap(*px);
  } else {
    poster->setText(QStringLiteral("…"));
    poster->setAlignment(Qt::AlignCenter);
  }

  connect(poster, &ClickableLabel::clicked, this, [id](Qt::MouseButton button) {
    if (button != Qt::LeftButton) return;
    QDesktopServices::openUrl(QUrl(sync::animePageUrl(id)));
  });

  hl->addWidget(poster);

  auto* vl = new QVBoxLayout();
  vl->setContentsMargins(0, 0, 0, 0);
  vl->setSpacing(2);

  const Anime* a = anime::db.item(id);
  QString title = animeDisplayTitle(a, id);
  QString meta;

  if (a) {
    const QString fmt = formatLabel(a->type);
    const QString eps = a->episode_count > 0 ? tr("%1 eps").arg(a->episode_count) : tr("?");
    meta = fmt.isEmpty() ? eps : tr("%1 • %2").arg(fmt, eps);
  }

  QString rel = relation_badge;
  if (rel.isEmpty()) {
    if (id == m_seedId) {
      rel = tr("Picked from Planning");
    } else if (m_relationTypeFromSeed.contains(id)) {
      const QString l = relationTypeLabel(m_relationTypeFromSeed.value(id));
      if (!l.isEmpty()) rel = l;
    }
  }

  auto* titleLabel = new ClickableLabel(card);
  titleLabel->setText(title);
  titleLabel->setWordWrap(true);
  if (compact) {
    titleLabel->setStyleSheet(
        QStringLiteral("ClickableLabel{font-weight:600; font-size:11px; color: palette(link); "
                       "text-decoration: underline;}"));
  } else {
    titleLabel->setStyleSheet(QStringLiteral(
        "ClickableLabel{font-weight:600; color: palette(link); text-decoration: underline;}"));
  }
  titleLabel->setCursor(Qt::PointingHandCursor);
  titleLabel->setToolTip(tr("Open AniList page"));
  connect(titleLabel, &ClickableLabel::clicked, this, [id](Qt::MouseButton button) {
    if (button != Qt::LeftButton) return;
    QDesktopServices::openUrl(QUrl(sync::animePageUrl(id)));
  });
  vl->addWidget(titleLabel);

  if (!meta.isEmpty()) {
    auto* metaLabel = new QLabel(meta, card);
    metaLabel->setStyleSheet(
        QStringLiteral("QLabel{color: palette(placeholderText); font-size:%1px;}")
            .arg(compact ? 9 : 10));
    vl->addWidget(metaLabel);
  }

  const ListEntry* e = anime::db.entry(id);
  const auto listStatus = e ? e->status : anime::list::Status::NotInList;

  if (!compact) {
    auto* statusLabel = new QLabel(tr("List: %1").arg(listStatusLabel(listStatus)), card);
    statusLabel->setStyleSheet(
        QStringLiteral("QLabel{color: palette(placeholderText); font-size:10px;}"));
    vl->addWidget(statusLabel);
  }

  if (!rel.isEmpty() && !(compact && relation_badge == tr("Alternative"))) {
    auto* relLabel = new QLabel(rel, card);
    relLabel->setStyleSheet(
        QStringLiteral("QLabel{color: palette(placeholderText); font-size:%1px;}")
            .arg(compact ? 9 : 11));
    vl->addWidget(relLabel);
  }

  if (!compact && listStatus == anime::list::Status::Completed) {
    auto* badge = new QLabel(tr("Completed"), card);
    badge->setStyleSheet(
        QStringLiteral("QLabel{padding:2px 8px; border-radius:9px; background: rgba(76,175,80,40); "
                       "color: palette(text);}"));
    vl->addWidget(badge, 0, Qt::AlignLeft);
  }

  if (!compact) vl->addStretch(1);
  hl->addLayout(vl, compact ? 0 : 1);

  auto* right = new QVBoxLayout();
  right->setContentsMargins(0, 0, 0, 0);
  right->setSpacing(compact ? 4 : 6);

  const bool isCompleted = listStatus == anime::list::Status::Completed;
  const bool isSeed = id == m_seedId;
  const bool alreadyWatching = listStatus == anime::list::Status::Watching && !isCompleted;
  const bool showWatchBtn = !alreadyWatching;

  const int iconPx = compact ? 18 : 24;
  const int btnSide = compact ? 32 : 40;

  QPushButton* watchBtn = nullptr;
  if (showWatchBtn) {
    watchBtn = new QPushButton(card);
    watchBtn->setIcon(theme.getIcon(isCompleted ? "history" : "skip_next"));
    watchBtn->setText(QString());
    watchBtn->setToolTip(isCompleted ? tr("Rewatch: move to Watching and reset progress.")
                                     : tr("Move to Watching: start or continue this title."));
    watchBtn->setIconSize(QSize(iconPx, iconPx));
    watchBtn->setFixedSize(btnSide, btnSide);
    watchBtn->setFocusPolicy(Qt::NoFocus);
    connect(watchBtn, &QPushButton::clicked, this, [this, id, isCompleted]() {
      commitSetStatus(id, anime::list::Status::Watching, /*force_rewatch=*/isCompleted);
    });
  }

  QPushButton* planBtn = nullptr;
  if (!isSeed && !isCompleted && listStatus != anime::list::Status::PlanToWatch) {
    planBtn = new QPushButton(card);
    planBtn->setIcon(theme.getIcon("add_box"));
    planBtn->setText(QString());
    planBtn->setToolTip(tr("Add to Planning."));
    planBtn->setIconSize(QSize(iconPx, iconPx));
    planBtn->setFixedSize(btnSide, btnSide);
    planBtn->setFocusPolicy(Qt::NoFocus);
    connect(planBtn, &QPushButton::clicked, this, [this, id]() {
      commitSetStatus(id, anime::list::Status::PlanToWatch, /*force_rewatch=*/false);
    });
  }

  if (compact && (watchBtn || planBtn)) {
    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(4);
    if (watchBtn) btnRow->addWidget(watchBtn, 0, Qt::AlignTop);
    if (planBtn) btnRow->addWidget(planBtn, 0, Qt::AlignTop);
    btnRow->addStretch(1);
    right->addLayout(btnRow);
  } else {
    if (watchBtn) right->addWidget(watchBtn);
    if (planBtn) right->addWidget(planBtn);
    if (!compact) right->addStretch(1);
  }

  hl->addLayout(right);

  return card;
}

void WatchNextDialog::commitSetStatus(const int id, const anime::list::Status status,
                                      const bool force_rewatch) {
  const Anime* a = anime::db.item(id);
  if (!a) return;

  ListEntry entry = anime::db.entry(id) ? *anime::db.entry(id) : ListEntry{};
  entry.anime_id = id;

  const std::time_t now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());

  if (status == anime::list::Status::Watching) {
    if (force_rewatch || entry.status == anime::list::Status::Completed) {
      entry.status = anime::list::Status::Watching;
      entry.rewatching = true;
      entry.watched_episodes = 0;
      entry.last_updated = now;
    } else {
      entry.status = anime::list::Status::Watching;
      entry.rewatching = false;
      entry.last_updated = now;
    }
  } else if (status == anime::list::Status::PlanToWatch) {
    entry.status = anime::list::Status::PlanToWatch;
    entry.rewatching = false;
    entry.watched_episodes = 0;
    entry.last_updated = now;
  } else {
    entry.status = status;
    entry.rewatching = false;
    entry.last_updated = now;
  }

  gui::commitListEntryLocalAndMaybeRemote(entry, this);
  m_didChange = true;
  if (m_sessionKind == SessionKind::EmbeddedGuide && !m_bulkListCommit) {
    emit listChangeCommitted();
  }
}

void WatchNextDialog::commitAddAllWatching() {
  if (m_displayIds.isEmpty()) return;
  m_bulkListCommit = true;
  for (const int id : m_displayIds) {
    const ListEntry* e = anime::db.entry(id);
    if (e && e->status == anime::list::Status::Completed) continue;
    if (e && e->status == anime::list::Status::Watching) continue;
    commitSetStatus(id, anime::list::Status::Watching, /*force_rewatch=*/false);
  }
  m_bulkListCommit = false;
  if (m_sessionKind == SessionKind::EmbeddedGuide && m_didChange) {
    emit listChangeCommitted();
  }
}

void WatchNextDialog::commitAddAllPlanning() {
  if (m_displayIds.isEmpty()) return;
  m_bulkListCommit = true;
  for (const int id : m_displayIds) {
    const ListEntry* e = anime::db.entry(id);
    if (e && e->status == anime::list::Status::PlanToWatch) continue;
    commitSetStatus(id, anime::list::Status::PlanToWatch, /*force_rewatch=*/false);
  }
  m_bulkListCommit = false;
  if (m_sessionKind == SessionKind::EmbeddedGuide && m_didChange) {
    emit listChangeCommitted();
  }
}

void WatchNextDialog::updateBulkActionButtons() {
  if (!m_addAllWatchingBtn || !m_addAllPlanningBtn) return;
  if (m_sessionKind == SessionKind::EmbeddedGuide) return;
  if (m_displayIds.isEmpty() || m_seedId <= 0) {
    m_addAllWatchingBtn->setEnabled(false);
    m_addAllPlanningBtn->setEnabled(false);
    return;
  }
  bool anyNeedWatching = false;
  bool anyNeedPlanning = false;
  for (const int id : m_displayIds) {
    const ListEntry* e = anime::db.entry(id);
    if (!anyNeedWatching) {
      if (e && e->status == anime::list::Status::Completed) {
        // skip
      } else if (e && e->status == anime::list::Status::Watching) {
        // skip
      } else {
        anyNeedWatching = true;
      }
    }
    if (!anyNeedPlanning) {
      if (!e || e->status != anime::list::Status::PlanToWatch) {
        anyNeedPlanning = true;
      }
    }
    if (anyNeedWatching && anyNeedPlanning) break;
  }
  m_addAllWatchingBtn->setEnabled(anyNeedWatching);
  m_addAllPlanningBtn->setEnabled(anyNeedPlanning);
}

}  // namespace gui
