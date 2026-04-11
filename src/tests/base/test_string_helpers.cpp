#include <QTest>

#include "base/string.hpp"

namespace base::test {

class StringHelpersTest final : public QObject {
  Q_OBJECT

private slots:
  void compare_strings_respects_case() {
    QCOMPARE(compareStrings("a", "a", Qt::CaseSensitive), 0);
    QVERIFY(compareStrings("a", "A", Qt::CaseSensitive) != 0);
    QCOMPARE(compareStrings("a", "A", Qt::CaseInsensitive), 0);
  }

  void join_strings_empty_returns_placeholder() {
    const std::vector<std::string> empty;
    QCOMPARE(joinStrings(empty, QStringLiteral("(none)")), QStringLiteral("(none)"));
  }

  void join_strings_joins_with_comma_space() {
    const std::vector<std::string> v{"one", "two"};
    QCOMPARE(joinStrings(v), QStringLiteral("one, two"));
  }

  void remove_html_tags_strips_simple_tags() {
    QString s = QStringLiteral("Hello <b>World</b>");
    removeHtmlTags(s);
    QCOMPARE(s, QStringLiteral("Hello World"));
  }

  void replace_whole_word_skips_substrings() {
    QString s = QStringLiteral("cat category");
    replaceWholeWord(s, QStringLiteral("cat"), QStringLiteral("dog"));
    QCOMPARE(s, QStringLiteral("dog category"));
  }

  void to_vector_converts_qstringlist() {
    const QStringList list{QStringLiteral("a"), QStringLiteral("b")};
    const auto v = toVector(list);
    QCOMPARE(static_cast<int>(v.size()), 2);
    QCOMPARE(v[0], std::string("a"));
    QCOMPARE(v[1], std::string("b"));
  }
};

}  // namespace base::test

QTEST_MAIN(base::test::StringHelpersTest)

#include "test_string_helpers.moc"
