#include <QComboBox>
#include <QListView>
#include <QTest>

#include "gui/search/search_widget.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"

namespace gui::test {

class SearchYearComboPopupTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    taiga::settings.init();
    anime::db.init();
    gui::theme.initStyle();
  }

  void year_combo_popup_is_bounded() {
    SearchWidget w(nullptr);
    QComboBox* year = nullptr;
    for (auto* c : w.findChildren<QComboBox*>()) {
      if (c && c->placeholderText() == "Year") {
        year = c;
        break;
      }
    }
    QVERIFY2(year != nullptr, "Could not find Year combo box.");
    QVERIFY2(year->maxVisibleItems() > 0 && year->maxVisibleItems() <= 20,
             "Year combo popup should have a reasonable maxVisibleItems bound.");

    // Ensure years won't appear as "..." due to popup eliding.
    year->showPopup();
    QTest::qWait(1);
    if (auto* lv = qobject_cast<QListView*>(year->view())) {
      QCOMPARE(lv->textElideMode(), Qt::ElideNone);
      QVERIFY(!lv->hasAutoScroll());
      QVERIFY(!lv->hasMouseTracking());
      QCOMPARE(lv->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);
    }
    year->hidePopup();
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::SearchYearComboPopupTest)

#include "test_search_year_combo_popup.moc"

