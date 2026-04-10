#include <QTest>

#include "gui/models/list_title_color.hpp"

namespace gui::test {

class ListTitleColorTest final : public QObject {
  Q_OBJECT

private slots:
  void blue_overrides_green() {
    const auto c = decideListTitleColor(
        /*caught_up_or_done=*/true,
        /*next_unwatched_episode_on_disk=*/true,
        /*aired_but_not_downloaded=*/false);
    QVERIFY(c.has_value());
    QCOMPARE(*c, QColor(0x42, 0xa5, 0xf5));
  }

  void green_when_caught_up_and_not_on_disk() {
    const auto c = decideListTitleColor(
        /*caught_up_or_done=*/true,
        /*next_unwatched_episode_on_disk=*/false,
        /*aired_but_not_downloaded=*/false);
    QVERIFY(c.has_value());
    QCOMPARE(*c, QColor(0x4c, 0xaf, 0x50));
  }

  void grey_when_aired_not_downloaded() {
    const auto c = decideListTitleColor(
        /*caught_up_or_done=*/false,
        /*next_unwatched_episode_on_disk=*/false,
        /*aired_but_not_downloaded=*/true);
    QVERIFY(c.has_value());
    QCOMPARE(*c, QColor(0x9e, 0x9e, 0x9e));
  }

  void default_when_no_flags() {
    const auto c = decideListTitleColor(
        /*caught_up_or_done=*/false,
        /*next_unwatched_episode_on_disk=*/false,
        /*aired_but_not_downloaded=*/false);
    QVERIFY(!c.has_value());
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::ListTitleColorTest)

#include "test_list_title_color.moc"

