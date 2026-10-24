// SPDX-License-Identifier: MIT
#include "docking/tab_strip.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
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
    void shrunkStickyTabsTakeTheChosenWidth();
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
};

void StickyTabsTest::sizedStickyTabsUseFixedWidths()
{
    for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Compact, StickyTabSizing::Shrink}) {
        GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 4, 2, sized(sizing));
        tabs.resize(900, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        auto* bar = tabs.tabBar();
        const int expected = sizing == StickyTabSizing::Compact ? compactStickyTabWidth
            : sizing == StickyTabSizing::Shrink ? defaultShrinkStickyTabWidth : bar->naturalTabWidth(0);
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

void StickyTabsTest::shrunkStickyTabsTakeTheChosenWidth()
{
    auto appearance = sized(StickyTabSizing::Shrink); appearance.shrinkStickyTabWidth = 140;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 4, 2, appearance);
    tabs.resize(900, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    QCOMPARE(bar->displayTabRect(0).width(), 140); QCOMPARE(bar->displayTabRect(1).width(), 140);
    QCOMPARE(bar->stickyRunExtent(), 280);
    // A preview among the sticky tabs takes the same width.
    tabs.setInsertionPreview(1, 200, true);
    QCOMPARE(bar->insertionPreviewRect().width(), 140);
    tabs.setInsertionPreview(-1, 0);
    // Changing the width resizes the shown tabs at once, wider than the title too.
    appearance.shrinkStickyTabWidth = 300; tabs.setAppearance(appearance, false);
    QCOMPARE(bar->displayTabRect(0).width(), 300); QCOMPARE(bar->displayTabRect(1).left(), 300);
    QVERIFY(bar->naturalTabWidth(0) < 300);
    QCOMPARE(bar->displayTabRect(2).left(), 600); QCOMPARE(bar->displayTabRect(2).width(), bar->naturalTabWidth(2));
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
        const int width = sizing == StickyTabSizing::Compact ? compactStickyTabWidth : defaultShrinkStickyTabWidth;
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

void StickyTabsTest::separateRowHoldsStickyTabsAboveTheOthers()
{
    auto appearance = sized(StickyTabSizing::Normal); appearance.stickyTabsOnSeparateRow = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 12, 3, appearance);
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int line = bar->lineHeight();
    for (int index = 0; index < 3; ++index) QCOMPARE(bar->displayTabRect(index).top(), 0);
    for (int index = 3; index < 12; ++index) QCOMPARE(bar->displayTabRect(index).top(), line);
    QCOMPARE(bar->displayTabRect(3).left(), 0); QCOMPARE(bar->height(), 2 * line);
    // The list button sits beside the other tabs' row.
    auto* list = tabs.findChild<QWidget*>("TabListButton"); auto* header = tabs.findChild<QWidget*>("TabGroupHeader");
    QVERIFY(list && header);
    QCOMPARE(list->mapTo(header, QPoint()).y() - bar->mapTo(header, QPoint()).y(), line);
    // The other tabs scroll; the sticky row stays and covers none of them.
    QCOMPARE(bar->stickyRunExtent(), 0);
    tabs.scrollByWheel(QPoint(0, -600)); QTRY_VERIFY(tabs.scrollOffset() > 0);
    QCOMPARE(bar->displayTabRect(0).left() - tabs.scrollOffset(), 0);
    QCOMPARE(bar->displayTabRect(1).left() - tabs.scrollOffset(), bar->naturalTabWidth(0));
    QVERIFY(bar->displayTabRect(3).left() < tabs.scrollOffset());
    // Each row is its own drop target; right after the sticky tabs is both.
    const auto global = [bar](int x, int row) { return bar->mapToGlobal(QPoint(x, row * bar->lineHeight() + bar->lineHeight() / 2)); };
    const int end = bar->displayTabRect(2).right() + 4;
    QCOMPARE(tabs.insertionRow(global(end, 0)), std::optional<bool>(true));
    QCOMPARE(tabs.insertionIndex(global(end, 0)), 3);
    QCOMPARE(tabs.insertionRow(global(tabs.scrollOffset() + 2, 1)), std::optional<bool>(false));
    QVERIFY(tabs.insertionIndex(global(tabs.scrollOffset() + 2, 1)) >= 3);
    QCOMPARE(tabs.insertionIndex(global(tabs.scrollOffset() + 2, 0)), 0);
    // Without sticky tabs there is one row.
    tabs.setStickyCount(0);
    QCOMPARE(bar->height(), line); QCOMPARE(tabs.insertionRow(global(4, 0)), std::nullopt);
    // Sticky tabs wrap on their own rows; sized ones keep their width.
    GroupTabs narrow(defaultTheme(), nullptr); populate(narrow, 6, 3, appearance);
    narrow.resize(260, 200); narrow.show(); QVERIFY(QTest::qWaitForWindowExposed(&narrow));
    auto* rows = narrow.tabBar();
    QVERIFY(rows->displayTabRect(2).top() > 0);
    QCOMPARE(rows->displayTabRect(3).top(), rows->displayTabRect(2).top() + rows->lineHeight());
    appearance.stickySizing = StickyTabSizing::Compact; narrow.setAppearance(appearance);
    QCOMPARE(rows->displayTabRect(2).top(), 0); QCOMPARE(rows->displayTabRect(2).width(), compactStickyTabWidth);
    QCOMPARE(rows->displayTabRect(3).top(), rows->lineHeight());
}

void StickyTabsTest::fullStickyRowsDoNotMakeTheStripScroll()
{
    // The sticky rows wrap within the viewport and the new-tab buttons follow
    // the few other tabs on the next row; nothing overflows, so nothing scrolls.
    auto appearance = sized(StickyTabSizing::Shrink); appearance.stickyTabsOnSeparateRow = true;
    appearance.showNewTabButton = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 14, 12, appearance);
    tabs.setAppearance(appearance);
    auto* bar = tabs.tabBar();
    // Fill the sticky rows as far as possible: the last full row ends close to the viewport edge.
    tabs.resize(defaultShrinkStickyTabWidth * 8 + 20, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    QVERIFY(bar->displayTabRect(11).top() > 0); QCOMPARE(bar->displayTabRect(12).top(), bar->displayTabRect(11).top() + bar->lineHeight());
    auto* create = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(create && create->isVisible());
    QCOMPARE(create->y(), bar->displayTabRect(12).top());
    auto* scrollbar = tabs.findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar);
    QTRY_COMPARE(scrollbar->maximum(), 0); QCOMPARE(tabs.scrollOffset(), 0);
    QCOMPARE(bar->displayTabRect(12).left(), 0);
}

void StickyTabsTest::collapsedRowsKeepTheStickyRowShown()
{
    auto appearance = sized(StickyTabSizing::Normal); appearance.stickyTabsOnSeparateRow = true;
    appearance.multiline = true; appearance.wheelExpandRows = true;
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 20, 2, appearance);
    tabs.resize(500, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int line = bar->lineHeight();
    tabs.setCurrentIndex(17);
    // Collapsed: the sticky row and the active tab's row.
    QCOMPARE(bar->height(), 2 * line);
    QCOMPARE(bar->displayTabRect(0).top(), 0); QCOMPARE(bar->displayTabRect(17).top(), line);
    QVERIFY(bar->displayTabRect(2).isEmpty());
    // Activating a sticky tab keeps the other row it showed.
    tabs.setCurrentIndex(1);
    QCOMPARE(bar->displayTabRect(17).top(), line); QCOMPARE(bar->height(), 2 * line);
    // Expanded: every row, the sticky one first.
    tabs.scrollByWheel(QPoint(0, -120));
    QVERIFY(bar->rowCount() > 3); QCOMPARE(bar->height(), bar->rowCount() * line);
    QCOMPARE(bar->displayTabRect(1).top(), 0); QCOMPARE(bar->displayTabRect(2).top(), line);
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
        const int y = bar->mapToGlobal(QPoint(0, (stickyRow ? 0 : bar->lineHeight()) + bar->lineHeight() / 2)).y();
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
        auto* marker = row->findChild<QLabel*>("TabListStickyMarker");
        QCOMPARE(bool(marker), index < 2);
        if (marker) { QVERIFY(marker->isVisible()); QVERIFY(!marker->pixmap().isNull()); }
    }
    // The marker is not a control: the row's close button still closes.
    auto* first = rows.first(); QCOMPARE(first->property("tabId").toUuid(), ids.first());
    QEnterEvent enter(QPointF(8, 8), QPointF(8, 8), first->mapToGlobal(QPoint(8, 8)));
    QApplication::sendEvent(first, &enter);
    auto* close = first->findChild<QToolButton*>("TabListCloseButton"); QVERIFY(close && close->isVisible());
    close->click(); QCOMPARE(closed, 0);
}

QTEST_MAIN(StickyTabsTest)
#include "sticky_tabs_test.moc"
