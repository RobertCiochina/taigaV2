#include <QFileInfo>
#include <QTest>
#include <anitomy.hpp>

#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/recognition_normalize.hpp"

namespace track::recognition::test {

class RecognitionParseFileTest final : public QObject {
  Q_OBJECT

private slots:
  void techmod_episode_filename_keeps_sequel_subtitle() {
    const auto ep = parse(
        "[Techmod] Reikenzan Eichi E No Shikaku - 01 (1080p) [C2C36A8C].mkv");
    const auto title = ep.element(anitomy::ElementKind::Title);
    const auto episode_title = ep.element(anitomy::ElementKind::EpisodeTitle);
    const QString combined =
        QString::fromStdString(title + " " + episode_title).toLower();
    QVERIFY2(combined.contains(QStringLiteral("eichi")),
             qPrintable(QStringLiteral("title='%1' episodeTitle='%2'")
                            .arg(QString::fromStdString(title),
                                 QString::fromStdString(episode_title))));
    QCOMPARE(ep.element(anitomy::ElementKind::Episode), std::string("01"));
  }

  void techmod_pack_folder_uses_library_grandparent_as_secondary_title() {
    const QFileInfo fi{QStringLiteral(
        "F:/MyAnime/Reikenzan Eichi E No Shikaku/"
        "[Techmod] Reikenzan Eichi E No Shikaku (1080p)/"
        "[Techmod] Reikenzan Eichi E No Shikaku - 01 (1080p) [C2C36A8C].mkv")};
    const auto ep = parseFileInfo(fi, {}, true);
    const auto titles = ep.allElements(anitomy::ElementKind::Title);
    bool found_library_folder = false;
    for (const auto& t : titles) {
      if (normalize(t) == normalize("Reikenzan Eichi E No Shikaku")) {
        found_library_folder = true;
        break;
      }
    }
    QVERIFY2(found_library_folder, "expected library folder title hint");
  }
};

}  // namespace track::recognition::test

QTEST_MAIN(track::recognition::test::RecognitionParseFileTest)

#include "test_recognition_parse_file.moc"
