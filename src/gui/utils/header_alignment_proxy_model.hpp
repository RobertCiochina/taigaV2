/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 */

#pragma once

#include <QIdentityProxyModel>

namespace gui::tables {

/// Forces horizontal header text alignment, regardless of model-provided TextAlignmentRole.
class HeaderAlignmentProxyModel final : public QIdentityProxyModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(HeaderAlignmentProxyModel)

public:
  explicit HeaderAlignmentProxyModel(QObject* parent = nullptr);
  ~HeaderAlignmentProxyModel() override = default;

  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
};

}  // namespace gui::tables

