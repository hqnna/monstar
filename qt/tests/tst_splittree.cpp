#include "SplitTree.h"

#include <QTest>

class TestSplitTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void singlePaneFillsTheArea()
    {
        SplitTree tree(1);
        const QRectF area(0, 0, 800, 600);
        QCOMPARE(tree.layout(area, 1).value(1), area);
        QVERIFY(tree.dividers(area, 1).isEmpty());
        QVERIFY(!tree.remove(1));
        QCOMPARE(tree.next(1), 1);
    }

    void splitsPlaceNewPanesOnTheRequestedSide()
    {
        SplitTree tree(1);
        QVERIFY(tree.split(1, SplitDirection::Right, 2));
        QVERIFY(tree.split(2, SplitDirection::Up, 3));
        QCOMPARE(tree.panes(), (QList<int>{1, 3, 2}));

        const auto rects = tree.layout(QRectF(0, 0, 801, 601), 1);
        QCOMPARE(rects.value(1), QRectF(0, 0, 400, 601));
        QCOMPARE(rects.value(3), QRectF(401, 0, 400, 300));
        QCOMPARE(rects.value(2), QRectF(401, 301, 400, 300));

        const auto dividers = tree.dividers(QRectF(0, 0, 801, 601), 1);
        QCOMPARE(dividers.size(), 2);
        QVERIFY(dividers.at(0).vertical);
        QCOMPARE(dividers.at(0).rect, QRectF(400, 0, 1, 601));
        QVERIFY(!dividers.at(1).vertical);
        QCOMPARE(dividers.at(1).rect, QRectF(401, 300, 400, 1));
        QVERIFY(!tree.split(42, SplitDirection::Down, 4));
    }

    void removingAPanePromotesItsSibling()
    {
        SplitTree tree(1);
        tree.split(1, SplitDirection::Right, 2);
        tree.split(2, SplitDirection::Down, 3);
        QVERIFY(tree.remove(2));
        QCOMPARE(tree.panes(), (QList<int>{1, 3}));
        QVERIFY(tree.remove(1));
        QCOMPARE(tree.panes(), (QList<int>{3}));
        QCOMPARE(tree.layout(QRectF(0, 0, 100, 100), 1).value(3), QRectF(0, 0, 100, 100));
    }

    void navigationFollowsGeometryAndReadingOrder()
    {
        SplitTree tree(1);
        tree.split(1, SplitDirection::Right, 2);
        tree.split(2, SplitDirection::Down, 3);
        const QRectF area(0, 0, 801, 601);
        QCOMPARE(tree.neighbor(1, SplitDirection::Right, area, 1), 2);
        QCOMPARE(tree.neighbor(3, SplitDirection::Left, area, 1), 1);
        QCOMPARE(tree.neighbor(2, SplitDirection::Down, area, 1), 3);
        QCOMPARE(tree.neighbor(3, SplitDirection::Up, area, 1), 2);
        QCOMPARE(tree.neighbor(1, SplitDirection::Left, area, 1), -1);
        QCOMPARE(tree.next(3), 1);
        QCOMPARE(tree.previous(1), 3);
    }

    void zoomFillsTheAreaUntilCleared()
    {
        SplitTree tree(1);
        tree.split(1, SplitDirection::Right, 2);
        tree.setZoomed(2);
        const QRectF area(0, 0, 100, 50);
        const auto rects = tree.layout(area, 1);
        QCOMPARE(rects.size(), 1);
        QCOMPARE(rects.value(2), area);
        QVERIFY(tree.dividers(area, 1).isEmpty());
        // Navigation still sees the hidden panes.
        QCOMPARE(tree.neighbor(2, SplitDirection::Left, area, 1), 1);
        tree.split(2, SplitDirection::Down, 3);
        QCOMPARE(tree.zoomed(), -1);
    }

    void resizingMovesTheNearestMatchingDivider()
    {
        SplitTree tree(1);
        tree.split(1, SplitDirection::Right, 2);
        tree.split(2, SplitDirection::Down, 3);
        const QRectF area(0, 0, 801, 601);
        QVERIFY(tree.resize(3, SplitDirection::Left, 100, area, 1));
        QCOMPARE(tree.layout(area, 1).value(1).width(), 300);
        QVERIFY(tree.resize(3, SplitDirection::Up, 50, area, 1));
        QCOMPARE(tree.layout(area, 1).value(2).height(), 250);
        // A lone column has no horizontal divider.
        SplitTree column(1);
        QVERIFY(!column.resize(1, SplitDirection::Left, 10, area, 1));
        // Ratios stay within bounds.
        QVERIFY(tree.resize(1, SplitDirection::Right, 10000, area, 1));
        QVERIFY(tree.layout(area, 1).value(2).width() > 0);
    }

    void equalizeWeighsPanesAlongEachAxis()
    {
        SplitTree tree(1);
        tree.split(1, SplitDirection::Right, 2);
        tree.split(2, SplitDirection::Right, 3);
        tree.equalize();
        const auto rects = tree.layout(QRectF(0, 0, 902, 100), 1);
        QCOMPARE(rects.value(1).width(), 300);
        QCOMPARE(rects.value(2).width(), 300);
        QCOMPARE(rects.value(3).width(), 300);
    }

    void dividerRatiosAreClamped()
    {
        SplitTree tree(1);
        tree.split(1, SplitDirection::Down, 2);
        const int id = tree.dividers(QRectF(0, 0, 100, 101), 1).at(0).id;
        QVERIFY(tree.setDividerRatio(id, 0.25));
        QCOMPARE(tree.layout(QRectF(0, 0, 100, 101), 1).value(1).height(), 25);
        QVERIFY(tree.setDividerRatio(id, -3));
        QCOMPARE(tree.layout(QRectF(0, 0, 100, 101), 1).value(1).height(), 5);
        QVERIFY(!tree.setDividerRatio(id + 1, 0.5));
    }
};

QTEST_APPLESS_MAIN(TestSplitTree)
#include "tst_splittree.moc"
