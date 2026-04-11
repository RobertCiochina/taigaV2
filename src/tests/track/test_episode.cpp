#include <QTest>

#include <anitomy.hpp>

#include "media/anime.hpp"
#include "track/episode.hpp"

namespace track::test {

class EpisodeTest final : public QObject {
  Q_OBJECT

private slots:
  void default_anime_id_is_unknown() {
    Episode ep;
    QCOMPARE(ep.animeId(), anime::kUnknownId);
  }

  void set_anime_id_round_trips() {
    Episode ep;
    ep.setAnimeId(42);
    QCOMPARE(ep.animeId(), 42);
  }

  void add_and_read_element() {
    Episode ep;
    ep.addElement(anitomy::ElementKind::EpisodeTitle, "Alpha");
    QVERIFY(ep.contains(anitomy::ElementKind::EpisodeTitle));
    QCOMPARE(ep.element(anitomy::ElementKind::EpisodeTitle), std::string("Alpha"));
    QCOMPARE(ep.element(anitomy::ElementKind::Title, "x"), std::string("x"));
  }

  void set_element_updates_existing() {
    Episode ep;
    ep.addElement(anitomy::ElementKind::Episode, "1");
    ep.setElement(anitomy::ElementKind::Episode, "2");
    QCOMPARE(ep.element(anitomy::ElementKind::Episode), std::string("2"));
  }

  void set_elements_replaces_vector() {
    Episode ep;
    std::vector<anitomy::Element> els;
    els.push_back({anitomy::ElementKind::ReleaseGroup, "Subs", 0});
    ep.setElements(els);
    QCOMPARE(ep.elements().size(), static_cast<size_t>(1));
    QCOMPARE(ep.element(anitomy::ElementKind::ReleaseGroup), std::string("Subs"));
  }

  void file_path_round_trip() {
    Episode ep;
    ep.setFilePath("/tmp/video.mkv");
    QCOMPARE(ep.filePath(), std::string("/tmp/video.mkv"));
  }
};

}  // namespace track::test

QTEST_MAIN(track::test::EpisodeTest)

#include "test_episode.moc"
