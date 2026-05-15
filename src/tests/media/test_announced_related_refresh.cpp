#include <QDateTime>
#include <QTest>

#include "media/anime.hpp"
#include "media/announced_related_refresh.hpp"

namespace anime::test {

class AnnouncedRelatedRefreshTest final : public QObject {
  Q_OBJECT

private slots:
  void known_empty_fresh_not_stale() {
    Details item;
    item.relations_cache = RelationsCache::KnownEmpty;
    item.last_modified = QDateTime::currentSecsSinceEpoch();
    const qint64 now = item.last_modified + 1;
    constexpr qint64 kStaleAfter = 30LL * 24 * 60 * 60;
    QVERIFY(!isStaleForAnnouncedRelatedRefresh(&item, now, kStaleAfter));
  }

  void known_empty_aged_is_stale() {
    Details item;
    item.relations_cache = RelationsCache::KnownEmpty;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    constexpr qint64 kStaleAfter = 30LL * 24 * 60 * 60;
    item.last_modified = now - kStaleAfter - 1;
    QVERIFY(isStaleForAnnouncedRelatedRefresh(&item, now, kStaleAfter));
  }

  void unknown_fresh_is_stale() {
    Details item;
    item.relations_cache = RelationsCache::Unknown;
    item.last_modified = QDateTime::currentSecsSinceEpoch();
    const qint64 now = item.last_modified + 1;
    constexpr qint64 kStaleAfter = 30LL * 24 * 60 * 60;
    QVERIFY(isStaleForAnnouncedRelatedRefresh(&item, now, kStaleAfter));
  }

  void cached_non_sequel_edges_fresh_not_stale() {
    Details item;
    item.relations_cache = RelationsCache::Cached;
    item.last_modified = QDateTime::currentSecsSinceEpoch();
    item.relations.push_back({.related_id = 99, .type = RelationType::SideStory});
    const qint64 now = item.last_modified + 1;
    constexpr qint64 kStaleAfter = 30LL * 24 * 60 * 60;
    QVERIFY(!isStaleForAnnouncedRelatedRefresh(&item, now, kStaleAfter));
  }

  void null_item_is_stale() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVERIFY(isStaleForAnnouncedRelatedRefresh(nullptr, now, 86400));
  }
};

}  // namespace anime::test

QTEST_MAIN(anime::test::AnnouncedRelatedRefreshTest)

#include "test_announced_related_refresh.moc"
