// SPDX-License-Identifier: MIT
// WindowSystem design section 6: spare-space owner, compression, reset
// priority, and a viewport smaller than the combined minimum widths.
#include "chrome/strip_layout.hpp"
#include "chrome/strip_layout_state.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace window_system;
Q_DECLARE_METATYPE(QVector<StripWidthInput>)

class ToolbarLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void allocationKeepsMinimaAndSpaceOwner_data();
    void allocationKeepsMinimaAndSpaceOwner();
    void storageUsesAuthoredWidthsAndCurrentFormat();
    void restoreKeepsNewStripsAndRejectsAmbiguousRows();
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

QTEST_GUILESS_MAIN(ToolbarLayoutTest)
#include "toolbar_layout_test.moc"
