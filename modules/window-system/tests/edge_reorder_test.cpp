// SPDX-License-Identifier: MIT
// Rail geometry and pointer prediction, with no exposed windows or input.
#include "docking/edge_strip.hpp"
#include "docking/drag_presentation.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QWindow>

using namespace window_system;

namespace {
class LabelButton final : public QAbstractButton {
public:
    explicit LabelButton(bool vertical) : size_(vertical ? QSize(28, 70) : QSize(90, 28)) {}
    QSize sizeHint() const override { return size_; }
protected:
    void paintEvent(QPaintEvent*) override {}
private:
    QSize size_;
};

struct Rail {
    QWidget host;
    EdgeStrip* strip;
    QList<QAbstractButton*> buttons;
    bool vertical;
    explicit Rail(Edge edge, int count = 4, int length = 420)
        : strip(new EdgeStrip(edge, defaultTheme(), &host)), vertical(edge == Edge::Left || edge == Edge::Right)
    {
        host.setAttribute(Qt::WA_DontShowOnScreen);
        host.resize(600, 600);
        strip->setGeometry(vertical ? QRect(20, 20, 28, length) : QRect(20, 20, length, 28));
        for (int index = 0; index < count; ++index) {
            auto* button = new LabelButton(vertical);
            button->setProperty("tabId", QUuid::createUuid());
            strip->addButton(button); buttons.append(button);
        }
        strip->setTailWidget(new QWidget);
        host.show(); strip->show();
        settle();
    }
    void settle()
    {
        strip->layout()->activate();
        QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
    }
    QRect global(QWidget* widget) const { return {widget->mapToGlobal(QPoint()), widget->size()}; }
    QRect gap() const { return global(strip->findChild<QWidget*>("EdgeInsertionPreview")); }
    int extent() const { return vertical ? 70 : 90; }
};

void sides()
{
    QTest::addColumn<Edge>("edge");
    QTest::newRow("left") << Edge::Left; QTest::newRow("right") << Edge::Right;
    QTest::newRow("top") << Edge::Top; QTest::newRow("bottom") << Edge::Bottom;
}
}

class EdgeReorderTest final : public QObject {
    Q_OBJECT
private slots:
    void acceptedGapUsesSymmetricEdges_data() { sides(); }
    void acceptedGapUsesSymmetricEdges()
    {
        QFETCH(Edge, edge); Rail rail(edge);
        rail.strip->setDraggedTab(rail.buttons[1]->property("tabId").toUuid());
        rail.strip->setInsertionPreview(1, rail.extent()); rail.settle();
        auto carried = rail.gap(); const auto next = rail.global(rail.buttons[2]);
        if (rail.vertical) carried.moveBottom(next.center().y()); else carried.moveRight(next.center().x());
        QCOMPARE(rail.strip->insertionIndex(carried.center(), carried), 1);
        carried.translate(rail.vertical ? QPoint(0, 1) : QPoint(1, 0));
        QCOMPARE(rail.strip->insertionIndex(carried.center(), carried), 2);
        rail.strip->setInsertionPreview(2, rail.extent()); rail.settle();
        QCOMPARE(rail.strip->insertionIndex(carried.center(), carried), 2);
        const auto previous = rail.global(rail.buttons[2]);
        if (rail.vertical) carried.moveTop(previous.center().y()); else carried.moveLeft(previous.center().x());
        QCOMPARE(rail.strip->insertionIndex(carried.center(), carried), 2);
        carried.translate(rail.vertical ? QPoint(0, -1) : QPoint(-1, 0));
        QCOMPARE(rail.strip->insertionIndex(carried.center(), carried), 1);
        QVERIFY(!rail.host.windowHandle() || !rail.host.windowHandle()->isExposed());
    }

    void viewportClipsTheCarrierAndTailNeverBecomesATab_data() { sides(); }
    void viewportClipsTheCarrierAndTailNeverBecomesATab()
    {
        QFETCH(Edge, edge); Rail rail(edge, 8, 180);
        rail.strip->setDraggedTab(rail.buttons[1]->property("tabId").toUuid());
        rail.strip->setInsertionPreview(3, rail.extent()); rail.settle();
        rail.strip->restoreScrollOffset(rail.extent() * 2); rail.settle();
        QVERIFY(rail.strip->scrollOffset() > 0);
        const auto viewport = rail.strip->viewportRectGlobal();
        auto carried = rail.gap();
        QCOMPARE(rail.strip->insertionIndex(viewport.center(), carried), 3);
        // An oversized trailing edge cannot displace labels whose centers
        // are outside the currently readable track viewport.
        if (rail.vertical) carried.setBottom(viewport.bottom() + 10000);
        else carried.setRight(viewport.right() + 10000);
        QCOMPARE(rail.strip->insertionIndex(viewport.center(), carried), 3);
        const auto* area = rail.strip->findChild<QScrollArea*>("EdgeScrollViewport");
        QVERIFY(area);
        const auto edgePoint = rail.vertical ? QPoint(viewport.center().x(), rail.strip->mapToGlobal(rail.strip->rect().bottomRight()).y())
            : QPoint(rail.strip->mapToGlobal(rail.strip->rect().bottomRight()).x(), viewport.center().y());
        QCOMPARE(rail.strip->insertionIndex(edgePoint, carried), -1);
        QVERIFY(!rail.host.windowHandle() || !rail.host.windowHandle()->isExposed());
    }

    void labelPredictionUsesTheNewPointerAndNormalizedAnchor()
    {
        QPixmap image(240, 64); image.setDevicePixelRatio(2); image.fill(Qt::white);
        const DragAnchor anchor{{150, 20}, {180, 40}};
        DragLabelPreview preview(image, anchor);
        const auto size = image.deviceIndependentSize().toSize();
        for (const auto pointer : {QPoint(10, 20), QPoint(800, -40), QPoint(-500, 900)}) {
            const auto expected = QRect(pointer - anchor.forSize(size), size);
            QCOMPARE(preview.labelRectAt(pointer), expected);
            QVERIFY(expected.contains(pointer));
            preview.move(pointer + QPoint(6000, -3000));
            QCOMPARE(preview.labelRectAt(pointer), expected);
        }
        QVERIFY(!preview.isVisible());
        DragLabelPreview centered(image);
        QCOMPARE(centered.labelRectAt(QPoint(200, 300)).center(), QPoint(200, 300));
        QVERIFY(!centered.isVisible());
    }

    void edgeHostReportsOnlyItsLabel_data() { sides(); }
    void edgeHostReportsOnlyItsLabel()
    {
        QFETCH(Edge, edge);
        QWidget parent;
        auto* content = new QWidget;
        EdgeDragPresentation host(QStringLiteral("Dragged workspace"), edge, defaultTheme(), content, &parent);
        host.resize(500, 300); host.layout()->activate();
        QVERIFY(!host.labelRect().isEmpty());
        QCOMPARE(host.labelAnchor(), host.labelRect().center());
        QCOMPARE(host.visibleRegion(), QRegion(host.labelRect()));
        QVERIFY(host.labelRect().size() != host.size());
        QVERIFY(!parent.isVisible());
    }
};

QTEST_MAIN(EdgeReorderTest)
#include "edge_reorder_test.moc"
