#include <QListView>
#include <QStringListModel>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QWidget>

#include "gui/common/anime_list_item_delegate_cards.hpp"

namespace gui::test {

class CardDelegateLayoutTest final : public QObject {
  Q_OBJECT

private slots:
  void size_hint_is_positive_when_list_is_tiny_before_layout() {
    QListView list;
    list.setViewMode(QListView::IconMode);
    list.setSpacing(16);
    list.resize(1, 1);

    QStringListModel model(QStringList() << QStringLiteral("One"));
    list.setModel(&model);

    ListItemDelegateCards delegate(&list);
    list.setItemDelegate(&delegate);

    const QModelIndex ix = model.index(0, 0);
    QVERIFY(ix.isValid());
    QStyleOptionViewItem opt;
    opt.initFrom(&list);

    const QSize sh = delegate.sizeHint(opt, ix);
    QVERIFY(sh.width() >= 180);
    QCOMPARE(sh.height(), 210);
  }

  void size_hint_when_parent_is_not_qlistview_uses_default_tile() {
    QWidget holder;
    ListItemDelegateCards delegate(&holder);

    QStringListModel model(QStringList() << QStringLiteral("X"));
    QStyleOptionViewItem opt;
    opt.initFrom(&holder);
    const QModelIndex ix = model.index(0, 0);
    QVERIFY(ix.isValid());

    const QSize sh = delegate.sizeHint(opt, ix);
    QCOMPARE(sh.width(), 360);
    QCOMPARE(sh.height(), 210);
  }

  void size_hint_respects_wide_viewport_for_icon_grid() {
    QListView list;
    list.setViewMode(QListView::IconMode);
    list.setSpacing(16);
    list.resize(1680, 720);

    QStringListModel model(QStringList() << QStringLiteral("Wide"));
    list.setModel(&model);

    ListItemDelegateCards delegate(&list);
    list.setItemDelegate(&delegate);

    const QModelIndex ix = model.index(0, 0);
    QStyleOptionViewItem opt;
    opt.initFrom(&list);

    const QSize sh = delegate.sizeHint(opt, ix);
    QVERIFY(sh.width() >= 180);
    QCOMPARE(sh.height(), 210);
  }
};

}  // namespace gui::test

QTEST_MAIN(gui::test::CardDelegateLayoutTest)

#include "test_card_delegate_layout.moc"
