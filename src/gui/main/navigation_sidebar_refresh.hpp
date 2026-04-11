/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <optional>

namespace anime::list {
enum class Status;
}

namespace gui {

class NavigationWidget;

enum class MainWindowPage;

/// Rebuilds sidebar counts while keeping the logical page (and optional list status row) selected,
/// without emitting `currentPageChanged` during the refresh (avoids jumping the stacked widget).
void refreshNavigationSidebarPreserving(NavigationWidget* nav, MainWindowPage page,
                                        const std::optional<anime::list::Status>& list_status);

}  // namespace gui
