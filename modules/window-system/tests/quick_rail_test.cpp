// SPDX-License-Identifier: MIT
// Phase 10.3, Quick sidebar rail (window interaction design, sidebars):
// order along the rail, the all-pin action at its end, drops by pointer,
// the dragged tab's gap, wheel scrolling and the anchored active tab.
#include "quick/button_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/rail_item.hpp"

#include <QQuickWindow>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
struct Fixture {
    explicit Fixture(Edge edge, int count = 4, QSize size = {600, 400})
    {
        window.resize(size);
        rail = new RailItem(edge, defaultTheme(), window.contentItem());
        const bool vertical = edge == Edge::Left || edge == Edge::Right;
        for (int index = 0; index < count; ++index) {
            const auto tab = QUuid::createUuid(); tabs.append(tab);
            rail->addButton(new RailButtonItem(defaultTheme(), tab, QStringLiteral("Sidebar tab %1").arg(index), vertical,
                                               defaultTheme().metrics.edgeThickness));
        }
        rail->setTail(new ButtonItem(defaultTheme()));
        rail->setSize(vertical ? QSizeF(rail->thickness(), size.height()) : QSizeF(size.width(), rail->thickness()));
        window.show();
    }
    [[nodiscard]] QRect buttonRect(int index) const
    {
        auto* button = rail->buttons().at(index);
        return QRect(button->mapToGlobal(QPointF()).toPoint(), button->size().toSize());
    }
    QQuickWindow window;
    RailItem* rail;
    QVector<TabId> tabs;
};
} // namespace

class QuickRailTest final : public QObject {
    Q_OBJECT
private slots:
    void buttonsFollowTheirOrderAndTheTailStaysAtTheEnd_data()
    {
        QTest::addColumn<int>("edge");
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) QTest::newRow(qPrintable(QString::number(int(edge)))) << int(edge);
    }

    void buttonsFollowTheirOrderAndTheTailStaysAtTheEnd()
    {
        QFETCH(int, edge);
        Fixture fixture{Edge(edge)};
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        const bool vertical = fixture.rail->vertical();
        QVERIFY(fixture.rail->thickness() >= defaultTheme().metrics.edgeThickness);
        for (int index = 1; index < fixture.tabs.size(); ++index) {
            const auto previous = fixture.buttonRect(index - 1), current = fixture.buttonRect(index);
            QVERIFY(vertical ? current.top() > previous.bottom() : current.left() > previous.right());
        }
        const auto viewport = fixture.rail->viewportRectGlobal();
        QVERIFY(viewport.contains(fixture.buttonRect(0).topLeft()));
        const auto railRect = QRect(fixture.rail->mapToGlobal(QPointF()).toPoint(), fixture.rail->size().toSize());
        QVERIFY(vertical ? viewport.bottom() < railRect.bottom() : viewport.right() < railRect.right());
    }

    void dropsFollowThePointerAndTheDraggedTabLeavesAGap()
    {
        Fixture fixture(Edge::Left, 4, QSize(600, 760));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        const auto first = fixture.buttonRect(0), last = fixture.buttonRect(3);
        QVERIFY(fixture.rail->viewportRectGlobal().contains(last));
        QCOMPARE(fixture.rail->insertionIndex(first.center() - QPoint(0, first.height() / 4)), 0);
        QCOMPARE(fixture.rail->insertionIndex(QPoint(last.center().x(), last.bottom() + 4)), 4);
        QCOMPARE(fixture.rail->insertionIndex(first.topLeft() - QPoint(40, 0)), -1);
        // The carried tab leaves; a gap as long as it opens where it lands.
        fixture.rail->setDraggedTab(fixture.tabs[1]);
        QVERIFY(!fixture.rail->buttons().at(1)->isVisible());
        fixture.rail->setInsertionPreview(2, 50);
        const auto gap = fixture.rail->insertionPreviewRect();
        QCOMPARE(gap.height(), 50);
        QVERIFY(gap.top() > fixture.buttonRect(2).bottom());
        QVERIFY(gap.bottom() < fixture.buttonRect(3).top());
        fixture.rail->setInsertionPreview(-1, 0); fixture.rail->setDraggedTab({});
        QVERIFY(fixture.rail->insertionPreviewRect().isEmpty()); QVERIFY(fixture.rail->buttons().at(1)->isVisible());
    }

    void wheelScrollsWithinTheTrackAndRebuildsKeepTheActiveTab()
    {
        Fixture fixture(Edge::Left, 14, QSize(600, 300));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        fixture.rail->scrollByWheel({0, -90}, false);
        QCOMPARE(fixture.rail->scrollOffset(), 90);
        fixture.rail->scrollByWheel({0, -100000}, false);
        const int end = fixture.rail->scrollOffset();
        QVERIFY(end > 90);
        fixture.rail->scrollByWheel({0, 100000}, false);
        QCOMPARE(fixture.rail->scrollOffset(), 0);
        fixture.rail->scrollByWheel({0, -200}, false);
        const auto viewport = fixture.rail->viewportRectGlobal();
        int shown = 1;
        while (shown < fixture.tabs.size() && !viewport.contains(fixture.buttonRect(shown))) ++shown;
        QVERIFY(shown < fixture.tabs.size());
        fixture.rail->buttons().at(shown)->setChecked(true);
        const auto before = fixture.buttonRect(shown);
        const auto anchor = fixture.rail->scrollAnchor();
        QVERIFY(anchor.active); QCOMPARE(anchor.tab, fixture.tabs[shown]);
        fixture.rail->restoreScrollOffset(0);
        fixture.rail->restoreScrollAnchor(anchor);
        QCOMPARE(fixture.buttonRect(shown).top(), before.top());
    }
};

QTEST_MAIN(QuickRailTest)
#include "quick_rail_test.moc"
