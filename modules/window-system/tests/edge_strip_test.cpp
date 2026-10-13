// SPDX-License-Identifier: MIT
#include "docking/edge_strip.hpp"
#include <window_system/window_manager.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

using namespace window_system;

namespace {

bool isVertical(Edge edge) { return edge == Edge::Left || edge == Edge::Right; }
int axis(Edge edge, QPoint point) { return isVertical(edge) ? point.y() : point.x(); }
int extent(Edge edge, QRect rect) { return isVertical(edge) ? rect.height() : rect.width(); }

void addEdges()
{
    QTest::addColumn<int>("edgeValue");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}

class TestButton final : public QAbstractButton {
public:
    TestButton(Edge edge, int ordinal) : edge_(edge)
    {
        setObjectName("EdgeButton"); setProperty("ordinal", ordinal);
        setText(QString::number(ordinal));
    }
    QSize sizeHint() const override { return isVertical(edge_) ? QSize(22, 100) : QSize(100, 22); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.fillRect(rect(), QColor("#6a568c")); }
private:
    Edge edge_;
};

class Fixture final {
public:
    explicit Fixture(Edge edge) : edge(edge), strip(new EdgeStrip(edge, defaultTheme(), &root))
    {
        auto* layout = new QBoxLayout(isVertical(edge) ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom, &root);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
        layout->addWidget(strip); layout->addStretch();
        for (int index = 0; index < 8; ++index) {
            auto* button = new TestButton(edge, index); buttons.append(button); strip->addButton(button);
            QObject::connect(button, &QAbstractButton::clicked, &root, [this] { ++clicks; });
        }
        tail = new QToolButton; tail->setObjectName("PinEdgeButton");
        tail->setIcon(iconFor("pin-all")); strip->setTailWidget(tail);
        applyTheme(&root);
        root.resize(isVertical(edge) ? QSize(160, 280) : QSize(360, 160)); root.show();
    }
    QScrollArea* scroll() const { return strip->findChild<QScrollArea*>("EdgeScrollViewport"); }
    QScrollBar* overlay() const { return strip->findChild<QScrollBar*>("EdgeScrollBar"); }
    QScrollBar* axisBar() const { return isVertical(edge) ? scroll()->verticalScrollBar() : scroll()->horizontalScrollBar(); }
    Edge edge;
    QWidget root;
    EdgeStrip* strip;
    QList<TestButton*> buttons;
    QToolButton* tail;
    int clicks = 0;
};

void enter(QWidget* widget)
{
    const QPoint point(10, 10);
    QEnterEvent event(point, widget->mapTo(widget->window(), point), widget->mapToGlobal(point));
    QCoreApplication::sendEvent(widget, &event);
}

} // namespace

class EdgeStripTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void boundedViewportScrollsWithoutActivatingButtons_data() { addEdges(); }
    void boundedViewportScrollsWithoutActivatingButtons();
    void insertionGapMovesRealButtonsAndCapturesItsIndex_data() { addEdges(); }
    void insertionGapMovesRealButtonsAndCapturesItsIndex();
    void dragHoverScrollsThenStopsAndNotifiesGeometry_data() { addEdges(); }
    void dragHoverScrollsThenStopsAndNotifiesGeometry();
    void previewDoesNotCreateModelTabsOrReplaceContent_data() { addEdges(); }
    void previewDoesNotCreateModelTabsOrReplaceContent();
};

void EdgeStripTest::boundedViewportScrollsWithoutActivatingButtons()
{
    QFETCH(int, edgeValue); const auto edge = Edge(edgeValue); Fixture fixture(edge);
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.root));
    auto* scroll = fixture.scroll(); auto* bar = fixture.axisBar(); auto* overlay = fixture.overlay();
    QVERIFY(scroll); QVERIFY(bar); QVERIFY(overlay); QTRY_VERIFY(bar->maximum() > 0);
    QTest::mouseMove(&fixture.root, QPoint(fixture.root.width() - 1, fixture.root.height() - 1));
    QTRY_VERIFY(!overlay->underMouse());
    const auto viewport = fixture.strip->viewportRectGlobal(); QVERIFY(!viewport.isEmpty());
    QCOMPARE(isVertical(edge) ? viewport.width() : viewport.height(), 22);
    QCOMPARE(overlay->parentWidget(), scroll->viewport());
    QCOMPARE(isVertical(edge) ? overlay->width() : overlay->height(), 4);
    QCOMPARE(isVertical(edge) ? overlay->height() : overlay->width(), extent(edge, viewport));
    auto* button = fixture.buttons.first(); const auto before = button->mapToGlobal(QPoint());
    const auto local = button->rect().center();
    QWheelEvent wheel(local, button->mapToGlobal(local), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(button, &wheel); QCoreApplication::processEvents();
    QVERIFY(bar->value() > 0); QCOMPARE(overlay->value(), bar->value()); QCOMPARE(fixture.clicks, 0);
    QCOMPARE(axis(edge, button->mapToGlobal(QPoint()) - before), -bar->value());
    auto* unusedAxis = isVertical(edge) ? scroll->horizontalScrollBar() : scroll->verticalScrollBar();
    QCOMPARE(unusedAxis->maximum(), 0);
    enter(scroll->viewport()); QVERIFY(overlay->isVisible());
    QStyleOptionSlider option; option.initFrom(overlay);
    option.orientation = isVertical(edge) ? Qt::Vertical : Qt::Horizontal;
    option.minimum = overlay->minimum(); option.maximum = overlay->maximum();
    option.sliderPosition = overlay->value(); option.pageStep = overlay->pageStep();
    const auto handle = overlay->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, overlay);
    QVERIFY(!handle.isEmpty());
    const auto image = fixture.strip->grab().toImage(); const auto point = overlay->mapTo(fixture.strip, handle.center());
    QCOMPARE(image.pixelColor(qRound(point.x() * image.devicePixelRatio()), qRound(point.y() * image.devicePixelRatio())).rgb(),
        defaultTheme().border.rgb());
    QVERIFY(overlay->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarAddLine, overlay).isEmpty());
    QVERIFY(overlay->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSubLine, overlay).isEmpty());
    bar->setValue(bar->maximum());
    QVERIFY(viewport.contains(fixture.tail->mapToGlobal(fixture.tail->rect().center())));
    QCOMPARE(fixture.tail->parentWidget(), fixture.buttons.first()->parentWidget());
    auto* layout = fixture.tail->parentWidget()->layout(); QVERIFY(layout);
    QVERIFY(layout->indexOf(fixture.tail) > layout->indexOf(fixture.buttons.last()));
}

void EdgeStripTest::insertionGapMovesRealButtonsAndCapturesItsIndex()
{
    QFETCH(int, edgeValue); const auto edge = Edge(edgeValue); Fixture fixture(edge);
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.root));
    QList<QRect> before;
    for (auto* button : fixture.buttons) before.append(button->geometry());
    const auto tailBefore = fixture.tail->geometry();
    const auto childCount = fixture.strip->findChildren<QAbstractButton*>().size();
    fixture.axisBar()->setValue(axis(edge, before[2].topLeft()) - 40);
    fixture.strip->setInsertionPreview(2, 60);
    for (int index = 0; index < fixture.buttons.size(); ++index) {
        auto* button = fixture.buttons[index]; QCOMPARE(button->property("ordinal").toInt(), index);
        const auto delta = isVertical(edge) ? QPoint(0, index >= 2 ? 62 : 0) : QPoint(index >= 2 ? 62 : 0, 0);
        QCOMPARE(button->geometry(), before[index].translated(delta));
    }
    QCOMPARE(fixture.tail->geometry(), tailBefore.translated(isVertical(edge) ? QPoint(0, 62) : QPoint(62, 0)));
    QCOMPARE(fixture.strip->findChildren<QAbstractButton*>().size(), childCount);
    const auto gap = fixture.strip->insertionPreviewRect(); QVERIFY(!gap.isEmpty()); QCOMPARE(extent(edge, gap), 60);
    for (int offset : {1, 15, 30, 45, 58}) {
        const auto point = isVertical(edge) ? QPoint(gap.center().x(), gap.top() + offset)
            : QPoint(gap.left() + offset, gap.center().y());
        QCOMPARE(fixture.strip->insertionIndex(point), 2);
    }
    const auto marker = fixture.strip->insertionMarkerRect(2); QVERIFY(marker.isValid()); QCOMPARE(extent(edge, marker), 4);
    const auto image = fixture.strip->grab().toImage(); const auto point = fixture.strip->mapFromGlobal(gap.center());
    QCOMPARE(image.pixelColor(qRound(point.x() * image.devicePixelRatio()), qRound(point.y() * image.devicePixelRatio())).rgb(),
        defaultTheme().inactiveTab.rgb());
    fixture.strip->setInsertionPreview(-1, 0);
    QVERIFY(fixture.strip->insertionPreviewRect().isEmpty());
    for (int index = 0; index < fixture.buttons.size(); ++index) QCOMPARE(fixture.buttons[index]->geometry(), before[index]);
    QCOMPARE(fixture.tail->geometry(), tailBefore); QCOMPARE(fixture.clicks, 0);

    fixture.strip->setInsertionPreview(fixture.buttons.size(), 42);
    auto* gapWidget = fixture.strip->findChild<QWidget*>("EdgeInsertionPreview"); QVERIFY(gapWidget);
    QVERIFY(axis(edge, gapWidget->geometry().bottomRight()) < axis(edge, fixture.tail->geometry().topLeft()));
    fixture.strip->setInsertionPreview(fixture.buttons.size() + 1, 42);
    QVERIFY(fixture.strip->insertionPreviewRect().isEmpty());
    QCOMPARE(fixture.tail->geometry(), tailBefore);
}

void EdgeStripTest::dragHoverScrollsThenStopsAndNotifiesGeometry()
{
    QFETCH(int, edgeValue); const auto edge = Edge(edgeValue); Fixture fixture(edge);
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.root)); QTRY_VERIFY(fixture.axisBar()->maximum() > 0);
    int notifications = 0; fixture.strip->hoverGeometryChanged = [&] { ++notifications; };
    const auto viewport = fixture.strip->viewportRectGlobal();
    const auto end = isVertical(edge) ? QPoint(viewport.center().x(), viewport.bottom() - 4)
        : QPoint(viewport.right() - 4, viewport.center().y());
    fixture.strip->setDragHover(end);
    QTRY_VERIFY(fixture.axisBar()->value() > 0); QTRY_VERIFY(notifications > 0);
    auto* timer = fixture.strip->findChild<QTimer*>("EdgeAutoScrollTimer"); QVERIFY(timer); QCOMPARE(timer->interval(), 35);
    fixture.strip->setDragHover(QPoint(-10000, -10000)); QVERIFY(!timer->isActive());
    const int stopped = fixture.axisBar()->value(); QTest::qWait(100); QCOMPARE(fixture.axisBar()->value(), stopped);
    fixture.strip->setDragHover(end); QVERIFY(timer->isActive()); QCOMPARE(timer->interval(), 200);
    fixture.strip->hide(); QVERIFY(!timer->isActive()); QVERIFY(fixture.overlay()->isHidden());
}

void EdgeStripTest::previewDoesNotCreateModelTabsOrReplaceContent()
{
    QFETCH(int, edgeValue); const auto edge = Edge(edgeValue);
    WindowManager manager; const auto id = manager.createWindow();
    manager.addTab(id, new QLabel("Center"), {"Center", "label", {}});
    auto* firstContent = new QLabel("Live side content");
    const auto first = manager.addTab(id, firstContent, {"Side 0", "label", {}});
    const auto firstPane = manager.model().tab(first)->activePane;
    QVERIFY(manager.moveTabToEdge(first, edge));
    for (int index = 1; index < 4; ++index) {
        const auto tab = manager.addTab(id, new QLabel("Side"), {QString("Side %1").arg(index), "label", {}});
        QVERIFY(manager.moveTab(tab, manager.model().groupForTab(first)));
    }
    manager.showWindow(id); auto* window = manager.windowWidget(id);
    QVERIFY(QTest::qWaitForWindowExposed(window)); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EdgeStrip* strip = nullptr;
    for (auto* frame : window->findChildren<QFrame*>("EdgeBar"))
        if (frame->property("edge").toInt() == edgeValue) strip = dynamic_cast<EdgeStrip*>(frame);
    QVERIFY(strip);
    const auto model = manager.model().toJson(); const auto order = manager.model().autoHiddenTabs(id, edge);
    const auto buttons = strip->findChildren<QAbstractButton*>("EdgeButton");
    const auto parents = buttons.first()->parentWidget();
    strip->setInsertionPreview(2, 50);
    QVERIFY(!strip->insertionPreviewRect().isEmpty());
    QCOMPARE(manager.model().toJson(), model); QCOMPARE(manager.model().autoHiddenTabs(id, edge), order);
    QCOMPARE(strip->findChildren<QAbstractButton*>("EdgeButton"), buttons);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    for (auto* button : buttons) QCOMPARE(button->parentWidget(), parents);
    strip->setInsertionPreview(-1, 0);
    QVERIFY(strip->insertionPreviewRect().isEmpty()); QCOMPARE(manager.model().toJson(), model);
    QCOMPARE(manager.paneWidget(firstPane), firstContent); QVERIFY(manager.model().validate().ok);
}

QTEST_MAIN(EdgeStripTest)
#include "edge_strip_test.moc"
