/**
 * Taiga
 */

#include "gui/settings/cache_diagnostics_dialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include "gui/main/main_window.hpp"
#include "track/cache_debug_log.hpp"
#include "track/scanner.hpp"

namespace gui {

namespace {

bool textEditAtBottom(const QTextEdit& edit) {
  if (auto* bar = edit.verticalScrollBar()) {
    return bar->maximum() - bar->value() <= 4;
  }
  return true;
}

void appendLogLine(QTextEdit* log, const QString& line) {
  if (!log) return;
  const bool follow = textEditAtBottom(*log);
  if (!log->toPlainText().isEmpty()) {
    log->append(line);
  } else {
    log->setPlainText(line);
  }
  if (follow) {
    log->moveCursor(QTextCursor::End);
  }
}

class CacheDiagnosticsWindow final : public QDialog {
public:
  explicit CacheDiagnosticsWindow(QWidget* parent) : QDialog(parent) {
    setModal(false);
    setWindowTitle(tr("Cache diagnostics log"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 420);

    auto* root = new QVBoxLayout(this);

    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    {
      QFont mono = m_log->font();
      mono.setStyleHint(QFont::Monospace);
      mono.setFamily(QStringLiteral("Consolas"));
      m_log->setFont(mono);
    }
    m_log->setPlainText(track::libraryEpisodeIndexCacheDebugLog());
    m_log->moveCursor(QTextCursor::End);
    root->addWidget(m_log, 1);

    auto* row = new QHBoxLayout();
    auto* btnCopy = new QPushButton(tr("Copy to clipboard"), this);
    auto* btnClear = new QPushButton(tr("Clear log"), this);
    auto* btnClose = new QPushButton(tr("Close"), this);
    row->addWidget(btnCopy);
    row->addWidget(btnClear);
    row->addStretch(1);
    row->addWidget(btnClose);
    root->addLayout(row);

    connect(btnCopy, &QPushButton::clicked, this, [this]() {
      if (auto* cb = QApplication::clipboard()) {
        cb->setText(m_log->toPlainText());
      }
    });
    connect(btnClear, &QPushButton::clicked, this, [this]() {
      track::clearLibraryEpisodeIndexCacheDebugLog();
      m_log->clear();
    });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

    connect(track::CacheDebugLog::instance(), &track::CacheDebugLog::lineAppended, this,
            [this](const QString& line) { appendLogLine(m_log, line); });
  }

  QTextEdit* m_log = nullptr;
};

void detachCacheLogFromHost(const QPointer<CacheDiagnosticsWindow>& log) {
  if (!log) return;
  QWidget* mw = mainWindow();
  log->setParent(mw ? mw : nullptr);
  log->setWindowFlag(Qt::Window, true);
  log->show();
  log->raise();
  log->activateWindow();
}

class CacheLogHostCloseFilter final : public QObject {
public:
  explicit CacheLogHostCloseFilter(QPointer<CacheDiagnosticsWindow> log, QObject* parent = nullptr)
      : QObject(parent), m_log_(std::move(log)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    Q_UNUSED(watched);
    if (event->type() == QEvent::Close) {
      detachCacheLogFromHost(m_log_);
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<CacheDiagnosticsWindow> m_log_;
};

}  // namespace

void CacheDiagnosticsDialog::show(QWidget* parent) {
  static QPointer<CacheDiagnosticsWindow> open;
  if (open) {
    open->raise();
    open->activateWindow();
    return;
  }
  // While Settings is open it is application-modal; parent the log to Settings so it
  // stays interactive and on top, then detach to MainWindow before Settings is destroyed.
  QWidget* owner = parent ? parent : mainWindow();
  auto* dlg = new CacheDiagnosticsWindow(owner);
  open = dlg;
  QObject::connect(dlg, &QObject::destroyed, []() { open.clear(); });
  if (auto* host = qobject_cast<QDialog*>(parent)) {
    const QPointer<CacheDiagnosticsWindow> logPtr = dlg;
    auto* filter = new CacheLogHostCloseFilter(logPtr, host);
    host->installEventFilter(filter);
    QObject::connect(host, &QObject::destroyed, filter, &QObject::deleteLater);
    QObject::connect(host, &QDialog::finished, dlg,
                     [logPtr](int) { detachCacheLogFromHost(logPtr); });
  }
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

}  // namespace gui
