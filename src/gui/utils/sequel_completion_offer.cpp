/**
 * Taiga
 */

#include "sequel_completion_offer.hpp"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <algorithm>

#include "gui/main/main_window.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/ui_title.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "sync/anilist.hpp"
#include "sync/service.hpp"
#include "taiga/settings.hpp"

namespace gui {
namespace {

QString trOffer(const char* text) {
  return QApplication::translate("SequelCompletionOffer", text);
}

qint64 startDateKey(const Anime* a) {
  if (!a || a->date_started.empty()) return 0;
  return (static_cast<qint64>(static_cast<int>(a->date_started.year())) * 10000) +
         (static_cast<qint64>(static_cast<unsigned>(a->date_started.month())) * 100) +
         static_cast<qint64>(static_cast<unsigned>(a->date_started.day()));
}

bool sequelIsCompleted(const int sequelId) {
  const ListEntry* e = anime::db.entry(sequelId);
  return e && e->status == anime::list::Status::Completed;
}

struct NextSequelPick {
  int sequel_id = 0;
  bool waiting_for_media = false;
};

NextSequelPick pickNextSequel(const int completedAnimeId) {
  NextSequelPick out;
  const Anime* a = anime::db.item(completedAnimeId);
  if (!a) return out;

  struct Candidate {
    int id = 0;
    qint64 key = 0;
  };
  QVector<Candidate> candidates;
  for (const auto& rel : a->relations) {
    if (rel.type != anime::RelationType::Sequel) continue;
    if (rel.related_id <= 0) continue;
    const Anime* s = anime::db.item(rel.related_id);
    if (!s) {
      out.waiting_for_media = true;
      continue;
    }
    candidates.push_back({rel.related_id, startDateKey(s)});
  }
  if (out.waiting_for_media) return out;
  if (candidates.isEmpty()) return out;

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) {
    if (x.key != y.key) return x.key < y.key;
    return x.id < y.id;
  });

  for (const auto& c : candidates) {
    if (sequelIsCompleted(c.id)) continue;
    out.sequel_id = c.id;
    return out;
  }
  return out;
}

class SequelOfferCoordinator : public QObject {
public:
  static SequelOfferCoordinator& instance() {
    static SequelOfferCoordinator* s = nullptr;
    if (!s) s = new SequelOfferCoordinator(qApp);
    return *s;
  }

  void begin(const int completedAnimeId) {
    if (completedAnimeId <= 0) return;
    m_completed_id_ = completedAnimeId;
    m_fetched_.clear();
    m_attempts_ = 0;
    if (!m_connected_) {
      connect(&anime::db, &anime::Database::itemUpdated, this,
              &SequelOfferCoordinator::onItemUpdated);
      m_connected_ = true;
    }
    tryOffer();
  }

private:
  explicit SequelOfferCoordinator(QObject* parent) : QObject(parent) {}

  void onItemUpdated(const int id) {
    if (m_completed_id_ <= 0) return;
    if (id != m_completed_id_ && !m_pending_sequels_.contains(id)) return;
    m_pending_sequels_.remove(id);
    QTimer::singleShot(0, this, [this]() { tryOffer(); });
  }

  void ensureFetched(const int id) {
    if (id <= 0 || m_fetched_.contains(id)) return;
    m_fetched_.insert(id);
    sync::anilist::Service::instance()->fetchAnime(id);
  }

  void tryOffer() {
    if (m_completed_id_ <= 0) return;
    if (!taiga::settings.notifySequelsOnCompletion()) {
      finish();
      return;
    }
    if (sync::currentServiceId() != sync::ServiceId::AniList) {
      finish();
      return;
    }

    const Anime* a = anime::db.item(m_completed_id_);
    if (!a) {
      finish();
      return;
    }

    if (a->relations_cache == anime::RelationsCache::Unknown) {
      ensureFetched(m_completed_id_);
      if (++m_attempts_ > 20) finish();
      return;
    }

    m_pending_sequels_.clear();
    for (const auto& rel : a->relations) {
      if (rel.type != anime::RelationType::Sequel || rel.related_id <= 0) continue;
      if (!anime::db.item(rel.related_id)) {
        m_pending_sequels_.insert(rel.related_id);
        ensureFetched(rel.related_id);
      }
    }

    const NextSequelPick pick = pickNextSequel(m_completed_id_);
    if (pick.waiting_for_media) {
      if (++m_attempts_ > 20) finish();
      return;
    }
    if (pick.sequel_id <= 0) {
      finish();
      return;
    }

    presentOffer(pick.sequel_id);
    finish();
  }

  void presentOffer(const int sequelId) {
    MainWindow* mw = mainWindow();
    if (!mw) return;
    const Anime* s = anime::db.item(sequelId);
    if (!s) return;

    const QString title = uiTitle(*s);
    const QString type = formatType(s->type);
    // Prefer a readable accent from the palette; fall back to a clear blue if link is too dim.
    QColor accent = mw->palette().color(QPalette::Link);
    if (!accent.isValid() || accent.lightness() > 220 || accent.lightness() < 40) {
      accent = QColor(QStringLiteral("#2B7DE9"));
    }
    const QString titleHtml = QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>")
                                  .arg(accent.name(), title.toHtmlEscaped());
    const QString message =
        type.isEmpty() ? trOffer("Sequel available: %1").arg(titleHtml)
                       : trOffer("Sequel available: %1 (%2)").arg(titleHtml, type.toHtmlEscaped());

    QPointer<MainWindow> mwPtr(mw);
    mw->showSequelCompletionOffer(message, [mwPtr, sequelId]() {
      if (!mwPtr) return;
      if (!taiga::settings.notifySequelsOnCompletion()) return;
      if (showSequelStatusDialog(mwPtr, sequelId)) {
        mwPtr->clearSequelCompletionOffer();
      }
    });
  }

  void finish() {
    m_completed_id_ = 0;
    m_pending_sequels_.clear();
  }

  int m_completed_id_ = 0;
  int m_attempts_ = 0;
  bool m_connected_ = false;
  QSet<int> m_fetched_;
  QSet<int> m_pending_sequels_;
};

}  // namespace

void maybeOfferNextSequelAfterCompletion(const int completedAnimeId) {
  if (completedAnimeId <= 0) return;
  if (!taiga::settings.notifySequelsOnCompletion()) return;
  if (sync::currentServiceId() != sync::ServiceId::AniList) return;
  SequelOfferCoordinator::instance().begin(completedAnimeId);
}

bool showSequelStatusDialog(QWidget* parent, const int sequelAnimeId) {
  const Anime* item = anime::db.item(sequelAnimeId);
  if (!item) return false;

  QDialog dlg(parent);
  dlg.setWindowTitle(trOffer("Sequel status"));
  dlg.setModal(true);

  auto* layout = new QFormLayout(&dlg);
  const QString title = uiTitle(*item);
  const QString type = formatType(item->type);
  auto* titleLabel = new QLabel(type.isEmpty() ? title : trOffer("%1 (%2)").arg(title, type), &dlg);
  titleLabel->setWordWrap(true);
  layout->addRow(trOffer("Title:"), titleLabel);

  const ListEntry* existing = anime::db.entry(sequelAnimeId);
  const auto currentStatus = existing ? existing->status : anime::list::Status::NotInList;
  layout->addRow(
      trOffer("Current status:"),
      new QLabel(currentStatus == anime::list::Status::NotInList ? trOffer("Not in list")
                                                                 : formatListStatus(currentStatus),
                 &dlg));

  auto* combo = new QComboBox(&dlg);
  for (const auto status : anime::list::kStatuses) {
    combo->addItem(formatListStatus(status), static_cast<int>(status));
  }
  if (currentStatus != anime::list::Status::NotInList) {
    const int idx = combo->findData(static_cast<int>(currentStatus));
    if (idx >= 0) combo->setCurrentIndex(idx);
  } else {
    const int planning = combo->findData(static_cast<int>(anime::list::Status::PlanToWatch));
    if (planning >= 0) combo->setCurrentIndex(planning);
  }
  layout->addRow(trOffer("New status:"), combo);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  buttons->button(QDialogButtonBox::Ok)->setText(trOffer("Apply"));
  layout->addRow(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return false;

  const auto newStatus = static_cast<anime::list::Status>(combo->currentData().toInt());
  if (existing && existing->status == newStatus) return false;

  ListEntry entry = existing ? *existing : ListEntry{};
  entry.anime_id = sequelAnimeId;
  entry.status = newStatus;
  entry.rewatching = false;
  entry.last_updated = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch());
  if (newStatus == anime::list::Status::PlanToWatch && !existing) {
    entry.watched_episodes = 0;
  }
  commitListEntryLocalAndMaybeRemote(entry, parent);
  return true;
}

}  // namespace gui
