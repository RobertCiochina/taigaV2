#include <QTest>

#include "gui/utils/ui_strings.hpp"

namespace gui::test {

class UiStringsTest final : public QObject {
  Q_OBJECT

private slots:
  void shared_labels_are_non_empty() {
    QVERIFY(!gui::mediaViewDetailsActionLabel().isEmpty());
    QVERIFY(!gui::libraryOpenFolderActionLabel().isEmpty());
    QVERIFY(!gui::playNextEpisodeActionLabel().isEmpty());
    QVERIFY(!gui::synchronizeActionLabel().isEmpty());
    QVERIFY(!gui::settingsActionLabel().isEmpty());
    QVERIFY(!gui::copyTitleActionLabel().isEmpty());
  }

  void sync_disabled_hint_points_to_settings_anime_list() {
    const QString s = gui::synchronizationDisabledStatusHint();
    QVERIFY(s.contains(QStringLiteral("Settings")));
    QVERIFY(s.contains(QStringLiteral("Anime List")));
  }

  void library_setup_body_mentions_settings_arrow() {
    const QString s = gui::noLibraryFolderConfiguredBody();
    QVERIFY(s.contains(QStringLiteral("Settings")));
    QVERIFY(s.contains(QChar(0x2192)));  // →
  }

  void synchronize_status_messages_use_service_name() {
    const QString svc = QStringLiteral("MAL");
    QVERIFY(gui::synchronizingWithServiceStatus(svc).contains(svc));
    QVERIFY(gui::synchronizationFailedStatus(QStringLiteral("oops")).contains(QStringLiteral("oops")));
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::UiStringsTest)

#include "test_ui_strings.moc"
