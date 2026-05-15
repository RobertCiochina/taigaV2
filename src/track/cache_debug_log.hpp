/**
 * Taiga — live relay for library/cache diagnostics log lines.
 */

#pragma once

#include <QObject>
#include <QString>

namespace track {

class CacheDebugLog final : public QObject {
  Q_OBJECT

public:
  static CacheDebugLog* instance();

  /// Emits `lineAppended` on the GUI thread (safe to call from any thread).
  void relayLine(const QString& line);

signals:
  void lineAppended(const QString& line);

private slots:
  void onRelayLine(const QString& line);

private:
  explicit CacheDebugLog(QObject* parent = nullptr);
};

}  // namespace track
