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

#include "anime_list_item_delegate_cards.hpp"

#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <cmath>

#include "base/string.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/painter_state_saver.hpp"
#include "gui/utils/painters.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_season.hpp"


namespace gui {

constexpr int itemHeight = 210;
constexpr int posterWidth = itemHeight * 2 / 3;

ListItemDelegateCards::ListItemDelegateCards(QObject* parent) : QStyledItemDelegate(parent) {}

void ListItemDelegateCards::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
  const PainterStateSaver painterStateSaver(painter);

  const auto font = painter->font();

  const auto item =
      index.data(static_cast<int>(AnimeListItemDataRole::Anime)).value<const Anime*>();
  const auto entry =
      index.data(static_cast<int>(AnimeListItemDataRole::ListEntry)).value<const ListEntry*>();
  if (!item) return;

  QStyleOptionViewItem opt = option;
  QRect rect = opt.rect;
  if (rect.width() < 1 || rect.height() < 1) return;

  const bool selected = option.state & QStyle::State_Selected;

  QPainterPath path;
  path.addRoundedRect(rect, 4, 4);
  painter->setClipPath(path);

  // Background
  if (selected) {
    painter->fillRect(rect, opt.palette.highlight());
  } else if (theme.isDark()) {
    painter->fillRect(rect, opt.palette.mid());
  } else {
    painter->fillRect(rect, opt.palette.alternateBase());
  }

  // Poster
  {
    QRect posterRect = rect;
    posterRect.setWidth(posterWidth);

    if (!selected) {
      if (theme.isDark()) {
        painter->fillRect(posterRect, opt.palette.dark());
      } else {
        painter->fillRect(posterRect, opt.palette.mid());
      }
    }

    const QPixmap pixmap =
        index.data(static_cast<int>(AnimeListItemDataRole::Poster)).value<QPixmap>();

    if (!pixmap.isNull() && posterRect.width() > 0 && posterRect.height() > 0) {
      const QSize scaled =
          pixmap.size().scaled(posterRect.size(), Qt::AspectRatioMode::KeepAspectRatioByExpanding);
      if (scaled.width() < 1 || scaled.height() < 1) {
        // Degenerate layout / pixmap — avoid division by zero in crop math.
      } else {
        QRect sourceRect{pixmap.rect()};
        if (scaled.width() > posterRect.width()) {
          const auto half = (scaled.width() - posterRect.width()) / 2.0f;
          const auto scale =
              static_cast<float>(pixmap.width()) / static_cast<float>(scaled.width());
          sourceRect.adjust(static_cast<int>(half * scale), 0, static_cast<int>(-half * scale), 0);
        } else {
          const auto half = (scaled.height() - posterRect.height()) / 2.0f;
          const auto scale =
              static_cast<float>(pixmap.height()) / static_cast<float>(scaled.height());
          sourceRect.adjust(0, static_cast<int>(half * scale), 0, static_cast<int>(-half * scale));
        }

        painter->drawPixmap(posterRect, pixmap, sourceRect);
      }
    }
  }

  if (entry) {
    auto progressOptions = opt;
    progressOptions.rect = rect;
    progressOptions.rect.setWidth(posterWidth);
    progressOptions.rect.setTop(progressOptions.rect.bottom() - 28);
    progressOptions.rect.adjust(4, 4, -4, -4);
    paintProgressBar(painter, progressOptions, item, entry);
  }

  rect.adjust(posterWidth, 0, 0, 0);

  // Title
  {
    QRect titleRect = rect;
    titleRect.setHeight(32);

    if (!selected) {
      painter->fillRect(titleRect, opt.palette.dark());
    }
    titleRect.adjust(12, 0, -12, 0);

    auto titleFont = font;
    titleFont.setPointSize(10);
    titleFont.setWeight(QFont::Weight::DemiBold);
    painter->setFont(titleFont);

    if (selected) {
      painter->setPen(opt.palette.color(QPalette::HighlightedText));
    } else if (const QVariant fg = index.data(Qt::ForegroundRole); fg.canConvert<QColor>()) {
      painter->setPen(fg.value<QColor>());
    } else {
      painter->setPen(opt.palette.windowText().color());
    }

    const QString title = index.data(Qt::DisplayRole).toString();
    const QFontMetrics metrics(painter->font());
    const QString elidedTitle = metrics.elidedText(title, Qt::ElideRight, titleRect.width());

    painter->drawText(titleRect, Qt::AlignVCenter | Qt::TextSingleLine, elidedTitle);

    rect.adjust(12, 32 + 8, -12, -12);
  }

  // Summary
  {
    QStringList parts{
        formatType(item->type),
        formatSeasonDate(item->date_started),
        formatScore(item->score),
    };
    if (item->episode_count != 1) {
      parts.insert(1, tr("%1 episodes").arg(formatNumber(item->episode_count, "?")));
    }
    const QString summary = parts.join(" · ");

    auto summaryFont = font;
    summaryFont.setWeight(QFont::Weight::DemiBold);
    painter->setFont(summaryFont);

    const QFontMetrics metrics(painter->font());
    QRect summaryRect = rect;
    summaryRect.setHeight(metrics.height());

    if (selected) {
      painter->setPen(opt.palette.color(QPalette::HighlightedText));
    } else {
      painter->setPen(opt.palette.windowText().color());
    }
    painter->drawText(summaryRect, Qt::AlignVCenter | Qt::TextSingleLine, summary);

    rect.adjust(0, summaryRect.height() + 8, 0, 0);
  }

  // Details
  {
    const QStringList lines{
        u"%1 (%2)"_s.arg(formatFuzzyDateRange(item->date_started, item->date_finished))
            .arg(formatStatus(item->status)),
        joinStrings(item->genres),
        joinStrings(!item->studios.empty() ? item->studios : item->producers),
    };

    painter->setFont(font);

    const QFontMetrics metrics(painter->font());
    QRect lineStrip = rect;

    if (selected) {
      painter->setPen(opt.palette.color(QPalette::HighlightedText));
    } else {
      painter->setPen(opt.palette.windowText().color());
    }
    for (const auto& line : lines) {
      QRect lineRect = lineStrip;
      lineRect.setHeight(metrics.height());
      const QString elidedLine = metrics.elidedText(line, Qt::ElideRight, lineRect.width());
      painter->drawText(lineRect, Qt::TextSingleLine, elidedLine);
      lineStrip.adjust(0, metrics.height(), 0, 0);
    }

    rect.adjust(0, (metrics.height() * lines.size()) + 8, 0, 0);
  }

  // Synopsis
  {
    QString synopsis = QString::fromStdString(item->synopsis);
    synopsis.replace("<br>", "\n");
    removeHtmlTags(synopsis);
    synopsis = synopsis.simplified();

    painter->setPen(selected ? opt.palette.color(QPalette::HighlightedText)
                             : opt.palette.placeholderText().color());

    auto synopsisFont = painter->font();
    synopsisFont.setPointSize(8);
    painter->setFont(synopsisFont);
    const QFontMetrics metrics(painter->font());

    QRect synopsisRect = rect;
    synopsisRect.setHeight(qMin(synopsisRect.height(), metrics.height() * 5));

    painter->drawText(synopsisRect, Qt::TextWordWrap, synopsis);
  }
}

QSize ListItemDelegateCards::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
  if (index.isValid()) return itemSize();
  return QStyledItemDelegate::sizeHint(option, index);
}

void ListItemDelegateCards::initStyleOption(QStyleOptionViewItem* option,
                                            const QModelIndex& index) const {
  QStyledItemDelegate::initStyleOption(option, index);

  option->features &= ~QStyleOptionViewItem::ViewItemFeature::HasDisplay;
  option->features &= ~QStyleOptionViewItem::ViewItemFeature::HasDecoration;
}

QSize ListItemDelegateCards::itemSize() const {
  constexpr int maxColumns = 4;
  constexpr int maxItemWidth = 360;
  constexpr int minItemWidth = 180;

  const auto* list = qobject_cast<const QListView*>(this->parent());
  if (!list) return QSize(maxItemWidth, itemHeight);

  const int spacing = list->spacing();
  const QScrollBar* vsb = list->verticalScrollBar();
  const int scrollbarW = (vsb && vsb->isVisible()) ? vsb->width() : 0;
  int vw = list->viewport() ? list->viewport()->width() : list->width();
  // Width is often 0 before the first layout pass; negative item widths break QListView layout.
  vw = qMax(vw, maxItemWidth + 2 * spacing + scrollbarW);

  int availableWidth = vw - ((2 * spacing) + scrollbarW);
  availableWidth = qMax(availableWidth, minItemWidth);

  const int columns = [&]() {
    for (int i = maxColumns; i >= 1; --i) {
      if (availableWidth - (i * spacing) > i * maxItemWidth) return i;
    }
    return 1;
  }();

  const int columnsWidth = availableWidth - (columns * spacing);
  const float itemWidth = qMax(static_cast<float>(minItemWidth),
                               static_cast<float>(columnsWidth) / static_cast<float>(columns));

  return QSize(static_cast<int>(std::floor(itemWidth)), itemHeight);
}

}  // namespace gui
