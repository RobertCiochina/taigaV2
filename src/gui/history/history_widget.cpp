/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
 */

#include "history_widget.hpp"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QUrl>
#include <functional>

#include "gui/main/main_window.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/models/history_model.hpp"
#include "gui/utils/painters.hpp"
#include "gui/utils/ui_strings.hpp"
#include "gui/utils/ui_title.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "sync/service.hpp"
#include "track/play.hpp"

namespace gui {

namespace {

class HistoryTreeView final : public QTreeView {
public:
  explicit HistoryTreeView(QWidget* parent = nullptr) : QTreeView(parent) {}

  std::function<void()> onEnterPressed;
  std::function<void(const QModelIndex&)> onMiddleClick;

protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (onEnterPressed && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
      const QModelIndex idx = currentIndex();
      if (idx.isValid()) {
        onEnterPressed();
        event->accept();
        return;
      }
    }
    QTreeView::keyPressEvent(event);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (onMiddleClick && event->button() == Qt::MiddleButton) {
      const QModelIndex idx = indexAt(event->pos());
      if (idx.isValid()) {
        setCurrentIndex(idx);
        onMiddleClick(idx);
        event->accept();
        return;
      }
    }
    QTreeView::mousePressEvent(event);
  }

  void paintEvent(QPaintEvent* event) override {
    if (model()) {
      if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(model())) {
        if (proxy->sourceModel()->rowCount() == 0) {
          paintEmptyListText(
              this, tr("No history yet.\nTitles appear here after media detection records an "
                       "episode.\n"
                       "Imported offline list updates also show here after import."));
        } else if (proxy->rowCount() == 0) {
          paintEmptyListText(
              this, tr("No entries match the filter.\nTry clearing the toolbar search box."));
        }
      }
    }
    QTreeView::paintEvent(event);
  }
};

}  // namespace

HistoryWidget::HistoryWidget(QWidget* parent)
    : PageWidget{parent},
      m_model(new HistoryModel(parent)),
      m_proxyModel(new QSortFilterProxyModel(parent)),
      m_view(new HistoryTreeView(parent)) {
  m_proxyModel->setSourceModel(m_model);
  m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxyModel->setFilterKeyColumn(HistoryModel::COLUMN_TITLE);

  auto* tree = static_cast<HistoryTreeView*>(m_view);

  m_view->setObjectName("historyView");
  m_view->setFrameShape(QFrame::Shape::NoFrame);
  m_view->setModel(m_proxyModel);
  m_view->setAlternatingRowColors(true);
  m_view->setAllColumnsShowFocus(true);
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  m_view->setRootIsDecorated(false);
  m_view->setUniformRowHeights(true);

  m_view->header()->setSectionsClickable(false);
  m_view->header()->setSectionsMovable(false);
  m_view->header()->setStretchLastSection(false);
  m_view->header()->setTextElideMode(Qt::ElideRight);
  m_view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  m_view->header()->setSectionResizeMode(HistoryModel::COLUMN_TITLE, QHeaderView::Stretch);
  m_view->header()->setSectionResizeMode(HistoryModel::COLUMN_DETAILS, QHeaderView::Stretch);

  m_view->sortByColumn(HistoryModel::COLUMN_MODIFIED, Qt::SortOrder::DescendingOrder);
  m_view->setSortingEnabled(true);

  layout()->addWidget(m_view);

  connect(m_view, &QWidget::customContextMenuRequested, this, &HistoryWidget::showContextMenu);
  connect(m_view, &QAbstractItemView::doubleClicked, this,
          [this](const QModelIndex& idx) { openDetailsForProxyIndex(idx); });

  tree->onEnterPressed = [this, tree]() { openDetailsForProxyIndex(tree->currentIndex()); };
  tree->onMiddleClick = [this](const QModelIndex& idx) {
    const int anime_id = idx.data(HistoryModel::AnimeIdRole).toInt();
    if (anime_id <= 0) return;
    if (track::playNextEpisode(anime_id)) {
      mainWindow()->statusBar()->showMessage(playingNextEpisodeStatusMessage(), 4000);
    } else {
      QMessageBox::information(mainWindow(), tr("Taiga"), playNextEpisodeNotFoundMessage());
    }
  };
}

void HistoryWidget::applyToolbarTextFilter(const QString& text) {
  m_proxyModel->setFilterFixedString(text);
}

void HistoryWidget::openDetailsForProxyIndex(const QModelIndex& proxyIndex) const {
  if (!proxyIndex.isValid()) return;
  const int anime_id = proxyIndex.data(HistoryModel::AnimeIdRole).toInt();
  if (anime_id <= 0) return;
  const auto* item = anime::db.item(anime_id);
  if (!item) return;
  const auto* entry = anime::db.entry(anime_id);
  MediaDialog::show(mainWindow(), MediaDialogPage::Details, *item,
                    entry ? std::optional<ListEntry>{*entry} : std::nullopt);
}

void HistoryWidget::showContextMenu() const {
  const auto index = m_view->currentIndex();

  if (!index.isValid()) return;

  const int anime_id = index.data(HistoryModel::AnimeIdRole).toInt();
  if (anime_id <= 0) return;

  const auto* anime_item = anime::db.item(anime_id);

  QMenu menu;
  menu.addAction(mediaViewDetailsActionLabel(),
                 [this, index]() { openDetailsForProxyIndex(index); });
  menu.addAction(playNextEpisodeActionLabel(), [this, anime_id]() {
    if (track::playNextEpisode(anime_id)) {
      mainWindow()->statusBar()->showMessage(playingNextEpisodeStatusMessage(), 4000);
    } else {
      QMessageBox::information(mainWindow(), tr("Taiga"), playNextEpisodeNotFoundMessage());
    }
  });
  menu.addSeparator();
  if (anime_item) {
    menu.addAction(copyTitleActionLabel(), [this, anime_id]() {
      if (const auto* a = anime::db.item(anime_id)) {
        QGuiApplication::clipboard()->setText(gui::uiTitle(*a));
        mainWindow()->statusBar()->showMessage(copiedTitleToClipboardStatus(), 2500);
      }
    });
    const QString page = sync::animePageUrl(anime_id);
    if (!page.isEmpty()) {
      menu.addAction(tr("Open %1 page…").arg(sync::serviceName(sync::currentServiceId())),
                     [page]() { QDesktopServices::openUrl(QUrl(page)); });
    }
  }
  menu.addSeparator();
  menu.addAction(tr("Go to anime list"), [anime_id]() {
    mainWindow()->navigateTo(MainWindowPage::List);
    if (const auto* item = anime::db.item(anime_id)) {
      mainWindow()->searchBox()->setText(gui::uiTitle(*item));
    }
  });
  menu.exec(QCursor::pos());
}

}  // namespace gui
