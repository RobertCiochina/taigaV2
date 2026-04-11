/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "navigation_sidebar_refresh.hpp"

#include <QSignalBlocker>

#include "gui/main/main_window.hpp"
#include "gui/main/navigation_widget.hpp"

namespace gui {

void refreshNavigationSidebarPreserving(NavigationWidget* nav, const MainWindowPage page,
                                        const std::optional<anime::list::Status>& list_status) {
  if (!nav) return;
  const QSignalBlocker blocker(nav);
  nav->refresh();
  nav->setCurrentNavigationPage(page, list_status);
}

}  // namespace gui
