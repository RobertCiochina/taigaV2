#include <QTest>
#include <QToolBar>

#include "gui/search/search_widget.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"

namespace gui::test {

class SearchWidgetToolbarTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    // Minimal offline init to avoid relying on main-window startup.
    taiga::settings.init();
    anime::db.init();
    gui::theme.initStyle();

    // Prevent Search from auto-loading a season (external call) during widget construction.
    taiga::session.setSearchListSeasonYearCustomized(true);
  }

  void toolbar_has_no_more_action() {
    SearchWidget w(nullptr);

    auto* toolbar = w.findChild<QToolBar*>();
    QVERIFY2(toolbar != nullptr, "Expected SearchWidget to have a QToolBar child.");

    const auto actions = toolbar->actions();
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.front()->text(), QString("View"));
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::SearchWidgetToolbarTest)

#include "test_search_widget_toolbar.moc"

