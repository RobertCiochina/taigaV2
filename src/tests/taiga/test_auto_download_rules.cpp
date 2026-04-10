#include <QDateTime>
#include <QTest>

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
    item.last_modified = static_cast<std::time_t>(
        QDateTime::currentSecsSinceEpoch() - anime::kRedundantMediaFetchTtlSeconds - 10);
    item.relations.push_back({1, anime::RelationType::Sequel});
    QVERIFY(!anime::shouldSkipRedundantMediaFetch(item));
  }
};

}  // namespace taiga::test

QTEST_MAIN(taiga::test::AutoDownloadRulesTest)

#include "test_auto_download_rules.moc"

