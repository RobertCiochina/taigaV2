#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include "gui/main/main_window.hpp"
#include "gui/main/navigation_item_delegate.hpp"
#include "gui/main/navigation_sidebar_refresh.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "taiga/settings.hpp"

namespace gui::test {

class NavigationSidebarRefreshTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    taiga::settings.init();
    anime::db.init();
    gui::theme.initStyle();
  }

  void null_navigation_pointer_is_noop() {
    refreshNavigationSidebarPreserving(nullptr, MainWindowPage::Home, std::nullopt);
  }

  void refresh_preserving_search_emits_no_current_page_changed() {
    QWidget host;
    NavigationWidget nav(&host);
    nav.refresh();

    QSignalSpy spy(&nav, &NavigationWidget::currentPageChanged);
    nav.setCurrentNavigationPage(MainWindowPage::Search, std::nullopt);
    QCOMPARE(spy.count(), 1);
    spy.clear();

    refreshNavigationSidebarPreserving(&nav, MainWindowPage::Search, std::nullopt);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(nav.findItemByPage(MainWindowPage::Search), nav.currentItem());
  }

  void refresh_preserving_list_watching_row_survives() {
    QWidget host;
    NavigationWidget nav(&host);
    nav.refresh();

    constexpr auto statusRole = static_cast<int>(NavigationItemDataRole::ListStatus);
    QSignalSpy spy(&nav, &NavigationWidget::currentPageChanged);
    nav.setCurrentNavigationPage(MainWindowPage::List, anime::list::Status::Watching);
    spy.clear();

    QCOMPARE(nav.currentItem()->data(0, statusRole).value<anime::list::Status>(),
             anime::list::Status::Watching);

    refreshNavigationSidebarPreserving(&nav, MainWindowPage::List, anime::list::Status::Watching);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(nav.currentItem()->data(0, statusRole).value<anime::list::Status>(),
             anime::list::Status::Watching);
  }

  void refresh_preserving_can_move_selection_without_emitting() {
    QWidget host;
    NavigationWidget nav(&host);
    nav.refresh();
    nav.setCurrentNavigationPage(MainWindowPage::Search, std::nullopt);

    QSignalSpy spy(&nav, &NavigationWidget::currentPageChanged);
    spy.clear();

    refreshNavigationSidebarPreserving(&nav, MainWindowPage::Home, std::nullopt);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(nav.findItemByPage(MainWindowPage::Home), nav.currentItem());
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::NavigationSidebarRefreshTest)

#include "test_navigation_sidebar_refresh.moc"
