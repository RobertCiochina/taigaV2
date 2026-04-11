#include <QTest>

#include <chrono>

#include "media/anime_season.hpp"

namespace anime::test {

class AnimeSeasonTest final : public QObject {
  Q_OBJECT

private slots:
  void default_season_is_falsey() {
    Season s;
    QVERIFY(!static_cast<bool>(s));
  }

  void season_from_spring_date() {
    const Date d{std::chrono::year{2021}, std::chrono::April, std::chrono::day{10}};
    Season s{d};
    QVERIFY(static_cast<bool>(s));
    QCOMPARE(s.name, SeasonName::Spring);
    QCOMPARE(s.year, std::chrono::year{2021});
  }

  void december_maps_to_following_winter_year() {
    const FuzzyDate fd{"2018-12-15"};
    Season s{fd};
    QCOMPARE(s.name, SeasonName::Winter);
    QCOMPARE(s.year, std::chrono::year{2019});
  }

  void increment_season_advances_within_year_until_winter_rolls_year() {
    Season s{SeasonName::Fall, std::chrono::year{2020}};
    ++s;
    QCOMPARE(s.name, SeasonName::Winter);
    QCOMPARE(s.year, std::chrono::year{2021});
  }

  void decrement_season_fall_wraps_previous_year() {
    Season s{SeasonName::Winter, std::chrono::year{2021}};
    --s;
    QCOMPARE(s.name, SeasonName::Fall);
    QCOMPARE(s.year, std::chrono::year{2020});
  }

  void compare_orders_by_year_then_season() {
    const Season a{SeasonName::Summer, std::chrono::year{2020}};
    const Season b{SeasonName::Spring, std::chrono::year{2021}};
    QVERIFY(a < b);
  }

  void unknown_season_sorts_after_known_same_year() {
    const Season known{SeasonName::Spring, std::chrono::year{2020}};
    const Season unknown{SeasonName::Unknown, std::chrono::year{2020}};
    QVERIFY(known < unknown);
  }
};

}  // namespace anime::test

QTEST_MAIN(anime::test::AnimeSeasonTest)

#include "test_anime_season.moc"
