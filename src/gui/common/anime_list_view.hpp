/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
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

#pragma once

#include <QTreeView>

class QHeaderView;

namespace gui {

class AnimeListModel;
class AnimeListProxyModel;
class ListViewBase;

/// Places the **Guide** column immediately after **Title** (default enum order puts it last).
void positionAnimeListGuideColumnAfterTitle(QHeaderView* header);

/// **Title** stretches to use extra width; **Guide** stays fixed. Call after `restoreState`.
void applyAnimeListHorizontalStretch(QHeaderView* header);

class ListView final : public QTreeView {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ListView)

public:
  ListView(QWidget* parent, AnimeListModel* model, AnimeListProxyModel* proxyModel);
  ~ListView() = default;

  ListViewBase* baseView() {
    return m_base;
  }

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

private:
  ListViewBase* m_base = nullptr;
};

}  // namespace gui
