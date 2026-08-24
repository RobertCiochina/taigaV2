#include <QDateTime>
#include <QTest>
#include <chrono>
#include <optional>
#include <vector>

#include "base/chrono.hpp"
#include "media/anime.hpp"
#include "media/anime_utils.hpp"
#include "taiga/auto_download_rules.hpp"

namespace taiga::test {

class AutoDownloadRulesTest final : public QObject {
  Q_OBJECT

private slots:
  void conservative_when_metadata_missing_does_not_guess_aired() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 12;
    item.last_aired_episode = 0;
    item.next_episode_time = 0;

    const int watched = 1;
    const std::int64_t now = 123456;
    QCOMPARE(taiga::computeLastAiredEpisodeForAutoDownload(item, watched, now), watched);
  }

  void finished_airing_uses_episode_count_when_known() {
    anime::Details item;
    item.status = anime::Status::FinishedAiring;
    item.episode_count = 12;
    item.last_aired_episode = 0;
    item.next_episode_time = 0;

    const int watched = 1;
    const std::int64_t now = 123456;
    QCOMPARE(taiga::computeLastAiredEpisodeForAutoDownload(item, watched, now), 12);
  }

  void finished_airing_prefers_episode_count_over_stale_last_aired() {
    anime::Details item;
    item.status = anime::Status::FinishedAiring;
    item.episode_count = 12;
    // Mid-season value left behind when the show finished without a recount.
    item.last_aired_episode = 5;
    item.next_episode_time = 0;

    const int watched = 1;
    const std::int64_t now = 123456;
    QCOMPARE(taiga::computeLastAiredEpisodeForAutoDownload(item, watched, now), 12);
  }

  void date_finished_implied_finished_uses_episode_count() {
    anime::Details item;
    // Stale/unknown service status, but end date is clearly in the past.
    item.status = anime::Status::Unknown;
    item.episode_count = 12;
    item.last_aired_episode = 0;
    item.next_episode_time = 0;
    item.date_started =
        FuzzyDate{std::chrono::year{2020}, std::chrono::month{1}, std::chrono::day{1}};
    item.date_finished =
        FuzzyDate{std::chrono::year{2020}, std::chrono::month{3}, std::chrono::day{1}};

    const int watched = 1;
    const std::int64_t now = 123456;
    QCOMPARE(taiga::computeLastAiredEpisodeForAutoDownload(item, watched, now), 12);
  }

  void next_episode_time_in_future_means_nothing_new_aired() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 12;
    item.last_aired_episode = 0;
    item.next_episode_time = static_cast<std::time_t>(2000);

    const int watched = 5;
    const std::int64_t now = 1000;
    QCOMPARE(taiga::computeLastAiredEpisodeForAutoDownload(item, watched, now), watched);
  }

  void next_episode_time_in_past_is_not_enough_to_guess_aired_without_last_aired_episode() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 12;
    item.last_aired_episode = 0;
    item.next_episode_time = static_cast<std::time_t>(1000);

    const int watched = 5;
    const std::int64_t now = 2000;
    // Without a reliable last-aired field, don't infer that ep 6 aired just because the schedule
    // timestamp is stale (common when sync is disabled).
    QCOMPARE(taiga::computeLastAiredEpisodeForAutoDownload(item, watched, now), watched);
  }

  void redundant_media_fetch_skips_when_recent_and_has_relations() {
    anime::Details item;
    item.last_modified = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch() - 30);
    item.relations.push_back({1, anime::RelationType::Sequel});
    QVERIFY(anime::shouldSkipRedundantMediaFetch(item));
  }

  void redundant_media_fetch_not_when_no_relations() {
    anime::Details item;
    item.last_modified = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch() - 30);
    QVERIFY(!anime::shouldSkipRedundantMediaFetch(item));
  }

  void redundant_media_fetch_not_when_stale() {
    anime::Details item;
    item.last_modified = static_cast<std::time_t>(QDateTime::currentSecsSinceEpoch() -
                                                  anime::kRedundantMediaFetchTtlSeconds - 10);
    item.relations.push_back({1, anime::RelationType::Sequel});
    QVERIFY(!anime::shouldSkipRedundantMediaFetch(item));
  }

  void just_aired_window_includes_boundary() {
    QVERIFY(taiga::isJustAiredRelease(1000, 1000, 300));
    QVERIFY(taiga::isJustAiredRelease(1300, 1000, 300));
    QVERIFY(!taiga::isJustAiredRelease(1301, 1000, 300));
    QVERIFY(!taiga::isJustAiredRelease(999, 1000, 300));
  }

  void detect_just_aired_uses_current_when_still_pointing_at_aired_ep() {
    const auto air = taiga::detectJustAiredAt(1100, 1000, 1000, 0, 300);
    QVERIFY(air.has_value());
    QCOMPARE(*air, 1000);
  }

  void detect_just_aired_uses_previous_when_schedule_already_advanced() {
    const std::int64_t a_air = 1000;
    const std::int64_t b_air = 1000 + 7 * 24 * 3600;
    const auto air = taiga::detectJustAiredAt(1100, b_air, a_air, 0, 300);
    QVERIFY(air.has_value());
    QCOMPARE(*air, a_air);
  }

  void detect_just_aired_none_when_only_future_schedule() {
    QVERIFY(!taiga::detectJustAiredAt(1000, 2000, 2000, 0, 300).has_value());
    QVERIFY(!taiga::detectJustAiredAt(1000, 2000, 0, 0, 300).has_value());
  }

  void detect_just_aired_after_sleep_when_previous_crossed_since_last_poll() {
    const std::int64_t last_poll = 1000;
    const std::int64_t a_air = 1300;
    const std::int64_t now = 1000 + 8 * 3600;
    const std::int64_t b_air = a_air + 7 * 24 * 3600;
    const auto air = taiga::detectJustAiredAt(now, b_air, a_air, last_poll, 300);
    QVERIFY(air.has_value());
    QCOMPARE(*air, a_air);
  }

  void detect_just_aired_current_crossed_since_last_poll_outside_window() {
    const std::int64_t last_poll = 900;
    const std::int64_t a_air = 1000;
    const std::int64_t now = 1400;
    const auto air = taiga::detectJustAiredAt(now, a_air, a_air, last_poll, 300);
    QVERIFY(air.has_value());
    QCOMPARE(*air, a_air);
  }

  void detect_just_aired_schedule_advanced_outside_window_if_last_poll_before_air() {
    const std::int64_t last_poll = 900;
    const std::int64_t a_air = 1000;
    const std::int64_t now = 1000 + 400;
    const std::int64_t b_air = a_air + 7 * 24 * 3600;
    const auto air = taiga::detectJustAiredAt(now, b_air, a_air, last_poll, 300);
    QVERIFY(air.has_value());
    QCOMPARE(*air, a_air);
  }

  void detect_just_aired_stale_already_past_at_last_poll_ignored_outside_window() {
    const std::int64_t last_poll = 2000;
    const std::int64_t stale_air = 1000;
    const std::int64_t now = 2100;
    QVERIFY(!taiga::detectJustAiredAt(now, stale_air, stale_air, last_poll, 300).has_value());
  }

  void remember_keeps_previous_when_current_jumps_to_later_future() {
    const std::int64_t now = 900;
    const std::int64_t a_air = 1000;
    const std::int64_t b_air = a_air + 7 * 24 * 3600;
    QCOMPARE(taiga::rememberNextEpisodeTime(now, b_air, a_air), a_air);
  }

  void remember_uses_current_once_previous_is_past() {
    const std::int64_t now = 1100;
    const std::int64_t a_air = 1000;
    const std::int64_t b_air = a_air + 7 * 24 * 3600;
    QCOMPARE(taiga::rememberNextEpisodeTime(now, b_air, a_air), b_air);
  }

  void remember_keeps_previous_when_current_cleared_while_still_upcoming() {
    QCOMPARE(taiga::rememberNextEpisodeTime(900, 0, 1000), 1000);
  }

  void infer_just_aired_uses_last_aired_plus_one() {
    QCOMPARE(taiga::inferJustAiredEpisode(12, 10, false), 13);
  }

  void infer_just_aired_falls_back_to_watched_plus_one() {
    QCOMPARE(taiga::inferJustAiredEpisode(0, 5, false), 6);
  }

  void infer_just_aired_keeps_last_aired_when_schedule_already_advanced() {
    QCOMPARE(taiga::inferJustAiredEpisode(12, 10, true), 12);
  }

  void due_at_is_air_plus_delay_not_clustered() {
    const std::int64_t a = 1000;
    const std::int64_t delay = 3600;
    const std::int64_t b = a + delay;
    QCOMPARE(taiga::delayedAutoDownloadDueAt(a, delay), a + delay);
    QCOMPARE(taiga::delayedAutoDownloadDueAt(b, delay), b + delay);
    QVERIFY(taiga::delayedAutoDownloadDueAt(a, delay) < taiga::delayedAutoDownloadDueAt(b, delay));
  }

  void bump_last_aired_does_not_pass_recorded_episode() {
    QCOMPARE(taiga::lastAiredForDelayedAutoDownload(5, 6), 6);
    QCOMPARE(taiga::lastAiredForDelayedAutoDownload(6, 6), 6);
    QCOMPARE(taiga::lastAiredForDelayedAutoDownload(7, 6), 7);
  }

  void fifo_queue_airs_twenty_thirty_forty_keep_separate_dues() {
    const std::int64_t delay = 3600;
    const std::int64_t hour = 0;
    const std::int64_t a_air = hour + 20 * 60;
    const std::int64_t b_air = hour + 30 * 60;
    const std::int64_t c_air = hour + 40 * 60;
    std::vector<taiga::DelayedAutoDownloadJob> q;
    taiga::insertDelayedAutoDownloadJob(q, {1, taiga::delayedAutoDownloadDueAt(a_air, delay), 2});
    taiga::insertDelayedAutoDownloadJob(q, {2, taiga::delayedAutoDownloadDueAt(b_air, delay), 2});
    taiga::insertDelayedAutoDownloadJob(q, {3, taiga::delayedAutoDownloadDueAt(c_air, delay), 2});
    QCOMPARE(taiga::soonestDelayedAutoDownloadDue(q), a_air + delay);

    auto first = taiga::takeNextDelayedAutoDownloadJobs(q, a_air + delay);
    QCOMPARE(int(first.size()), 1);
    QCOMPARE(first.front().anime_id, 1);
    QCOMPARE(int(q.size()), 2);

    auto second = taiga::takeNextDelayedAutoDownloadJobs(q, a_air + delay);
    QVERIFY(second.empty());

    second = taiga::takeNextDelayedAutoDownloadJobs(q, b_air + delay);
    QCOMPARE(int(second.size()), 1);
    QCOMPARE(second.front().anime_id, 2);

    auto third = taiga::takeNextDelayedAutoDownloadJobs(q, c_air + delay);
    QCOMPARE(int(third.size()), 1);
    QCOMPARE(third.front().anime_id, 3);
    QVERIFY(q.empty());
  }

  void fifo_same_due_time_taken_together() {
    std::vector<taiga::DelayedAutoDownloadJob> q;
    taiga::insertDelayedAutoDownloadJob(q, {2, 5000, 1});
    taiga::insertDelayedAutoDownloadJob(q, {1, 5000, 1});
    taiga::insertDelayedAutoDownloadJob(q, {3, 6000, 1});
    auto taken = taiga::takeNextDelayedAutoDownloadJobs(q, 5000);
    QCOMPARE(int(taken.size()), 2);
    QCOMPARE(taken[0].anime_id, 1);
    QCOMPARE(taken[1].anime_id, 2);
    QCOMPARE(int(q.size()), 1);
    QCOMPARE(q.front().anime_id, 3);
  }

  void fifo_does_not_pop_later_overdue_jobs_in_same_take() {
    std::vector<taiga::DelayedAutoDownloadJob> q;
    taiga::insertDelayedAutoDownloadJob(q, {1, 1000, 1});
    taiga::insertDelayedAutoDownloadJob(q, {2, 2000, 1});
    auto taken = taiga::takeNextDelayedAutoDownloadJobs(q, 2500);
    QCOMPARE(int(taken.size()), 1);
    QCOMPARE(taken.front().anime_id, 1);
    QCOMPARE(q.front().anime_id, 2);
  }

  void season_pack_only_for_full_cour_catchup() {
    QVERIFY(!taiga::allowSeasonPackAutoDownload(0, 1, 8, 12));
    QVERIFY(!taiga::allowSeasonPackAutoDownload(3, 5, 12, 12));
    QVERIFY(!taiga::allowSeasonPackAutoDownload(0, 5, 8, 12));
    QVERIFY(!taiga::allowSeasonPackAutoDownload(0, 12, 12, 0));
    QVERIFY(taiga::allowSeasonPackAutoDownload(0, 12, 12, 12));
  }

  void pending_due_is_soonest_for_anime() {
    std::vector<taiga::DelayedAutoDownloadJob> q;
    taiga::insertDelayedAutoDownloadJob(q, {1, 5000, 8});
    taiga::insertDelayedAutoDownloadJob(q, {2, 4000, 8});
    taiga::insertDelayedAutoDownloadJob(q, {1, 9000, 9});
    QCOMPARE(*taiga::pendingDelayedAutoDownloadDueAt(q, 1), 5000);
    QCOMPARE(*taiga::pendingDelayedAutoDownloadDueAt(q, 2), 4000);
    QVERIFY(!taiga::pendingDelayedAutoDownloadDueAt(q, 3).has_value());
  }

  void detect_first_poll_lookback_catches_air_inside_delay() {
    const std::int64_t now = 10'000;
    const std::int64_t delay = 3600;
    const std::int64_t last_poll = now - delay;
    const std::int64_t air = now - 1200;
    QCOMPARE(*taiga::detectJustAiredAt(now, air, 0, last_poll), air);
    QVERIFY(!taiga::detectJustAiredAt(now, now - 2 * 24 * 3600, 0, last_poll).has_value());
  }

  void take_due_catches_up_all_overdue_jobs() {
    std::vector<taiga::DelayedAutoDownloadJob> q;
    taiga::insertDelayedAutoDownloadJob(q, {1, 1000, 1});
    taiga::insertDelayedAutoDownloadJob(q, {2, 2000, 1});
    taiga::insertDelayedAutoDownloadJob(q, {3, 4000, 1});
    auto taken = taiga::takeDueDelayedAutoDownloadJobs(q, 2500);
    QCOMPARE(int(taken.size()), 2);
    QCOMPARE(taken[0].anime_id, 1);
    QCOMPARE(taken[1].anime_id, 2);
    QCOMPARE(int(q.size()), 1);
    QCOMPARE(q.front().anime_id, 3);
  }

  void take_due_scales_to_fifty_staggered_dues_in_one_hour() {
    std::vector<taiga::DelayedAutoDownloadJob> q;
    const std::int64_t start = 10'000;
    const int n = 50;
    for (int i = 0; i < n; ++i) {
      taiga::insertDelayedAutoDownloadJob(q, {i + 1, start + i * 60, 2});
    }
    auto first_half = taiga::takeDueDelayedAutoDownloadJobs(q, start + 24 * 60);
    QCOMPARE(int(first_half.size()), 25);
    QCOMPARE(first_half.front().anime_id, 1);
    QCOMPARE(first_half.back().anime_id, 25);
    QCOMPARE(int(q.size()), 25);

    auto rest = taiga::takeDueDelayedAutoDownloadJobs(q, start + 49 * 60);
    QCOMPARE(int(rest.size()), 25);
    QCOMPARE(rest.front().anime_id, 26);
    QCOMPARE(rest.back().anime_id, 50);
    QVERIFY(q.empty());
  }
};

}  // namespace taiga::test

QTEST_MAIN(taiga::test::AutoDownloadRulesTest)

#include "test_auto_download_rules.moc"
