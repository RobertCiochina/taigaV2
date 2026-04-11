#include <QTest>

#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_utils.hpp"

namespace anime::list::test {

class AnimeListUtilsTest final : public QObject {
  Q_OBJECT

private slots:
  void get_progress_ratio_zero_total_uses_placeholder() {
    anime::Details item;
    item.episode_count = 0;
    list::Entry e;
    e.watched_episodes = 0;
    QCOMPARE(list::getProgressRatio(&item, &e), 0.8f);
  }

  void get_progress_ratio_caps_at_one() {
    anime::Details item;
    item.episode_count = 10;
    list::Entry e;
    e.watched_episodes = 50;
    QCOMPARE(list::getProgressRatio(&item, &e), 1.0f);
  }

  void last_aired_finished_uses_episode_count() {
    anime::Details item;
    item.status = anime::Status::FinishedAiring;
    item.episode_count = 24;
    item.last_aired_episode = 0;
    list::Entry e;
    e.watched_episodes = 5;
    QCOMPARE(list::lastAiredEpisodeForProgress(item, &e), 24);
  }

  void last_aired_airing_uses_max_of_entry_and_last_aired() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 12;
    item.last_aired_episode = 10;
    list::Entry e;
    e.watched_episodes = 3;
    QCOMPARE(list::lastAiredEpisodeForProgress(item, &e), 10);
  }

  void get_progress_bar_ratios_null_item_zeroes() {
    float a = 1.0f, w = 1.0f;
    list::getProgressBarRatios(nullptr, nullptr, a, w);
    QCOMPARE(a, 0.0f);
    QCOMPARE(w, 0.0f);
  }

  void get_progress_bar_ratios_with_totals_uses_fractions() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 10;
    item.last_aired_episode = 5;
    list::Entry e;
    e.watched_episodes = 2;
    float ra = 0.0f, rw = 0.0f;
    list::getProgressBarRatios(&item, &e, ra, rw);
    QCOMPARE(ra, 0.5f);
    QCOMPARE(rw, 0.2f);
  }

  void get_progress_ratio_mid_series() {
    anime::Details item;
    item.episode_count = 12;
    list::Entry e;
    e.watched_episodes = 3;
    QCOMPARE(list::getProgressRatio(&item, &e), 0.25f);
  }

  void last_aired_for_progress_null_entry_uses_item_last_aired() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 24;
    item.last_aired_episode = 8;
    QCOMPARE(list::lastAiredEpisodeForProgress(item, nullptr), 8);
  }

  void last_aired_for_progress_finished_zero_count_returns_zero() {
    anime::Details item;
    item.status = anime::Status::FinishedAiring;
    item.episode_count = 0;
    list::Entry e;
    e.watched_episodes = 99;
    QCOMPARE(list::lastAiredEpisodeForProgress(item, &e), 0);
  }

  void get_progress_bar_ratios_caps_ratios_at_one() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 5;
    item.last_aired_episode = 20;
    list::Entry e;
    e.watched_episodes = 99;
    float ra = 0.0f, rw = 0.0f;
    list::getProgressBarRatios(&item, &e, ra, rw);
    QCOMPARE(ra, 1.0f);
    QCOMPARE(rw, 1.0f);
  }
};

}  // namespace anime::list::test

QTEST_MAIN(anime::list::test::AnimeListUtilsTest)

#include "test_anime_list_utils.moc"
