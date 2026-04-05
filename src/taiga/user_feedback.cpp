/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "user_feedback.hpp"

#include <mutex>

namespace taiga {

namespace {

std::mutex g_mutex;
std::function<void(const QString& message, bool error)> g_handler;

}  // namespace

void setUserFeedbackHandler(std::function<void(const QString& message, bool error)> handler) {
  std::lock_guard lock(g_mutex);
  g_handler = std::move(handler);
}

void userFeedback(const QString& message, const bool error) {
  std::function<void(const QString&, bool)> copy;
  {
    std::lock_guard lock(g_mutex);
    copy = g_handler;
  }
  if (copy) copy(message, error);
}

}  // namespace taiga
