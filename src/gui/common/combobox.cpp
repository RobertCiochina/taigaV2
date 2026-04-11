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

#include "combobox.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QStyledItemDelegate>

namespace gui {

namespace {

class ComboPopupListView final : public QListView {
public:
  explicit ComboPopupListView(QWidget* parent) : QListView(parent) {}

  QModelIndex hoveredIndex() const {
    return hovered_index_;
  }

protected:
  void mouseMoveEvent(QMouseEvent* event) override {
    // Prevent "hover selects row" behavior (which can also auto-scroll near edges).
    // Keep drag interactions working (e.g. selecting while holding a button).
    if (event && event->buttons() == Qt::NoButton) {
      const QModelIndex idx = indexAt(event->pos());
      if (idx != hovered_index_) {
        const QModelIndex prev = hovered_index_;
        hovered_index_ = idx;
        if (prev.isValid()) viewport()->update(visualRect(prev));
        if (hovered_index_.isValid()) viewport()->update(visualRect(hovered_index_));
      }
      event->accept();
      return;
    }
    QListView::mouseMoveEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    const QModelIndex prev = hovered_index_;
    hovered_index_ = {};
    if (prev.isValid()) viewport()->update(visualRect(prev));
    QListView::leaveEvent(event);
  }

private:
  QModelIndex hovered_index_;
};

class ComboPopupItemDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt = option;
    const auto* view = static_cast<const ComboPopupListView*>(opt.widget);
    const bool hovered = view && index == view->hoveredIndex();
    if (hovered) opt.state |= QStyle::State_MouseOver;

    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    // Avoid the heavy/dark platform-selected background; we draw our own.
    if (selected) opt.state &= ~QStyle::State_Selected;

    // Make hover obvious even if the platform style doesn't draw State_MouseOver strongly.
    if (hovered && painter) {
      QColor c = QApplication::palette().color(QPalette::Highlight);
      c.setAlpha(64);
      painter->save();
      painter->setPen(Qt::NoPen);
      painter->setBrush(c);
      painter->drawRect(opt.rect);
      painter->restore();
    }

    // Subtle highlight for the currently selected value (no black strip).
    if (selected && painter) {
      QColor c = QApplication::palette().color(QPalette::Highlight);
      c.setAlpha(40);
      painter->save();
      painter->setPen(Qt::NoPen);
      painter->setBrush(c);
      painter->drawRect(opt.rect);
      painter->restore();
    }

    QStyledItemDelegate::paint(painter, opt, index);
  }
};

}  // namespace

ComboBox::ComboBox(QWidget* parent) : QComboBox(parent) {
  // We intentionally do NOT rely on the native QComboBox popup container because some platforms
  // show an edge-hover "scroll zone" (small arrow strips). We'll show a simple Qt::Popup frame
  // with our own QListView instead.
  ensurePopup();
}

void ComboBox::ensurePopup() {
  if (popup_frame_) return;

  popup_frame_ = new QFrame(nullptr, Qt::Popup);
  popup_frame_->setObjectName(QStringLiteral("taigaComboPopup"));
  popup_frame_->setFrameShape(QFrame::StyledPanel);
  popup_frame_->setFrameShadow(QFrame::Plain);

  auto* layout = new QHBoxLayout(popup_frame_);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  popup_list_ = new ComboPopupListView(popup_frame_);
  popup_list_->setUniformItemSizes(true);
  popup_list_->setTextElideMode(Qt::ElideNone);
  popup_list_->setMouseTracking(true);  // used only for hover highlight (no selection/scroll)
  popup_list_->setAutoScroll(false);
  popup_list_->setAutoScrollMargin(0);
  popup_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popup_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popup_list_->setCursor(Qt::PointingHandCursor);
  popup_list_->setItemDelegate(new ComboPopupItemDelegate(popup_list_));
  if (auto* vp = popup_list_->viewport()) vp->setMouseTracking(true);

  layout->addWidget(popup_list_);

  // Close on focus/activation loss.
  popup_frame_->installEventFilter(this);

  // Click / keyboard activation selects + closes. Must emit `activated(int)` like the native
  // popup — slots (e.g. Watch Next layout → `rebuildCards`) connect to that, not
  // `currentIndexChanged`.
  const auto apply_popup_choice = [this](const QModelIndex& idx) {
    if (!idx.isValid()) return;
    if (!popup_frame_ || !popup_frame_->isVisible()) return;
    const int row = idx.row();
    if (row < 0 || row >= count()) return;
    setCurrentIndex(row);
    hidePopup();
    emit activated(row);
  };
  connect(popup_list_, &QListView::clicked, this, apply_popup_choice);
  connect(popup_list_, &QAbstractItemView::activated, this, apply_popup_choice);
}

void ComboBox::positionAndShowPopup() {
  ensurePopup();
  if (!popup_frame_ || !popup_list_) return;

  popup_list_->setModel(model());
  popup_list_->setRootIndex(rootModelIndex());

  // Selection: keep current row visible (best-effort).
  if (currentIndex() >= 0) {
    popup_list_->setCurrentIndex(model()->index(currentIndex(), modelColumn(), rootModelIndex()));
    popup_list_->scrollTo(popup_list_->currentIndex(), QAbstractItemView::PositionAtCenter);
  }

  // Size.
  const int max_rows = std::max(1, maxVisibleItems());
  const int total_rows = model() ? model()->rowCount(rootModelIndex()) : 0;
  const int visible_rows = std::clamp(total_rows, 1, max_rows);
  int row_h = popup_list_->sizeHintForRow(0);
  if (row_h <= 0) row_h = fontMetrics().height() + 6;
  const int fw = popup_frame_->frameWidth();
  const int chrome_h = std::max(2, fw * 2 + 2);
  const int max_h = row_h * visible_rows + chrome_h;

  // Fixed height prevents unused blank space for short lists (Season/Type/Status).
  popup_frame_->setFixedHeight(max_h);
  popup_list_->setFixedHeight(std::max(1, max_h - fw * 2));

  const int col_w = std::max(0, popup_list_->sizeHintForColumn(0));
  const int sb_w =
      popup_list_->verticalScrollBar() ? popup_list_->verticalScrollBar()->sizeHint().width() : 0;
  const bool content_width =
      this->property("taiga.popupWidthMode").toString() == QStringLiteral("content");
  const int want_w =
      content_width ? std::max(60, col_w + sb_w + 24) : std::max(width(), col_w + sb_w + 24);
  // Use fixed width (min=max) so the popup doesn't expand to the wide toolbar combobox.
  popup_list_->setMinimumWidth(want_w);
  popup_list_->setMaximumWidth(want_w);
  popup_frame_->setMinimumWidth(want_w);
  popup_frame_->setMaximumWidth(want_w);
  popup_frame_->resize(want_w, max_h);

  // Position under the combobox, clamped to the current screen.
  const QPoint below = mapToGlobal(QPoint(0, height()));
  QScreen* screen = QGuiApplication::screenAt(below);
  if (!screen) screen = QGuiApplication::primaryScreen();
  const QRect avail = screen ? screen->availableGeometry() : QRect{};

  QPoint pos = below;
  if (avail.isValid()) {
    if (pos.x() + want_w > avail.right()) pos.setX(std::max(avail.left(), avail.right() - want_w));
    if (pos.y() + max_h > avail.bottom()) pos.setY(std::max(avail.top(), avail.bottom() - max_h));
  }

  popup_frame_->move(pos);
  popup_frame_->show();
  popup_frame_->raise();
}

void ComboBox::showPopup() {
  positionAndShowPopup();
}

void ComboBox::hidePopup() {
  if (popup_frame_) popup_frame_->hide();
  QComboBox::hidePopup();
}

bool ComboBox::eventFilter(QObject* watched, QEvent* event) {
  // Close custom popup when it loses activation/focus.
  if (popup_frame_ && watched == popup_frame_ && event) {
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::FocusOut) {
      hidePopup();
      return false;
    }
  }
  return QComboBox::eventFilter(watched, event);
}

void ComboBox::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    const QVariant esc = property("taiga.clearOnEscape");
    const bool clear_on_escape = !esc.isValid() || esc.toBool();
    if (clear_on_escape) {
      setCurrentIndex(-1);
      return;
    }
    if (popup_frame_ && popup_frame_->isVisible()) {
      hidePopup();
      event->accept();
      return;
    }
  }

  QComboBox::keyPressEvent(event);
}

void ComboBox::mousePressEvent(QMouseEvent* event) {
  const QVariant chord = property("taiga.clearOnChordClicks");
  const bool clear_on_chord = !chord.isValid() || chord.toBool();
  if (clear_on_chord && (event->button() == Qt::MouseButton::MiddleButton ||
                         event->button() == Qt::MouseButton::RightButton)) {
    setCurrentIndex(-1);
    return;
  }

  QComboBox::mousePressEvent(event);
}

}  // namespace gui
