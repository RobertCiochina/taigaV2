/**
 * Taiga — sidebar page listing upcoming sequels linked from Completed/Planning entries.
 */

#pragma once

#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace gui {

class AnnouncedReleasesWidget final : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AnnouncedReleasesWidget)

public:
  explicit AnnouncedReleasesWidget(QWidget* parent = nullptr);

  void refresh();
  void applyToolbarTextFilter(const QString& text);

private:
  void rebuildRows();

  QString m_filter;
  QVBoxLayout* m_rowsLayout = nullptr;
};

}  // namespace gui
