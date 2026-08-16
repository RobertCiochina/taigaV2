#include <QTest>

#include "base/rss.hpp"
#include "gui/torrent/torrent_feed_widget.hpp"
#include "gui/utils/rss_feed_parser.hpp"
#include "media/anime.hpp"
#include "track/recognition_normalize.hpp"

namespace gui::test {

namespace {

rss::Item rssTitle(const char* title) {
  rss::Item it;
  it.title = title;
  return it;
}

anime::Details tvShow(const int id, const char* romaji, const char* english = "") {
  anime::Details item;
  item.id = id;
  item.type = anime::Type::Tv;
  item.titles.romaji = romaji;
  item.titles.english = english;
  item.episode_count = 12;
  return item;
}

}  // namespace

class TorrentRssContextGuardTest final : public QObject {
  Q_OBJECT

private slots:
  void sequel_colon_subtitle_does_not_append_s01() {
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Reikenzan: Eichi e no Shikaku")),
             QString());
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("BLEACH: Thousand-Year Blood War")),
             QString());
  }

  void colon_without_space_still_appends_s01() {
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Re:Zero kara Hajimeru Isekai Seikatsu")),
             QStringLiteral("Re:Zero kara Hajimeru Isekai Seikatsu S01"));
  }

  void unmarked_season_one_appends_s01() {
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Boku no Hero Academia")),
             QStringLiteral("Boku no Hero Academia S01"));
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Spy x Family")),
             QStringLiteral("Spy x Family S01"));
  }

  void later_cour_wording_becomes_compact_snn() {
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Boku no Hero Academia 2nd Season")),
             QStringLiteral("Boku no Hero Academia S02"));
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Foo 3rd Season")),
             QStringLiteral("Foo S03"));
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Bar Season 4")),
             QStringLiteral("Bar S04"));
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Baz Part 2")),
             QStringLiteral("Baz S02"));
  }

  void colon_subtitle_with_ordinal_season_still_converts() {
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Foo 2nd Season: Bar")),
             QStringLiteral("Foo S02: Bar"));
  }

  void existing_snn_is_left_alone() {
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Foo S03")), QString());
    QCOMPARE(gui::nyaaSeasonQualifiedTitle(QStringLiteral("Spy x Family S02")), QString());
  }

  void season_number_from_release_text() {
    QCOMPARE(gui::seasonNumberFromTitleText(QStringLiteral("Foo 2nd Season - 01")).value_or(0), 2);
    QCOMPARE(gui::seasonNumberFromTitleText(QStringLiteral("[Erai] Foo S04 - 05")).value_or(0), 4);
    QCOMPARE(gui::seasonNumberFromTitleText(QStringLiteral("Foo Season 3")).value_or(0), 3);
    QCOMPARE(gui::seasonNumberFromTitleText(QStringLiteral("Foo Part 2")).value_or(0), 2);
    QVERIFY(!gui::seasonNumberFromTitleText(QStringLiteral("Reikenzan - Eichi e no Shikaku - 09")));
  }

  void expected_season_unmarked_is_one_marked_uses_token() {
    QCOMPARE(gui::expectedTorrentSeasonForAnime(tvShow(1, "Boku no Hero Academia")), 1);
    QCOMPARE(gui::expectedTorrentSeasonForAnime(tvShow(2, "Boku no Hero Academia 2nd Season")), 2);
    QCOMPARE(gui::expectedTorrentSeasonForAnime(tvShow(3, "Reikenzan: Eichi e no Shikaku")), 1);
  }

  void dash_subtitle_overlaps_colon_official_but_not_other_cour() {
    const auto s2 = tvShow(200, "Reikenzan: Eichi e no Shikaku");
    const auto s1 = tvShow(100, "Reikenzan: Hoshikuzu-tachi no Utage");
    QVERIFY(gui::rssReleaseTitleOverlapsAnime(
        "[KamiFS] Reikenzan - Eichi e no Shikaku - 09 [720p].mkv", s2));
    QVERIFY(!gui::rssReleaseTitleOverlapsAnime(
        "[KamiFS] Reikenzan - Eichi e no Shikaku - 09 [720p].mkv", s1));
    QVERIFY(!gui::rssReleaseTitleOverlapsAnime("[Group] Reikenzan - 01 [720p].mkv", s2));
  }

  void s1_name_is_substring_of_2nd_season_filename_but_season_guard_rejects() {
    const auto s1 = tvShow(10, "Boku no Hero Academia", "My Hero Academia");
    // identify() would return S2; even if it returned S1, the season token must win.
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Erai-raws] Boku no Hero Academia 2nd Season - 01 [720p].mkv"), s1, 10));
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Subs] Boku no Hero Academia - S02E01 [1080p].mkv"), s1, 10));
  }

  void s2_context_rejects_s1_hero_academia_and_keeps_2nd_season() {
    const auto s2 = tvShow(11, "Boku no Hero Academia 2nd Season", "My Hero Academia Season 2");
    QVERIFY(gui::rssItemBelongsToAnime(
        rssTitle("[Erai-raws] Boku no Hero Academia 2nd Season - 01 [720p].mkv"), s2, 11));
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Erai-raws] Boku no Hero Academia - 01 [720p].mkv"), s2, 10));
    // identify() hitting S1 must not keep the S1 episode just because the S2 official title
    // contains the S1 name as a prefix.
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Erai-raws] Boku no Hero Academia - 12 [720p].mkv"), s2, 10));
  }

  void reikenzan_s2_keeps_dash_subtitle_when_identify_hits_s1() {
    const auto s2 = tvShow(200, "Reikenzan: Eichi e no Shikaku");
    QVERIFY(gui::rssItemBelongsToAnime(
        rssTitle("[KamiFS] Reikenzan - Eichi e no Shikaku - 09 [720p] [424919AA].mkv"), s2, 100));
    QVERIFY(gui::rssItemBelongsToAnime(
        rssTitle("[DeadFish] Reikenzan: Eichi e no Shikaku - 03 [720p][AAC].mp4"), s2, 100));
    QVERIFY(gui::rssItemBelongsToAnime(rssTitle("[Techmod] Reikenzan Eichi E No Shikaku (1080p)"),
                                       s2, 100));
    QVERIFY(
        gui::rssItemBelongsToAnime(rssTitle("Reikenzan - Eichi e no Shikaku [batch]"), s2, 100));
  }

  void reikenzan_s2_rejects_s1_episode_and_movie() {
    const auto s2 = tvShow(200, "Reikenzan: Eichi e no Shikaku");
    QVERIFY(!gui::rssItemBelongsToAnime(rssTitle("[Group] Reikenzan - 01 [720p].mkv"), s2, 100));
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Group] Reikenzan: Eichi e no Shikaku Movie [1080p].mkv"), s2, 200));
  }

  void s3_context_rejects_s1_part_2_token() {
    const auto s3 = tvShow(30, "Honzuki no Gekokujou 3rd Season");
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Subs] Honzuki no Gekokujou - S01E02 [1080p].mkv"), s3, 10));
    QVERIFY(gui::rssItemBelongsToAnime(
        rssTitle("[Subs] Honzuki no Gekokujou 3rd Season - 05 [1080p].mkv"), s3, 30));
  }

  void unknown_id_keeps_glued_subtitle_not_bare_franchise() {
    const auto s2 = tvShow(200, "Reikenzan: Eichi e no Shikaku");
    QVERIFY(gui::rssItemBelongsToAnime(
        rssTitle("[KamiFS] Reikenzan - Eichi e no Shikaku - 09 [720p].mkv"), s2,
        anime::kUnknownId));
    QVERIFY(!gui::rssItemBelongsToAnime(rssTitle("[Group] Reikenzan - 01 [720p].mkv"), s2,
                                        anime::kUnknownId));
  }

  void year_token_far_from_start_is_rejected() {
    auto s2 = tvShow(200, "Reikenzan: Eichi e no Shikaku");
    s2.date_started =
        FuzzyDate{std::chrono::year{2017}, std::chrono::month{1}, std::chrono::day{8}};
    QVERIFY(!gui::rssItemBelongsToAnime(
        rssTitle("[Group] Reikenzan - Eichi e no Shikaku (2010) [1080p].mkv"), s2, 200));
  }

  void nyaa_rss_with_atom_and_nyaa_namespaces_parses_items() {
    const QByteArray xml = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<rss xmlns:atom=\"http://www.w3.org/2005/Atom\" "
        "xmlns:nyaa=\"https://nyaa.si/xmlns/nyaa\" version=\"2.0\">"
        "<channel>"
        "<title>Nyaa - &#34;Reikenzan: Eichi e no Shikaku&#34;</title>"
        "<link>https://nyaa.si/</link>"
        "<atom:link href=\"https://nyaa.si/?page=rss\" rel=\"self\" "
        "type=\"application/rss+xml\" />"
        "<item>"
        "<title>[Techmod] Reikenzan Eichi E No Shikaku (1080p)</title>"
        "<link>https://nyaa.si/download/2064794.torrent</link>"
        "<guid isPermaLink=\"true\">https://nyaa.si/view/2064794</guid>"
        "<pubDate>Fri, 16 Jan 2026 04:53:22 -0000</pubDate>"
        "<nyaa:seeders>6</nyaa:seeders>"
        "<description><![CDATA[pack]]></description>"
        "</item>"
        "<item>"
        "<title>Reikenzan - Eichi e no Shikaku [batch]</title>"
        "<link>https://nyaa.si/download/952398.torrent</link>"
        "<pubDate>Thu, 24 Aug 2017 11:06:45 -0000</pubDate>"
        "</item>"
        "</channel></rss>");
    QString err;
    const auto feed = gui::parseSyndicationFeed(xml, &err);
    QVERIFY2(feed.has_value(), qPrintable(err));
    QCOMPARE(static_cast<int>(feed->items.size()), 2);
    QCOMPARE(QString::fromStdString(feed->items[0].title),
             QStringLiteral("[Techmod] Reikenzan Eichi E No Shikaku (1080p)"));
    QCOMPARE(QString::fromStdString(feed->items[1].title),
             QStringLiteral("Reikenzan - Eichi e no Shikaku [batch]"));
  }

  void contextual_search_variants_include_native_and_synonyms_last() {
    anime::Details item;
    item.id = 200;
    item.titles.romaji = "Reikenzan: Eichi e no Shikaku";
    item.titles.english = "Reikenzan: Eichi e no Shikaku";
    item.titles.japanese = "霊剣山 叡智への資格";
    item.titles.synonyms.push_back("Reikenzan - Eichi e no Shikaku");
    const QStringList v =
        gui::buildManualSearchTitleVariants(item, QStringLiteral("Reikenzan: Eichi e no Shikaku"));
    QVERIFY(v.contains(QStringLiteral("霊剣山 叡智への資格")));
    QVERIFY(v.contains(QStringLiteral("Reikenzan - Eichi e no Shikaku")));
    QCOMPARE(v.front(), QStringLiteral("Reikenzan: Eichi e no Shikaku"));
    QVERIFY(v.indexOf(QStringLiteral("霊剣山 叡智への資格")) >
            v.indexOf(QStringLiteral("Reikenzan: Eichi e no Shikaku")));
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::TorrentRssContextGuardTest)

#include "test_nyaa_season_qualified_title.moc"
