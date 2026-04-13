/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#pragma once

#include <QIdentityProxyModel>

namespace gui::tables {

/// Forces cell text alignment for all data cells.
class CellAlignmentProxyModel final : public QIdentityProxyModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CellAlignmentProxyModel)

public:
  explicit CellAlignmentProxyModel(QObject* parent = nullptr);
  ~CellAlignmentProxyModel() override = default;

  QVariant data(const QModelIndex& index, int role) const override;
};

}  // namespace gui::tables

