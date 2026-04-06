/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "stats_dialog.hpp"

#include <cmath>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include <QDir>

#include "taiga/settings.hpp"
#include "taiga/stats.hpp"

namespace gui {

namespace {

QString formatDuration(int total_seconds) {
  if (total_seconds <= 0) {
    return QDialog::tr("None");
  }
  const int days = total_seconds / 86400;
  const int hours = (total_seconds % 86400) / 3600;
  const int minutes = (total_seconds % 3600) / 60;
  if (days > 0) {
    return QDialog::tr("%1 days, %2 hours").arg(days).arg(hours);
  }
  if (hours > 0) {
    return QDialog::tr("%1 hours, %2 minutes").arg(hours).arg(minutes);
  }
  return QDialog::tr("%1 minutes").arg(minutes);
}

QString formatBytes(qint64 bytes) {
  constexpr qint64 k = 1024;
  if (bytes < k) return QString::number(bytes) + QDialog::tr(" B");
  if (bytes < k * k) return QDialog::tr("%1 KB").arg(bytes / k);
  if (bytes < k * k * k) return QDialog::tr("%1 MB").arg(bytes / (k * k));
  return QDialog::tr("%1 GB").arg(bytes / (k * k * k));
}

QLabel* sectionTitle(const QString& text, QWidget* parent) {
  auto* l = new QLabel(text, parent);
  QFont f = l->font();
  f.setBold(true);
  l->setFont(f);
  l->setContentsMargins(0, 12, 0, 4);
  return l;
}

}  // namespace

void StatsDialog::show(QWidget* parent) {
  const taiga::ListStatistics s = taiga::computeListStatistics();

  auto* dlg = new QDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->setWindowTitle(QDialog::tr("Statistics"));
  dlg->resize(480, 520);

  auto* root = new QVBoxLayout(dlg);
  auto* scroll = new QScrollArea(dlg);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto* body = new QWidget();
  auto* lay = new QVBoxLayout(body);

  lay->addWidget(sectionTitle(QDialog::tr("Anime list"), body));
  {
    const QString mean =
        s.scored_title_count > 0
            ? QString::number(static_cast<double>(s.mean_score_0_100) / 10.0, 'f', 2)
            : QDialog::tr("—");
    const QString dev =
        s.scored_title_count > 0
            ? QString::number(static_cast<double>(s.score_stddev_0_100) / 10.0, 'f', 2)
            : QDialog::tr("—");
    auto* t = new QLabel(
        QDialog::tr("<table style=\"margin-left:0.2em\" cellspacing=\"6\">"
                    "<tr><td><b>%1</b></td><td>%2</td></tr>"
                    "<tr><td><b>%3</b></td><td>%4</td></tr>"
                    "<tr><td><b>%5</b></td><td>%6</td></tr>"
                    "<tr><td><b>%7</b></td><td>%8</td></tr>"
                    "<tr><td><b>%9</b></td><td>%10 / 10 (σ %11)</td></tr>"
                    "</table>")
            .arg(QDialog::tr("Titles on list"))
            .arg(s.anime_on_list)
            .arg(QDialog::tr("Episodes watched (incl. rewatches)"))
            .arg(s.episode_equivalents)
            .arg(QDialog::tr("Time spent watching (est.)"))
            .arg(formatDuration(s.spent_watch_seconds))
            .arg(QDialog::tr("Time left to watch (est.)"))
            .arg(formatDuration(s.planned_watch_seconds))
            .arg(QDialog::tr("Mean score"))
            .arg(mean)
            .arg(dev),
        body);
    t->setTextFormat(Qt::RichText);
    lay->addWidget(t);
  }

  lay->addWidget(sectionTitle(QDialog::tr("Score distribution (0–100, by tens)"), body));
  {
    auto* box = new QVBoxLayout();
    constexpr int kMaxBar = 36;
    for (int label = 10; label >= 0; --label) {
      const size_t idx = static_cast<size_t>(label);
      const int count = s.score_histogram[idx];
      const int filled =
          std::max(0, static_cast<int>(std::lround(s.score_bar_fraction[idx] * kMaxBar)));
      const QString bar = QString(filled, QChar(0x2588));
      auto* row =
          new QLabel(QDialog::tr("%1: <span style='font-family:monospace'>%2</span> %3")
                         .arg(label)
                         .arg(bar)
                         .arg(count),
                     body);
      row->setTextFormat(Qt::RichText);
      box->addWidget(row);
    }
    lay->addLayout(box);
  }

  lay->addWidget(sectionTitle(QDialog::tr("Local data"), body));
  {
    const auto folders = taiga::settings.libraryFolders();
    QString folder_lines;
    if (folders.empty()) {
      folder_lines = QDialog::tr("<i>None configured</i> (Settings → Library)");
    } else {
      constexpr int kMaxLines = 6;
      for (size_t i = 0; i < folders.size() && i < kMaxLines; ++i) {
        const QString p = QString::fromStdString(folders[i]).toHtmlEscaped();
        folder_lines += QDialog::tr("<div style=\"margin-left:1em\">%1</div>").arg(p);
      }
      if (folders.size() > static_cast<size_t>(kMaxLines)) {
        folder_lines += QDialog::tr("<div style=\"margin-left:1em;color:#666\">…</div>");
      }
    }
    auto* t = new QLabel(
        QDialog::tr("<table style=\"margin-left:0.2em\" cellspacing=\"6\">"
                    "<tr><td><b>%1</b></td><td>%2</td></tr>"
                    "<tr><td><b>%3</b></td><td>%4</td></tr>"
                    "<tr><td><b>%5</b></td><td>%6 (%7)</td></tr>"
                    "<tr><td colspan=\"2\" style=\"color:#666;font-size:small\">%8</td></tr>"
                    "</table>")
            .arg(QDialog::tr("Anime in database"))
            .arg(s.db_items)
            .arg(QDialog::tr("Library folders"))
            .arg(static_cast<int>(folders.size()))
            .arg(QDialog::tr("Cached posters"))
            .arg(s.poster_file_count)
            .arg(formatBytes(s.poster_bytes))
            .arg(QDialog::tr("Torrent cache is not used in this build.")),
        body);
    t->setTextFormat(Qt::RichText);
    lay->addWidget(t);
    auto* paths = new QLabel(
        QDialog::tr("<div style=\"margin-left:0.2em;margin-top:-4px\"><b>%1</b>%2</div>")
            .arg(QDialog::tr("Paths: "))
            .arg(folder_lines),
        body);
    paths->setTextFormat(Qt::RichText);
    paths->setWordWrap(true);
    lay->addWidget(paths);

    const QString t_client = QString::fromStdString(taiga::settings.torrentClientDownloadPath());
    const QString t_save = QString::fromStdString(taiga::settings.torrentFileSavePath());
    const QString t_app = QString::fromStdString(taiga::settings.torrentAppExecutablePath());
    QString torrent_block;
    if (t_client.isEmpty() && t_save.isEmpty() && t_app.isEmpty()) {
      torrent_block = QDialog::tr("<i>Not set</i> (Settings → Library → Torrents)");
    } else {
      const bool same = !t_client.isEmpty() && !t_save.isEmpty() &&
                        QDir{t_client}.absolutePath() == QDir{t_save}.absolutePath();
      if (same) {
        torrent_block = QDialog::tr("<div style=\"margin-left:1em\">%1</div>")
                            .arg(t_client.toHtmlEscaped());
      } else {
        if (!t_client.isEmpty()) {
          torrent_block += QDialog::tr("<div style=\"margin-left:1em\"><b>%1</b> %2</div>")
                               .arg(QDialog::tr("Client download:"))
                               .arg(t_client.toHtmlEscaped());
        }
        if (!t_save.isEmpty()) {
          torrent_block += QDialog::tr("<div style=\"margin-left:1em\"><b>%1</b> %2</div>")
                               .arg(QDialog::tr(".torrent save:"))
                               .arg(t_save.toHtmlEscaped());
        }
      }
      if (!t_app.isEmpty()) {
        torrent_block += QDialog::tr("<div style=\"margin-left:1em\"><b>%1</b> %2</div>")
                             .arg(QDialog::tr("Custom client:"))
                             .arg(t_app.toHtmlEscaped());
      }
    }
    auto* tpaths = new QLabel(
        QDialog::tr("<div style=\"margin-left:0.2em;margin-top:6px\"><b>%1</b>%2</div>")
            .arg(QDialog::tr("Torrent paths: "))
            .arg(torrent_block),
        body);
    tpaths->setTextFormat(Qt::RichText);
    tpaths->setWordWrap(true);
    lay->addWidget(tpaths);
  }

  lay->addStretch(1);
  scroll->setWidget(body);
  root->addWidget(scroll);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
  root->addWidget(buttons);

  dlg->exec();
}

}  // namespace gui
