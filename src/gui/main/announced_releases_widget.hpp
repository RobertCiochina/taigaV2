/**
 * Taiga — sidebar page listing upcoming sequels linked from Completed/Planning entries.
 */

#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QTimer;
class QVBoxLayout;

namespace gui {

class AnnouncedReleasesWidget final : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AnnouncedReleasesWidget)

public:
  explicit AnnouncedReleasesWidget(QWidget* parent = nullptr);

  void refresh();
  void applyToolbarTextFilter(const QString& text);

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void rebuildRows();
  void updateScanScheduleLabel();

  QString m_filter;
  QVBoxLayout* m_rowsLayout = nullptr;
  QTimer* m_dbRefreshDebounce_ = nullptr;
  QLabel* m_scheduleLabel_ = nullptr;
  QTimer* m_scheduleTick_ = nullptr;
};

}  // namespace gui
