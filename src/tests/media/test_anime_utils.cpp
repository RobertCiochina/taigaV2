#include <QTest>

#include "media/anime.hpp"
#include "media/anime_utils.hpp"

namespace anime::test {

class AnimeUtilsTest final : public QObject {
  Q_OBJECT

private slots:
  void estimate_episode_count_returns_known_positive_count() {
    Details item;
    item.episode_count = 7;
    QCOMPARE(estimateEpisodeCount(item, 0), 7);
    QCOMPARE(estimateEpisodeCount(item, 99), 7);
  }

  void estimate_episode_count_branches_on_last_known_when_count_unknown() {
    Details item;
    item.episode_count = 0;
    QCOMPARE(estimateEpisodeCount(item, 0), 12);
    QCOMPARE(estimateEpisodeCount(item, 11), 12);
    QCOMPARE(estimateEpisodeCount(item, 12), 26);
    QCOMPARE(estimateEpisodeCount(item, 23), 26);
    QCOMPARE(estimateEpisodeCount(item, 24), 52);
    QCOMPARE(estimateEpisodeCount(item, 49), 52);
    QCOMPARE(estimateEpisodeCount(item, 50), 0);
  }

  void estimate_episode_length_returns_configured_length() {
    Details item;
    item.episode_length = 42;
    QCOMPARE(estimateEpisodeLength(item), 42);
  }

  void estimate_episode_length_defaults_by_type() {
    Details item;
    item.episode_length = 0;
    item.type = Type::Tv;
    QCOMPARE(estimateEpisodeLength(item), 24);
    item.type = Type::Ova;
    QCOMPARE(estimateEpisodeLength(item), 24);
    item.type = Type::Movie;
    QCOMPARE(estimateEpisodeLength(item), 90);
    item.type = Type::Special;
    QCOMPARE(estimateEpisodeLength(item), 12);
    item.type = Type::Ona;
    QCOMPARE(estimateEpisodeLength(item), 24);
    item.type = Type::Music;
    QCOMPARE(estimateEpisodeLength(item), 5);
    item.type = Type::Unknown;
    QCOMPARE(estimateEpisodeLength(item), 24);
  }

  void is_nsfw_r18() {
    Details item;
    item.age_rating = AgeRating::R18;
    QVERIFY(isNsfw(item));
  }

  void is_nsfw_unknown_with_hentai_genre() {
    Details item;
    item.age_rating = AgeRating::Unknown;
    item.genres = {"Hentai"};
    QVERIFY(isNsfw(item));
  }

  void is_nsfw_false_for_typical_entry() {
    Details item;
    item.age_rating = AgeRating::PG13;
    item.genres = {"Action"};
    QVERIFY(!isNsfw(item));
  }
};

}  // namespace anime::test

QTEST_MAIN(anime::test::AnimeUtilsTest)

#include "test_anime_utils.moc"
