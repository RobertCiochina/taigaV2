/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#include "header_alignment_proxy_model.hpp"

namespace gui::tables {

HeaderAlignmentProxyModel::HeaderAlignmentProxyModel(QObject* parent) : QIdentityProxyModel(parent) {}

QVariant HeaderAlignmentProxyModel::headerData(const int section, const Qt::Orientation orientation,
                                              const int role) const {
  if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
    return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
  }
  return QIdentityProxyModel::headerData(section, orientation, role);
}

}  // namespace gui::tables

