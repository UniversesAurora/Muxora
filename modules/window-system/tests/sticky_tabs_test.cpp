// SPDX-License-Identifier: MIT
#include "docking/tab_strip.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScopeGuard>
#include <QScreen>
#include <QTest>

using namespace window_system;

// Sticky tab sizing and placement from 8.15b: compact and shrunk sticky tabs
// have fixed widths and, in a single row, stay at the viewport's start while
// the other tabs scroll under them.
namespace {
TabColorPreset solidPreset(const QString& id, QColor background)
{
    auto preset = defaultTabColorPresets().front(); preset.id = id;
    preset.groupActive = preset.groupInactive = preset.autoHideActive = preset.autoHideInactive
        = {background, Qt::black, QColor("#123456"), QColor("#2468ac")};
    return preset;
}

// Sticky tabs get a red preset and the others a blue one, so a sample shows
// which tab is drawn at a point.
const QColor stickyColor("#ff0000"), plainColor("#0000ff");

QVector<TabId> populate(GroupTabs& tabs, int count, int sticky, TabAppearance appearance, QIcon icon = {})
{
    appearance.showNewTabButton = false;
    appearance.colors = {solidPreset("sticky", stickyColor), solidPreset("plain", plainColor)};
    tabs.setAppearance(appearance);
    QVector<TabId> ids;
    for (int index = 0; index < count; ++index) {
        const auto id = QUuid::createUuid(); ids.append(id);
        const auto title = QStringLiteral("Wide title %1").arg(index + 1);
        tabs.addTab(new QLabel(title), icon, title, id);
        TabRecord record; record.id = id; record.title = title; record.colorId = index < sticky ? "sticky" : "plain";
        tabs.setTabMetadata(index, record);
    }
    tabs.setStickyCount(sticky);
    applyTheme(&tabs); return ids;
}

QImage render(QWidget* widget)
{
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); widget->render(&painter); painter.end(); return image;
}

void sendBarMouse(QWidget* bar, QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, point, bar->mapToGlobal(point), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(bar, &event);
}

void sendMouse(QWidget* receiver, QEvent::Type type, QPoint global,
               Qt::MouseButton button = Qt::NoButton, Qt::MouseButtons buttons = Qt::LeftButton)
{
    test::authoredMouseInput().takePriority();
    QMouseEvent event(type, receiver->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
    test::authoredMouseInput().takePriority();
    QApplication::processEvents();
}

GroupTabs* groupView(QWidget* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}

QWidget* labelPreview()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "DragLabelPreview") return widget;
    return nullptr;
}

TabAppearance sized(StickyTabSizing sizing)
{
    TabAppearance appearance; appearance.showIcons = false; appearance.stickySizing = sizing; return appearance;
}
}

class StickyTabsTest final : public QObject {
    Q_OBJECT
private slots:
    void sizedStickyTabsUseFixedWidths();
    void compactStickyTabsShowTheirIconOrInitialWithoutAPin();
    void sizedStickyTabsStayInViewWhileOthersScroll();
    void activatingAScrolledTabRevealsItPastTheStickyTabs();
    void draggingIntoAndOutOfASizedRunUsesEachSegmentsWidth();
};

void StickyTabsTest::sizedStickyTabsUseFixedWidths()
{
    for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Compact, StickyTabSizing::Shrink}) {
        GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 4, 2, sized(sizing));
        tabs.resize(900, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        auto* bar = tabs.tabBar();
        const int expected = sizing == StickyTabSizing::Compact ? compactStickyTabWidth
            : sizing == StickyTabSizing::Shrink ? shrinkStickyTabWidth : bar->naturalTabWidth(0);
        QCOMPARE(bar->displayTabRect(0).width(), expected);
        QCOMPARE(bar->displayTabRect(1).width(), sizing == StickyTabSizing::Normal ? bar->naturalTabWidth(1) : expected);
        QCOMPARE(bar->displayTabRect(1).left(), bar->displayTabRect(0).right() + 1);
        // The other tabs keep their natural width right after the sticky ones.
        QCOMPARE(bar->displayTabRect(2).width(), bar->naturalTabWidth(2));
        QCOMPARE(bar->displayTabRect(2).left(), bar->displayTabRect(1).right() + 1);
        // Unsticking a tab returns it to its natural width.
        tabs.setStickyCount(1);
        QCOMPARE(bar->displayTabRect(1).width(), bar->naturalTabWidth(1));
    }
}

void StickyTabsTest::compactStickyTabsShowTheirIconOrInitialWithoutAPin()
{
    QPixmap pixmap(16, 16); pixmap.fill(QColor("#00ff00"));
    auto appearance = sized(StickyTabSizing::Compact); appearance.showIcons = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 3, 2, appearance, QIcon(pixmap));
    // The second sticky tab has no icon and shows its initial instead.
    tabs.tabBar()->setTabIcon(1, {});
    int unstuck = 0; tabs.unstickRequested = [&unstuck](int) { ++unstuck; };
    tabs.resize(900, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); tabs.setCurrentIndex(2);
    auto image = render(bar);
    const auto first = bar->displayTabRect(0), second = bar->displayTabRect(1);
    // The icon is the preset's icon color, centered in the tab.
    const auto iconColor = QColor("#2468ac");
    QCOMPARE(image.pixelColor(first.center()).rgb(), iconColor.rgb());
    int left = first.right(), right = first.left();
    for (int x = first.left(); x <= first.right(); ++x)
        if (image.pixelColor(x, first.center().y()).rgb() == iconColor.rgb()) { left = std::min(left, x); right = std::max(right, x); }
    QCOMPARE(right - left + 1, 16); QVERIFY(qAbs((left - first.left()) - (first.right() - right)) <= 1);
    // The initial is drawn in the text color; nothing is drawn in a pin slot.
    // The last sticky tab's final column is the border to the other tabs.
    QCOMPARE(image.pixelColor(second.right(), second.center().y()).rgb(), defaultTheme().border.rgb());
    bool inked = false;
    for (int y = second.top() + 2; y < second.bottom() - 2; ++y)
        for (int x = second.left(); x < second.right(); ++x)
            if (image.pixelColor(x, y).rgb() != stickyColor.rgb()) {
                inked = true; QVERIFY(x > second.left() + 8 && x < second.right() - 8);
            }
    QVERIFY(inked);
    // A press at the trailing end selects the tab rather than unsticking it.
    const QPoint trailing(first.right() - 4, first.center().y());
    sendBarMouse(bar, QEvent::MouseButtonPress, trailing, Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, trailing, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(unstuck, 0); QCOMPARE(tabs.currentIndex(), 0);
}

void StickyTabsTest::sizedStickyTabsStayInViewWhileOthersScroll()
{
    for (const auto sizing : {StickyTabSizing::Compact, StickyTabSizing::Shrink}) {
        GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 20, 2, sized(sizing));
        tabs.resize(500, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        auto* bar = tabs.tabBar(); tabs.setCurrentIndex(0);
        const int width = sizing == StickyTabSizing::Compact ? compactStickyTabWidth : shrinkStickyTabWidth;
        QCOMPARE(bar->stickyRunExtent(), 2 * width);
        tabs.scrollByWheel(QPoint(0, -600)); QTRY_VERIFY(tabs.scrollOffset() > 3 * width);
        const int offset = tabs.scrollOffset();
        QCOMPARE(bar->displayTabRect(0).left() - offset, 0);
        QCOMPARE(bar->displayTabRect(1).left() - offset, width);
        // The sticky tabs cover the scrolled tabs; the next one is drawn after them.
        const auto image = render(bar); const int y = bar->height() / 2;
        QCOMPARE(image.pixelColor(offset + 3, y).rgb(), stickyColor.rgb());
        QCOMPARE(image.pixelColor(offset + 2 * width - 3, y).rgb(), stickyColor.rgb());
        QCOMPARE(image.pixelColor(offset + 2 * width + 3, y).rgb(), plainColor.rgb());
        // A click over them selects the sticky tab, not the one scrolled beneath.
        const QPoint point(offset + 10, y);
        sendBarMouse(bar, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        sendBarMouse(bar, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(tabs.currentIndex(), 0);
        // A drop over them goes among the sticky tabs, after them among the others.
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(offset + 3, y))), 0);
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(offset + 2 * width - 3, y))), 2);
        QVERIFY(tabs.insertionIndex(bar->mapToGlobal(QPoint(offset + 2 * width + 3, y))) > 2);
    }
    // Normal sticky tabs scroll away, and in several rows nothing is kept in view.
    GroupTabs normal(defaultTheme(), nullptr); populate(normal, 20, 2, sized(StickyTabSizing::Normal));
    normal.resize(500, 200); normal.show(); QVERIFY(QTest::qWaitForWindowExposed(&normal));
    normal.scrollByWheel(QPoint(0, -600)); QTRY_VERIFY(normal.scrollOffset() > 0);
    QCOMPARE(normal.tabBar()->stickyRunExtent(), 0);
    QVERIFY(normal.tabBar()->displayTabRect(0).left() < normal.scrollOffset());
    auto multiline = sized(StickyTabSizing::Compact); multiline.multiline = true;
    GroupTabs rows(defaultTheme(), nullptr); populate(rows, 20, 2, multiline);
    rows.resize(500, 300); rows.show(); QVERIFY(QTest::qWaitForWindowExposed(&rows));
    QCOMPARE(rows.tabBar()->stickyRunExtent(), 0);
    QCOMPARE(rows.tabBar()->displayTabRect(0).width(), compactStickyTabWidth);
}

void StickyTabsTest::activatingAScrolledTabRevealsItPastTheStickyTabs()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 20, 2, sized(StickyTabSizing::Shrink));
    tabs.resize(500, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int run = bar->stickyRunExtent();
    tabs.setCurrentIndex(19); QTRY_VERIFY(tabs.scrollOffset() > 0);
    // A tab under the sticky tabs scrolls out from under them.
    int hidden = 2;
    while (hidden + 1 < bar->count() && bar->displayTabRect(hidden + 1).left() < tabs.scrollOffset() + run) ++hidden;
    QVERIFY(bar->displayTabRect(hidden).left() < tabs.scrollOffset() + run);
    tabs.setCurrentIndex(hidden);
    QCOMPARE(bar->displayTabRect(hidden).left() - tabs.scrollOffset(), run);
    // A sticky tab is always shown and never scrolls the strip.
    const int offset = tabs.scrollOffset();
    tabs.setCurrentIndex(1); QCOMPARE(tabs.scrollOffset(), offset);
}

void StickyTabsTest::draggingIntoAndOutOfASizedRunUsesEachSegmentsWidth()
{
    // Real strip drags with compact sticky tabs: the preview among them is
    // compact, among the others it has the carried tab's natural width, and
    // the drop matches the preview.
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = sized(StickyTabSizing::Compact); appearance.showNewTabButton = false;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto window = manager.createWindow(WindowKind::Workbench, {"Sized sticky drag", {900, 360}});
    QVector<TabId> ids;
    for (int index = 0; index < 5; ++index)
        ids.append(manager.addTab(window, new QLabel(QString("Content %1").arg(index)), {QString("Tab %1").arg(index), "label", {}}));
    QVERIFY(manager.setTabSticky(ids[0], true)); QVERIFY(manager.setTabSticky(ids[1], true)); // [0 1 | 2 3 4]
    const auto group = manager.model().groupForTab(ids[0]);
    manager.windowWidget(window)->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto drag = [&](TabId tab, int targetIndex, bool sticky) {
        auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
        auto* bar = view->tabBar(); const int from = manager.model().group(group)->tabs.indexOf(tab);
        const int natural = bar->naturalTabWidth(from);
        const auto start = bar->mapToGlobal(bar->displayTabRect(from).center());
        sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
        sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
        auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
        QPoint point;
        for (int x = view->viewportRectGlobal().left() + 1; x < view->viewportRectGlobal().right(); x += 2) {
            const QPoint candidate(x, start.y());
            sendMouse(receiver, QEvent::MouseMove, candidate);
            if (view->insertionIndex(candidate) == targetIndex && view->insertionSticky() == sticky) { point = candidate; break; }
        }
        QVERIFY(!point.isNull());
        QCOMPARE(bar->insertionPreviewRect().width(), sticky ? compactStickyTabWidth : natural);
        sendMouse(receiver, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
        QTRY_VERIFY(!labelPreview());
        QCOMPARE(manager.model().groupForTab(tab), group);
        QCOMPARE(manager.isTabSticky(tab), sticky);
        QCOMPARE(manager.model().group(group)->tabs.indexOf(tab), targetIndex);
    };
    drag(ids[4], 0, true);  // into the run: [4 0 1 | 2 3]
    drag(ids[0], 3, false); // out of it: [4 1 | 2 0 3]
    drag(ids[1], 1, true);  // right after the run, still sticky: [4 1 | 2 0 3]
    QCOMPARE(manager.model().group(group)->stickyTabs, 2);
    QVERIFY(manager.model().validate());
}

QTEST_MAIN(StickyTabsTest)
#include "sticky_tabs_test.moc"
