#include <QTest>

#include "taiga/season_browse_cache.hpp"

namespace taiga::test {

class SeasonBrowseCacheTest final : public QObject {
  Q_OBJECT

private slots:
  void should_fetch_if_key_missing() {
    const QStringList loaded{};
    QVERIFY(taiga::shouldFetchSeasonBrowse(loaded, "anilist:2026:1", /*force_refresh=*/false));
  }

  void should_not_fetch_if_key_present_and_not_forced() {
    const QStringList loaded{"anilist:2026:1"};
    QVERIFY(!taiga::shouldFetchSeasonBrowse(loaded, "anilist:2026:1", /*force_refresh=*/false));
  }

  void should_fetch_if_forced_even_if_present() {
    const QStringList loaded{"anilist:2026:1"};
    QVERIFY(taiga::shouldFetchSeasonBrowse(loaded, "anilist:2026:1", /*force_refresh=*/true));
  }

  void add_is_deduping() {
    QStringList loaded{"anilist:2026:1"};
    loaded = taiga::seasonBrowseCacheAdd(std::move(loaded), "anilist:2026:1");
    QCOMPARE(loaded.size(), 1);
    loaded = taiga::seasonBrowseCacheAdd(std::move(loaded), "anilist:2026:2");
    QCOMPARE(loaded.size(), 2);
  }
};

}  // namespace taiga::test

QTEST_MAIN(taiga::test::SeasonBrowseCacheTest)

#include "test_season_browse_cache.moc"

