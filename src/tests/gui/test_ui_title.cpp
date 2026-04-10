#include <QTest>

#include "gui/utils/ui_title.hpp"

namespace gui::test {

class UiTitleTest final : public QObject {
  Q_OBJECT

private slots:
  void prefers_english_over_romaji() {
    anime::Details a;
    a.titles.english = "English";
    a.titles.romaji = "Romaji";
    QCOMPARE(gui::uiTitle(a), QString("English"));
  }

  void falls_back_to_romaji_if_english_missing() {
    anime::Details a;
    a.titles.romaji = "Romaji";
    QCOMPARE(gui::uiTitle(a), QString("Romaji"));
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::UiTitleTest)

#include "test_ui_title.moc"

