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

#include "startup_splash.hpp"

#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollBar>
#include <QVBoxLayout>

namespace gui {

StartupSplash::StartupSplash(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("startupSplash"));
  setWindowTitle(tr("Taiga"));
  // Modeless: we drive the event loop manually during startup.
  setModal(false);
  setWindowModality(Qt::ApplicationModal);
  // Frameless so it can't be dragged (Windows move loop can appear "frozen" during heavy startup work).
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setSizeGripEnabled(false);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(18, 18, 18, 18);
  root->setSpacing(10);

  auto* title = new QLabel(tr("<b>Starting Taiga…</b>"), this);
  title->setTextFormat(Qt::RichText);
  root->addWidget(title);

  m_stepLabel = new QLabel(tr("Preparing…"), this);
  m_stepLabel->setWordWrap(true);
  root->addWidget(m_stepLabel);

  m_bar = new QProgressBar(this);
  m_bar->setRange(0, 0);  // indeterminate
  root->addWidget(m_bar);

  m_log = new QPlainTextEdit(this);
  m_log->setReadOnly(true);
  m_log->setMinimumHeight(120);
  m_log->setMaximumBlockCount(300);
  root->addWidget(m_log, 1);

  setFixedSize(560, 260);
}

void StartupSplash::setStepText(const QString& text) {
  if (!m_stepLabel) return;
  m_stepLabel->setText(text);
}

void StartupSplash::appendLine(const QString& line) {
  if (!m_log) return;
  m_log->appendPlainText(line);
  m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

}  // namespace gui

