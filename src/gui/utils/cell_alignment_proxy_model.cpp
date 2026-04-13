/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#include "cell_alignment_proxy_model.hpp"

#include <QString>

namespace gui::tables {

CellAlignmentProxyModel::CellAlignmentProxyModel(QObject* parent) : QIdentityProxyModel(parent) {}

QVariant CellAlignmentProxyModel::data(const QModelIndex& index, const int role) const {
  if (role == Qt::TextAlignmentRole) {
    // Policy: "Title" (and first column) left-aligned; all other columns centered.
    // Do this centrally so all tables behave the same.
    const int col = index.column();
    const QString header =
        sourceModel()
            ? sourceModel()->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString().trimmed()
            : QString{};
    const bool is_title =
        (col == 0) || (!header.isEmpty() && header.contains(QStringLiteral("title"), Qt::CaseInsensitive));
    return is_title ? QVariant(Qt::AlignLeft | Qt::AlignVCenter)
                    : QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
  }
  return QIdentityProxyModel::data(index, role);
}

}  // namespace gui::tables

