// SPDX-License-Identifier: MIT
#include "docking/tab_strip.hpp"
#include "style/tab_label_geometry.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>
#include <QToolTip>

using namespace window_system;

// Sticky tab sizing and placement from 8.15b: compact sticky tabs have a fixed
// width, shrunk ones clip their titles to a limit of their own, and in a single
// row both stay at the viewport's start while the other tabs scroll under them.
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

// The width of a tab without number or icon whose title fills `title`; every
// title here is wider than the limits used. A sticky tab also keeps room for
// its action, which always shows (tab strip design 5).
int shrunkWidth(int title = defaultShrinkStickyTitleWidth, bool sticky = false) { return tabBarTabWidth({0, 0, title}, sticky); }
int stickyShrunkWidth(int title = defaultShrinkStickyTitleWidth) { return shrunkWidth(title, true); }
// A tab's natural width with the sticky action's room added.
int withAction(int natural) { return natural - tabBarLeading + tabActionGap + tabActionSize + tabBarTrailing; }
}

class StickyTabsTest final : public QObject {
    Q_OBJECT
private slots:
    void sizedStickyTabsUseTheirWidths();
    void shrunkStickyTitlesUseTheirOwnLimit();
    void compactStickyTabsShowTheirIconOrInitialWithoutAPin();
    void sizedStickyTabsStayInViewWhileOthersScroll();
    void activatingAScrolledTabRevealsItPastTheStickyTabs();
    void draggingIntoAndOutOfASizedRunUsesEachSegmentsWidth();
    void separateRowHoldsStickyTabsAboveTheOthers();
    void collapsedRowsKeepTheStickyRowShown();
    void fullStickyRowsDoNotMakeTheStripScroll();
    void draggingBetweenRowsSticksAndUnsticks();
    void closeProtectionFollowsTheSetting_data();
    void closeProtectionFollowsTheSetting();
    void blockedCloseShortcutShowsAnotherGroupsTab();
    void tabListMarksStickyTabs();
    void tabActionTooltipsDescribeTheAction_data();
    void tabActionTooltipsDescribeTheAction();
    void tabListCanUnstickWithoutActivatingOrClosing_data();
    void tabListCanUnstickWithoutActivatingOrClosing();
    void tabListUnstickResolvesItsTargetAfterPopupCallbacks();
};

void StickyTabsTest::sizedStickyTabsUseTheirWidths()
{
    for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Compact, StickyTabSizing::Shrink}) {
        GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 4, 2, sized(sizing));
        tabs.resize(900, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        auto* bar = tabs.tabBar();
        const int expected = sizing == StickyTabSizing::Compact ? compactStickyTabWidth
            : sizing == StickyTabSizing::Shrink ? stickyShrunkWidth() : withAction(bar->naturalTabWidth(0));
        if (sizing == StickyTabSizing::Shrink) QVERIFY(expected < withAction(bar->naturalTabWidth(0)));
        QCOMPARE(bar->displayTabRect(0).width(), expected);
        QCOMPARE(bar->displayTabRect(1).width(), sizing == StickyTabSizing::Normal ? withAction(bar->naturalTabWidth(1)) : expected);
        QCOMPARE(bar->displayTabRect(1).left(), bar->displayTabRect(0).right() + 1);
        // The other tabs keep their natural width right after the sticky ones.
        QCOMPARE(bar->displayTabRect(2).width(), bar->naturalTabWidth(2));
        QCOMPARE(bar->displayTabRect(2).left(), bar->displayTabRect(1).right() + 1);
        // Unsticking a tab returns it to its natural width.
        tabs.setStickyCount(1);
        QCOMPARE(bar->displayTabRect(1).width(), bar->naturalTabWidth(1));
    }
}

void StickyTabsTest::shrunkStickyTitlesUseTheirOwnLimit()
{
    auto appearance = sized(StickyTabSizing::Shrink); appearance.shrinkStickyTitleWidth = 20;
    // The regular clipping is separate: a tighter limit there leaves the
    // shrunk sticky titles alone and still clips the other tabs.
    appearance.clipTitles = true; appearance.maximumTitleWidth = 10;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 4, 2, appearance);
    tabs.resize(900, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    QCOMPARE(bar->displayTabRect(0).width(), stickyShrunkWidth(20)); QCOMPARE(bar->displayTabRect(1).width(), stickyShrunkWidth(20));
    QCOMPARE(bar->frozenExtent(), 2 * stickyShrunkWidth(20));
    QCOMPARE(bar->displayTabRect(2).width(), shrunkWidth(10));
    // A carried tab among the sticky tabs is no wider than a shrunk tab.
    const auto gapAt = bar->mapToGlobal(QPoint(bar->displayTabRect(1).left() + 2, bar->displayTabRect(1).center().y()));
    QCOMPARE(tabs.insertionIndex(gapAt), 1);
    tabs.setInsertionPreview(1, {{QStringLiteral("A carried tab with a long title"), false}}, true);
    QVERIFY(tabs.insertionSticky());
    QCOMPARE(bar->dropPreviewRect().width(), stickyShrunkWidth(20));
    tabs.setInsertionPreview(-1, {});
    // A limit above the title width shows the whole title with no space after
    // it; the other tabs follow right after.
    appearance.shrinkStickyTitleWidth = 300; appearance.clipTitles = false; tabs.setAppearance(appearance, false);
    QCOMPARE(bar->displayTabRect(0).width(), withAction(bar->naturalTabWidth(0))); QVERIFY(bar->naturalTabWidth(0) < shrunkWidth(300));
    QCOMPARE(bar->displayTabRect(1).left(), bar->displayTabRect(0).right() + 1);
    QCOMPARE(bar->displayTabRect(2).left(), bar->displayTabRect(1).right() + 1);
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
    // A line ends the sticky run.
    QVERIFY(image.pixelColor(second.right(), second.center().y()).rgb() != stickyColor.rgb());
    bool inked = false;
    // Within the tab's straight sides, away from its rounded corners.
    for (int y = second.top() + bar->metrics().radius; y < second.bottom() - bar->metrics().radius; ++y)
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
        const int width = sizing == StickyTabSizing::Compact ? compactStickyTabWidth : stickyShrunkWidth();
        QCOMPARE(bar->frozenExtent(), 2 * width);
        tabs.scrollByWheel(QPoint(0, -600)); QTRY_VERIFY(tabs.scrollOffset() > 3 * width);
        // The sticky tabs stay at the viewport's start.
        const int start = bar->scrollViewport().left();
        QCOMPARE(bar->displayTabRect(0).left(), start);
        QCOMPARE(bar->displayTabRect(1).left(), start + width);
        // They cover the scrolled tabs; the next one is drawn after them.
        const auto image = render(bar); const int y = bar->displayTabRect(0).center().y();
        QCOMPARE(image.pixelColor(start + 3, y).rgb(), stickyColor.rgb());
        QCOMPARE(image.pixelColor(start + 2 * width - 3, y).rgb(), stickyColor.rgb());
        // Past the fade that marks scrolled-away tabs (tab strip design 3.1),
        // the other tabs show (their titles aside).
        bool plain = false;
        for (int x = start + 2 * width + bar->metrics().edgeFade; x < start + 2 * width + bar->metrics().edgeFade + 40; ++x)
            plain |= image.pixelColor(x, y).rgb() == plainColor.rgb();
        QVERIFY(plain);
        // A click over them selects the sticky tab, not the one scrolled beneath.
        const QPoint point(start + 10, y);
        sendBarMouse(bar, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        sendBarMouse(bar, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(tabs.currentIndex(), 0);
        // A drop over them goes among the sticky tabs, after them among the others.
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(start + 3, y))), 0);
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(start + 2 * width - 3, y))), 2);
        QVERIFY(tabs.insertionIndex(bar->mapToGlobal(QPoint(start + 2 * width + 3, y))) > 2);
    }
    // Normal sticky tabs scroll away, and in several rows nothing is kept in view.
    GroupTabs normal(defaultTheme(), nullptr); populate(normal, 20, 2, sized(StickyTabSizing::Normal));
    normal.resize(500, 200); normal.show(); QVERIFY(QTest::qWaitForWindowExposed(&normal));
    normal.scrollByWheel(QPoint(0, -600)); QTRY_VERIFY(normal.scrollOffset() > 0);
    QCOMPARE(normal.tabBar()->frozenExtent(), 0);
    QTRY_VERIFY(normal.tabBar()->displayTabRect(0).isEmpty());
    auto multiline = sized(StickyTabSizing::Compact); multiline.multiline = true; multiline.stickyMultiline = true;
    GroupTabs rows(defaultTheme(), nullptr); populate(rows, 20, 2, multiline);
    rows.resize(500, 300); rows.show(); QVERIFY(QTest::qWaitForWindowExposed(&rows));
    QCOMPARE(rows.tabBar()->frozenExtent(), 0);
    QCOMPARE(rows.tabBar()->displayTabRect(0).width(), compactStickyTabWidth);
}

void StickyTabsTest::activatingAScrolledTabRevealsItPastTheStickyTabs()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 20, 2, sized(StickyTabSizing::Shrink));
    tabs.resize(500, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const int runEnd = bar->scrollViewport().left() + bar->frozenExtent();
    tabs.setCurrentIndex(19); QTRY_VERIFY(tabs.scrollOffset() > 0);
    // A tab under the sticky tabs scrolls out from under them.
    int hidden = -1;
    for (int index = 2; index < bar->count(); ++index)
        if (bar->contentTabRect(index).left() - tabs.scrollOffset() < runEnd) hidden = index;
    QVERIFY(hidden >= 2);
    tabs.setCurrentIndex(hidden);
    QCOMPARE(bar->displayTabRect(hidden).left(), runEnd);
    QCOMPARE(bar->displayTabRect(hidden).width(), bar->contentTabRect(hidden).width());
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
        QCOMPARE(bar->dropPreviewRect().width(), sticky ? compactStickyTabWidth : natural);
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

void StickyTabsTest::separateRowHoldsStickyTabsAboveTheOthers()
{
    auto appearance = sized(StickyTabSizing::Normal); appearance.stickyTabsOnSeparateRow = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 12, 3, appearance);
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int row = bar->metrics().rowHeight;
    const int top = bar->displayTabRect(0).top();
    for (int index = 0; index < 3; ++index) QCOMPARE(bar->displayTabRect(index).top(), top);
    for (int index = 3; index < 12; ++index) if (!bar->displayTabRect(index).isEmpty()) QCOMPARE(bar->displayTabRect(index).top(), top + row);
    QCOMPARE(bar->displayTabRect(3).left(), bar->displayTabRect(0).left());
    // The list button ends the first row (tab strip design 2.1).
    auto* list = tabs.findChild<QWidget*>("TabListButton"); QVERIFY(list && list->isVisible());
    QCOMPARE(list->mapTo(bar, QPoint()).y(), top);
    // The other tabs scroll; the sticky row stays and covers none of them.
    QCOMPARE(bar->frozenExtent(), 0);
    tabs.scrollByWheel(QPoint(0, -600)); QTRY_VERIFY(tabs.scrollOffset() > 0);
    QCOMPARE(bar->displayTabRect(0).left(), bar->scrollViewport().left());
    QCOMPARE(bar->displayTabRect(1).left(), bar->displayTabRect(0).right() + 1);
    QTRY_VERIFY(bar->displayTabRect(3).isEmpty());
    // Each row is its own drop target; right after the sticky tabs is both.
    const auto sticky = [bar](int x) { return bar->insertionAt(QPoint(x, bar->displayTabRect(0).center().y())); };
    const auto other = [bar, row](int x) { return bar->insertionAt(QPoint(x, bar->displayTabRect(0).center().y() + row)); };
    const int end = bar->displayTabRect(2).right() + 4;
    QVERIFY(sticky(end).sticky); QCOMPARE(sticky(end).index, 3);
    QVERIFY(!other(bar->scrollViewport().left() + 2).sticky); QVERIFY(other(bar->scrollViewport().left() + 2).index >= 3);
    QCOMPARE(sticky(bar->scrollViewport().left() + 2).index, 0);
    // Without sticky tabs there is one row.
    const int twoRows = bar->height();
    tabs.setStickyCount(0);
    QCOMPARE(bar->height(), twoRows - row);
    // In one row each the sticky row scrolls on its own; with several rows
    // the sticky tabs wrap on rows of their own. Sized ones keep their width.
    GroupTabs narrow(defaultTheme(), nullptr); populate(narrow, 6, 3, appearance);
    narrow.resize(260, 200); narrow.show(); QVERIFY(QTest::qWaitForWindowExposed(&narrow));
    auto* rows = narrow.tabBar();
    QCOMPARE(rows->contentTabRect(2).top(), rows->contentTabRect(0).top());
    QCOMPARE(rows->contentTabRect(3).top(), rows->contentTabRect(0).top() + row);
    QVERIFY(rows->isOverflowing());
    const int regularOffset = narrow.scrollOffset();
    QVERIFY(rows->scrollAt(QPoint(rows->displayTabRect(0).center()), QPoint(0, -120)));
    QCOMPARE(narrow.scrollOffset(), regularOffset);
    QVERIFY(rows->displayTabRect(0).isEmpty() || rows->displayTabRect(0).left() < rows->contentTabRect(0).left());
    appearance.multiline = true; appearance.stickyMultiline = true; narrow.setAppearance(appearance);
    QVERIFY(rows->contentTabRect(2).top() > rows->contentTabRect(0).top());
    QVERIFY(rows->contentTabRect(3).top() > rows->contentTabRect(2).top());
    appearance.stickySizing = StickyTabSizing::Compact; narrow.setAppearance(appearance);
    QCOMPARE(rows->contentTabRect(2).top(), rows->contentTabRect(0).top()); QCOMPARE(rows->contentTabRect(2).width(), compactStickyTabWidth);
    QCOMPARE(rows->contentTabRect(3).top(), rows->contentTabRect(0).top() + row);
}

void StickyTabsTest::fullStickyRowsDoNotMakeTheStripScroll()
{
    // The sticky rows wrap within the viewport and the new-tab buttons follow
    // the few other tabs on the next row; nothing overflows, so nothing scrolls.
    auto appearance = sized(StickyTabSizing::Shrink); appearance.stickyTabsOnSeparateRow = true;
    appearance.showNewTabButton = true; appearance.multiline = true; appearance.stickyMultiline = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 14, 12, appearance);
    tabs.setAppearance(appearance);
    auto* bar = tabs.tabBar();
    tabs.resize(shrunkWidth() * 8 + 40, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    QVERIFY(bar->displayTabRect(11).top() > bar->displayTabRect(0).top());
    QCOMPARE(bar->displayTabRect(12).top(), bar->displayTabRect(11).top() + bar->metrics().rowHeight);
    auto* create = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(create && create->isVisible());
    QCOMPARE(create->mapTo(bar, QPoint()).y(), bar->displayTabRect(13).top());
    QVERIFY(!bar->isOverflowing());
    QCOMPARE(bar->displayTabRect(12).left(), bar->displayTabRect(0).left());
}

void StickyTabsTest::collapsedRowsKeepTheStickyRowShown()
{
    auto appearance = sized(StickyTabSizing::Normal); appearance.stickyTabsOnSeparateRow = true;
    appearance.multiline = true; appearance.stickyMultiline = true; appearance.showAllRows = false; appearance.stickyShowAllRows = false; appearance.rowExpansion = TabRowExpansion::Push; appearance.wheelExpandRows = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 20, 2, appearance);
    tabs.resize(500, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int row = bar->metrics().rowHeight;
    tabs.setCurrentIndex(17);
    // Collapsed: the sticky row and the active tab's row.
    const int top = bar->displayTabRect(0).top();
    QCOMPARE(bar->displayTabRect(17).top(), top + row);
    QVERIFY(bar->displayTabRect(2).isEmpty() || bar->displayTabRect(2).top() == top + row);
    QVERIFY(bar->hiddenTabs() > 0);
    // Activating a sticky tab keeps the other row it showed.
    tabs.setCurrentIndex(1);
    QCOMPARE(bar->displayTabRect(17).top(), top + row);
    // Expanded: every row, the sticky one first.
    tabs.scrollByWheel(QPoint(0, -120));
    QTRY_VERIFY(bar->expanded());
    QCOMPARE(bar->hiddenTabs(), 0);
    QCOMPARE(bar->displayTabRect(1).top(), top); QCOMPARE(bar->displayTabRect(2).top(), top + row);
}

void StickyTabsTest::draggingBetweenRowsSticksAndUnsticks()
{
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = sized(StickyTabSizing::Normal);
    appearance.showNewTabButton = false; appearance.stickyTabsOnSeparateRow = true;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto window = manager.createWindow(WindowKind::Workbench, {"Sticky rows", {900, 360}});
    QVector<TabId> ids;
    for (int index = 0; index < 5; ++index)
        ids.append(manager.addTab(window, new QLabel(QString("Content %1").arg(index)), {QString("Tab %1").arg(index), "label", {}}));
    QVERIFY(manager.setTabSticky(ids[0], true)); QVERIFY(manager.setTabSticky(ids[1], true)); // [0 1 | 2 3 4]
    const auto group = manager.model().groupForTab(ids[0]);
    manager.windowWidget(window)->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    // Drags a tab to the slot `targetIndex` on the sticky row or the other one.
    const auto drag = [&](TabId tab, int targetIndex, bool stickyRow) {
        auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
        auto* bar = view->tabBar(); const int from = manager.model().group(group)->tabs.indexOf(tab);
        const auto start = bar->mapToGlobal(bar->displayTabRect(from).center());
        sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
        sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
        auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
        const int y = bar->mapToGlobal(QPoint(0, bar->displayTabRect(0).center().y() + (stickyRow ? 0 : bar->metrics().rowHeight))).y();
        QPoint point;
        for (int x = view->viewportRectGlobal().left() + 1; x < view->viewportRectGlobal().right(); x += 2) {
            const QPoint candidate(x, y);
            sendMouse(receiver, QEvent::MouseMove, candidate);
            if (view->insertionIndex(candidate) == targetIndex && view->insertionSticky() == stickyRow) { point = candidate; break; }
        }
        QVERIFY(!point.isNull());
        sendMouse(receiver, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
        QTRY_VERIFY(!labelPreview());
        QCOMPARE(manager.isTabSticky(tab), stickyRow);
        QCOMPARE(manager.model().group(group)->tabs.indexOf(tab), targetIndex);
    };
    drag(ids[2], 2, true);  // end of the sticky row: [0 1 2 | 3 4]
    drag(ids[1], 2, false); // start of the other row: [0 2 | 1 3 4]
    drag(ids[4], 0, true);  // start of the sticky row: [4 0 2 | 1 3]
    QCOMPARE(manager.model().group(group)->stickyTabs, 3);
    QVERIFY(manager.model().validate());
}

void StickyTabsTest::closeProtectionFollowsTheSetting_data()
{
    QTest::addColumn<int>("guard");
    QTest::newRow("keyboardAndMouse") << int(StickyTabCloseGuard::KeyboardAndMouse);
    QTest::newRow("keyboard") << int(StickyTabCloseGuard::Keyboard);
    QTest::newRow("mouse") << int(StickyTabCloseGuard::Mouse);
    QTest::newRow("never") << int(StickyTabCloseGuard::Never);
}

void StickyTabsTest::closeProtectionFollowsTheSetting()
{
    QFETCH(int, guard);
    const auto protection = StickyTabCloseGuard(guard);
    const bool keyboard = protection == StickyTabCloseGuard::KeyboardAndMouse || protection == StickyTabCloseGuard::Keyboard;
    const bool mouse = protection == StickyTabCloseGuard::KeyboardAndMouse || protection == StickyTabCloseGuard::Mouse;
    const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
    WindowManager manager; auto appearance = manager.tabAppearance(); appearance.preventStickyClose = protection;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Close protection", {900, 400}});
    QVector<TabId> ids; QVector<QPointer<QPlainTextEdit>> editors;
    for (int index = 0; index < 5; ++index) {
        auto* editor = new QPlainTextEdit; editors << editor;
        ids << manager.addTab(window, editor, {QString("Tab %1").arg(index), "editor", {}});
    }
    for (const int index : {0, 1, 2}) QVERIFY(manager.setTabSticky(ids[index], true)); // [0 1 2 | 3 4]
    const auto group = manager.model().groupForTab(ids[0]);
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto press = [&](int index, Qt::Key key, Qt::KeyboardModifiers modifiers) {
        QVERIFY(manager.focusPane(ids[index], manager.model().tab(ids[index])->activePane));
        QTest::keyClick(editors[index], key, modifiers);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    // The close shortcut keeps a sticky tab and shows the first other tab.
    press(0, Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(bool(manager.model().tab(ids[0])), keyboard);
    if (keyboard) QCOMPARE(manager.model().group(group)->activeTab, ids[3]);
    // So does the close-pane shortcut, which would close the tab with its only pane.
    press(1, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(bool(manager.model().tab(ids[1])), keyboard);
    // A middle click on a sticky tab.
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    auto* bar = view->tabBar(); const auto point = bar->displayTabRect(manager.model().group(group)->tabs.indexOf(ids[2])).center();
    sendBarMouse(bar, QEvent::MouseButtonPress, point, Qt::MiddleButton, Qt::MiddleButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, point, Qt::MiddleButton, Qt::NoButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(bool(manager.model().tab(ids[2])), mouse);
    // Other tabs close by shortcut, and commands close sticky tabs too.
    press(3, Qt::Key_W, Qt::ControlModifier);
    QVERIFY(!manager.model().tab(ids[3]));
    for (const auto tab : {ids[0], ids[1], ids[2]}) if (manager.model().tab(tab)) {
        auto context = manager.context(window); context.group = group; context.tab = tab;
        context.pane = manager.model().tab(tab)->activePane;
        QVERIFY(manager.commands().execute("workspace.closeTab", context));
        QVERIFY(!manager.model().tab(tab));
    }
}

void StickyTabsTest::blockedCloseShortcutShowsAnotherGroupsTab()
{
    const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
    WindowManager manager;
    const auto window = manager.createWindow(WindowKind::Workbench, {"Close protection groups", {900, 400}});
    auto* editor = new QPlainTextEdit;
    const auto sticky = manager.addTab(window, editor, {"Sticky", "editor", {}});
    const auto other = manager.addTab(window, new QPlainTextEdit, {"Other", "editor", {}});
    QVERIFY(manager.setTabSticky(sticky, true));
    QVERIFY(manager.moveTab(other, manager.model().groupForTab(sticky), Placement::Right));
    QVERIFY(manager.model().groupForTab(other) != manager.model().groupForTab(sticky));
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.focusPane(sticky, manager.model().tab(sticky)->activePane));
    QCOMPARE(manager.context(window).tab, sticky);
    QTest::keyClick(editor, Qt::Key_W, Qt::ControlModifier);
    QVERIFY(manager.model().tab(sticky));
    QCOMPARE(manager.context(window).tab, other);
    // With no other tab at all, the shortcut does nothing.
    QVERIFY(manager.setTabSticky(other, true));
    QVERIFY(manager.focusPane(sticky, manager.model().tab(sticky)->activePane));
    QTest::keyClick(editor, Qt::Key_W, Qt::ControlModifier);
    QVERIFY(manager.model().tab(sticky)); QVERIFY(manager.model().tab(other));
    QCOMPARE(manager.context(window).tab, sticky);
}

void StickyTabsTest::tabListMarksStickyTabs()
{
    GroupTabs tabs(defaultTheme(), nullptr); const auto ids = populate(tabs, 5, 2, sized(StickyTabSizing::Compact));
    int closed = -1; tabs.closeRequested = [&closed](int index) { closed = index; };
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* list = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(list); list->click();
    auto* menu = tabs.findChild<QMenu*>(); QVERIFY(menu); QTRY_VERIFY(menu->isVisible());
    const auto rows = menu->findChildren<QWidget*>("TabListRow"); QCOMPARE(rows.size(), ids.size());
    for (auto* row : rows) {
        const int index = ids.indexOf(row->property("tabId").toUuid()); QVERIFY(index >= 0);
        auto* pin = row->findChild<QToolButton*>("TabListUnstickButton");
        QCOMPARE(bool(pin), index < 2);
        if (pin) {
            QVERIFY(pin->isVisible()); QVERIFY(!pin->icon().isNull());
            auto* close = row->findChild<QToolButton*>("TabListCloseButton"); QVERIFY(close);
            QCOMPARE(pin->size(), close->size()); QCOMPARE(pin->geometry().center().y(), close->geometry().center().y());
            // The actions sit together at the row's end (tab strip design §8.2).
            QCOMPARE(close->geometry().left() - pin->geometry().right() - 1, 2);
            QCOMPARE(row->width() - close->geometry().right() - 1, 4);
            QCOMPARE(pin->toolTip(), QCoreApplication::translate("WindowSystem", "Unstick tab"));
            QCOMPARE(pin->accessibleName(), pin->toolTip());
        }
    }
    // The separate close button keeps its original action on sticky rows.
    auto* first = rows.first(); QCOMPARE(first->property("tabId").toUuid(), ids.first());
    QEnterEvent enter(QPointF(8, 8), QPointF(8, 8), first->mapToGlobal(QPoint(8, 8)));
    QApplication::sendEvent(first, &enter);
    auto* close = first->findChild<QToolButton*>("TabListCloseButton"); QVERIFY(close && close->isVisible());
    close->click(); QCOMPARE(closed, 0);
}

void StickyTabsTest::tabActionTooltipsDescribeTheAction_data()
{
    QTest::addColumn<int>("rows"); QTest::addColumn<int>("sizing");
    for (int rows = 0; rows < 4; ++rows)
        for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Compact, StickyTabSizing::Shrink})
            QTest::newRow(qPrintable(QString("rows-%1-size-%2").arg(rows).arg(int(sizing)))) << rows << int(sizing);
}
void StickyTabsTest::tabActionTooltipsDescribeTheAction()
{
    QFETCH(int, rows); QFETCH(int, sizing);
    const auto hideTip = qScopeGuard([] { QToolTip::hideText(); });
    auto appearance = sized(StickyTabSizing(sizing));
    appearance.stickyTabsOnSeparateRow = rows == 1 || rows == 3;
    appearance.multiline = rows >= 2; appearance.wheelExpandRows = rows == 3;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 6, 2, appearance);
    tabs.setCurrentIndex(2); tabs.resize(600, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const auto showTip = [&](QPoint point) {
        sendBarMouse(bar, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
        QHelpEvent help(QEvent::ToolTip, point, bar->mapToGlobal(point));
        QApplication::sendEvent(bar, &help);
    };
    for (const int index : {0, 2}) {
        const auto label = bar->displayTabRect(index); QVERIFY(!label.isEmpty());
        const auto action = tabBarCloseRect(label, bar->layoutDirection());
        const auto title = tabBarLabelArea(label, bar->layoutDirection()).center();
        showTip(title); QCOMPARE(QToolTip::text(), bar->tabToolTip(index));
        showTip(action.center());
        const auto expected = index == 0 && StickyTabSizing(sizing) == StickyTabSizing::Compact ? bar->tabToolTip(index)
            : QCoreApplication::translate("WindowSystem", index == 0 ? "Unstick tab" : "Close tab");
        QCOMPARE(QToolTip::text(), expected);
        showTip(title); QCOMPARE(QToolTip::text(), bar->tabToolTip(index));
    }
    QCOMPARE(tabs.currentIndex(), 2);
}
void StickyTabsTest::tabListCanUnstickWithoutActivatingOrClosing_data()
{
    QTest::addColumn<bool>("active"); QTest::newRow("active") << true; QTest::newRow("inactive") << false;
}
void StickyTabsTest::tabListCanUnstickWithoutActivatingOrClosing()
{
    QFETCH(bool, active);
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new QLabel("Sticky content");
    const auto moving = manager.addTab(window, content, {"First", "label", {}});
    const auto other = manager.addTab(window, new QLabel("Other"), {"Second", "label", {}});
    const auto plain = manager.addTab(window, new QLabel("Plain"), {"Third", "label", {}});
    const auto pane = manager.model().tab(moving)->activePane;
    QVERIFY(manager.setTabSticky(moving, true)); QVERIFY(manager.setTabSticky(other, true));
    const auto selected = active ? moving : plain; QVERIFY(manager.activateTab(selected));
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto group = manager.model().groupForTab(moving);
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    auto* list = view->findChild<QToolButton*>("TabListButton"); QVERIFY(list); list->click();
    QPointer<QMenu> menu = view->findChild<QMenu*>(); QVERIFY(menu && menu->isVisible());
    QWidget* row = nullptr;
    for (auto* candidate : menu->findChildren<QWidget*>("TabListRow"))
        if (candidate->property("tabId").toUuid() == moving) row = candidate;
    QVERIFY(row);
    auto* pin = row->findChild<QToolButton*>("TabListUnstickButton"); QVERIFY(pin && pin->isVisible());
    QTest::mouseClick(pin, Qt::LeftButton);
    QVERIFY(!manager.isTabSticky(moving)); QVERIFY(manager.isTabSticky(other));
    QCOMPARE(manager.model().group(group)->activeTab, selected);
    QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{other, moving, plain}));
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(!menu || !menu->isVisible());
    view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    list = view->findChild<QToolButton*>("TabListButton"); QVERIFY(list); list->click();
    menu = view->findChild<QMenu*>(); QVERIFY(menu && menu->isVisible());
    for (auto* candidate : menu->findChildren<QWidget*>("TabListRow")) {
        const auto id = candidate->property("tabId").toUuid();
        QCOMPARE(bool(candidate->findChild<QToolButton*>("TabListUnstickButton")), id == other);
    }
    menu->close(); QVERIFY(manager.model().validate());
}

void StickyTabsTest::tabListUnstickResolvesItsTargetAfterPopupCallbacks()
{
    GroupTabs tabs(defaultTheme(), nullptr); const auto ids = populate(tabs, 5, 2, sized(StickyTabSizing::Normal));
    tabs.setCurrentIndex(2); tabs.resize(600, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    TabId unstuck; int closed = -1;
    tabs.unstickRequested = [&](int index) { unstuck = tabs.tabBar()->tabData(index).toUuid(); };
    tabs.closeRequested = [&](int index) { closed = index; };
    auto* list = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(list); list->click();
    auto* menu = tabs.findChild<QMenu*>(); QVERIFY(menu && menu->isVisible());
    QToolButton* pin = nullptr;
    for (auto* row : menu->findChildren<QWidget*>("TabListRow"))
        if (row->property("tabId").toUuid() == ids[1]) pin = row->findChild<QToolButton*>("TabListUnstickButton");
    QVERIFY(pin && pin->isVisible());
    QObject::connect(menu, &QMenu::aboutToHide, &tabs, [&] {
        // A host can synchronously remove a preceding tab when the popup
        // closes. The requested tab then moves from index one to zero.
        QVERIFY(tabs.takePage(ids[0])); tabs.setStickyCount(1);
    });
    QTest::mouseClick(pin, Qt::LeftButton);
    QCOMPARE(unstuck, ids[1]); QCOMPARE(closed, -1);
    QCOMPARE(tabs.tabBar()->tabData(tabs.currentIndex()).toUuid(), ids[2]);
}

QTEST_MAIN(StickyTabsTest)
#include "sticky_tabs_test.moc"
