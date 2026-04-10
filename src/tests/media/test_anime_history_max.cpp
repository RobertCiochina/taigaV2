#include <QTest>

#include "media/anime_history.hpp"

namespace anime::test {

class AnimeHistoryMaxTest final : public QObject {
  Q_OBJECT

private slots:
  void returns_max_episode_for_anime_id() {
    anime::History h(nullptr);
    h.recordEpisode(1, 3);
    h.recordEpisode(2, 10);
    h.recordEpisode(1, 7);
    QCOMPARE(h.maxRecordedEpisodeForAnime(1), 7);
    QCOMPARE(h.maxRecordedEpisodeForAnime(2), 10);
    QCOMPARE(h.maxRecordedEpisodeForAnime(3), 0);
  }
};

}  // namespace anime::test

QTEST_MAIN(anime::test::AnimeHistoryMaxTest)

#include "test_anime_history_max.moc"

