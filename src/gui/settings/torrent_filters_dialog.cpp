/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#include "torrent_filters_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>

#include "base/string.hpp"
#include "taiga/settings.hpp"

namespace gui {

namespace {

QStringList splitLines(const QString& t) {
  QStringList out;
  for (QString s : t.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
    s = s.trimmed();
    if (!s.isEmpty()) out.push_back(s);
  }
  return out;
}

void addUniqueLine(QPlainTextEdit* edit, const QString& line) {
  if (!edit) return;
  const QString trimmed = line.trimmed();
  if (trimmed.isEmpty()) return;
  QStringList lines = splitLines(edit->toPlainText());
  if (lines.contains(trimmed)) return;
  QString cur = edit->toPlainText().trimmed();
  if (!cur.isEmpty() && !cur.endsWith('\n')) cur += QLatin1Char('\n');
  cur += trimmed;
  edit->setPlainText(cur + QLatin1Char('\n'));
}

}  // namespace

TorrentFiltersDialog::TorrentFiltersDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Torrent filters"));
  setModal(true);
  resize(720, 520);

  auto* layout = new QVBoxLayout(this);

  auto* help = new QLabel(
      tr("These rules filter the in-app Torrents RSS table. Each line is a case-insensitive regular "
         "expression applied to the concatenated row text (title/date/page/torrent/magnet)."),
      this);
  help->setWordWrap(true);
  layout->addWidget(help);

  auto* warn = new QLabel(this);
  warn->setWordWrap(true);
  layout->addWidget(warn);

  auto* includeEdit = new QPlainTextEdit(this);
  includeEdit->setPlaceholderText(tr("Show only if matches (one regex per line)…"));
  includeEdit->setPlainText(QString::fromStdString(taiga::settings.torrentFeedIncludeRegexList()));

  auto* excludeEdit = new QPlainTextEdit(this);
  excludeEdit->setPlaceholderText(tr("Hide if matches (one regex per line)…"));
  excludeEdit->setPlainText(QString::fromStdString(taiga::settings.torrentFeedExcludeRegexList()));

  auto* hideDropped = new QCheckBox(tr("Hide titles on my Dropped list"), this);
  hideDropped->setChecked(taiga::settings.torrentFeedHideDropped());
  hideDropped->setToolTip(tr("v1-style filter: keep dropped anime out of torrent results."));
  auto* hideNotInList = new QCheckBox(tr("Hide titles not on my list"), this);
  hideNotInList->setChecked(taiga::settings.torrentFeedHideNotInList());
  hideNotInList->setToolTip(tr("v1-style filter: show only torrents that match anime on your list."));
  auto* hideWatched = new QCheckBox(tr("Hide watched episodes"), this);
  hideWatched->setChecked(taiga::settings.torrentFeedHideWatchedEpisodes());
  hideWatched->setToolTip(tr("v1-style filter: hide torrents for episodes at or below your watched progress."));
  auto* hideAvailable = new QCheckBox(tr("Hide episodes already on disk"), this);
  hideAvailable->setChecked(taiga::settings.torrentFeedHideAvailableEpisodes());
  hideAvailable->setToolTip(tr("v1-style filter: hide torrents for episodes already found in your last library scan."));
  auto* hideOlderVersions = new QCheckBox(tr("Hide older versions when a newer version exists (v2+)"), this);
  hideOlderVersions->setChecked(taiga::settings.torrentFeedHideOlderVersionsWhenNewerExists());
  hideOlderVersions->setToolTip(tr("v1-style preference: when both v1 and v2+ releases exist for the same episode in the feed, hide the older ones."));

  {
    auto* form = new QFormLayout();
    form->addRow(tr("List-based filters:"), new QLabel(tr("Applies after title identification."), this));
    form->addRow(QString{}, hideDropped);
    form->addRow(QString{}, hideNotInList);
    form->addRow(QString{}, hideWatched);
    form->addRow(QString{}, hideAvailable);
    form->addRow(QString{}, hideOlderVersions);
    form->addRow(tr("Show only if matches:"), includeEdit);
    form->addRow(tr("Hide if matches:"), excludeEdit);
    layout->addLayout(form);
  }

  // v1-style archive: discarded torrent titles
  auto* archiveLabel = new QLabel(tr("Discarded titles (archive):"), this);
  auto* archiveList = new QListWidget(this);
  archiveList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  archiveList->setToolTip(tr("Titles you discarded from the Torrents page. These are hidden from future RSS results."));
  const auto refillArchiveList = [archiveList]() {
    archiveList->clear();
    const QStringList titles = taiga::settings.torrentFeedDiscardedTitleArchive();
    for (const QString& t : titles) {
      archiveList->addItem(new QListWidgetItem(t));
    }
  };
  refillArchiveList();
  auto* archiveBtns = new QHBoxLayout();
  auto* b_remove_arch = new QPushButton(tr("Remove selected"), this);
  auto* b_clear_arch = new QPushButton(tr("Clear archive"), this);
  archiveBtns->addWidget(b_remove_arch);
  archiveBtns->addWidget(b_clear_arch);
  archiveBtns->addStretch();
  layout->addWidget(archiveLabel);
  layout->addWidget(archiveList);
  layout->addLayout(archiveBtns);

  auto* presets = new QHBoxLayout();
  presets->addWidget(new QLabel(tr("Presets:"), this));
  auto* b_bad = new QPushButton(tr("Discard bad video keywords (v1)"), this);
  auto* b_1080 = new QPushButton(tr("Prefer 1080p (v1)"), this);
  auto* b_v2 = new QPushButton(tr("Prefer v2+ (v1)"), this);
  auto* b_cam = new QPushButton(tr("Discard CAM/TS/TC"), this);
  auto* b_x265 = new QPushButton(tr("Prefer x265/HEVC"), this);
  auto* b_mkv = new QPushButton(tr("Prefer MKV"), this);
  auto* b_aac = new QPushButton(tr("Prefer AAC/FLAC"), this);
  auto* b_add = new QPushButton(tr("Add rule…"), this);
  auto* b_ci = new QPushButton(tr("Clear “show only”"), this);
  auto* b_ce = new QPushButton(tr("Clear “hide”"), this);

  for (auto* w : {b_bad, b_1080, b_v2, b_cam, b_x265, b_mkv, b_aac, b_add, b_ci, b_ce}) {
    presets->addWidget(w);
  }
  presets->addStretch();
  layout->addLayout(presets);

  connect(b_bad, &QPushButton::clicked, this, [excludeEdit]() {
    addUniqueLine(excludeEdit, QStringLiteral("\\b(AVI|DIVX|LQ|RMVB|SD|WMV|XVID)\\b"));
  });
  connect(b_1080, &QPushButton::clicked, this, [includeEdit]() {
    addUniqueLine(includeEdit, QStringLiteral("\\b1080p\\b"));
  });
  connect(b_v2, &QPushButton::clicked, this, [includeEdit]() {
    addUniqueLine(includeEdit, QStringLiteral("\\bv[2-9]\\b"));
    addUniqueLine(includeEdit, QStringLiteral("\\bv\\d{2,}\\b"));
  });
  connect(b_cam, &QPushButton::clicked, this, [excludeEdit]() {
    addUniqueLine(excludeEdit, QStringLiteral("\\b(CAM|TS|TC)\\b"));
  });
  connect(b_x265, &QPushButton::clicked, this, [includeEdit]() {
    addUniqueLine(includeEdit, QStringLiteral("\\b(x265|hevc)\\b"));
  });
  connect(b_mkv, &QPushButton::clicked, this, [includeEdit]() {
    addUniqueLine(includeEdit, QStringLiteral("\\bMKV\\b"));
  });
  connect(b_aac, &QPushButton::clicked, this, [includeEdit]() {
    addUniqueLine(includeEdit, QStringLiteral("\\b(AAC|FLAC)\\b"));
  });
  connect(b_ci, &QPushButton::clicked, includeEdit, &QPlainTextEdit::clear);
  connect(b_ce, &QPushButton::clicked, excludeEdit, &QPlainTextEdit::clear);

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

  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(box);
  QPushButton* ok = box->button(QDialogButtonBox::Ok);

  const auto updateValid = [=]() {
    const QStringList inc = splitLines(includeEdit->toPlainText());
    const QStringList exc = splitLines(excludeEdit->toPlainText());
    const QSet<QString> exc_set(exc.begin(), exc.end());

    QString warning;
    bool valid = true;

    if (!inc.isEmpty() && exc_set.contains(QStringLiteral(".*"))) {
      warning = tr("Your “Hide if matches” list contains <code>.*</code>, which hides everything, so no results can appear.");
      valid = false;
    } else if (!inc.isEmpty()) {
      int covered = 0;
      for (const QString& s : inc) {
        if (exc_set.contains(s)) ++covered;
      }
      if (covered == inc.size()) {
        warning = tr("All “Show only if matches” rules are also present in “Hide if matches”, so no results can appear.");
        valid = false;
      }
    }

    warn->setText(warning.isEmpty()
                      ? QString{}
                      : (u"<span style=\"color:#c33\"><b>%1</b></span>"_s.arg(warning)));
    ok->setEnabled(valid);
  };

  connect(includeEdit, &QPlainTextEdit::textChanged, this, updateValid);
  connect(excludeEdit, &QPlainTextEdit::textChanged, this, updateValid);
  updateValid();

  connect(b_add, &QPushButton::clicked, this, [=]() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add torrent filter rule"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout();
    auto* target = new QComboBox(&dlg);
    target->addItem(tr("Show only if matches"), 0);
    target->addItem(tr("Hide if matches"), 1);
    auto* mode = new QComboBox(&dlg);
    mode->addItem(tr("Contains (case-insensitive)"), 0);
    mode->addItem(tr("Whole word (case-insensitive)"), 1);
    mode->addItem(tr("Regular expression"), 2);
    auto* text = new QLineEdit(&dlg);
    text->setPlaceholderText(tr("Example: 1080p"));
    form->addRow(tr("Apply to:"), target);
    form->addRow(tr("Match:"), mode);
    form->addRow(tr("Text / regex:"), text);
    lay->addLayout(form);

    auto* box2 = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QPushButton* ok2 = box2->button(QDialogButtonBox::Ok);
    ok2->setEnabled(false);
    lay->addWidget(box2);
    connect(box2, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box2, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    const auto rebuildOk = [=]() {
      const QString v = text->text().trimmed();
      if (v.isEmpty()) {
        ok2->setEnabled(false);
        return;
      }
      if (mode->currentData().toInt() == 2) {
        QRegularExpression re(v, QRegularExpression::CaseInsensitiveOption);
        ok2->setEnabled(re.isValid());
        return;
      }
      ok2->setEnabled(true);
    };
    connect(text, &QLineEdit::textChanged, &dlg, [rebuildOk]() { rebuildOk(); });
    connect(mode, &QComboBox::currentIndexChanged, &dlg, [rebuildOk](int) { rebuildOk(); });
    rebuildOk();

    if (dlg.exec() != QDialog::Accepted) return;

    const QString raw = text->text().trimmed();
    const int where = target->currentData().toInt();
    const int how = mode->currentData().toInt();
    QString line;
    if (how == 2) {
      line = raw;
    } else {
      const QString escaped = QRegularExpression::escape(raw);
      line = (how == 1) ? QStringLiteral("\\b%1\\b").arg(escaped) : escaped;
    }
    addUniqueLine(where == 1 ? excludeEdit : includeEdit, line);
  });

  connect(box, &QDialogButtonBox::accepted, this, [=]() {
    taiga::settings.setTorrentFeedIncludeRegexList(includeEdit->toPlainText().trimmed().toStdString());
    taiga::settings.setTorrentFeedExcludeRegexList(excludeEdit->toPlainText().trimmed().toStdString());
    taiga::settings.setTorrentFeedHideDropped(hideDropped->isChecked());
    taiga::settings.setTorrentFeedHideNotInList(hideNotInList->isChecked());
    taiga::settings.setTorrentFeedHideWatchedEpisodes(hideWatched->isChecked());
    taiga::settings.setTorrentFeedHideAvailableEpisodes(hideAvailable->isChecked());
    taiga::settings.setTorrentFeedHideOlderVersionsWhenNewerExists(hideOlderVersions->isChecked());
    accept();
  });
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

}  // namespace gui

