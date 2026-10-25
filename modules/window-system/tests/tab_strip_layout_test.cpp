// SPDX-License-Identifier: MIT
// The tab strip widget (docs/design/tab-strip-design.md §2–§5, §8, §10, §12):
// how settings, input and painting reach the real TabBar and GroupTabs. The
// geometry itself is covered at the logic layer by tab-layout; here each
// wiring is checked with a few representative states.
//
// State dimensions:
//   placement      top, bottom, left, right; central and pinned independent
//   rows           single, multiple; collapse off, push, overlay
//   sticky sizing  normal, shrink, compact (sticky-tabs covers the details)
//   folders        none, expanded, collapsed with or without the current tab
//   input          click, middle click, hover, press, wheel, context menu, drag
#include "docking/tab_strip.hpp"
#include "commands/popup_menu.hpp"
#include "docking/dock_surface.hpp"
#include "docking/edge_tab_button.hpp"
#include "style/tab_label_geometry.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QAccessible>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QStackedWidget>
#include <QTest>
#include <QToolButton>
#include <QToolTip>

using namespace window_system;

namespace {
QVector<TabId> populate(GroupTabs& tabs, int count, bool withIcon = false)
{
    QVector<TabId> ids;
    QPixmap icon(16, 16); icon.fill(Qt::white);
    for (int index = 0; index < count; ++index) {
        const auto id = QUuid::createUuid(); ids.append(id);
        const auto title = QStringLiteral("MMMM tab %1").arg(index + 1);
        tabs.addTab(new QLabel(title), withIcon ? QIcon(icon) : QIcon{}, title, id);
        TabRecord record; record.id = id; record.title = title;
        tabs.setTabMetadata(index, record);
    }
    applyTheme(&tabs); return ids;
}
TabColorPreset solidPreset(const QString& id, QColor background = Qt::white)
{
    auto preset = defaultTabColorPresets().front(); preset.id = id;
    preset.groupActive = preset.groupInactive = preset.autoHideActive = preset.autoHideInactive
        = {background, Qt::black, QColor("#123456"), QColor("#2468ac")};
    return preset;
}
QImage render(QWidget* widget)
{
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); widget->render(&painter); painter.end(); return image;
}
QColor sample(QWidget* widget, QPoint point) { return render(widget).pixelColor(point); }
bool containsColor(const QImage& image, QColor color)
{
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y).rgb() == color.rgb()) return true;
    return false;
}
// The distinct rows that show tabs, top to bottom.
QVector<int> shownRows(const TabBar* bar)
{
    QVector<int> rows;
    for (int index = 0; index < bar->count(); ++index) {
        const auto rect = bar->displayTabRect(index);
        if (!rect.isEmpty() && !rows.contains(rect.top())) rows.append(rect.top());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}
QRect geometryIn(QWidget* widget, QWidget* target) { return QRect(widget->mapTo(target, QPoint()), widget->size()); }
GroupTabs* groupView(QWidget* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
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
}

class TabStripLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); TabBar::setAnimationsEnabled(false); }
    void stripSizesFollowTheDesign();
    void paintingAndHitTestingAgree_data();
    void paintingAndHitTestingAgree();
    void placementPutsTheStripOnItsSide_data();
    void placementPutsTheStripOnItsSide();
    void multilineWrapsAndKeepsEveryTab();
    void endControlsEndTheFirstRow_data();
    void endControlsEndTheFirstRow();
    void collapsedRowsShowACountAndExpand_data();
    void collapsedRowsShowACountAndExpand();
    void wheelExpandsAndCollapsesRows();
    void wheelScrollsAOneRowStrip();
    void wheelExpandedRowsSurviveRebuildsUntilCollapsed();
    void dragOverACollapsedStripShowsEveryRow();
    void displayFlagsChangeActualLabelWidthsAndCreationControls();
    void clippedTitlesKeepTheirFullText();
    void tabColorsFillTheBackgroundOnly();
    void closeTargetShowsHoverAndPressFeedback();
    void stickyTabsAlwaysShowTheirAction();
    void titleWidthChangesKeepTheActiveTabInPlace();
    void tooltipsNameTheDisplayedTab();
    void middleClickClosesTheTabUnderThePointer();
    void middleClickClosesManagedStripAndRailTabs();
    void newTabButtonsShareTheRowAndOpenTheTypeMenu();
    void insertionFollowsThePointerRow();
    void multilineDragReordersAndTearsOffOnlyPastEveryRow_data();
    void multilineDragReordersAndTearsOffOnlyPastEveryRow();
    void verticalStripResizesAndFoldsToIcons();
    void folderHeadersToggleOpenMenusAndCarry_data();
    void folderHeadersToggleOpenMenusAndCarry();
    void accessibilityListsTabsAndFolders();
    void groupNamesEndTheStripUnlessTurnedOff();
};

void TabStripLayoutTest::stripSizesFollowTheDesign()
{
    // The one place the design's numbers are checked (tab strip design 2.1).
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 3);
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const auto& metrics = bar->metrics();
    QCOMPARE(metrics.rowHeight, 28); QCOMPARE(metrics.outerMargin, 3); QCOMPARE(metrics.scrollbar, 3);
    QCOMPARE(metrics.tile, QSize(44, 32));
    for (int index = 0; index < 3; ++index) QCOMPARE(bar->displayTabRect(index).height(), 28);
    QCOMPARE(bar->displayTabRect(0).top(), metrics.outerMargin);
    // Rows touch: no gap between tabs of one row.
    QCOMPARE(bar->displayTabRect(1).left(), bar->displayTabRect(0).right() + 1);
    // Symmetric padding around the label (tab strip design 2.1).
    QCOMPARE(tabBarLeading, 10);
    // A large font makes rows taller instead of clipping text.
    auto large = tabs.font(); large.setPixelSize(30); tabs.tabBar()->setFont(large);
    QTRY_VERIFY(bar->displayTabRect(0).height() >= QFontMetrics(large).height() + 10);
}

void TabStripLayoutTest::paintingAndHitTestingAgree_data()
{
    QTest::addColumn<int>("placement"); QTest::addColumn<bool>("multiline"); QTest::addColumn<int>("sizing");
    for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right})
        for (const bool multiline : {false, true})
            for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Compact}) {
                if (multiline && (placement == TabBarPlacement::Left || placement == TabBarPlacement::Right)) continue;
                QTest::newRow(qPrintable(QStringLiteral("p%1-rows%2-sizing%3").arg(int(placement)).arg(multiline).arg(int(sizing))))
                    << int(placement) << multiline << int(sizing);
            }
}
void TabStripLayoutTest::paintingAndHitTestingAgree()
{
    QFETCH(int, placement); QFETCH(bool, multiline); QFETCH(int, sizing);
    TabAppearance appearance; appearance.centralPlacement = TabBarPlacement(placement); appearance.multiline = multiline;
    appearance.stickySizing = StickyTabSizing(sizing);
    GroupTabs tabs(defaultTheme(), nullptr); tabs.setAppearance(appearance); populate(tabs, 14, true);
    tabs.setStickyCount(2);
    tabs.tabBar()->setFolders({{QUuid::createUuid(), "Folder", {}, false, 4, 3}});
    tabs.resize(560, 420); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    QVector<QRect> shown;
    for (int index = 0; index < bar->count(); ++index) {
        const auto rect = bar->displayTabRect(index);
        if (rect.isEmpty()) continue;
        QVERIFY(bar->rect().contains(rect));
        QCOMPARE(bar->tabAt(rect.center()), index);
        for (const auto& other : shown) QVERIFY(!other.intersects(rect));
        shown.append(rect);
        // A click selects what is drawn there.
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(tabs.currentIndex(), index);
    }
    QVERIFY(shown.size() >= 4);
    const auto header = bar->folderHeaderRect(0);
    if (!header.isEmpty()) {
        QCOMPARE(bar->folderAt(header.center()), 0); QCOMPARE(bar->tabAt(header.center()), -1);
        for (const auto& rect : shown) QVERIFY(!rect.intersects(header));
    }
}

void TabStripLayoutTest::placementPutsTheStripOnItsSide_data()
{
    QTest::addColumn<int>("central"); QTest::addColumn<int>("pinned");
    QTest::newRow("bottom/top") << int(TabBarPlacement::Bottom) << int(TabBarPlacement::Top);
    QTest::newRow("top/bottom") << int(TabBarPlacement::Top) << int(TabBarPlacement::Bottom);
    QTest::newRow("left/right") << int(TabBarPlacement::Left) << int(TabBarPlacement::Right);
    QTest::newRow("right/left") << int(TabBarPlacement::Right) << int(TabBarPlacement::Left);
}
void TabStripLayoutTest::placementPutsTheStripOnItsSide()
{
    QFETCH(int, central); QFETCH(int, pinned);
    TabAppearance appearance; appearance.centralPlacement = TabBarPlacement(central); appearance.pinnedPlacement = TabBarPlacement(pinned);
    GroupTabs centralGroup(defaultTheme(), nullptr), pinnedGroup(defaultTheme(), nullptr);
    centralGroup.setAppearance(appearance); pinnedGroup.setAppearance(appearance, true);
    populate(centralGroup, 2); populate(pinnedGroup, 2);
    for (auto* group : {&centralGroup, &pinnedGroup}) { group->resize(500, 300); group->show(); QVERIFY(QTest::qWaitForWindowExposed(group)); }
    const auto check = [](GroupTabs& group, TabBarPlacement placement) {
        const auto strip = group.tabBar()->geometry();
        const auto content = group.findChild<QStackedWidget*>()->geometry();
        QVERIFY(!strip.intersects(content));
        switch (placement) {
        case TabBarPlacement::Top: QVERIFY(strip.bottom() < content.top()); break;
        case TabBarPlacement::Bottom: QVERIFY(strip.top() > content.bottom()); break;
        case TabBarPlacement::Left: QVERIFY(strip.right() < content.left()); break;
        case TabBarPlacement::Right: QVERIFY(strip.left() > content.right()); break;
        }
        QCOMPARE(group.tabBar()->placement(), placement);
    };
    check(centralGroup, TabBarPlacement(central)); check(pinnedGroup, TabBarPlacement(pinned));
    QCOMPARE(centralGroup.count(), 2); QCOMPARE(pinnedGroup.count(), 2);
}

void TabStripLayoutTest::multilineWrapsAndKeepsEveryTab()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    TabAppearance appearance; appearance.multiline = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 9);
    tabs.resize(410, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    QTRY_VERIFY(shownRows(bar).size() > 1);
    QVERIFY(!bar->isOverflowing());
    for (int index = 0; index < bar->count(); ++index) {
        const auto rect = bar->displayTabRect(index); QVERIFY(!rect.isEmpty());
        QVERIFY(bar->rect().contains(rect));
        QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
        for (int other = index + 1; other < bar->count(); ++other) QVERIFY(!rect.intersects(bar->displayTabRect(other)));
    }
    // Rows touch, so colored tabs join across rows.
    const auto rows = shownRows(bar);
    for (int row = 1; row < rows.size(); ++row) QCOMPARE(rows[row], rows[row - 1] + bar->metrics().rowHeight);
    const auto last = bar->displayTabRect(bar->count() - 1);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, last.center());
    QCOMPARE(tabs.currentIndex(), bar->count() - 1);
    QCOMPARE(tabs.findChild<QStackedWidget*>()->currentIndex(), bar->count() - 1);
}

void TabStripLayoutTest::endControlsEndTheFirstRow_data()
{
    QTest::addColumn<bool>("pinned"); QTest::addColumn<bool>("newTab"); QTest::addColumn<bool>("multiline");
    for (const bool pinned : {false, true}) for (const bool newTab : {false, true}) for (const bool multiline : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("pinned%1-new%2-rows%3").arg(pinned).arg(newTab).arg(multiline))) << pinned << newTab << multiline;
}
void TabStripLayoutTest::endControlsEndTheFirstRow()
{
    // The list and sidebar actions end the first row, aligned to its end;
    // only the new-tab controls follow the last tab (tab strip design 3.5).
    QFETCH(bool, pinned); QFETCH(bool, newTab); QFETCH(bool, multiline);
    GroupTabs tabs(defaultTheme(), nullptr);
    TabAppearance appearance; appearance.multiline = multiline; appearance.showNewTabButton = newTab;
    tabs.setAppearance(appearance, pinned); populate(tabs, 12);
    if (pinned) tabs.setPinnedActions([] {}, [] {});
    tabs.resize(520, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    auto* end = tabs.findChild<QWidget*>("TabHeaderActions"); QVERIFY(end && end->isVisible());
    const auto endRect = geometryIn(end, bar);
    QCOMPARE(endRect.right(), bar->width() - 1);
    QCOMPARE(endRect.top(), shownRows(bar).first());
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).intersects(endRect));
    auto* create = tabs.findChild<QWidget*>("TabCreateActions"); QVERIFY(create);
    QCOMPARE(create->isVisible(), newTab);
    if (!newTab) return;
    const auto createRect = geometryIn(create, bar);
    QVERIFY(!createRect.intersects(endRect));
    // Scrolled to the end, the new-tab controls follow the last tab.
    bar->setScrollOffset(bar->maximumScrollOffset());
    const auto last = bar->displayTabRect(bar->count() - 1);
    QCOMPARE(geometryIn(create, bar).left(), last.right() + 1);
    QCOMPARE(geometryIn(create, bar).top(), last.top());
}

void TabStripLayoutTest::collapsedRowsShowACountAndExpand_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("push") << int(TabRowCollapse::Push);
    QTest::newRow("overlay") << int(TabRowCollapse::Overlay);
}
void TabStripLayoutTest::collapsedRowsShowACountAndExpand()
{
    QFETCH(int, mode);
    GroupTabs tabs(defaultTheme(), nullptr);
    TabAppearance appearance; appearance.multiline = true; appearance.collapseRows = TabRowCollapse(mode);
    tabs.setAppearance(appearance); populate(tabs, 20);
    tabs.resize(500, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); auto* stack = tabs.findChild<QStackedWidget*>();
    tabs.setCurrentIndex(15);
    // Collapsed: one row, the current tab's, and a count of the rest.
    QCOMPARE(shownRows(bar).size(), 1);
    QVERIFY(!bar->displayTabRect(15).isEmpty());
    auto* count = bar->findChild<QToolButton*>("TabRowCountButton"); QVERIFY(count && count->isVisible());
    QVERIFY(bar->hiddenTabs() > 0);
    QCOMPARE(count->text(), QStringLiteral("+%1").arg(bar->hiddenTabs()));
    QVERIFY(count->toolTip().contains(QString::number(bar->hiddenTabs())));
    const auto contentBefore = stack->geometry();
    count->click();
    QTRY_VERIFY(bar->expanded());
    QCOMPARE(bar->hiddenTabs(), 0);
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).isEmpty() || bar->isOverflowing());
    if (TabRowCollapse(mode) == TabRowCollapse::Push) {
        // Push: the content gives way.
        QTRY_VERIFY(stack->geometry().top() > contentBefore.top());
        // Selecting a tab keeps it open; the count collapses it.
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->displayTabRect(1).center());
        QVERIFY(bar->expanded());
        count->click();
    } else {
        // Overlay: the content keeps its size; the rows cover it.
        QCOMPARE(stack->geometry(), contentBefore);
        QVERIFY(bar->geometry().bottom() > contentBefore.top());
        // A press elsewhere closes it.
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, stack->height() - 5), stack->mapToGlobal(QPointF(5, stack->height() - 5)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(stack, &press);
    }
    QTRY_VERIFY(!bar->expanded());
    QCOMPARE(stack->geometry(), contentBefore);
    QCOMPARE(shownRows(bar).size(), 1);
}

void TabStripLayoutTest::wheelExpandsAndCollapsesRows()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    TabAppearance appearance; appearance.multiline = true; appearance.collapseRows = TabRowCollapse::Push;
    appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); populate(tabs, 12); tabs.resize(410, 300); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    QCOMPARE(shownRows(bar).size(), 1);
    // Down expands, up collapses (tab strip design 3.4).
    tabs.scrollByWheel({0, -120});
    QTRY_VERIFY(bar->expanded());
    QVERIFY(shownRows(bar).size() > 1);
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).isEmpty());
    const int later = bar->count() - 1;
    tabs.setCurrentIndex(later);
    tabs.scrollByWheel({0, 120});
    QTRY_VERIFY(!bar->expanded());
    // Collapsed again to the current tab's row.
    QCOMPARE(shownRows(bar).size(), 1);
    QVERIFY(!bar->displayTabRect(later).isEmpty());
    QCOMPARE(tabs.currentIndex(), later);
    // Without the setting the wheel does nothing to the rows.
    appearance.wheelExpandRows = false; tabs.setAppearance(appearance);
    tabs.scrollByWheel({0, -120});
    QVERIFY(!bar->expanded());
}

void TabStripLayoutTest::wheelScrollsAOneRowStrip()
{
    TabAppearance appearance; appearance.showNewTabButton = false; appearance.wheelExpandRows = true;
    GroupTabs tabs(defaultTheme(), nullptr); tabs.setAppearance(appearance); populate(tabs, 12);
    tabs.resize(400, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    QCOMPARE(shownRows(tabs.tabBar()).size(), 1);
    tabs.scrollByWheel({0, -120});
    QTRY_VERIFY(tabs.scrollOffset() > 0);
    tabs.scrollByWheel({0, 120});
    QTRY_COMPARE(tabs.scrollOffset(), 0);
}

void TabStripLayoutTest::wheelExpandedRowsSurviveRebuildsUntilCollapsed()
{
    // A pushed expansion stays until it is collapsed, even when changes
    // rebuild the strip (tab strip design 3.4).
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.collapseRows = TabRowCollapse::Push; appearance.wheelExpandRows = true;
    appearance.showNewTabButton = false;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Rows reconstruction", {480, 340}});
    QVector<TabId> ids; QVector<PaneId> panes; QVector<QPointer<QWidget>> contents;
    for (int index = 0; index < 9; ++index) {
        QPointer<QWidget> content = new QLabel(QString("Content %1").arg(index)); contents.append(content);
        const auto tab = manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}); ids.append(tab);
        panes.append(manager.model().tab(tab)->activePane);
    }
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto group = manager.model().groupForTab(ids.first());
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QVERIFY(view->tabBar()->hiddenTabs() > 0);
    view->scrollByWheel({0, -120}); QTRY_VERIFY(view->tabBar()->expanded());
    const auto created = manager.addTab(window, new QLabel("Created while expanded"), {"MMMM new tab", "label", {}}, group);
    QVERIFY(!created.isNull()); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QTRY_VERIFY(view->tabBar()->expanded());
    QCOMPARE(view->count(), 10); QCOMPARE(view->tabBar()->hiddenTabs(), 0);
    QVERIFY(manager.setTabIconVisible(ids.first(), false)); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QVERIFY(view->tabBar()->expanded());
    for (int index = 0; index < ids.size(); ++index) {
        QCOMPARE(manager.model().tab(ids[index])->activePane, panes[index]);
        QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
    }
    view->scrollByWheel({0, 120}); QTRY_VERIFY(!view->tabBar()->expanded());
    const auto collapsedTab = manager.addTab(window, new QLabel("Created while collapsed"), {"MMMM final tab", "label", {}}, group);
    QVERIFY(!collapsedTab.isNull()); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QVERIFY(!view->tabBar()->expanded()); QVERIFY(view->tabBar()->hiddenTabs() > 0);
    QCOMPARE(view->count(), 11); QVERIFY(manager.model().validate());
}

void TabStripLayoutTest::dragOverACollapsedStripShowsEveryRow()
{
    // Every row takes drops while a drag is over a collapsed strip; it
    // returns afterwards (tab strip design 3.4).
    GroupTabs tabs(defaultTheme(), nullptr);
    TabAppearance appearance; appearance.multiline = true; appearance.collapseRows = TabRowCollapse::Push;
    tabs.setAppearance(appearance); populate(tabs, 20);
    tabs.resize(500, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); auto* stack = tabs.findChild<QStackedWidget*>();
    const auto content = stack->geometry();
    QVERIFY(bar->hiddenTabs() > 0);
    bar->setDragExpanded(true);
    QCOMPARE(bar->hiddenTabs(), 0); QVERIFY(bar->overlayActive());
    QCOMPARE(stack->geometry(), content);
    bar->setDragExpanded(false);
    QVERIFY(bar->hiddenTabs() > 0); QVERIFY(!bar->overlayActive());
}

void TabStripLayoutTest::displayFlagsChangeActualLabelWidthsAndCreationControls()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 2, true);
    tabs.resize(640, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int original = bar->displayTabRect(0).width();
    // Each label part takes its size and one part gap.
    TabAppearance appearance; appearance.showIcons = false; tabs.setAppearance(appearance);
    QTRY_COMPARE(bar->displayTabRect(0).width(), original - bar->iconSize().width() - tabPartGap);
    const int noIcon = bar->displayTabRect(0).width(); appearance.showNumbers = true; tabs.setAppearance(appearance);
    QTRY_COMPARE(bar->displayTabRect(0).width(), noIcon + bar->fontMetrics().horizontalAdvance("1") + tabPartGap);
    auto* controls = tabs.findChild<QWidget*>("TabCreateActions"); QVERIFY(controls);
    QVERIFY(controls->isVisible()); appearance.showNewTabButton = false; tabs.setAppearance(appearance);
    QVERIFY(!controls->isVisible());
    appearance.showNewTabButton = true; tabs.setAppearance(appearance); QVERIFY(controls->isVisible());
}

void TabStripLayoutTest::clippedTitlesKeepTheirFullText()
{
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs);
    const QString title = QStringLiteral("A rather long title that will not fit");
    tabs.addTab(new QLabel("Content"), {}, title, QUuid::createUuid());
    tabs.resize(800, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const int natural = bar->naturalTabWidth(0);
    TabAppearance appearance; appearance.clipTitles = true; appearance.maximumTitleWidth = 60; appearance.showIcons = false;
    tabs.setAppearance(appearance);
    QCOMPARE(bar->displayTabRect(0).width(), tabBarTabWidth({0, 0, 60}));
    QVERIFY(bar->displayTabRect(0).width() < natural);
    // The text stays whole for the tooltip, the list and screen readers.
    QCOMPARE(bar->tabText(0), title);
    auto* accessible = QAccessible::queryAccessibleInterface(bar); QVERIFY(accessible);
    QCOMPARE(accessible->child(0)->text(QAccessible::Name), title);
    appearance.clipTitles = false; tabs.setAppearance(appearance);
    QCOMPARE(bar->displayTabRect(0).width(), bar->naturalTabWidth(0));
}

void TabStripLayoutTest::tabColorsFillTheBackgroundOnly()
{
    // A tab's color is its background; text and icon use the preset's
    // colors, and no accent line is drawn (tab strip design 2.1, 9).
    GroupTabs tabs(defaultTheme(), nullptr); const auto ids = populate(tabs, 2, true);
    TabAppearance appearance;
    auto preset = appearance.colors.front(); preset.id = "test";
    preset.groupActive = {QColor("#d4e5f6"), QColor("#172839"), QColor("#112233"), QColor("#cc55aa")};
    preset.groupInactive = {QColor("#f5dfc9"), QColor("#334455"), QColor("#556677"), QColor("#887766")};
    appearance.colors = {preset}; tabs.setAppearance(appearance);
    for (int index = 0; index < ids.size(); ++index) { TabRecord record; record.id = ids[index]; record.colorId = "test"; tabs.setTabMetadata(index, record); }
    tabs.setCurrentIndex(0); tabs.resize(640, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    for (int index = 0; index < 2; ++index) {
        const auto rect = bar->displayTabRect(index); const auto colors = index == 0 ? preset.groupActive : preset.groupInactive;
        QCOMPARE(sample(bar, {rect.left() + 8, rect.center().y()}).rgb(), colors.background.rgb());
        QCOMPARE(sample(bar, {rect.left() + 8, rect.bottom()}).rgb(), colors.background.rgb());
        QVERIFY(containsColor(render(bar).copy(rect), colors.text));
    }
    QVERIFY(containsColor(render(bar).copy(bar->displayTabRect(0)), preset.groupActive.icon));
    tabs.setCurrentIndex(1);
    QCOMPARE(sample(bar, {bar->displayTabRect(0).left() + 8, bar->displayTabRect(0).center().y()}).rgb(), preset.groupInactive.background.rgb());
}

void TabStripLayoutTest::closeTargetShowsHoverAndPressFeedback()
{
    TabAppearance appearance; appearance.showNewTabButton = false; appearance.showIcons = false;
    const auto preset = solidPreset("close-feedback"); appearance.colors = {preset};
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs); tabs.setAppearance(appearance);
    const auto id = QUuid::createUuid(); tabs.addTab(new QLabel("Content"), {}, "Feedback", id);
    TabRecord record; record.id = id; record.title = "Feedback"; record.colorId = preset.id;
    tabs.setTabMetadata(0, record);
    int closed = 0; tabs.closeRequested = [&closed](int) { ++closed; };
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const auto tab = bar->displayTabRect(0);
    const auto target = bar->actionRect(0); QVERIFY(!target.isEmpty()); QVERIFY(tab.contains(target));
    const QPoint body(tab.left() + 4, tab.center().y());
    sendBarMouse(bar, QEvent::MouseMove, body, Qt::NoButton, Qt::NoButton);
    const auto shown = render(bar).copy(target);
    sendBarMouse(bar, QEvent::MouseMove, target.center(), Qt::NoButton, Qt::NoButton);
    const auto hovered = render(bar).copy(target);
    sendBarMouse(bar, QEvent::MouseButtonPress, target.center(), Qt::LeftButton, Qt::LeftButton);
    const auto pressed = render(bar).copy(target);
    // Hovering the target adds a wash, pressing a stronger one.
    const QPoint corner(1, 1);
    QVERIFY(hovered.pixelColor(corner).rgb() != shown.pixelColor(corner).rgb());
    QVERIFY(qGray(pressed.pixelColor(corner).rgb()) < qGray(hovered.pixelColor(corner).rgb()));
    // Releasing elsewhere cancels and clears the feedback; releasing on it closes.
    sendBarMouse(bar, QEvent::MouseMove, body, Qt::NoButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, body, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(closed, 0);
    QCOMPARE(render(bar).copy(target), shown);
    sendBarMouse(bar, QEvent::MouseButtonPress, target.center(), Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, target.center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(closed, 1);
}

void TabStripLayoutTest::stickyTabsAlwaysShowTheirAction()
{
    TabAppearance appearance; appearance.showNewTabButton = false; appearance.showIcons = false;
    const auto preset = solidPreset("sticky-pin"); appearance.colors = {preset};
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs); tabs.setAppearance(appearance);
    for (const auto* title : {"Sticky", "Plain"}) {
        const auto id = QUuid::createUuid(); tabs.addTab(new QLabel("Content"), {}, title, id);
        TabRecord record; record.id = id; record.title = title; record.colorId = preset.id;
        tabs.setTabMetadata(tabs.count() - 1, record);
    }
    int closed = 0, unstuck = 0;
    tabs.closeRequested = [&closed](int) { ++closed; };
    tabs.unstickRequested = [&unstuck](int index) { QCOMPARE(index, 0); ++unstuck; };
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const auto inked = [](const QImage& image) {
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).rgb() != QColor(Qt::white).rgb()) return true;
        return false;
    };
    tabs.setStickyCount(1);
    sendBarMouse(bar, QEvent::MouseMove, QPoint(bar->width() - 2, bar->height() - 2), Qt::NoButton, Qt::NoButton);
    // Without hover a sticky tab shows its action in room of its own; a
    // plain tab shows its close button only while hovered.
    QVERIFY(inked(render(bar).copy(bar->actionRect(0))));
    const auto resting = render(bar).copy(bar->actionRect(1));
    sendBarMouse(bar, QEvent::MouseMove, bar->displayTabRect(1).center(), Qt::NoButton, Qt::NoButton);
    QVERIFY(render(bar).copy(bar->actionRect(1)) != resting);
    QVERIFY(bar->displayTabRect(0).width() > bar->naturalTabWidth(0));
    // Pressing a sticky tab's action unsticks it instead of closing.
    sendBarMouse(bar, QEvent::MouseButtonPress, bar->actionRect(0).center(), Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, bar->actionRect(0).center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(unstuck, 1); QCOMPARE(closed, 0);
    sendBarMouse(bar, QEvent::MouseMove, bar->actionRect(1).center(), Qt::NoButton, Qt::NoButton);
    sendBarMouse(bar, QEvent::MouseButtonPress, bar->actionRect(1).center(), Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, bar->actionRect(1).center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(closed, 1); QCOMPARE(unstuck, 1);
}

void TabStripLayoutTest::titleWidthChangesKeepTheActiveTabInPlace()
{
    TabAppearance appearance; appearance.showNewTabButton = false; appearance.showIcons = false;
    appearance.clipTitles = true; appearance.maximumTitleWidth = 120;
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs); tabs.setAppearance(appearance);
    for (int index = 0; index < 30; ++index)
        tabs.addTab(new QLabel("Content"), {}, QString("Tab %1 ").arg(index) + QString(30, QLatin1Char('W')), QUuid::createUuid());
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const auto viewport = bar->scrollViewport();
    // Where a tab starts in the viewport, scrolled.
    const auto screenLeft = [&](int index) { return bar->contentTabRect(index).left() - tabs.scrollOffset() - viewport.left(); };
    tabs.setCurrentIndex(15);
    // Put the active tab 200 px into the viewport, away from both edges.
    tabs.restoreScrollOffset(bar->contentTabRect(15).left() - viewport.left() - 200);
    QTRY_COMPARE(screenLeft(15), 200);
    QCoreApplication::processEvents();
    const auto apply = [&](int width) { appearance.maximumTitleWidth = width; tabs.setAppearance(appearance); QCoreApplication::processEvents(); };
    // The active tab stays where it is while the others resize around it.
    for (const int width : {40, 0, 80, 120}) { apply(width); QCOMPARE(screenLeft(15), 200); }
    // Growing past the end pushes it back only as far as needed.
    apply(400);
    QCOMPARE(screenLeft(15) + bar->contentTabRect(15).width(), viewport.width());
    // Shrinking again keeps its new place; back and forth does not drift.
    apply(120); const int pushed = screenLeft(15); QVERIFY(pushed < 200);
    apply(40); apply(120); QCOMPARE(screenLeft(15), pushed);
    // Without a shown active tab, the tab at the leading edge stays in place.
    tabs.restoreScrollOffset(bar->contentTabRect(3).left() - viewport.left());
    QTRY_COMPARE(screenLeft(3), 0);
    QCoreApplication::processEvents();
    QVERIFY(screenLeft(15) >= viewport.width());
    apply(60); QCOMPARE(screenLeft(3), 0);
    apply(200); QCOMPARE(screenLeft(3), 0);
}

void TabStripLayoutTest::tooltipsNameTheDisplayedTab()
{
    TabAppearance appearance; appearance.showNewTabButton = false; appearance.multiline = true;
    GroupTabs tabs(defaultTheme(), nullptr); tabs.setAppearance(appearance); populate(tabs, 8);
    for (int index = 0; index < 8; ++index) tabs.tabBar()->setTabToolTip(index, QStringLiteral("Tip %1").arg(index));
    tabs.resize(320, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); QTRY_VERIFY(shownRows(bar).size() > 2);
    const auto hideTip = qScopeGuard([] { QToolTip::hideText(); });
    for (int index = 0; index < bar->count(); ++index) {
        const auto point = QPoint(bar->displayTabRect(index).left() + 4, bar->displayTabRect(index).center().y());
        QHelpEvent help(QEvent::ToolTip, point, bar->mapToGlobal(point));
        QApplication::sendEvent(bar, &help);
        QTRY_COMPARE(QToolTip::text(), bar->tabToolTip(index));
    }
}

void TabStripLayoutTest::middleClickClosesTheTabUnderThePointer()
{
    TabAppearance appearance; appearance.showNewTabButton = false;
    GroupTabs tabs(defaultTheme(), nullptr); tabs.setAppearance(appearance); populate(tabs, 3);
    QVector<int> closed;
    tabs.closeRequested = [&closed](int index) { closed << index; };
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); tabs.setCurrentIndex(0);
    const auto click = [bar](int from, int to) {
        sendBarMouse(bar, QEvent::MouseButtonPress, bar->displayTabRect(from).center(), Qt::MiddleButton, Qt::MiddleButton);
        sendBarMouse(bar, QEvent::MouseButtonRelease, bar->displayTabRect(to).center(), Qt::MiddleButton, Qt::NoButton);
    };
    // The pressed tab closes without being selected first.
    click(2, 2); QCOMPARE(closed, QVector<int>{2}); QCOMPARE(tabs.currentIndex(), 0);
    // Releasing on another tab cancels, as does a left click.
    click(1, 2); QCOMPARE(closed, QVector<int>{2});
    sendBarMouse(bar, QEvent::MouseButtonPress, bar->displayTabRect(1).center(), Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, bar->displayTabRect(1).center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(closed, QVector<int>{2}); QCOMPARE(tabs.currentIndex(), 1);
    // An auto-hide rail tab closes the same way, without opening its panel.
    QWidget root; applyTheme(&root);
    EdgeTabButton button("Rail", true, defaultTheme().metrics.edgeThickness, &root); button.resize(button.sizeHint());
    int railClosed = 0, railClicked = 0;
    button.close = [&railClosed] { ++railClosed; };
    QObject::connect(&button, &QAbstractButton::clicked, &button, [&railClicked] { ++railClicked; });
    sendBarMouse(&button, QEvent::MouseButtonPress, button.rect().center(), Qt::MiddleButton, Qt::MiddleButton);
    sendBarMouse(&button, QEvent::MouseButtonRelease, button.rect().center(), Qt::MiddleButton, Qt::NoButton);
    QCOMPARE(railClosed, 1); QCOMPARE(railClicked, 0);
    sendBarMouse(&button, QEvent::MouseButtonPress, button.rect().center(), Qt::MiddleButton, Qt::MiddleButton);
    sendBarMouse(&button, QEvent::MouseButtonRelease, button.rect().bottomRight() + QPoint(5, 5), Qt::MiddleButton, Qt::NoButton);
    QCOMPARE(railClosed, 1);
}

void TabStripLayoutTest::middleClickClosesManagedStripAndRailTabs()
{
    const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
    WindowManager manager;
    const auto window = manager.createWindow(WindowKind::Workbench, {"Middle close", {900, 360}});
    QVector<TabId> ids;
    for (int index = 0; index < 3; ++index)
        ids.append(manager.addTab(window, new QLabel(QString("Content %1").arg(index)), {QString("Tab %1").arg(index), "label", {}}));
    QVERIFY(manager.moveTabToEdgeBar(ids[2], window, Edge::Left, -1));
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto middle = [](QWidget* target, QPoint point) {
        sendBarMouse(target, QEvent::MouseButtonPress, point, Qt::MiddleButton, Qt::MiddleButton);
        sendBarMouse(target, QEvent::MouseButtonRelease, point, Qt::MiddleButton, Qt::NoButton);
    };
    auto* view = groupView(manager.windowWidget(window), manager.model().groupForTab(ids[0])); QVERIFY(view);
    middle(view->tabBar(), view->tabBar()->displayTabRect(1).center());
    QTRY_VERIFY(!manager.model().tab(ids[1]));
    QAbstractButton* rail = nullptr;
    for (auto* button : manager.windowWidget(window)->findChildren<QAbstractButton*>())
        if (dynamic_cast<EdgeTabButton*>(button) && button->property("tabId").toUuid() == ids[2]) rail = button;
    QVERIFY(rail);
    middle(rail, rail->rect().center());
    QTRY_VERIFY(!manager.model().tab(ids[2]));
    QVERIFY(manager.model().tab(ids[0]));
}

void TabStripLayoutTest::newTabButtonsShareTheRowAndOpenTheTypeMenu()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 2); tabs.resize(700, 250); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* add = tabs.findChild<QToolButton*>("NewTabButton"); auto* types = tabs.findChild<QToolButton*>("NewTabTypeButton");
    QVERIFY(add && types);
    QVERIFY(types->x() > add->geometry().right());
    QCOMPARE(add->height(), types->height()); QCOMPARE(add->height(), tabs.tabBar()->metrics().rowHeight);
    int requested = 0; QPointer<QMenu> popup;
    tabs.newTabMenuRequested = [&](QWidget* parent) { ++requested; popup = new DismissOnlyMenu(parent); popup->addAction("Create"); applyTheme(popup); return popup.data(); };
    for (auto* button : {add, types}) {
        QContextMenuEvent event(QContextMenuEvent::Mouse, button->rect().center(), button->mapToGlobal(button->rect().center()));
        QApplication::sendEvent(button, &event); QVERIFY(popup); QTRY_VERIFY(popup->isVisible());
        popup->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!popup);
    }
    QCOMPARE(requested, 2);
}

void TabStripLayoutTest::insertionFollowsThePointerRow()
{
    // A carried tab lands on the row under it; its gap is one row high and
    // lies on that row (tab strip design 6.1).
    GroupTabs tabs(defaultTheme(), nullptr);
    TabAppearance appearance; appearance.multiline = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); populate(tabs, 12);
    tabs.resize(420, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const auto rows = shownRows(bar); QVERIFY(rows.size() >= 3);
    int first = -1;
    for (int index = 0; index < bar->count(); ++index) if (bar->displayTabRect(index).top() == rows[1]) { first = index; break; }
    QVERIFY(first > 0);
    const auto target = bar->displayTabRect(first);
    const auto global = bar->mapToGlobal(QPoint(target.left() + 3, target.center().y()));
    QCOMPARE(tabs.insertionIndex(global), first);
    tabs.setInsertionPreview(first, {{QStringLiteral("Carried"), false}});
    // The gap is one row high and stands where the tab will be: right after
    // the tab before it, which may be at the end of the row above.
    const auto gap = bar->dropPreviewRect();
    QCOMPARE(gap.height(), bar->metrics().rowHeight);
    const auto before = bar->displayTabRect(first - 1), after = bar->displayTabRect(first);
    QVERIFY((gap.top() == before.top() && gap.left() == before.right() + 1) || (gap.top() > before.top() && gap.left() == bar->displayTabRect(0).left()));
    QVERIFY((after.top() == gap.top() && after.left() == gap.right() + 1) || after.top() > gap.top());
    // The decision stays with the gap open.
    QCOMPARE(tabs.insertionIndex(global), first);
    tabs.setInsertionPreview(-1, {});
}

void TabStripLayoutTest::multilineDragReordersAndTearsOffOnlyPastEveryRow_data()
{
    QTest::addColumn<bool>("bottom");
    QTest::newRow("top") << false; QTest::newRow("bottom") << true;
}
void TabStripLayoutTest::multilineDragReordersAndTearsOffOnlyPastEveryRow()
{
    // Real input: a lifted tab moves among the rows of its strip without
    // leaving it; past every row it tears off; Escape restores the order.
    QFETCH(bool, bottom);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance(); appearance.multiline = true;
    appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto window = manager.createWindow(WindowKind::Workbench, {"Multiline drag", {480, qMin(420, available.height() - 24)}});
    QVector<TabId> ids; QVector<QPointer<QWidget>> contents;
    for (int index = 0; index < 9; ++index) {
        auto* content = new QLabel(QString("Content %1").arg(index)); contents.append(content);
        ids.append(manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}));
    }
    const auto source = ids[1], group = manager.model().groupForTab(source);
    manager.windowWidget(window)->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    auto* bar = view->tabBar(); QTRY_VERIFY(shownRows(bar).size() >= 3);
    const auto start = bar->mapToGlobal(bar->displayTabRect(1).center());
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    QCOMPARE(manager.model().windows().size(), 1); QVERIFY(contents[1]->isVisible());
    // Onto a later row: still this strip, the gap on that row.
    const auto later = bar->displayTabRect(6);
    const auto destination = bar->mapToGlobal(QPoint(later.left() + 4, later.center().y()));
    sendMouse(receiver, QEvent::MouseMove, destination);
    QCOMPARE(manager.model().windows().size(), 1);
    const auto gap = view->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), bar->metrics().rowHeight);
    QVERIFY(view->viewportRectGlobal().contains(gap));
    // Past every row it tears off.
    const auto bounds = view->viewportRectGlobal();
    const QPoint outside(bounds.center().x(), bottom ? bounds.top() - 60 : bounds.bottom() + 60);
    sendMouse(receiver, QEvent::MouseMove, outside);
    QTRY_COMPARE(manager.model().windows().size(), 2);
    QCOMPARE(manager.paneWidget(manager.model().tab(source)->activePane), contents[1].data());
    receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    QTest::keyClick(receiver, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(manager.model().windows().size(), 1); QCOMPARE(manager.model().group(group)->tabs, ids);
    QCOMPARE(manager.paneWidget(manager.model().tab(source)->activePane), contents[1].data());
    QVERIFY(!QWidget::mouseGrabber());
}

void TabStripLayoutTest::verticalStripResizesAndFoldsToIcons()
{
    TabAppearance appearance; appearance.centralPlacement = TabBarPlacement::Left;
    GroupTabs tabs(defaultTheme(), nullptr); tabs.setAppearance(appearance); populate(tabs, 6);
    int width = -1; std::optional<bool> icons;
    tabs.verticalWidthChanged = [&width](int value) { width = value; };
    tabs.iconModeChanged = [&icons](bool value) { icons = value; };
    tabs.resize(900, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); QVERIFY(bar->vertical());
    QCOMPARE(bar->width(), defaultVerticalTabWidth);
    // Tabs are rows across the column.
    for (int index = 0; index < 6; ++index) QCOMPARE(bar->displayTabRect(index).width(), bar->width() - 2 * bar->metrics().verticalInset);
    QCOMPARE(bar->displayTabRect(1).top(), bar->displayTabRect(0).bottom() + 1 + bar->metrics().rowGap);
    // Dragging the inner edge resizes; a double click restores the default.
    auto* handle = tabs.findChild<QWidget*>("TabStripResizeHandle"); QVERIFY(handle && handle->isVisible());
    const auto center = handle->rect().center();
    sendBarMouse(handle, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(handle, QEvent::MouseMove, center + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
    sendBarMouse(handle, QEvent::MouseButtonRelease, center + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(width, defaultVerticalTabWidth + 40); QCOMPARE(bar->width(), defaultVerticalTabWidth + 40);
    QMouseEvent twice(QEvent::MouseButtonDblClick, QPointF(center), handle->mapToGlobal(QPointF(center)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &twice);
    QCOMPARE(width, 0); QCOMPARE(bar->width(), defaultVerticalTabWidth);
    // A named group's name titles the column (tab strip design 13).
    tabs.setGroupName(QStringLiteral("Servers"));
    auto* title = tabs.findChild<QLabel*>("TabGroupTitle"); QVERIFY(title && title->isVisible());
    QVERIFY(title->geometry().bottom() < bar->geometry().top());
    QVERIFY(!tabs.findChild<QLabel*>("TabGroupName")->isVisible());
    // Folded to icons: a narrow column that peeks at full width on hover.
    auto* fold = tabs.findChild<QToolButton*>("TabStripIconModeButton"); QVERIFY(fold && fold->isVisible());
    fold->click();
    QCOMPARE(icons, std::optional<bool>(true)); QVERIFY(bar->iconMode()); QVERIFY(!title->isVisible());
    QCOMPARE(bar->width(), bar->metrics().verticalIconWidth);
    const auto content = tabs.findChild<QStackedWidget*>()->geometry();
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), bar->mapToGlobal(QPointF(5, 5)));
    QApplication::sendEvent(bar, &enter);
    QTRY_VERIFY(bar->overlayActive());
    QVERIFY(bar->width() > bar->metrics().verticalIconWidth);
    QCOMPARE(tabs.findChild<QStackedWidget*>()->geometry(), content);
    QEvent leave(QEvent::Leave); QApplication::sendEvent(bar, &leave);
    QTRY_VERIFY(!bar->overlayActive());
    QCOMPARE(bar->width(), bar->metrics().verticalIconWidth);
}

void TabStripLayoutTest::folderHeadersToggleOpenMenusAndCarry_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("horizontal") << false; QTest::newRow("vertical") << true;
}
void TabStripLayoutTest::folderHeadersToggleOpenMenusAndCarry()
{
    // Tab folders (tab strip design 12): the header leads its tabs; a click
    // toggles, the context menu and drags reach their callbacks; collapsed,
    // only the current tab of the folder stays shown.
    QFETCH(bool, vertical);
    TabAppearance appearance; if (vertical) appearance.centralPlacement = TabBarPlacement::Left;
    GroupTabs tabs(defaultTheme(), nullptr); tabs.setAppearance(appearance); populate(tabs, 6);
    auto* bar = tabs.tabBar();
    QVector<int> toggled, created; QVector<QPair<int, QPoint>> menus, drags;
    bar->toggleFolder = [&toggled](int folder) { toggled << folder; };
    bar->newTabInFolder = [&created](int folder) { created << folder; };
    bar->folderMenu = [&menus](int folder, QPoint point) { menus << QPair{folder, point}; };
    bar->folderDrag = [&drags](int folder, QPoint point) { drags << QPair{folder, point}; };
    TabStripFolder folder{QUuid::createUuid(), QStringLiteral("Work"), {}, false, 1, 3};
    bar->setFolders({folder});
    tabs.setCurrentIndex(0);
    tabs.resize(900, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    const auto header = bar->folderHeaderRect(0); QVERIFY(!header.isEmpty());
    if (vertical) {
        QVERIFY(header.top() > bar->displayTabRect(0).bottom());
        QVERIFY(bar->displayTabRect(1).top() > header.bottom());
        // Its tabs sit indented under it; the next tab is not.
        QVERIFY(bar->displayTabRect(1).left() > header.left());
        QCOMPARE(bar->displayTabRect(4).left(), header.left());
    } else {
        QVERIFY(header.left() > bar->displayTabRect(0).right());
        QVERIFY(bar->displayTabRect(1).left() > header.right());
    }
    QCOMPARE(bar->folderAt(header.center()), 0); QCOMPARE(bar->tabAt(header.center()), -1);
    // A click toggles; on a vertical header its add button opens a tab inside.
    const QPoint grip(header.left() + 4, header.center().y());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, grip);
    QCOMPARE(toggled, QVector<int>{0}); QCOMPARE(tabs.currentIndex(), 0);
    if (vertical) {
        sendBarMouse(bar, QEvent::MouseMove, header.center(), Qt::NoButton, Qt::NoButton);
        const auto add = TabPainter(bar->metrics(), TabPalette::forTheme(defaultTheme())).folderAddRect(header);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, add.center());
        QCOMPARE(created, QVector<int>{0}); QCOMPARE(toggled.size(), 1);
    }
    QContextMenuEvent context(QContextMenuEvent::Mouse, grip, bar->mapToGlobal(grip));
    QApplication::sendEvent(bar, &context);
    QCOMPARE(menus.size(), 1); QCOMPARE(menus.first().first, 0);
    // Dragging the header carries the folder.
    sendBarMouse(bar, QEvent::MouseButtonPress, grip, Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseMove, grip + QPoint(QApplication::startDragDistance() + 2, QApplication::startDragDistance() + 2), Qt::NoButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, grip, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(drags.size(), 1); QCOMPARE(drags.first().first, 0); QCOMPARE(toggled.size(), 1);
    // Collapsed: its tabs leave the strip except the current one.
    folder.collapsed = true; bar->setFolders({folder});
    for (const int index : {1, 2, 3}) QVERIFY(bar->displayTabRect(index).isEmpty());
    QVERIFY(!bar->displayTabRect(4).isEmpty());
    tabs.setCurrentIndex(2);
    QVERIFY(!bar->displayTabRect(2).isEmpty());
    QVERIFY(bar->displayTabRect(1).isEmpty() && bar->displayTabRect(3).isEmpty());
    // A carried tab dropped on a collapsed header joins it at its end.
    const auto decision = bar->insertionAt(bar->folderHeaderRect(0).center());
    QCOMPARE(decision.folder, 0); QCOMPARE(decision.index, 4);
}

void TabStripLayoutTest::accessibilityListsTabsAndFolders()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 4);
    auto* bar = tabs.tabBar();
    int toggled = 0; bar->toggleFolder = [&toggled](int) { ++toggled; };
    bar->setFolders({{QUuid::createUuid(), QStringLiteral("Logs"), {}, true, 2, 2}});
    tabs.resize(700, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* list = QAccessible::queryAccessibleInterface(bar); QVERIFY(list);
    QCOMPARE(list->role(), QAccessible::PageTabList);
    QCOMPARE(list->childCount(), 5);
    for (int index = 0; index < 4; ++index) {
        auto* tab = list->child(index); QVERIFY(tab);
        QCOMPARE(tab->role(), QAccessible::PageTab); QCOMPARE(tab->text(QAccessible::Name), bar->tabText(index));
        QCOMPARE(tab->state().selected, index == tabs.currentIndex());
    }
    auto* folder = list->child(4); QVERIFY(folder);
    QCOMPARE(folder->role(), QAccessible::Button);
    QVERIFY(folder->text(QAccessible::Name).contains(QStringLiteral("Logs")));
    QVERIFY(folder->state().collapsed); QVERIFY(!folder->state().expanded);
    auto* action = folder->actionInterface(); QVERIFY(action);
    action->doAction(action->actionNames().first());
    QCOMPARE(toggled, 1);
    // Hit testing finds what is drawn.
    const auto center = bar->mapToGlobal(bar->displayTabRect(1).center());
    QCOMPARE(list->childAt(center.x(), center.y()), list->child(1));
    const auto header = bar->mapToGlobal(bar->folderHeaderRect(0).center());
    QCOMPARE(list->childAt(header.x(), header.y()), folder);
}

void TabStripLayoutTest::groupNamesEndTheStripUnlessTurnedOff()
{
    // A named group shows its name before the end controls (tab strip
    // design 13); the setting hides it.
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 3);
    tabs.resize(700, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* name = tabs.findChild<QLabel*>("TabGroupName"); QVERIFY(name);
    QVERIFY(!name->isVisible());
    tabs.setGroupName(QStringLiteral("Servers"));
    QVERIFY(name->isVisible()); QCOMPARE(name->text(), QStringLiteral("Servers"));
    auto* bar = tabs.tabBar(); auto* list = tabs.findChild<QWidget*>("TabListButton");
    const QRect nameRect(name->mapTo(bar, QPoint()), name->size()), listRect(list->mapTo(bar, QPoint()), list->size());
    QVERIFY(nameRect.right() < listRect.left());
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).intersects(nameRect));
    TabAppearance appearance; appearance.showGroupNames = false; tabs.setAppearance(appearance);
    tabs.setGroupName(QStringLiteral("Servers"));
    QVERIFY(!name->isVisible());
    appearance.showGroupNames = true; tabs.setAppearance(appearance); tabs.setGroupName({});
    QVERIFY(!name->isVisible());
}

QTEST_MAIN(TabStripLayoutTest)
#include "tab_strip_layout_test.moc"
