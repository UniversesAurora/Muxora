// SPDX-License-Identifier: MIT
// WindowSystem design section 6: spare-space owner, compression, reset
// priority, top/bottom row placement, vacancy repair on both sides of the
// elastic strip, and a viewport smaller than the combined minimum widths.
#include "chrome/strip_layout.hpp"
#include "chrome/strip_layout_state.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace window_system;
Q_DECLARE_METATYPE(QVector<StripWidthInput>)
Q_DECLARE_METATYPE(QVector<StripPolicyInput>)
Q_DECLARE_METATYPE(QVector<StripSlotInput>)

class ToolbarLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void allocationKeepsMinimaAndSpaceOwner_data();
    void allocationKeepsMinimaAndSpaceOwner();
    void storageUsesAuthoredWidthsAndCurrentFormat();
    void restoreKeepsNewStripsAndRejectsAmbiguousRows();
    void policiesKeepOneEligibleOwner_data();
    void policiesKeepOneEligibleOwner();
    void rowPlacementKeepsTheAnchoredSide();
    void areaRowNumbersPreserveExistingSlots();
    void areasGrowFromTheirOuterEdge();
    void vacancyFillsFromTheAnchoredSide_data();
    void vacancyFillsFromTheAnchoredSide();
};

void ToolbarLayoutTest::allocationKeepsMinimaAndSpaceOwner_data()
{
    QTest::addColumn<QVector<StripWidthInput>>("strips");
    QTest::addColumn<int>("available");
    QTest::addColumn<QVector<int>>("expected");
    QTest::newRow("empty") << QVector<StripWidthInput>{} << 0 << QVector<int>{};
    QTest::newRow("spare-goes-to-owner")
        << QVector<StripWidthInput>{{20, 60}, {10, 500, true}, {20, 40}}
        << 180 << QVector<int>{60, 80, 40};
    QTest::newRow("content-compresses-from-tail")
        << QVector<StripWidthInput>{{20, 80}, {20, 60}, {20, 40}}
        << 130 << QVector<int>{80, 30, 20};
    const QVector<StripWidthInput> bothSides{{10, 60}, {10, 40}, {10, 10, true}, {10, 40}, {10, 50}};
    QTest::newRow("left-before-right") << bothSides << 160 << QVector<int>{50, 10, 10, 40, 50};
    auto resetTail = bothSides;
    resetTail.last().naturalWidthPriority = true;
    QTest::newRow("reset-keeps-right-tail") << resetTail << 160 << QVector<int>{60, 30, 10, 10, 50};
    QTest::newRow("minima-can-exceed-viewport")
        << QVector<StripWidthInput>{{30, 50}, {30, 50}} << 40 << QVector<int>{30, 30};
}

void ToolbarLayoutTest::allocationKeepsMinimaAndSpaceOwner()
{
    QFETCH(QVector<StripWidthInput>, strips);
    QFETCH(int, available);
    QFETCH(QVector<int>, expected);
    QCOMPARE(stripRowWidths({strips.constData(), size_t(strips.size())}, available), expected);
}

namespace {
QJsonObject storedLayout()
{
    return QJsonDocument::fromJson(R"({"locked":false,"rows":[["menu"]],"bottomRows":[["tools"]],"strips":{
        "menu":{"visible":false,"width":96,"sizing":"content","excluded":["host.open"],"contentVisible":true,"naturalWidthPriority":true},
        "tools":{"visible":true,"width":-1,"sizing":"elastic","excluded":[],"contentVisible":false}}})").object();
}
}

void ToolbarLayoutTest::storageUsesAuthoredWidthsAndCurrentFormat()
{
    StripLayoutSnapshot snapshot;
    snapshot.rows = {{"menu"}, {"tools"}, {}, {}};
    snapshot.areas = {ToolbarArea::Top, ToolbarArea::Bottom, ToolbarArea::Top, ToolbarArea::Bottom};
    snapshot.locked = false;
    snapshot.strips.insert("menu", {false, 96, false, {"host.open"}, true, true});
    snapshot.strips.insert("tools", {true, -1, true, {}, false});
    QCOMPARE(encodeStripLayout(snapshot), storedLayout());
}

void ToolbarLayoutTest::restoreKeepsNewStripsAndRejectsAmbiguousRows()
{
    const QVector<QStringList> rows{{"menu"}, {"tools", "new"}, {"required"}};
    const QVector<ToolbarArea> areas{ToolbarArea::Top, ToolbarArea::Bottom, ToolbarArea::Top};
    QHash<QString, StripRestorePolicy> policies{{"menu", {}}, {"tools", {true, true}},
        {"new", {}}, {"required", {true, true}}};
    const auto restored = decodeStripLayout(storedLayout(), rows, areas, policies);
    QVERIFY(restored);
    QCOMPARE(restored->rows, rows); QCOMPARE(restored->areas, areas);
    QVERIFY(!restored->locked);
    QCOMPARE(restored->strips.value("menu").width, 96);
    QCOMPARE(restored->strips.value("menu").elastic, std::optional<bool>(false));
    QCOMPARE(restored->strips.value("menu").excluded, QStringList{"host.open"});
    QVERIFY(!restored->strips.contains("new"));
    auto bad = storedLayout();
    bad.insert("bottomRows", QJsonArray{QJsonArray{"menu", "tools"}});
    QVERIFY(!decodeStripLayout(bad, rows, areas, policies));
    bad = storedLayout(); bad.insert("bottomRows", QJsonArray{QJsonArray{"tools", "required"}});
    QVERIFY(!decodeStripLayout(bad, rows, areas, policies));
    policies["required"].requiredInLayout = true;
    QVERIFY(!decodeStripLayout(storedLayout(), rows, areas, policies));
}

void ToolbarLayoutTest::policiesKeepOneEligibleOwner_data()
{
    QTest::addColumn<QVector<StripPolicyInput>>("strips");
    QTest::addColumn<int>("preferred");
    QTest::addColumn<int>("elastic");
    QTest::addColumn<int>("priority");
    QTest::newRow("empty") << QVector<StripPolicyInput>{} << -1 << -1 << -1;
    QTest::newRow("last-eligible-owner") << QVector<StripPolicyInput>{
        {.elastic = true, .elasticAllowed = false}, {.elastic = true}, {.elastic = true}} << -1 << 2 << -1;
    QTest::newRow("explicit-choice") << QVector<StripPolicyInput>{{.elastic = true}, {.elastic = true}} << 0 << 0 << -1;
    QTest::newRow("host-required-owner") << QVector<StripPolicyInput>{
        {.elasticRequired = true}, {.elastic = true}} << 1 << 0 << -1;
    QTest::newRow("priority-needs-natural-content") << QVector<StripPolicyInput>{
        {.naturalWidthPriority = true}, {.elastic = true, .naturalWidthPriority = true},
        {.naturalWidthPriority = true, .hasRequestedWidth = true}} << -1 << 1 << 0;
    QTest::newRow("last-natural-priority") << QVector<StripPolicyInput>{
        {.naturalWidthPriority = true}, {.naturalWidthPriority = true}} << -1 << -1 << 1;
}

void ToolbarLayoutTest::policiesKeepOneEligibleOwner()
{
    QFETCH(QVector<StripPolicyInput>, strips);
    QFETCH(int, preferred); QFETCH(int, elastic); QFETCH(int, priority);
    const std::span<const StripPolicyInput> input(strips.constData(), size_t(strips.size()));
    QCOMPARE(stripElasticOwner(input, preferred), elastic);
    QCOMPARE(stripWidthPriorityOwner(input), priority);
}

void ToolbarLayoutTest::rowPlacementKeepsTheAnchoredSide()
{
    const QVector<StripWidthInput> content{{10, 20}, {10, 30}};
    const auto left = layoutStripRow({content.constData(), size_t(content.size())}, 200, 5, 4);
    QCOMPARE(left.widths, (QVector<int>{20, 30}));
    QCOMPARE(left.positions, (QVector<int>{5, 29}));
    const QVector<StripWidthInput> constrained{{40, 50}, {20, 500, true}, {30, 50}};
    const auto anchored = layoutStripRow({constrained.constData(), size_t(constrained.size())}, 70, 5, 4);
    QCOMPARE(anchored.widths, (QVector<int>{40, 20, 30}));
    QCOMPARE(anchored.positions, (QVector<int>{5, 49, 35}));
}

void ToolbarLayoutTest::areaRowNumbersPreserveExistingSlots()
{
    QVector<QStringList> rows{{"top"}, {"bottom"}, {}, {"inner-bottom"}};
    QVector<ToolbarArea> areas{ToolbarArea::Top, ToolbarArea::Bottom, ToolbarArea::Top, ToolbarArea::Bottom};
    const std::span<const ToolbarArea> input(areas.constData(), size_t(areas.size()));
    QCOMPARE(stripRowIndex(input, ToolbarArea::Top, 1), 2);
    QCOMPARE(stripRowIndex(input, ToolbarArea::Bottom, 1), 3);
    QCOMPARE(stripRowIndex(input, ToolbarArea::Top, 2), -1);
    QCOMPARE(ensureStripRow(rows, areas, ToolbarArea::Bottom, 3), 5);
    QCOMPARE(rows, (QVector<QStringList>{{"top"}, {"bottom"}, {}, {"inner-bottom"}, {}, {}}));
    QCOMPARE(areas.last(), ToolbarArea::Bottom);
    QCOMPARE(ensureStripRow(rows, areas, ToolbarArea::Top, 1), 2);
    QCOMPARE(rows.size(), 6);
}

void ToolbarLayoutTest::areasGrowFromTheirOuterEdge()
{
    const QVector<StripAreaRowInput> rows{{ToolbarArea::Top, 10, true}, {ToolbarArea::Bottom, 12, true},
        {ToolbarArea::Top, 20, true}, {ToolbarArea::Top, 99, false}, {ToolbarArea::Bottom, 8, true}};
    auto layout = layoutStripAreas({rows.constData(), size_t(rows.size())});
    QCOMPARE(layout.heights[0], 30); QCOMPARE(layout.heights[1], 20);
    QCOMPARE(layout.takeRow(ToolbarArea::Top, 10), 0);
    QCOMPARE(layout.takeRow(ToolbarArea::Bottom, 12), 8);
    QCOMPARE(layout.takeRow(ToolbarArea::Top, 20), 10);
    QCOMPARE(layout.takeRow(ToolbarArea::Bottom, 8), 0);
}

void ToolbarLayoutTest::vacancyFillsFromTheAnchoredSide_data()
{
    QTest::addColumn<QVector<StripSlotInput>>("strips");
    QTest::addColumn<int>("removed");
    QTest::addColumn<int>("target");
    QTest::addColumn<int>("width");
    QTest::newRow("left-fills-vacancy") << QVector<StripSlotInput>{{60}, {40}, {30}} << 1 << 0 << 104;
    QTest::newRow("left-adjacent-to-elastic") << QVector<StripSlotInput>{{60}, {40}, {.width = 100, .elastic = true}}
        << 1 << 0 << -1;
    QTest::newRow("right-fills-vacancy") << QVector<StripSlotInput>{{.width = 100, .elastic = true}, {40}, {30}, {50}}
        << 2 << 3 << 84;
    QTest::newRow("right-adjacent-to-elastic") << QVector<StripSlotInput>{{.width = 100, .elastic = true}, {30}, {50}}
        << 1 << 2 << -1;
    QTest::newRow("hidden-has-no-footprint") << QVector<StripSlotInput>{{60}, {.width = 40, .visible = false}, {30}}
        << 1 << -1 << -1;
    QTest::newRow("fixed-neighbor-keeps-width") << QVector<StripSlotInput>{{.width = 60, .fixedWidth = true}, {40}, {30}}
        << 1 << -1 << -1;
}

void ToolbarLayoutTest::vacancyFillsFromTheAnchoredSide()
{
    QFETCH(QVector<StripSlotInput>, strips);
    QFETCH(int, removed); QFETCH(int, target); QFETCH(int, width);
    const auto change = stripVacancyAdjustment({strips.constData(), size_t(strips.size())}, removed, 4);
    QCOMPARE(bool(change), target >= 0);
    if (change) { QCOMPARE(change->index, target); QCOMPARE(change->requestedWidth, width); }
}

QTEST_GUILESS_MAIN(ToolbarLayoutTest)
#include "toolbar_layout_test.moc"
