/**
 * Taiga
 */

#include "track/cache_debug_log.hpp"

#include <QMetaObject>
#include <QThread>

namespace track {

CacheDebugLog* CacheDebugLog::instance() {
  static CacheDebugLog* hub = new CacheDebugLog();
  return hub;
}

CacheDebugLog::CacheDebugLog(QObject* parent) : QObject(parent) {}

void CacheDebugLog::relayLine(const QString& line) {
  if (QThread::currentThread() == thread()) {
    emit lineAppended(line);
    return;
  }
  QMetaObject::invokeMethod(this, "onRelayLine", Qt::QueuedConnection, Q_ARG(QString, line));
}

void CacheDebugLog::onRelayLine(const QString& line) {
  emit lineAppended(line);
}

}  // namespace track
