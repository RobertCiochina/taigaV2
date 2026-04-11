#include <QTest>

#include "taiga/config.h"
#include "taiga/version.hpp"

namespace taiga::test {

class VersionTest final : public QObject {
  Q_OBJECT

private slots:
  void version_matches_config_macros() {
    const auto& v = taiga::version();
    QCOMPARE(static_cast<int>(v.major), TAIGA_VERSION_MAJOR);
    QCOMPARE(static_cast<int>(v.minor), TAIGA_VERSION_MINOR);
    QCOMPARE(static_cast<int>(v.patch), TAIGA_VERSION_PATCH);
  }

  void version_string_is_semver_core_plus_prerelease() {
    const std::string s = taiga::version().to_string();
    QVERIFY(s.starts_with("2.0.0"));
    QVERIFY(s.find("alpha") != std::string::npos);
  }
};

}  // namespace taiga::test

QTEST_MAIN(taiga::test::VersionTest)

#include "test_version.moc"
