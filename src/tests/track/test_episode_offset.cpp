#include <QTest>

#include "media/anime.hpp"
#include "track/episode_offset.hpp"
#include "track/recognition_normalize.hpp"
#include "track/recognition_titles.hpp"

namespace track::test {

class EpisodeOffsetTest final : public QObject {
  Q_OBJECT

private slots:
  void infer_offset_when_finished_and_last_aired_absolute() {
    anime::Details item;
    item.id = 1;
    item.status = anime::Status::FinishedAiring;
    item.episode_count = 14;
    item.last_aired_episode = 54;
    QCOMPARE(track::inferredEpisodeOffset(item), 40);
    QCOMPARE(track::toListEpisode(item, 41), 1);
    QCOMPARE(track::toReleaseEpisode(item, 1), 41);
    QCOMPARE(track::toListLastAiredEpisode(item, 54), 14);
  }

  void infer_offset_while_airing_when_last_aired_absolute() {
    anime::Details item;
    item.status = anime::Status::Airing;
    item.episode_count = 14;
    item.last_aired_episode = 41;
    QCOMPARE(track::inferredEpisodeOffset(item), 27);
    QCOMPARE(track::toListLastAiredEpisode(item, 41), 14);
  }

  void synthetic_strips_no_marker() {
    anime::Details item;
    item.titles.romaji = "Boku no Hero Academia No. 170+1: More";
    item.titles.english = "My Hero Academia More";
    const auto syns = track::recognition::syntheticTitleSynonyms(item);
    bool found = false;
    for (const auto& s : syns) {
      const auto n = track::recognition::normalize(s);
      if (n.find("bokunoheroacademia") != std::string::npos && n.find("170") == std::string::npos &&
          n.find("more") != std::string::npos) {
        found = true;
        break;
      }
    }
    QVERIFY(found);
  }

  void season_noise_strip_matches_more() {
    const auto full = track::recognition::normalize("Boku no Hero Academia Final Season - More");
    const auto stripped = track::recognition::stripSeasonNoiseFromNormalized(full);
    const auto target = track::recognition::normalize("Boku no Hero Academia More");
    QCOMPARE(stripped, target);
  }

  void franchise_only_title_rejected() {
    QVERIFY(track::recognition::isFranchiseOnlySearchTitle(QStringLiteral("BLEACH")));
    QVERIFY(!track::recognition::isFranchiseOnlySearchTitle(
        QStringLiteral("BLEACH: Thousand-Year Blood War")));
  }
};

}  // namespace track::test

QTEST_MAIN(track::test::EpisodeOffsetTest)
#include "test_episode_offset.moc"
