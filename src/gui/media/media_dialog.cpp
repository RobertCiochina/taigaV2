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

#include "media_dialog.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "base/string.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/list_commit.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/ui_strings.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_season.hpp"
#include "media/anime_utils.hpp"
#include "sync/service.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "track/episode_offset.hpp"
#include "track/play.hpp"
#include "track/recognition_cache.hpp"
#include "ui_media_dialog.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

MediaDialog::MediaDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::MediaDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  if (const auto geometry = taiga::session.mediaDialogGeometry(); !geometry.isEmpty()) {
    restoreGeometry(geometry);
    centerWidgetToScreen(this);
  }

  ui_->posterLabel->setFrameShape(QFrame::Shape::NoFrame);
  ui_->posterLabel->setScaledContents(false);

  ui_->splitter->setSizes({ui_->posterLabel->minimumWidth(), ui_->posterLabel->minimumWidth() * 4});
  if (const auto state = taiga::session.mediaDialogSplitterState(); !state.isEmpty()) {
    ui_->splitter->restoreState(state);
  }

  ui_->verticalLayoutRewatching->setAlignment(Qt::AlignBottom);

  connect(&imageProvider, &ImageProvider::posterChanged, this, [this](int id) {
    if (id == m_anime.id) loadPosterImage();
  });

  connect(&anime::db, &anime::Database::itemUpdated, this, [this](const int id) {
    if (id != m_anime.id) return;
    m_anime = *anime::db.item(id);
    initTitles();
    initDetails();
  });

  connect(ui_->posterLabel, &ClickableLabel::clicked, this, [this](Qt::MouseButton button) {
    if (button == Qt::MouseButton::LeftButton) {
      QUrl url{sync::animePageUrl(m_anime.id)};
      QDesktopServices::openUrl(url);
    }
  });

  connect(ui_->splitter, &QSplitter::splitterMoved, this, [this]() { resizePosterImage(); });

  connect(ui_->checkRewatching, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
    const bool isChecked = state == Qt::CheckState::Checked;
    const int progress = ui_->spinProgress->value();

    // Set status
    const int status =
        static_cast<int>(isChecked ? anime::list::Status::Watching : m_entry->status);
    if (const int index = ui_->comboStatus->findData(status); index > -1) {
      ui_->comboStatus->setCurrentIndex(index);  // @TODO: Don't do this for MyAnimeList
    }

    // Reset progress
    if (isChecked) {
      const bool isCompleted = m_entry->status == anime::list::Status::Completed;
      if (isCompleted && progress == m_entry->watched_episodes) {
        ui_->spinProgress->setValue(0);
      }
    } else {
      if (progress == 0) {
        ui_->spinProgress->setValue(m_entry->watched_episodes);
      }
    }
  });

  connect(ui_->comboStatus, &QComboBox::currentIndexChanged, this, [this](int index) {
    const int status = ui_->comboStatus->itemData(index).toInt();
    if (status != static_cast<int>(anime::list::Status::Completed)) return;
    if (m_entry->status == anime::list::Status::Completed) return;
    if (m_anime.episode_count < 1) return;
    ui_->spinProgress->setValue(m_anime.episode_count);
  });

  connect(ui_->checkDateStarted, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
    ui_->dateStarted->setEnabled(state == Qt::CheckState::Checked);
  });
  connect(ui_->checkDateCompleted, &QCheckBox::checkStateChanged, this,
          [this](Qt::CheckState state) {
            ui_->dateCompleted->setEnabled(state == Qt::CheckState::Checked);
          });

  // Rebuild the Settings tab with local recognition / torrent helpers.
  {
    auto* page = ui_->settingsTab;
    if (auto* old = page->layout()) {
      QLayoutItem* child = nullptr;
      while ((child = old->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
      }
      delete old;
    }
    auto* root = new QVBoxLayout(page);

    auto* epBox = new QGroupBox(tr("Episodes"), page);
    auto* epForm = new QFormLayout(epBox);
    m_useAutoEpisodeOffset_ = new QCheckBox(tr("Detect first episode number automatically"), epBox);
    m_firstEpisodeNumber_ = new QSpinBox(epBox);
    m_firstEpisodeNumber_->setRange(1, 9999);
    m_firstEpisodeNumber_->setToolTip(
        tr("Fansub absolute numbering: set to 41 when list episode 1 is release episode 41."));
    epForm->addRow(m_useAutoEpisodeOffset_);
    epForm->addRow(tr("First episode number:"), m_firstEpisodeNumber_);
    root->addWidget(epBox);

    connect(m_useAutoEpisodeOffset_, &QCheckBox::toggled, this, [this](bool on) {
      if (m_firstEpisodeNumber_) m_firstEpisodeNumber_->setEnabled(!on);
    });

    auto* titleBox = new QGroupBox(tr("Recognition"), page);
    auto* titleLay = new QVBoxLayout(titleBox);
    titleLay->addWidget(new QLabel(tr("Also recognize as (one title per line):"), titleBox));
    m_recognitionAliases_ = new QPlainTextEdit(titleBox);
    m_recognitionAliases_->setPlaceholderText(tr("Boku no Hero Academia Final Season - More"));
    m_recognitionAliases_->setMaximumHeight(120);
    titleLay->addWidget(m_recognitionAliases_);
    root->addWidget(titleBox);

    auto* searchBox = new QGroupBox(tr("Torrent search"), page);
    auto* searchForm = new QFormLayout(searchBox);
    m_torrentSearchTitle_ = new QLineEdit(searchBox);
    m_torrentSearchTitle_->setPlaceholderText(tr("Cached RSS search title (optional)"));
    searchForm->addRow(tr("Search title:"), m_torrentSearchTitle_);
    root->addWidget(searchBox);

    root->addStretch(1);
  }

  // Add a "Play next episode" button to the left side of the button box
  auto* playBtn =
      ui_->buttonBox->addButton(playNextEpisodeActionLabel(), QDialogButtonBox::ActionRole);
  playBtn->setObjectName("playNextEpisodeBtn");
  connect(playBtn, &QPushButton::clicked, this, [this]() {
    const int anime_id = m_anime.id;
    if (anime_id <= 0) return;
    if (!track::playNextEpisode(anime_id)) {
      QMessageBox::information(this, tr("Taiga"), playNextEpisodeNotFoundMessage());
    }
  });
}

void MediaDialog::closeEvent(QCloseEvent* event) {
  taiga::session.setMediaDialogGeometry(saveGeometry());
  taiga::session.setMediaDialogSplitterState(ui_->splitter->saveState());
  event->accept();
}

void MediaDialog::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_F5) {
    imageProvider.fetchPoster(m_anime.id);
    sync::fetchAnime(m_anime.id);
    return;
  }

  QDialog::keyPressEvent(event);
}

void MediaDialog::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  resizePosterImage();
}

void MediaDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  resizePosterImage();
}

void MediaDialog::show(QWidget* parent, MediaDialogPage page, const Anime& anime,
                       const std::optional<ListEntry> entry) {
  auto* dlg = new MediaDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setAnime(anime, entry);
  dlg->ui_->tabWidget->setCurrentIndex(static_cast<int>(page));
  dlg->QDialog::show();
}

void MediaDialog::setAnime(const Anime& anime, const std::optional<ListEntry> entry) {
  m_anime = anime;
  m_entry = entry;

  loadPosterImage();
  initTitles();
  initDetails();
  initList();
  initLocalSettings();

  if (anime::isStale(anime)) {
    sync::fetchAnime(anime.id);
  }
}

void MediaDialog::initLocalSettings() {
  if (!m_useAutoEpisodeOffset_ || !m_firstEpisodeNumber_ || !m_recognitionAliases_ ||
      !m_torrentSearchTitle_) {
    return;
  }

  const bool has_manual = track::hasManualEpisodeOffset(m_anime.id);
  m_useAutoEpisodeOffset_->setChecked(!has_manual);
  m_firstEpisodeNumber_->setEnabled(has_manual);

  const int offset = track::episodeOffset(m_anime);
  m_firstEpisodeNumber_->setValue(offset + 1);

  const QString inferred_hint =
      tr("Automatic would use first episode %1").arg(track::inferredEpisodeOffset(m_anime) + 1);
  m_useAutoEpisodeOffset_->setToolTip(inferred_hint);

  m_recognitionAliases_->setPlainText(
      taiga::settings.animeRecognitionTitles(m_anime.id).join(QChar('\n')));
  m_torrentSearchTitle_->setText(taiga::settings.torrentSearchTitleForAnime(m_anime.id));
}

void MediaDialog::saveLocalSettings() {
  if (m_anime.id <= 0) return;
  if (!m_useAutoEpisodeOffset_ || !m_firstEpisodeNumber_ || !m_recognitionAliases_ ||
      !m_torrentSearchTitle_) {
    return;
  }

  if (m_useAutoEpisodeOffset_->isChecked()) {
    taiga::settings.clearAnimeEpisodeOffsetOverride(m_anime.id);
  } else {
    const int first = m_firstEpisodeNumber_->value();
    taiga::settings.setAnimeEpisodeOffsetOverride(m_anime.id, std::max(0, first - 1));
  }

  const QStringList aliases =
      m_recognitionAliases_->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts);
  taiga::settings.setAnimeRecognitionTitles(m_anime.id, aliases);

  const QString search = m_torrentSearchTitle_->text().trimmed();
  if (search.isEmpty()) {
    taiga::settings.setTorrentSearchTitleForAnime(m_anime.id, {});
  } else {
    taiga::settings.setTorrentSearchTitleForAnime(m_anime.id, search);
  }

  // Refresh recognition keys for this title so aliases apply immediately.
  track::recognition::cache()->update(m_anime);
}

void MediaDialog::initTitles() {
  const auto mainTitle = QString::fromStdString(
      anime::preferredListTitleString(m_anime, anime::TitleLanguage::English));
  setWindowTitle(mainTitle);
  ui_->titleLabel->setText(mainTitle);

  QList<QString> altTitles;
  const auto addTitle = [&mainTitle, &altTitles](const QString& title) {
    if (title.isEmpty() || title == mainTitle) return;
    altTitles.push_back(title);
  };
  addTitle(QString::fromStdString(m_anime.titles.romaji));
  addTitle(QString::fromStdString(m_anime.titles.english));
  addTitle(QString::fromStdString(m_anime.titles.japanese));
  if (!altTitles.isEmpty()) ui_->altTitlesLabel->setText(altTitles.join(", "));
  ui_->altTitlesLabel->setHidden(altTitles.isEmpty());
}

void MediaDialog::initDetails() {
  while (ui_->infoLayout->rowCount() > 0) {
    ui_->infoLayout->removeRow(0);
  }

  const auto get_row_title = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setAlignment(Qt::AlignRight | Qt::AlignTop);
    label->setFont([label]() {
      auto font = label->font();
      font.setWeight(QFont::Weight::DemiBold);
      return font;
    }());
    return label;
  };

  const auto get_row_label = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setContextMenuPolicy(Qt::ContextMenuPolicy::NoContextMenu);
    label->setTextInteractionFlags(Qt::TextInteractionFlag::TextSelectableByMouse);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    return label;
  };

  auto seasonLabel =
      new QLabel(formatSeasonDateRange(m_anime.date_started, m_anime.date_finished), this);
  seasonLabel->setCursor(QCursor(Qt::CursorShape::WhatsThisCursor));
  seasonLabel->setToolTip(seasonLabel->text());

  if (!m_anime.titles.synonyms.empty()) {
    ui_->infoLayout->addRow(get_row_title(tr("Titles:")),
                            get_row_label(joinStrings(m_anime.titles.synonyms)));
  }
  ui_->infoLayout->addRow(get_row_title(tr("Type:")), get_row_label(formatType(m_anime.type)));
  ui_->infoLayout->addRow(get_row_title(tr("Episodes:")),
                          get_row_label(formatNumber(m_anime.episode_count, "?")));
  if (m_anime.episode_length > 0 && m_anime.type != anime::Type::Tv) {
    const auto duration = formatEpisodeLength(m_anime.episode_length);
    const auto label = m_anime.episode_count == 1 ? duration : u"%1 per episode"_s.arg(duration);
    ui_->infoLayout->addRow(get_row_title(tr("Duration:")), get_row_label(label));
  }
  ui_->infoLayout->addRow(get_row_title(tr("Status:")),
                          get_row_label(formatStatus(m_anime.status)));
  ui_->infoLayout->addRow(get_row_title(tr("Season:")), seasonLabel);
  ui_->infoLayout->addRow(get_row_title(tr("Score:")), get_row_label(formatScore(m_anime.score)));
  if (!m_anime.genres.empty()) {
    ui_->infoLayout->addRow(get_row_title(tr("Genres:")),
                            get_row_label(joinStrings(m_anime.genres)));
  }
  if (!m_anime.tags.empty()) {
    ui_->infoLayout->addRow(get_row_title(tr("Tags:")), get_row_label(joinStrings(m_anime.tags)));
  }
  if (!m_anime.studios.empty()) {
    ui_->infoLayout->addRow(get_row_title(tr("Studios:")),
                            get_row_label(joinStrings(m_anime.studios)));
  }
  if (!m_anime.producers.empty()) {
    ui_->infoLayout->addRow(get_row_title(tr("Producers:")),
                            get_row_label(joinStrings(m_anime.producers)));
  }

  // Age rating
  {
    using R = anime::AgeRating;
    const auto ratingStr = [this]() -> QString {
      switch (m_anime.age_rating) {
        case R::G:
          return u"G — All ages"_s;
        case R::PG:
          return u"PG — Children"_s;
        case R::PG13:
          return u"PG-13 — Teens 13+"_s;
        case R::R17:
          return u"R — 17+"_s;
        case R::R18:
          return u"R+ — Adults"_s;
        default:
          return {};
      }
    }();
    if (!ratingStr.isEmpty()) {
      ui_->infoLayout->addRow(get_row_title(tr("Rating:")), get_row_label(ratingStr));
    }
  }

  // Popularity rank
  if (m_anime.popularity_rank > 0) {
    ui_->infoLayout->addRow(get_row_title(tr("Popularity:")),
                            get_row_label(u"#%1"_s.arg(m_anime.popularity_rank)));
  }

  // Airing info: last aired episode and next episode countdown
  if (m_anime.status == anime::Status::Airing) {
    if (m_anime.last_aired_episode > 0) {
      ui_->infoLayout->addRow(get_row_title(tr("Last aired:")),
                              get_row_label(tr("Episode %1").arg(m_anime.last_aired_episode)));
    }
    if (m_anime.next_episode_time > 0) {
      const QString rel = formatAsRelativeTime(static_cast<qint64>(m_anime.next_episode_time));
      const int next_ep = m_anime.last_aired_episode + 1;
      const QString next_ep_label = next_ep > 0 ? tr("Episode %1 — %2").arg(next_ep).arg(rel) : rel;
      auto* next_label = get_row_label(next_ep_label);
      next_label->setToolTip(
          QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_anime.next_episode_time))
              .toString(Qt::RFC2822Date));
      ui_->infoLayout->addRow(get_row_title(tr("Next episode:")), next_label);
    }
  }

  // Trailer (YouTube link)
  if (!m_anime.trailer_id.empty()) {
    const QString trailerUrl =
        u"https://www.youtube.com/watch?v=%1"_s.arg(QString::fromStdString(m_anime.trailer_id));
    auto* trailer_label = get_row_label(u"<a href=\"%1\">Watch on YouTube</a>"_s.arg(trailerUrl));
    trailer_label->setOpenExternalLinks(true);
    trailer_label->setTextFormat(Qt::RichText);
    ui_->infoLayout->addRow(get_row_title(tr("Trailer:")), trailer_label);
  }

  const auto synopsis = QString::fromStdString(m_anime.synopsis);

  ui_->synopsisHeader->setHidden(synopsis.isEmpty());

  ui_->synopsis->document()->setDocumentMargin(0);
  ui_->synopsis->viewport()->setAutoFillBackground(false);
  ui_->synopsis->setContextMenuPolicy(Qt::ContextMenuPolicy::NoContextMenu);
  ui_->synopsis->setHtml(synopsis);
  ui_->synopsis->setHidden(synopsis.isEmpty());
}

void MediaDialog::addToList(anime::list::Status status) {
  const auto now = QDateTime::currentSecsSinceEpoch();
  ListEntry entry{};
  entry.id = -static_cast<int64_t>(m_anime.id);
  entry.anime_id = m_anime.id;
  entry.status = status;
  entry.watched_episodes = 0;
  entry.last_updated = static_cast<std::time_t>(now);
  m_entry = entry;
  gui::commitListEntryLocalAndMaybeRemote(entry, this);
  initList();
}

void MediaDialog::initList() {
  ui_->tabWidget->setTabVisible(1, true);

  // Lazy-create the "not in list" overlay panel (inserted once at position 0 of listTab's layout).
  if (!m_addToListPanel) {
    m_addToListPanel = new QWidget(ui_->listTab);
    auto* vbox = new QVBoxLayout(m_addToListPanel);
    vbox->setAlignment(Qt::AlignCenter);

    auto* lbl = new QLabel(tr("This title is not in your list yet."), m_addToListPanel);
    lbl->setAlignment(Qt::AlignCenter);
    QFont f = lbl->font();
    f.setPointSizeF(f.pointSizeF() * 1.1);
    lbl->setFont(f);
    vbox->addWidget(lbl);

    vbox->addSpacing(12);

    auto* btnRow = new QHBoxLayout;
    btnRow->setAlignment(Qt::AlignCenter);
    btnRow->setSpacing(8);

    for (const auto status : {anime::list::Status::Watching, anime::list::Status::PlanToWatch,
                              anime::list::Status::OnHold, anime::list::Status::Dropped,
                              anime::list::Status::Completed}) {
      auto* btn =
          new QPushButton(tr("Add as \"%1\"").arg(formatListStatus(status)), m_addToListPanel);
      connect(btn, &QPushButton::clicked, this, [this, status]() { addToList(status); });
      btnRow->addWidget(btn);
    }
    vbox->addLayout(btnRow);

    qobject_cast<QVBoxLayout*>(ui_->listTab->layout())->insertWidget(0, m_addToListPanel);
  }

  const bool hasEntry = m_entry.has_value();
  m_addToListPanel->setVisible(!hasEntry);

  // Show/hide all form widgets in the list tab (everything except m_addToListPanel).
  for (auto* child : ui_->listTab->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
    if (child != m_addToListPanel) child->setVisible(hasEntry);
  }

  if (!hasEntry) return;

  // Episodes watched
  if (m_anime.episode_count > 0) {
    ui_->spinProgress->setMaximum(m_anime.episode_count);
  }
  ui_->spinProgress->setValue(m_entry->watched_episodes);

  // Times rewatched
  ui_->spinRewatches->setValue(m_entry->rewatched_times);

  // Rewatching
  ui_->checkRewatching->setChecked(m_entry->rewatching);

  // Status
  ui_->comboStatus->clear();
  for (const auto status : anime::list::kStatuses) {
    ui_->comboStatus->addItem(formatListStatus(status), static_cast<int>(status));
    if (status == m_entry->status) {
      ui_->comboStatus->setCurrentIndex(ui_->comboStatus->count() - 1);
    }
  }

  // Score
  if (!ui_->comboScore->count()) {
    for (int i = 0; i <= 10; ++i) {
      ui_->comboScore->addItem(tr("%1").arg(i), i * 10);
    }
  }
  ui_->comboScore->setCurrentIndex(m_entry->score / 10);

  const auto fuzzy_to_date = [](const FuzzyDate& date) {
    return QDate{date.year(), date.month(), date.day()};
  };

  // Date started & completed
  ui_->checkDateStarted->setChecked(static_cast<bool>(m_entry->date_started));
  ui_->checkDateCompleted->setChecked(static_cast<bool>(m_entry->date_completed));
  if (m_anime.date_started) {
    ui_->dateStarted->setMinimumDate(fuzzy_to_date(m_anime.date_started));
    ui_->dateCompleted->setMinimumDate(fuzzy_to_date(m_anime.date_started));
  }
  ui_->dateStarted->setDate(m_entry->date_started ? fuzzy_to_date(m_entry->date_started)
                                                  : QDate::currentDate());
  ui_->dateCompleted->setDate(m_entry->date_completed ? fuzzy_to_date(m_entry->date_completed)
                                                      : QDate::currentDate());

  // Notes
  ui_->plainTextEditNotes->setPlainText(QString::fromStdString(m_entry->notes));
}

void MediaDialog::loadPosterImage() {
  imageProvider.loadPoster(m_anime.id);
  resizePosterImage();
  if (const QPixmap* p = imageProvider.loadPoster(m_anime.id); p->isNull()) {
    imageProvider.fetchPoster(m_anime.id);
  }
}

void MediaDialog::resizePosterImage() {
  const int label_w = ui_->posterLabel->width();
  if (label_w <= 0) {
    return;
  }

  const QPixmap* full = imageProvider.loadPoster(m_anime.id);
  if (!full || full->isNull()) {
    ui_->posterLabel->clear();
    ui_->posterLabel->setFixedHeight(qMax(1, (label_w * 3 + 1) / 2));
    return;
  }

  const int w = full->width();
  const int h = full->height();
  if (w <= 0 || h <= 0) {
    ui_->posterLabel->clear();
    ui_->posterLabel->setFixedHeight(qMax(1, (label_w * 3 + 1) / 2));
    return;
  }

  const int out_h = static_cast<int>(std::lround(
      static_cast<double>(h) * (static_cast<double>(label_w) / static_cast<double>(w))));
  const QPixmap scaled =
      full->scaled(label_w, out_h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  ui_->posterLabel->setPixmap(scaled);
  ui_->posterLabel->setFixedSize(label_w, scaled.height());
}

void MediaDialog::accept() {
  saveLocalSettings();

  if (!m_entry) {
    QDialog::accept();
    return;
  }

  m_entry->watched_episodes = ui_->spinProgress->value();
  m_entry->rewatched_times = ui_->spinRewatches->value();
  m_entry->rewatching = ui_->checkRewatching->isChecked();
  m_entry->status = ui_->comboStatus->currentData().value<anime::list::Status>();
  m_entry->score = ui_->comboScore->currentData().toInt();
  m_entry->date_started = ui_->checkDateStarted->isChecked()
                              ? FuzzyDate{ui_->dateStarted->date().toStdSysDays()}
                              : FuzzyDate{};
  m_entry->date_completed = ui_->checkDateCompleted->isChecked()
                                ? FuzzyDate{ui_->dateCompleted->date().toStdSysDays()}
                                : FuzzyDate{};
  m_entry->notes = ui_->plainTextEditNotes->toPlainText().toStdString();
  m_entry->last_updated = QDateTime::currentSecsSinceEpoch();

  // Completion prompt: show dialog if episodes now equals the total
  gui::maybePromptCompletion(this, m_anime, *m_entry);

  gui::commitListEntryLocalAndMaybeRemote(*m_entry, this);

  QDialog::accept();
}

}  // namespace gui
