#include <QStandardItemModel>
#include <QTest>

#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"

Q_DECLARE_METATYPE(const Anime*)
Q_DECLARE_METATYPE(const ListEntry*)

namespace gui::test {

class SearchMyListOnlyTest final : public QObject {
  Q_OBJECT

private slots:
  void filters_out_not_in_list_when_enabled() {
    QStandardItemModel src;
    src.setRowCount(2);
    src.setColumnCount(1);

    Anime a1;
    a1.id = 1;
    a1.titles.english = "A1";
    ListEntry e1;
    e1.anime_id = 1;
    e1.status = anime::list::Status::Watching;

    Anime a2;
    a2.id = 2;
    a2.titles.english = "A2";

    const QModelIndex i1 = src.index(0, 0);
    const QModelIndex i2 = src.index(1, 0);

    src.setData(i1, QVariant::fromValue<const Anime*>(&a1),
                static_cast<int>(AnimeListItemDataRole::Anime));
    src.setData(i1, QVariant::fromValue<const ListEntry*>(&e1),
                static_cast<int>(AnimeListItemDataRole::ListEntry));

    src.setData(i2, QVariant::fromValue<const Anime*>(&a2),
                static_cast<int>(AnimeListItemDataRole::Anime));
    src.setData(i2, QVariant::fromValue<const ListEntry*>(nullptr),
                static_cast<int>(AnimeListItemDataRole::ListEntry));

    AnimeListProxyModel proxy(nullptr);
    proxy.setSourceModel(&src);

    // My list only
    proxy.setListStatusFilter({/*status=*/0, /*anyStatus=*/true});
    QCOMPARE(proxy.rowCount(), 1);

    // All
    proxy.setListStatusFilter({});
    QCOMPARE(proxy.rowCount(), 2);

    // Not in my list
    proxy.setListStatusFilter({.notInList = true});
    QCOMPARE(proxy.rowCount(), 1);
    const auto* kept = proxy.index(0, 0)
                           .data(static_cast<int>(AnimeListItemDataRole::Anime))
                           .value<const Anime*>();
    QVERIFY(kept != nullptr);
    QCOMPARE(kept->id, 2);
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::SearchMyListOnlyTest)

#include "test_search_my_list_only.moc"
