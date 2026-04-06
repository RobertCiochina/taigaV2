/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#include "torrent_filters_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QVBoxLayout>

#include "base/string.hpp"
#include "taiga/settings.hpp"

namespace gui {

namespace {

// ---------------------------------------------------------------------------
// Rule helpers: human-readable display ↔ raw regex
// ---------------------------------------------------------------------------

/// Try to infer a friendly label for a stored regex line.
/// - `\bTEXT\b`  → "Whole word: TEXT"
/// - no special chars (plain text) → "Contains: TEXT"
/// - anything else → "Regex: ..."
QString displayForRule(const QString& regex) {
  // Whole-word pattern produced by "Add rule → Whole word" mode.
  static const QRegularExpression wbRe(u"^\\\\b(.+)\\\\b$"_s);
  const auto m = wbRe.match(regex);
  if (m.hasMatch()) {
    // Un-escape simple character escapes (e.g. \( → () for display.
    QString inner = m.captured(1);
    inner.replace(QStringLiteral("\\."), QStringLiteral("."));
    inner.replace(QStringLiteral("\\("), QStringLiteral("("));
    inner.replace(QStringLiteral("\\)"), QStringLiteral(")"));
    inner.replace(QStringLiteral("\\["), QStringLiteral("["));
    inner.replace(QStringLiteral("\\]"), QStringLiteral("]"));
    inner.replace(QStringLiteral("\\+"), QStringLiteral("+"));
    inner.replace(QStringLiteral("\\*"), QStringLiteral("*"));
    return QObject::tr("Whole word: %1").arg(inner);
  }

  // Check for special regex metacharacters.
  static const QRegularExpression specials(u"[\\\\()\\[\\]{}^$.|?*+]"_s);
  if (!specials.match(regex).hasMatch()) {
    return QObject::tr("Contains: %1").arg(regex);
  }

  return QObject::tr("Regex: %1").arg(regex);
}

/// Add a regex rule to a QListWidget, skipping exact duplicates.
void addToList(QListWidget* list, const QString& regex) {
  if (!list) return;
  const QString r = regex.trimmed();
  if (r.isEmpty()) return;
  for (int i = 0; i < list->count(); ++i) {
    if (list->item(i)->data(Qt::UserRole).toString() == r) return;
  }
  auto* item = new QListWidgetItem(displayForRule(r), list);
  item->setData(Qt::UserRole, r);
}

/// Collect raw regex strings from a list widget (for saving to settings).
QStringList collectRules(const QListWidget* list) {
  if (!list) return {};
  QStringList out;
  for (int i = 0; i < list->count(); ++i) {
    const QString r = list->item(i)->data(Qt::UserRole).toString().trimmed();
    if (!r.isEmpty()) out.append(r);
  }
  return out;
}

/// Load existing settings into list widgets.
void loadIntoList(QListWidget* list, const QString& rawSettings) {
  if (!list) return;
  for (const QString& line :
       rawSettings.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
    addToList(list, line.trimmed());
  }
}

}  // namespace

TorrentFiltersDialog::TorrentFiltersDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Torrent filters"));
  setModal(true);
  resize(760, 580);

  auto* layout = new QVBoxLayout(this);

  auto* help = new QLabel(
      tr("These rules filter the in-app Torrents RSS table. Rules are applied to the full row text "
         "(title, date, links). <b>Contains</b> and <b>Whole word</b> rules are case-insensitive; "
         "<b>Regex</b> rules use Qt regular expressions (also case-insensitive). "
         "Use the presets or <b>Add rule…</b> to build rules without writing regex."),
      this);
  help->setWordWrap(true);
  layout->addWidget(help);

  auto* warn = new QLabel(this);
  warn->setWordWrap(true);
  layout->addWidget(warn);

  // ── List-based filters ────────────────────────────────────────────────────
  {
    auto* grp = new QGroupBox(tr("List-based filters (applied after title identification)"), this);
    auto* gl = new QVBoxLayout(grp);

    auto* hideDropped = new QCheckBox(tr("Hide titles on my Dropped list"), grp);
    hideDropped->setObjectName(u"chkHideDropped"_s);
    hideDropped->setChecked(taiga::settings.torrentFeedHideDropped());
    hideDropped->setToolTip(tr("Keep dropped anime out of torrent results."));

    auto* hideNotInList = new QCheckBox(tr("Hide titles not on my list"), grp);
    hideNotInList->setObjectName(u"chkHideNotInList"_s);
    hideNotInList->setChecked(taiga::settings.torrentFeedHideNotInList());
    hideNotInList->setToolTip(tr("Show only torrents that match anime on your list."));

    auto* hideWatched = new QCheckBox(tr("Hide watched episodes"), grp);
    hideWatched->setObjectName(u"chkHideWatched"_s);
    hideWatched->setChecked(taiga::settings.torrentFeedHideWatchedEpisodes());
    hideWatched->setToolTip(tr("Hide torrents for episodes at or below your watched progress."));

    auto* hideAvailable = new QCheckBox(tr("Hide episodes already on disk"), grp);
    hideAvailable->setObjectName(u"chkHideAvailable"_s);
    hideAvailable->setChecked(taiga::settings.torrentFeedHideAvailableEpisodes());
    hideAvailable->setToolTip(
        tr("Hide torrents for episodes already found in your last library scan."));

    auto* hideOlderVersions =
        new QCheckBox(tr("Hide older release versions when a newer v2+ exists"), grp);
    hideOlderVersions->setObjectName(u"chkHideOlderVersions"_s);
    hideOlderVersions->setChecked(taiga::settings.torrentFeedHideOlderVersionsWhenNewerExists());
    hideOlderVersions->setToolTip(
        tr("When both v1 and v2+ releases exist for the same episode, hide the older one."));

    gl->addWidget(hideDropped);
    gl->addWidget(hideNotInList);
    gl->addWidget(hideWatched);
    gl->addWidget(hideAvailable);
    gl->addWidget(hideOlderVersions);
    layout->addWidget(grp);
  }

  // ── Rule lists ────────────────────────────────────────────────────────────
  auto* rulesRow = new QHBoxLayout();

  // Include list
  auto* includeGrp = new QGroupBox(tr("Show only if any rule matches:"), this);
  includeGrp->setToolTip(
      tr("Rows that do NOT match at least one rule here are hidden. Leave empty to show all."));
  auto* includeGroupLayout = new QVBoxLayout(includeGrp);
  auto* includeList = new QListWidget(includeGrp);
  includeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  includeList->setToolTip(tr("Select a rule and press Delete or use the button below to remove it."));
  auto* includeRemove = new QPushButton(tr("Remove selected"), includeGrp);
  includeGroupLayout->addWidget(includeList);
  includeGroupLayout->addWidget(includeRemove);

  // Exclude list
  auto* excludeGrp = new QGroupBox(tr("Hide if any rule matches:"), this);
  excludeGrp->setToolTip(tr("Rows that match at least one rule here are hidden."));
  auto* excludeGroupLayout = new QVBoxLayout(excludeGrp);
  auto* excludeList = new QListWidget(excludeGrp);
  excludeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  excludeList->setToolTip(tr("Select a rule and press Delete or use the button below to remove it."));
  auto* excludeRemove = new QPushButton(tr("Remove selected"), excludeGrp);
  excludeGroupLayout->addWidget(excludeList);
  excludeGroupLayout->addWidget(excludeRemove);

  rulesRow->addWidget(includeGrp);
  rulesRow->addWidget(excludeGrp);
  layout->addLayout(rulesRow, 1);

  // Load existing rules from settings.
  loadIntoList(includeList,
               QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()));
  loadIntoList(excludeList,
               QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()));

  // Remove buttons
  connect(includeRemove, &QPushButton::clicked, this, [includeList]() {
    for (auto* it : includeList->selectedItems()) delete it;
  });
  connect(excludeRemove, &QPushButton::clicked, this, [excludeList]() {
    for (auto* it : excludeList->selectedItems()) delete it;
  });

  // Delete key removes selected items
  const auto makeDeleteShortcut = [this](QListWidget* lw) {
    auto* sc = new QShortcut(QKeySequence{Qt::Key_Delete}, lw);
    sc->setContext(Qt::WidgetShortcut);
    connect(sc, &QShortcut::activated, lw, [lw]() {
      for (auto* it : lw->selectedItems()) delete it;
    });
  };
  makeDeleteShortcut(includeList);
  makeDeleteShortcut(excludeList);

  // ── Archive ────────────────────────────────────────────────────────────────
  auto* archiveGrp = new QGroupBox(tr("Discarded titles (archive)"), this);
  archiveGrp->setToolTip(tr(
      "Titles you discarded from the Torrents page. These are permanently hidden from RSS results."));
  auto* archiveLayout = new QVBoxLayout(archiveGrp);
  auto* archiveList = new QListWidget(archiveGrp);
  archiveList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  archiveList->setMaximumHeight(100);
  const auto refillArchiveList = [archiveList]() {
    archiveList->clear();
    const QStringList titles = taiga::settings.torrentFeedDiscardedTitleArchive();
    for (const QString& t : titles) {
      archiveList->addItem(new QListWidgetItem(t));
    }
  };
  refillArchiveList();
  auto* archiveBtns = new QHBoxLayout();
  auto* b_remove_arch = new QPushButton(tr("Remove selected"), archiveGrp);
  auto* b_clear_arch = new QPushButton(tr("Clear archive"), archiveGrp);
  archiveBtns->addWidget(b_remove_arch);
  archiveBtns->addWidget(b_clear_arch);
  archiveBtns->addStretch();
  archiveLayout->addWidget(archiveList);
  archiveLayout->addLayout(archiveBtns);
  layout->addWidget(archiveGrp);

  connect(b_remove_arch, &QPushButton::clicked, this, [archiveList, refillArchiveList]() {
    const QList<QListWidgetItem*> sel = archiveList->selectedItems();
    if (sel.isEmpty()) return;
    QSet<QString> remove;
    for (const QListWidgetItem* it : sel) {
      if (it) remove.insert(it->text());
    }
    QStringList titles = taiga::settings.torrentFeedDiscardedTitleArchive();
    QStringList kept;
    kept.reserve(titles.size());
    for (const QString& t : titles) {
      if (!remove.contains(t)) kept.push_back(t);
    }
    taiga::settings.setTorrentFeedDiscardedTitleArchive(kept);
    refillArchiveList();
  });
  connect(b_clear_arch, &QPushButton::clicked, this, [refillArchiveList]() {
    taiga::settings.setTorrentFeedDiscardedTitleArchive({});
    refillArchiveList();
  });

  // ── Presets & actions ─────────────────────────────────────────────────────
  auto* presetsLayout = new QHBoxLayout();
  presetsLayout->addWidget(new QLabel(tr("Quick presets:"), this));
  auto* b_bad = new QPushButton(tr("Block bad video (v1)"), this);
  b_bad->setToolTip(tr("Exclude: AVI, DIVX, LQ, RMVB, SD, WMV, XVID"));
  auto* b_1080 = new QPushButton(tr("Prefer 1080p"), this);
  b_1080->setToolTip(tr("Include only: 1080p"));
  auto* b_v2 = new QPushButton(tr("Prefer v2+"), this);
  b_v2->setToolTip(tr("Include only: v2 or higher versions"));
  auto* b_cam = new QPushButton(tr("Block CAM/TS"), this);
  b_cam->setToolTip(tr("Exclude: CAM, TS, TC rips"));
  auto* b_x265 = new QPushButton(tr("Prefer x265/HEVC"), this);
  auto* b_mkv = new QPushButton(tr("Prefer MKV"), this);
  auto* b_aac = new QPushButton(tr("Prefer AAC/FLAC"), this);
  auto* b_add = new QPushButton(tr("Add rule…"), this);
  b_add->setToolTip(
      tr("Open a dialog to add a 'Contains', 'Whole word', or custom 'Regex' rule."));

  for (auto* w : {b_bad, b_1080, b_v2, b_cam, b_x265, b_mkv, b_aac, b_add}) {
    presetsLayout->addWidget(w);
  }
  presetsLayout->addStretch();
  layout->addLayout(presetsLayout);

  connect(b_bad, &QPushButton::clicked, this, [excludeList]() {
    addToList(excludeList, QStringLiteral("\\b(AVI|DIVX|LQ|RMVB|SD|WMV|XVID)\\b"));
  });
  connect(b_1080, &QPushButton::clicked, this, [includeList]() {
    addToList(includeList, QStringLiteral("\\b1080p\\b"));
  });
  connect(b_v2, &QPushButton::clicked, this, [includeList]() {
    addToList(includeList, QStringLiteral("\\bv[2-9]\\b"));
    addToList(includeList, QStringLiteral("\\bv\\d{2,}\\b"));
  });
  connect(b_cam, &QPushButton::clicked, this, [excludeList]() {
    addToList(excludeList, QStringLiteral("\\b(CAM|TS|TC)\\b"));
  });
  connect(b_x265, &QPushButton::clicked, this, [includeList]() {
    addToList(includeList, QStringLiteral("\\b(x265|hevc)\\b"));
  });
  connect(b_mkv, &QPushButton::clicked, this, [includeList]() {
    addToList(includeList, QStringLiteral("\\bMKV\\b"));
  });
  connect(b_aac, &QPushButton::clicked, this, [includeList]() {
    addToList(includeList, QStringLiteral("\\b(AAC|FLAC)\\b"));
  });

  // ── Add rule dialog ───────────────────────────────────────────────────────
  connect(b_add, &QPushButton::clicked, this, [=]() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add torrent filter rule"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout();
    auto* targetCombo = new QComboBox(&dlg);
    targetCombo->addItem(tr("Show only if matches (include)"), 0);
    targetCombo->addItem(tr("Hide if matches (exclude)"), 1);

    auto* modeCombo = new QComboBox(&dlg);
    modeCombo->addItem(tr("Contains (case-insensitive text match)"), 0);
    modeCombo->addItem(tr("Whole word (matches word boundary)"), 1);
    modeCombo->addItem(tr("Regular expression (advanced)"), 2);

    auto* textEdit = new QLineEdit(&dlg);
    textEdit->setPlaceholderText(tr("e.g. 1080p, BluRay, SubGroup, …"));

    auto* previewLabel = new QLabel(&dlg);
    previewLabel->setWordWrap(true);
    previewLabel->setStyleSheet(u"color: grey; font-style: italic;"_s);

    form->addRow(tr("Rule type:"), targetCombo);
    form->addRow(tr("Match mode:"), modeCombo);
    form->addRow(tr("Text:"), textEdit);
    form->addRow(tr("Preview:"), previewLabel);
    lay->addLayout(form);

    auto* box2 =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QPushButton* ok2 = box2->button(QDialogButtonBox::Ok);
    ok2->setEnabled(false);
    lay->addWidget(box2);
    connect(box2, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box2, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    const auto updatePreview = [=]() {
      const QString v = textEdit->text().trimmed();
      if (v.isEmpty()) {
        ok2->setEnabled(false);
        previewLabel->setText({});
        return;
      }
      const int mode = modeCombo->currentData().toInt();
      QString regex;
      if (mode == 2) {
        QRegularExpression re(v, QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) {
          ok2->setEnabled(false);
          previewLabel->setText(tr("Invalid regex: %1").arg(re.errorString()));
          return;
        }
        regex = v;
      } else if (mode == 1) {
        regex = QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(v));
      } else {
        regex = QRegularExpression::escape(v);
      }
      ok2->setEnabled(true);
      previewLabel->setText(tr("Will store as: %1").arg(regex));
    };
    connect(textEdit, &QLineEdit::textChanged, &dlg, [updatePreview]() { updatePreview(); });
    connect(modeCombo, &QComboBox::currentIndexChanged, &dlg, [updatePreview](int) {
      updatePreview();
    });
    updatePreview();

    if (dlg.exec() != QDialog::Accepted) return;

    const QString raw = textEdit->text().trimmed();
    const int where = targetCombo->currentData().toInt();
    const int how = modeCombo->currentData().toInt();
    QString regex;
    if (how == 2) {
      regex = raw;
    } else if (how == 1) {
      regex = QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(raw));
    } else {
      regex = QRegularExpression::escape(raw);
    }
    addToList(where == 1 ? excludeList : includeList, regex);
  });

  // ── Validation (conflict detection) ───────────────────────────────────────
  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(box);
  QPushButton* ok = box->button(QDialogButtonBox::Ok);

  const auto updateValid = [=]() {
    const QStringList inc = collectRules(includeList);
    const QStringList exc = collectRules(excludeList);
    const QSet<QString> exc_set(exc.begin(), exc.end());

    QString warning;
    bool valid = true;

    if (!inc.isEmpty() && exc_set.contains(QStringLiteral(".*"))) {
      warning = tr("Your \"Hide\" list contains <code>.*</code>, which hides everything — no results can appear.");
      valid = false;
    } else if (!inc.isEmpty()) {
      int covered = 0;
      for (const QString& s : inc) {
        if (exc_set.contains(s)) ++covered;
      }
      if (covered == inc.size()) {
        warning = tr("Every \"Show only\" rule is also in the \"Hide\" list — no results can appear.");
        valid = false;
      }
    }

    warn->setText(warning.isEmpty()
                      ? QString{}
                      : (u"<span style=\"color:#c33\"><b>%1</b></span>"_s.arg(warning)));
    ok->setEnabled(valid);
  };

  // Re-validate whenever lists change.
  connect(includeList->model(), &QAbstractItemModel::rowsRemoved, this,
          [updateValid](const QModelIndex&, int, int) { updateValid(); });
  connect(includeList->model(), &QAbstractItemModel::rowsInserted, this,
          [updateValid](const QModelIndex&, int, int) { updateValid(); });
  connect(excludeList->model(), &QAbstractItemModel::rowsRemoved, this,
          [updateValid](const QModelIndex&, int, int) { updateValid(); });
  connect(excludeList->model(), &QAbstractItemModel::rowsInserted, this,
          [updateValid](const QModelIndex&, int, int) { updateValid(); });
  updateValid();

  // ── Save on OK ────────────────────────────────────────────────────────────
  connect(box, &QDialogButtonBox::accepted, this, [=]() {
    taiga::settings.setTorrentFeedIncludeRegexList(
        collectRules(includeList).join(QLatin1Char('\n')).toStdString());
    taiga::settings.setTorrentFeedExcludeRegexList(
        collectRules(excludeList).join(QLatin1Char('\n')).toStdString());

    // Save list-based checkboxes.
    if (auto* w = findChild<QCheckBox*>(u"chkHideDropped"_s))
      taiga::settings.setTorrentFeedHideDropped(w->isChecked());
    if (auto* w = findChild<QCheckBox*>(u"chkHideNotInList"_s))
      taiga::settings.setTorrentFeedHideNotInList(w->isChecked());
    if (auto* w = findChild<QCheckBox*>(u"chkHideWatched"_s))
      taiga::settings.setTorrentFeedHideWatchedEpisodes(w->isChecked());
    if (auto* w = findChild<QCheckBox*>(u"chkHideAvailable"_s))
      taiga::settings.setTorrentFeedHideAvailableEpisodes(w->isChecked());
    if (auto* w = findChild<QCheckBox*>(u"chkHideOlderVersions"_s))
      taiga::settings.setTorrentFeedHideOlderVersionsWhenNewerExists(w->isChecked());

    accept();
  });
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

}  // namespace gui
