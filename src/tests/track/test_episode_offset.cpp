#include <QTest>
#include <vector>

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

  void cour_stage_suffix_and_earlier_cour_offset() {
    QCOMPARE(track::recognition::stripCourStageSuffix(QStringLiteral(
                 "JoJo no Kimyou na Bouken: Steel Ball Run - 2nd & 3rd STAGE")),
             QStringLiteral("JoJo no Kimyou na Bouken: Steel Ball Run"));
    QCOMPARE(track::recognition::stripCourStageSuffix(
                 QStringLiteral("JoJo no Kimyou na Bouken: Steel Ball Run - 2nd - 3rd STAGE")),
             QStringLiteral("JoJo no Kimyou na Bouken: Steel Ball Run"));
    QCOMPARE(track::recognition::courStageKey(
                 QStringLiteral("Steel Ball Run - 2nd - 3rd STAGE, Multi-Audio")),
             QStringLiteral("2-3"));
    QCOMPARE(track::recognition::courStageKey(QStringLiteral("Show - 1st STAGE")),
             QStringLiteral("1"));

    anime::Details first;
    first.id = 1;
    first.episode_count = 1;
    first.titles.romaji = "Example Show - 1st STAGE";
    first.date_started =
        FuzzyDate{std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{19}};
    anime::Details second;
    second.id = 2;
    second.episode_count = 11;
    second.titles.romaji = "Example Show - 2nd & 3rd STAGE";
    second.date_started =
        FuzzyDate{std::chrono::year{2026}, std::chrono::month{9}, std::chrono::day{25}};
    const std::vector<const anime::Details*> catalog{&first, &second};
    QCOMPARE(track::earlierCourStageOffset(second, catalog), 1);
    QCOMPARE(track::earlierCourStageOffset(first, catalog), 0);
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
