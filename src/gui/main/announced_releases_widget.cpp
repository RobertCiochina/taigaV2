/**
 * Taiga
 */

#include "gui/main/announced_releases_widget.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "gui/main/main_window.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/ui_title.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/announced_releases.hpp"
#include "sync/service.hpp"
#include "taiga/session.hpp"

namespace gui {

namespace {

QString formatTypeLabel(const anime::Type t) {
  using anime::Type;
  switch (t) {
    case Type::Tv:
      return QApplication::translate("AnnouncedReleases", "TV");
    case Type::Movie:
      return QApplication::translate("AnnouncedReleases", "Movie");
    case Type::Ova:
      return QApplication::translate("AnnouncedReleases", "OVA");
    case Type::Ona:
      return QApplication::translate("AnnouncedReleases", "ONA");
    case Type::Special:
      return QApplication::translate("AnnouncedReleases", "Special");
    case Type::Music:
      return QApplication::translate("AnnouncedReleases", "Music");
    case Type::Unknown:
    default:
      return {};
  }
}

QString mediaStatusLabel(const anime::Status s) {
  using anime::Status;
  switch (s) {
    case Status::NotYetAired:
      return QApplication::translate("AnnouncedReleases", "Not yet aired");
    case Status::Airing:
      return QApplication::translate("AnnouncedReleases", "Airing");
    case Status::FinishedAiring:
      return QApplication::translate("AnnouncedReleases", "Finished");
    default:
      return {};
  }
}

}  // namespace

AnnouncedReleasesWidget::AnnouncedReleasesWidget(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(16, 16, 16, 16);
  outer->setSpacing(12);

  auto* hint = new QLabel(
      QApplication::translate(
          "AnnouncedReleases",
          "Shows anime that are <b>not yet aired</b> or <b>currently airing</b> when they are a "
          "<b>direct sequel</b> (on AniList) to something on your list as <b>Completed</b> or "
          "<b>Planning</b>. Synchronize and open media details so relations are cached."),
      this);
  hint->setWordWrap(true);
  hint->setTextFormat(Qt::RichText);
  outer->addWidget(hint);

  auto* top = new QHBoxLayout();
  auto* addAll = new QPushButton(QApplication::translate("AnnouncedReleases", "Add all to Planning"),
                                 this);
  addAll->setToolTip(
      QApplication::translate("AnnouncedReleases", "Adds every visible title to Planning on your list."));
  connect(addAll, &QPushButton::clicked, this, [this]() {
    const auto cands = anime::computeAnnouncedReleaseCandidates(
        taiga::session.announcedReleasesDismissedAnimeIds());
    QVector<int> ids;
    for (const auto& c : cands) {
      const Anime* a = anime::db.item(c.anime_id);
      if (!a) continue;
      const QString t = uiTitle(*a);
      if (!m_filter.trimmed().isEmpty() &&
          !t.contains(m_filter.trimmed(), Qt::CaseInsensitive)) {
        continue;
      }
      ids.push_back(c.anime_id);
    }
    if (ids.isEmpty()) {
      QMessageBox::information(this, QApplication::translate("AnnouncedReleases", "Announced releases"),
                               QApplication::translate("AnnouncedReleases", "Nothing to add."));
      return;
    }
    const auto answer = QMessageBox::question(
        this, QApplication::translate("AnnouncedReleases", "Add all to Planning"),
        QApplication::translate("AnnouncedReleases", "Add %1 title(s) to Planning?").arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) return;

    const std::time_t now = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
    for (const int aid : ids) {
      ListEntry e;
      e.anime_id = aid;
      e.status = anime::list::Status::PlanToWatch;
      e.watched_episodes = 0;
      e.last_updated = now;
      commitListEntryLocalAndMaybeRemote(e, this);
    }
    if (MainWindow* mw = mainWindow()) mw->refreshAnnouncedReleasesSurfaces();
  });
  top->addWidget(addAll);
  top->addStretch(1);
  outer->addLayout(top);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto* inner = new QWidget(scroll);
  m_rowsLayout = new QVBoxLayout(inner);
  m_rowsLayout->setContentsMargins(0, 0, 0, 0);
  m_rowsLayout->setSpacing(10);
  m_rowsLayout->addStretch(1);
  scroll->setWidget(inner);
  outer->addWidget(scroll, 1);
}

void AnnouncedReleasesWidget::applyToolbarTextFilter(const QString& text) {
  m_filter = text;
  rebuildRows();
}

void AnnouncedReleasesWidget::refresh() { rebuildRows(); }

void AnnouncedReleasesWidget::rebuildRows() {
  if (!m_rowsLayout) return;

  while (m_rowsLayout->count()) {
    QLayoutItem* it = m_rowsLayout->takeAt(0);
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }

  if (sync::currentServiceId() != sync::ServiceId::AniList) {
    auto* empty = new QLabel(
        QApplication::translate("AnnouncedReleases", "Announced releases require AniList as the active list service."),
        this);
    empty->setWordWrap(true);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    m_rowsLayout->addWidget(empty);
    m_rowsLayout->addStretch(1);
    return;
  }

  const auto cands =
      anime::computeAnnouncedReleaseCandidates(taiga::session.announcedReleasesDismissedAnimeIds());
  if (cands.isEmpty()) {
    auto* empty =
        new QLabel(QApplication::translate("AnnouncedReleases", "No matching titles right now."), this);
    empty->setWordWrap(true);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    m_rowsLayout->addWidget(empty);
    m_rowsLayout->addStretch(1);
    return;
  }

  int shown = 0;
  for (const auto& c : cands) {
    const Anime* a = anime::db.item(c.anime_id);
    const Anime* anchor = anime::db.item(c.anchor_anime_id);
    if (!a) continue;
    const QString title = uiTitle(*a);
    if (!m_filter.trimmed().isEmpty() && !title.contains(m_filter.trimmed(), Qt::CaseInsensitive)) {
      continue;
    }

    const QString anchorLine =
        anchor ? tr("From your list: %1").arg(uiTitle(*anchor)) : tr("From your list");

    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    auto* hl = new QHBoxLayout(frame);
    hl->setContentsMargins(10, 8, 10, 8);
    hl->setSpacing(10);

    auto* vl = new QVBoxLayout();
    auto* t1 = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), frame);
    t1->setTextFormat(Qt::RichText);
    t1->setWordWrap(true);
    const QString meta = QStringLiteral("%1 · %2")
                             .arg(mediaStatusLabel(a->status), formatTypeLabel(a->type));
    auto* t2 = new QLabel(meta, frame);
    t2->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText); font-size:11px;}"));
    auto* t3 = new QLabel(anchorLine, frame);
    t3->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText); font-size:11px;}"));
    t3->setWordWrap(true);
    vl->addWidget(t1);
    vl->addWidget(t2);
    vl->addWidget(t3);
    hl->addLayout(vl, 1);

    auto* addBtn = new QPushButton(tr("Add to Planning"), frame);
    const int aid = c.anime_id;
    connect(addBtn, &QPushButton::clicked, this, [this, aid]() {
      ListEntry e;
      e.anime_id = aid;
      e.status = anime::list::Status::PlanToWatch;
      e.watched_episodes = 0;
      e.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
      commitListEntryLocalAndMaybeRemote(e, this);
      if (MainWindow* mw = mainWindow()) mw->refreshAnnouncedReleasesSurfaces();
    });

    auto* woBtn = new QPushButton(tr("Watch order…"), frame);
    connect(woBtn, &QPushButton::clicked, this, [aid]() {
      if (MainWindow* mw = mainWindow()) mw->openWatchOrderGuideForAnime(aid);
    });

    auto* dismissBtn = new QPushButton(tr("Dismiss"), frame);
    dismissBtn->setStyleSheet(QStringLiteral("QPushButton{color: palette(placeholderText);}"));
    connect(dismissBtn, &QPushButton::clicked, this, [this, aid]() {
      const auto answer =
          QMessageBox::question(this, tr("Dismiss this title?"),
                                tr("Hide “%1” from Announced releases until it changes again?\n\n"
                                   "This does not change your anime list.")
                                    .arg(uiTitle(*anime::db.item(aid))),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) return;
      taiga::session.addAnnouncedReleaseDismissedAnimeId(aid);
      if (MainWindow* mw = mainWindow()) mw->refreshAnnouncedReleasesSurfaces();
    });

    auto* btnCol = new QVBoxLayout();
    btnCol->addWidget(addBtn);
    btnCol->addWidget(woBtn);
    btnCol->addWidget(dismissBtn);
    hl->addLayout(btnCol);

    m_rowsLayout->addWidget(frame);
    ++shown;
  }

  if (shown == 0) {
    auto* empty = new QLabel(
        QApplication::translate("AnnouncedReleases", "No titles match the filter."), this);
    empty->setStyleSheet(QStringLiteral("QLabel{color: palette(placeholderText);}"));
    m_rowsLayout->addWidget(empty);
  }
  m_rowsLayout->addStretch(1);
}

}  // namespace gui
