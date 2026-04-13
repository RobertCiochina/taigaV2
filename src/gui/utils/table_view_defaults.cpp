/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
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

#include "table_view_defaults.hpp"

#include <QAbstractItemView>
#include <QAbstractItemDelegate>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIdentityProxyModel>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTreeView>
#include <QTimer>

#include "gui/utils/cell_alignment_proxy_model.hpp"
#include "gui/utils/header_alignment_proxy_model.hpp"

namespace gui::tables {

namespace {

void relayoutNow(QAbstractItemView* view) {
  if (!view) return;
  if (auto* tv = qobject_cast<QTableView*>(view)) {
    tv->resizeRowsToContents();
    if (tv->viewport()) tv->viewport()->update();
  } else if (auto* tr = qobject_cast<QTreeView*>(view)) {
    tr->doItemsLayout();
    if (tr->viewport()) tr->viewport()->update();
  }
}

void autosizeColumnsNow(QAbstractItemView* view) {
  if (!view) return;
  QHeaderView* h = nullptr;
  QTableView* tv = qobject_cast<QTableView*>(view);
  QTreeView* tr = tv ? nullptr : qobject_cast<QTreeView*>(view);
  if (tv) h = tv->horizontalHeader();
  if (tr) h = tr->header();
  if (!h) return;
  if (h->count() <= 0) return;
  const int cap = view->property("_taiga_max_auto_col_w").toInt();
  if (cap <= 0) return;
  const int min_text = view->property("_taiga_min_text_col_w").toInt();
  const int min_first = view->property("_taiga_min_first_col_w").toInt();
  const auto* m = view->model();
  const QFontMetrics hfm(h->font());
  const int row_count = m ? m->rowCount() : 0;
  const bool use_qt_resize_to_contents = (row_count > 0 && row_count <= 500);
  int first_visible = -1;
  for (int c = 0; c < h->count(); ++c) {
    if (!h->isSectionHidden(c)) { first_visible = c; break; }
  }
  for (int c = 0; c < h->count(); ++c) {
    if (h->isSectionHidden(c)) continue;
    int w = 0;
    if (use_qt_resize_to_contents) {
      if (tv) tv->resizeColumnToContents(c);
      if (tr) tr->resizeColumnToContents(c);
      w = h->sectionSize(c);
    } else if (m) {
      w = 0;
      const int sample_n = std::min(row_count, 200);
      const int step = (sample_n > 0) ? std::max(1, row_count / sample_n) : 1;
      QFontMetrics fm(view->font());
      for (int r = 0, taken = 0; r < row_count && taken < sample_n; r += step, ++taken) {
        const QModelIndex idx = m->index(r, c);
        const QString text = idx.data(Qt::DisplayRole).toString();
        if (text.isEmpty()) continue;
        w = std::max(w, fm.horizontalAdvance(text));
        if (w > cap) break;
      }
      w += 24;
    } else {
      w = h->sectionSize(c);
    }
    if (w > cap) w = cap;

    int minw = std::max(0, h->minimumSectionSize());
    if (m) {
      const QString label = m->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
      if (!label.isEmpty()) minw = std::max(minw, hfm.horizontalAdvance(label) + 18);
      const auto align_v = m->headerData(c, Qt::Horizontal, Qt::TextAlignmentRole);
      const int align = align_v.isValid() ? align_v.toInt() : 0;
      const bool numeric_like = (align & Qt::AlignRight) != 0;
      if (!numeric_like && min_text > 0) minw = std::max(minw, min_text);
    } else if (min_text > 0) {
      minw = std::max(minw, min_text);
    }
    if (c == first_visible && min_first > 0) minw = std::max(minw, min_first);

    if (w < minw) w = minw;
    h->resizeSection(c, w);
  }
}

class DelegatingWrapDelegate final : public QAbstractItemDelegate {
public:
  DelegatingWrapDelegate(QAbstractItemView* view, QAbstractItemDelegate* inner, QObject* parent)
      : QAbstractItemDelegate(parent), view_(view), inner_(inner) {
    if (!inner_) {
      inner_ = new QStyledItemDelegate(this);
    } else {
      // Adopt the inner delegate so it lives at least as long as this wrapper.
      if (inner_->parent() == nullptr) inner_->setParent(this);
    }
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    inner_->paint(painter, option, index);
  }

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    QSize s = inner_->sizeHint(option, index);
    if (!view_ || !index.isValid()) return s;
    const QAbstractItemView* v = view_.data();
    const auto* tv = v ? qobject_cast<const QTableView*>(v) : nullptr;
    const auto* tr = tv ? nullptr : (v ? qobject_cast<const QTreeView*>(v) : nullptr);
    const bool wrap_on = (tv && tv->wordWrap()) || (tr && tr->wordWrap());
    if (!wrap_on) return s;

    const QString text = index.data(Qt::DisplayRole).toString();
    if (text.isEmpty()) return s;

    // Use actual column width so height only grows when wrapping is needed.
    int width = 0;
    if (auto* tv = qobject_cast<const QTableView*>(view_.data())) {
      width = tv->columnWidth(index.column());
    } else if (auto* tr = qobject_cast<const QTreeView*>(view_.data())) {
      width = tr->columnWidth(index.column());
    }
    width = std::max(40, width - 10);

    const QFontMetrics fm(option.font);
    const QRect br = fm.boundingRect(QRect(0, 0, width, 10000),
                                     Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, text);
    const int base_h = std::max(18, fm.height() + 6);
    s.setHeight(std::max({s.height(), base_h, br.height() + 6}));
    return s;
  }

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const override {
    return inner_->createEditor(parent, option, index);
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override {
    inner_->setEditorData(editor, index);
  }

  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override {
    inner_->setModelData(editor, model, index);
  }

  void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const override {
    inner_->updateEditorGeometry(editor, option, index);
  }

  bool editorEvent(QEvent* event, QAbstractItemModel* model,
                   const QStyleOptionViewItem& option,
                   const QModelIndex& index) override {
    return inner_->editorEvent(event, model, option, index);
  }

  bool helpEvent(QHelpEvent* event, QAbstractItemView* view,
                 const QStyleOptionViewItem& option,
                 const QModelIndex& index) override {
    return inner_->helpEvent(event, view, option, index);
  }

private:
  QPointer<QAbstractItemView> view_;
  QAbstractItemDelegate* inner_ = nullptr;
};

static constexpr const char* kDelegateProperty = "_taiga_wrap_delegate";

void ensureWrapDelegate(QAbstractItemView* view) {
  if (!view) return;
  if (view->property(kDelegateProperty).toBool()) return;
  QAbstractItemDelegate* inner = view->itemDelegate();
  auto* d = new DelegatingWrapDelegate(view, inner, view);
  view->setItemDelegate(d);
  view->setProperty(kDelegateProperty, true);
}

static constexpr const char* kCenteredHeaderProxyProperty = "_taiga_centered_header_proxy";

void ensureCenteredHeaderProxy(QAbstractItemView* view) {
  if (!view) return;
  if (view->property(kCenteredHeaderProxyProperty).toBool()) return;
  QAbstractItemModel* m = view->model();
  if (!m) return;
  if (qobject_cast<HeaderAlignmentProxyModel*>(m)) {
    view->setProperty(kCenteredHeaderProxyProperty, true);
    return;
  }
  auto* p = new HeaderAlignmentProxyModel(view);
  p->setSourceModel(m);
  view->setModel(p);
  view->setProperty(kCenteredHeaderProxyProperty, true);
}

static constexpr const char* kLeftCellProxyProperty = "_taiga_left_cell_proxy";

void ensureLeftCellProxy(QAbstractItemView* view) {
  if (!view) return;
  if (view->property(kLeftCellProxyProperty).toBool()) return;
  QAbstractItemModel* m = view->model();
  if (!m) return;
  if (qobject_cast<CellAlignmentProxyModel*>(m)) {
    view->setProperty(kLeftCellProxyProperty, true);
    return;
  }
  auto* p = new CellAlignmentProxyModel(view);
  p->setSourceModel(m);
  view->setModel(p);
  view->setProperty(kLeftCellProxyProperty, true);
}

static constexpr const char* kAdaptiveWrapHookProperty = "_taiga_adaptive_wrap_hook";

void ensureAdaptiveWrapHook(QAbstractItemView* view) {
  if (!view) return;
  if (view->property(kAdaptiveWrapHookProperty).toBool()) return;

  // Debounce relayout: QTreeView especially can lag behind column resizes unless we update
  // once the resize has settled. Keep it centralized and consistent.
  auto* t = new QTimer(view);
  t->setSingleShot(true);
  t->setInterval(25);

  const auto relayoutNowFn = [view]() { relayoutNow(view); };

  QObject::connect(t, &QTimer::timeout, view, [relayoutNowFn]() { relayoutNowFn(); });

  const auto relayout = [t]() {
    if (!t) return;
    t->start();
  };

  auto autosize_columns = [view, relayout]() {
    autosizeColumnsNow(view);
    relayout();
  };

  // Relayout when model changes.
  if (auto* m = view->model()) {
    QObject::connect(m, &QAbstractItemModel::modelReset, view, relayout);
    QObject::connect(m, &QAbstractItemModel::layoutChanged, view, relayout);
    QObject::connect(m, &QAbstractItemModel::rowsInserted, view, relayout);
    QObject::connect(m, &QAbstractItemModel::dataChanged, view, relayout);

    // Only auto-size columns on structural resets, not on every dataChanged.
    QObject::connect(m, &QAbstractItemModel::modelReset, view, [autosize_columns]() {
      QTimer::singleShot(0, autosize_columns);
    });
    QObject::connect(m, &QAbstractItemModel::columnsInserted, view, [autosize_columns]() {
      QTimer::singleShot(0, autosize_columns);
    });
  }

  const bool do_auto_size = view->property("_taiga_auto_size_cols").toBool();
  if (do_auto_size) {
    // Initial sizing once (after the view is shown/laid out).
    QTimer::singleShot(0, view, autosize_columns);
  }

  // Relayout when a user resizes columns (wrapped text depends on width).
  if (auto* tv = qobject_cast<QTableView*>(view)) {
    if (auto* h = tv->horizontalHeader()) {
      QObject::connect(h, &QHeaderView::sectionResized, view, [relayout](int, int, int) {
        relayout();
      });
    }
  } else if (auto* tr = qobject_cast<QTreeView*>(view)) {
    if (auto* h = tr->header()) {
      QObject::connect(h, &QHeaderView::sectionResized, view, [relayout](int, int, int) {
        relayout();
      });
    }
  }

  view->setProperty(kAdaptiveWrapHookProperty, true);
}

}  // namespace

void applyHeaderDefaults(QHeaderView* header, const Defaults& d) {
  if (!header) return;
  header->setTextElideMode(Qt::ElideRight);
  header->setDefaultAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  if (d.min_section_width_px > 0) header->setMinimumSectionSize(d.min_section_width_px);
  header->setStretchLastSection(false);
}

void applyDefaults(QAbstractItemView* view, const Defaults& d) {
  if (!view) return;

  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view->setAlternatingRowColors(d.alternating_row_colors);

  view->setProperty("_taiga_auto_size_cols", d.auto_size_columns_to_contents);
  if (d.wrap_cell_text) {
    view->setProperty("_taiga_max_auto_col_w", d.max_auto_column_width_px);
    view->setProperty("_taiga_min_text_col_w", d.min_text_column_width_px);
    view->setProperty("_taiga_min_first_col_w", d.min_first_visible_column_width_px);
    if (auto* tv = qobject_cast<QTableView*>(view)) {
      tv->setWordWrap(true);
      tv->setTextElideMode(Qt::ElideNone);
    } else if (auto* tr = qobject_cast<QTreeView*>(view)) {
      tr->setWordWrap(true);
      tr->setUniformRowHeights(false);
      tr->setTextElideMode(Qt::ElideNone);
    }
    ensureAdaptiveWrapHook(view);
  }

  ensureWrapDelegate(view);
  ensureLeftCellProxy(view);
  ensureCenteredHeaderProxy(view);

  if (auto* tv = qobject_cast<QTableView*>(view)) {
    if (auto* h = tv->horizontalHeader()) applyHeaderDefaults(h, d);
  } else if (auto* tr = qobject_cast<QTreeView*>(view)) {
    if (auto* h = tr->header()) applyHeaderDefaults(h, d);
  }
}

void warmupSizingNow(QAbstractItemView* view) {
  if (!view) return;
  if (!view->property("_taiga_auto_size_cols").toBool()) return;
  autosizeColumnsNow(view);
  relayoutNow(view);
}

}  // namespace gui::tables

