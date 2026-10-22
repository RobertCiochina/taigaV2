#include <QRegularExpression>
#include <QTest>

#include "base/rss.hpp"
#include "gui/torrent/movie_torrent_watch.hpp"

namespace gui::test {

namespace {

rss::Item rssTitle(const char* title) {
  rss::Item it;
  it.title = title;
  it.link = "https://nyaa.si/view/1";
  return it;
}

rss::Feed feedOf(std::initializer_list<const char*> titles) {
  rss::Feed feed;
  for (const char* title : titles) feed.items.push_back(rssTitle(title));
  return feed;
}

}  // namespace

class MovieTorrentMatchTest final : public QObject {
  Q_OBJECT

private slots:
  void movie_release_counts() {
    const rss::Feed feed = feedOf({
        "[Group] Your Name [1080p].mkv",
        "[Other] Kimi no Na wa. [1080p].mkv",
    });
    const MovieTorrentMatch match = matchMovieTorrentFeed(
        feed, {QStringLiteral("Your Name"), QStringLiteral("Kimi no Na wa.")}, {}, {});
    QCOMPARE(match.count, 2);
  }

  void series_episode_is_rejected() {
    const rss::Feed feed = feedOf(
        {"[Group] Spy x Family - 03 [1080p].mkv", "[Group] Spy x Family S01E03 [1080p].mkv"});
    const MovieTorrentMatch match =
        matchMovieTorrentFeed(feed, {QStringLiteral("Spy x Family")}, {}, {});
    QCOMPARE(match.count, 0);
  }

  void explicit_movie_token_counts() {
    const rss::Feed feed = feedOf({"[Group] Spy x Family Movie [1080p].mkv"});
    const MovieTorrentMatch match =
        matchMovieTorrentFeed(feed, {QStringLiteral("Spy x Family")}, {}, {});
    QCOMPARE(match.count, 1);
  }

  void batch_is_rejected() {
    const rss::Feed feed = feedOf({"[Group] Spy x Family Batch [1080p].mkv"});
    const MovieTorrentMatch match =
        matchMovieTorrentFeed(feed, {QStringLiteral("Spy x Family")}, {}, {});
    QCOMPARE(match.count, 0);
  }

  void include_and_exclude_regexes() {
    const rss::Feed feed = feedOf({
        "[Group] Your Name [1080p].mkv",
        "[Group] Your Name [720p].mkv",
        "[CAM] Your Name [1080p].mkv",
    });
    const QList<QRegularExpression> include{
        QRegularExpression(QStringLiteral("1080p"), QRegularExpression::CaseInsensitiveOption)};
    const QList<QRegularExpression> exclude{
        QRegularExpression(QStringLiteral("CAM"), QRegularExpression::CaseInsensitiveOption)};
    const MovieTorrentMatch match =
        matchMovieTorrentFeed(feed, {QStringLiteral("Your Name")}, include, exclude);
    QCOMPARE(match.count, 1);
    QVERIFY(match.bestTitle.contains(QStringLiteral("1080p")));
    QVERIFY(!match.bestTitle.contains(QStringLiteral("CAM")));
  }

  void resolution_dash_is_not_an_episode() {
    const rss::Feed feed = feedOf({"[Group] Your Name - 1080p.mkv"});
    const MovieTorrentMatch match =
        matchMovieTorrentFeed(feed, {QStringLiteral("Your Name")}, {}, {});
    QCOMPARE(match.count, 1);
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::MovieTorrentMatchTest)

#include "test_movie_torrent_match.moc"
