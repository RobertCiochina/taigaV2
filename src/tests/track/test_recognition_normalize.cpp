#include <QTest>

#include "track/recognition_normalize.hpp"

namespace track::recognition::test {

class RecognitionNormalizeTest final : public QObject {
  Q_OBJECT

private slots:
  void normalize_empty_is_empty() {
    QCOMPARE(normalize(""), std::string(""));
  }

  void normalize_removes_article_the_whole_word() {
    const auto out = normalize("The Promised Neverland");
    QVERIFY(out.find("the") == std::string::npos);
  }

  void normalize_roman_two_in_title() {
    const auto out = normalize("Show Title II");
    QVERIFY(out.find("ii") == std::string::npos);
    QVERIFY(out.find('2') != std::string::npos);
  }

  void normalize_season_phrase_becomes_digit() {
    const auto out = normalize("Franchise Season 2");
    QVERIFY(out.find("season") == std::string::npos);
    QVERIFY(out.find('2') != std::string::npos);
  }

  void normalize_roman_numbers_helper() {
    QString s = QStringLiteral("Part II and Part III");
    normalizeRomanNumbers(s);
    QVERIFY(!s.contains(QStringLiteral("II")));
    QVERIFY(!s.contains(QStringLiteral("III")));
    QVERIFY(s.contains(QStringLiteral("2")));
    QVERIFY(s.contains(QStringLiteral("3")));
  }

  void normalize_season_numbers_helper_part_four() {
    // replaceWholeWord is case-sensitive; titles are lowercased before this in normalize().
    QString s = QStringLiteral("ascendance of a bookworm part 4");
    normalizeSeasonNumbers(s);
    QVERIFY(s.contains(QStringLiteral("4")));
    QVERIFY(!s.contains(QStringLiteral("part 4")));
  }

  void normalize_ordinal_helper() {
    QString s = QStringLiteral("Episode second");
    normalizeOrdinalNumbers(s);
    QCOMPARE(s, QStringLiteral("Episode 2nd"));
  }

  void erase_punctuation_removes_basic_punctuation() {
    QString s = QStringLiteral("Hello, World!");
    erasePunctuation(s);
    QCOMPARE(s, QStringLiteral("HelloWorld"));
  }

  void normalize_unicode_casefolds_ascii() {
    QString s = QStringLiteral("UPPERCASE");
    normalizeUnicode(s);
    QCOMPARE(s, QStringLiteral("uppercase"));
  }
};

}  // namespace track::recognition::test

QTEST_MAIN(track::recognition::test::RecognitionNormalizeTest)

#include "test_recognition_normalize.moc"
