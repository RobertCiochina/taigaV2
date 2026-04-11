#include <QTest>

#include "track/streaming_sites.hpp"

namespace track::streaming::test {

class StreamingSitesTest final : public QObject {
  Q_OBJECT

private slots:
  void provider_list_is_non_empty_and_has_slugs() {
    const auto& entries = providerUiEntries();
    QVERIFY(!entries.empty());
    for (const auto& e : entries) {
      QVERIFY(!e.slug.empty());
      QVERIFY(e.label != nullptr);
      QVERIFY(std::strlen(e.label) > 0);
    }
  }

  void match_provider_empty_url_is_nullopt() {
    QVERIFY(!matchProviderSlugByUrl({}).has_value());
  }

  void match_provider_crunchyroll_host() {
    const auto a = matchProviderSlugByUrl("https://www.crunchyroll.com/watch/xyz");
    QVERIFY(a.has_value());
    QCOMPARE(std::string(*a), "crunchyroll");
    const auto b = matchProviderSlugByUrl("crunchyroll.com/foo");
    QVERIFY(b.has_value());
    QCOMPARE(std::string(*b), "crunchyroll");
  }

  void match_provider_youtube_watch() {
    const auto s = matchProviderSlugByUrl("https://www.youtube.com/watch?v=abc");
    QVERIFY(s.has_value());
    QCOMPARE(std::string(*s), "youtube");
  }

  void match_provider_unknown_returns_nullopt() {
    QVERIFY(!matchProviderSlugByUrl("https://example.com/video").has_value());
  }

  void normalize_browser_title_strips_when_title_starts_with_host() {
    std::string title = "www.crunchyroll.com";
    normalizeBrowserTitle("https://www.crunchyroll.com/series", title);
    QVERIFY(title.empty());
  }

  void normalize_browser_title_strips_http_prefixed_title() {
    std::string title = "https://example.com";
    normalizeBrowserTitle("example.com", title);
    QVERIFY(title.empty());
  }

  void normalize_browser_title_clears_common_tab_titles() {
    std::string t = "New Tab";
    normalizeBrowserTitle("https://example.com", t);
    QVERIFY(t.empty());
  }

  void normalize_browser_title_strips_trailing_audio_suffix() {
    std::string t = "Some Show - Audio muted";
    normalizeBrowserTitle("https://example.com", t);
    QCOMPARE(t, std::string("Some Show"));
  }

  void refine_crunchyroll_title_extracts_series_name() {
    std::string title = "My Series - English - Crunchyroll";
    QVERIFY(refineTitleForProvider("crunchyroll", title));
    QCOMPARE(title, std::string("My Series"));
  }

  void refine_youtube_title_extracts_video_name() {
    std::string title = "Cool AMV - YouTube";
    QVERIFY(refineTitleForProvider("youtube", title));
    QCOMPARE(title, std::string("Cool AMV"));
  }

  void refine_unknown_slug_returns_false() {
    std::string title = "Anything - YouTube";
    QVERIFY(!refineTitleForProvider("not-a-provider", title));
  }

  void refine_adn_replaces_colon_spaced_sequences() {
    std::string title = "Show Name : Part 1 - streaming - foo ADN";
    QVERIFY(refineTitleForProvider("adn", title));
    QCOMPARE(title, std::string("Show Name - Part 1"));
  }

  void refine_vrv_replaces_episode_marker() {
    std::string title = "Great Anime: EP 3 - Watch on VRV";
    QVERIFY(refineTitleForProvider("vrv", title));
    QCOMPARE(title, std::string("Great Anime - EP 3"));
  }
};

}  // namespace track::streaming::test

QTEST_MAIN(track::streaming::test::StreamingSitesTest)

#include "test_streaming_sites.moc"
