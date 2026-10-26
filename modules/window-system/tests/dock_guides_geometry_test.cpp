// SPDX-License-Identifier: MIT
// Contract: window-interaction-design.md docking targets. Pure geometry only.
// Dimensions: strip edge x extent x rail visibility x global origin.
#include "docking/dock_guides.hpp"
#include <QtTest/QTest>

using namespace window_system;

class DockGuidesGeometryTest : public QObject {
    Q_OBJECT
private slots:
    void edgePairsFollowTheContentBounds_data();
    void edgePairsFollowTheContentBounds();
    void intersectingStripsAreOrderIndependent();
    void contentInputDoesNotLoseTheVerticalColumnTwice();
};

void DockGuidesGeometryTest::edgePairsFollowTheContentBounds_data()
{
    QTest::addColumn<int>("side"); QTest::addColumn<int>("extent");
    QTest::addColumn<bool>("rail"); QTest::addColumn<QPoint>("origin");
    for (const auto side : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        for (int extent : {36, 90, 220}) for (bool rail : {false, true})
            for (const auto origin : {QPoint(120, 70), QPoint(-1600, -800)})
                QTest::newRow(qPrintable(QString("edge-%1-extent-%2-rail-%3-x-%4")
                    .arg(int(side)).arg(extent).arg(rail).arg(origin.x()))) << int(side) << extent << rail << origin;
}

void DockGuidesGeometryTest::edgePairsFollowTheContentBounds()
{
    QFETCH(int, side); QFETCH(int, extent); QFETCH(bool, rail); QFETCH(QPoint, origin);
    const auto edge = Edge(side);
    const QRect bounds(origin, QSize(1200, 800));
    const int railExtent = rail ? 22 : 0;
    QRect strip = bounds, railRect = bounds, content = bounds;
    switch (edge) {
    case Edge::Left:
        strip.setLeft(bounds.left() + railExtent); strip.setWidth(extent);
        railRect.setWidth(railExtent); content.setLeft(strip.right() + 1); break;
    case Edge::Right:
        strip.setRight(bounds.right() - railExtent); strip.setLeft(strip.right() + 1 - extent);
        railRect.setLeft(bounds.right() + 1 - railExtent); content.setRight(strip.left() - 1); break;
    case Edge::Top:
        strip.setTop(bounds.top() + railExtent); strip.setHeight(extent);
        railRect.setHeight(railExtent); content.setTop(strip.bottom() + 1); break;
    case Edge::Bottom:
        strip.setBottom(bounds.bottom() - railExtent); strip.setTop(strip.bottom() + 1 - extent);
        railRect.setTop(bounds.bottom() + 1 - railExtent); content.setBottom(strip.top() - 1); break;
    }
    const QHash<Edge, QRect> rails = rail ? QHash<Edge, QRect>{{edge, railRect}} : QHash<Edge, QRect>{};
    const auto guides = dockGuides(bounds, content, true, {{strip, edge}}, rails, DockGuideArea::Content);
    QCOMPARE(guides.size(), 13);
    QHash<Edge, int> pairs;
    for (const auto& guide : guides) {
        QVERIFY(content.contains(guide.rect)); QVERIFY(!guide.rect.intersects(strip));
        if (rail) QVERIFY(!guide.rect.intersects(railRect));
        if (!guide.toEdge) continue;
        ++pairs[guide.edge];
        const int inset = guide.pinned ? 34 : 6;
        switch (guide.edge) {
        case Edge::Left: QCOMPARE(guide.rect.left() - content.left(), inset); break;
        case Edge::Right: QCOMPARE(content.right() - guide.rect.right(), inset); break;
        case Edge::Top: QCOMPARE(guide.rect.top() - content.top(), inset); break;
        case Edge::Bottom: QCOMPARE(content.bottom() - guide.rect.bottom(), inset); break;
        }
    }
    for (const auto edgeValue : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) QCOMPARE(pairs.value(edgeValue), 2);
    for (qsizetype i = 0; i < guides.size(); ++i) for (qsizetype j = i + 1; j < guides.size(); ++j)
        QVERIFY(!guides[i].rect.intersects(guides[j].rect));
}

void DockGuidesGeometryTest::intersectingStripsAreOrderIndependent()
{
    const QRect bounds(-1000, -300, 1200, 800);
    QVector<DockStrip> strips{{QRect(-1000, -300, 90, 800), Edge::Left},
        {QRect(110, -300, 90, 800), Edge::Right}, {QRect(-1000, -300, 1200, 30), Edge::Top},
        {QRect(-1000, 470, 1200, 30), Edge::Bottom}};
    const QRect expected(-910, -270, 1020, 740);
    QCOMPARE(dockContentBounds(bounds, strips), expected);
    const auto first = dockGuides(bounds, bounds, true, strips);
    QCOMPARE(first.size(), 13);
    std::reverse(strips.begin(), strips.end());
    const auto second = dockGuides(bounds, bounds, true, strips);
    QCOMPARE(dockContentBounds(bounds, strips), expected);
    QCOMPARE(first.size(), second.size());
    for (qsizetype i = 0; i < first.size(); ++i) {
        QCOMPARE(first[i].rect, second[i].rect); QVERIFY(expected.contains(first[i].rect));
    }
}

void DockGuidesGeometryTest::contentInputDoesNotLoseTheVerticalColumnTwice()
{
    const QRect bounds(0, 0, 1200, 800), content(94, 4, 1102, 792);
    const QVector<DockStrip> strips{{QRect(4, 4, 90, 792), Edge::Left}};
    const auto guides = dockGuides(bounds, content, true, strips, {}, DockGuideArea::Content);
    QCOMPARE(guides.size(), 13);
    for (const auto& guide : guides) if (!guide.toEdge && guide.placement == Placement::Center) {
        QVERIFY((guide.rect.center() - content.center()).manhattanLength() <= 2);
        QCOMPARE(guide.rect.size(), QSize(28, 28));
    }
}

QTEST_APPLESS_MAIN(DockGuidesGeometryTest)
#include "dock_guides_geometry_test.moc"
