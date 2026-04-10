#include <QPushButton>
#include <QTest>

#include "gui/search/search_widget.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "taiga/user_feedback.hpp"

namespace gui::test {

class SearchLoadMyListInstantTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    taiga::settings.init();
    anime::db.init();
    gui::theme.initStyle();
  }

  void load_my_list_does_not_require_year_season() {
    bool saw_error_popup_message = false;
    taiga::setUserFeedbackHandler([&](const QString&, bool error) {
      if (error) saw_error_popup_message = true;
    });

    SearchWidget w(nullptr);

    QPushButton* btn = nullptr;
    for (auto* b : w.findChildren<QPushButton*>()) {
      if (b && b->text() == "Load my list") {
        btn = b;
        break;
      }
    }
    QVERIFY2(btn != nullptr, "Could not find the 'Load my list' button.");

    // No year/season selection; should still be instant and not error.
    QTest::mouseClick(btn, Qt::LeftButton);

    QVERIFY2(!saw_error_popup_message,
             "Load my list should not require year/season and should not raise an error.");
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::SearchLoadMyListInstantTest)

#include "test_search_load_my_list_is_instant.moc"

