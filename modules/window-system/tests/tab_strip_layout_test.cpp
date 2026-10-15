// SPDX-License-Identifier: MIT
#include "docking/tab_strip.hpp"
#include "commands/popup_menu.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "fixtures/authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QContextMenuEvent>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTest>
#include <QToolButton>

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
QColor sample(QWidget* widget, QPoint logical)
{
    const auto image = widget->grab().toImage(); const auto ratio = image.devicePixelRatio();
    return image.pixelColor(qRound(logical.x() * ratio), qRound(logical.y() * ratio));
}
bool containsColor(const QImage& image, QColor color)
{
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y).rgb() == color.rgb()) return true;
    return false;
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
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void multilineWrapsWithoutHorizontalScrollAndKeepsEveryTab();
    void wheelExpansionShowsEveryRowThenOnlyActiveRow();
    void placementIsIndependentForCentralAndPinnedGroups();
    void displayFlagsChangeActualLabelWidthsAndCreationControls();
    void paletteRendersSelectedAndUnselectedStatesAndActiveIcon();
    void newTabButtonsHaveGapAndBothOpenTypeMenuOnContextPress();
    void insertionPreviewAndHitTestingUseThePointerRow_data();
    void insertionPreviewAndHitTestingUseThePointerRow();
    void oversizedInsertionPreviewRemainsOneBoundedRow();
    void collapsedRowInsertionKeepsItsOriginalTabOrdinals();
    void multilineDragReordersInPlaceBeforeFirstExit_data();
    void multilineDragReordersInPlaceBeforeFirstExit();
    void multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow_data();
    void multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow();
    void detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow_data();
    void detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow();
    void wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse_data();
    void wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse();
    void activeOnlyLaterRowDragNeverFallsBackToFirstRow_data();
    void activeOnlyLaterRowDragNeverFallsBackToFirstRow();
};

void TabStripLayoutTest::multilineWrapsWithoutHorizontalScrollAndKeepsEveryTab()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 9);
    tabs.resize(410, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport");
    QVERIFY(scroll); QTRY_VERIFY(bar->rowCount() > 1);
    QCOMPARE(bar->count(), ids.size()); QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    QVERIFY(tabs.findChild<QScrollBar*>("TabScrollBar")->isHidden());
    QSet<int> rows;
    for (int index = 0; index < bar->count(); ++index) {
        const auto rect = bar->displayTabRect(index); QVERIFY(!rect.isEmpty());
        QVERIFY(scroll->viewport()->rect().contains(rect)); rows.insert(rect.top());
        QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
        for (int other = index + 1; other < bar->count(); ++other) QVERIFY(!rect.intersects(bar->displayTabRect(other)));
    }
    QCOMPARE(rows.size(), bar->rowCount());
    const auto last = bar->displayTabRect(bar->count() - 1);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, last.center());
    QCOMPARE(tabs.currentIndex(), bar->count() - 1);
    QCOMPARE(tabs.findChild<QStackedWidget*>()->currentIndex(), bar->count() - 1);
}

void TabStripLayoutTest::wheelExpansionShowsEveryRowThenOnlyActiveRow()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); populate(tabs, 9); tabs.resize(410, 300); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    tabs.scrollByWheel({0, -120});
    auto* bar = tabs.tabBar(); QTRY_VERIFY(bar->rowCount() > 1);
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).isEmpty());
    const int laterIndex = bar->count() - 1;
    const int laterRow = bar->displayTabRect(laterIndex).top(); QVERIFY(laterRow > 0);
    QVector<bool> belongs;
    for (int index = 0; index < bar->count(); ++index) belongs.append(bar->displayTabRect(index).top() == laterRow);
    tabs.setCurrentIndex(laterIndex);
    const int fullHeight = bar->height(); tabs.scrollByWheel({0, 120});
    QTRY_VERIFY(bar->height() < fullHeight);
    for (int index = 0; index < bar->count(); ++index) {
        QCOMPARE(!bar->displayTabRect(index).isEmpty(), belongs[index]);
        if (belongs[index]) QCOMPARE(bar->displayTabRect(index).top(), 0);
    }
    QCOMPARE(tabs.currentIndex(), laterIndex); QCOMPARE(tabs.scrollOffset(), 0);
    tabs.scrollByWheel({0, -120}); QTRY_COMPARE(bar->height(), fullHeight);
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).isEmpty());
}

void TabStripLayoutTest::placementIsIndependentForCentralAndPinnedGroups()
{
    auto appearance = TabAppearance{}; appearance.centralPlacement = TabBarPlacement::Bottom;
    GroupTabs central(defaultTheme(), nullptr), pinned(defaultTheme(), nullptr);
    central.setAppearance(appearance); pinned.setAppearance(appearance, true);
    populate(central, 1); populate(pinned, 1); central.resize(400, 220); pinned.resize(400, 220);
    central.show(); pinned.show();
    QVERIFY(QTest::qWaitForWindowExposed(&central)); QVERIFY(QTest::qWaitForWindowExposed(&pinned));
    auto* centralHeader = central.findChild<QWidget*>("TabGroupHeader"); auto* pinnedHeader = pinned.findChild<QWidget*>("TabGroupHeader");
    auto* centralContent = central.findChild<QStackedWidget*>(); auto* pinnedContent = pinned.findChild<QStackedWidget*>();
    QVERIFY(centralHeader && pinnedHeader && centralContent && pinnedContent);
    QVERIFY(centralHeader->y() > centralContent->y()); QVERIFY(pinnedHeader->y() < pinnedContent->y());
    appearance.centralPlacement = TabBarPlacement::Top; appearance.pinnedPlacement = TabBarPlacement::Bottom;
    central.setAppearance(appearance); pinned.setAppearance(appearance, true);
    QCoreApplication::processEvents();
    QVERIFY(centralHeader->y() < centralContent->y()); QVERIFY(pinnedHeader->y() > pinnedContent->y());
    QCOMPARE(central.count(), 1); QCOMPARE(pinned.count(), 1);
}

void TabStripLayoutTest::displayFlagsChangeActualLabelWidthsAndCreationControls()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 2, true);
    tabs.resize(640, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int original = bar->displayTabRect(0).width();
    auto appearance = TabAppearance{}; appearance.showIcons = false; tabs.setAppearance(appearance);
    QTRY_COMPARE(bar->displayTabRect(0).width(), original - bar->iconSize().width() - 4);
    const int noIcon = bar->displayTabRect(0).width(); appearance.showNumbers = true; tabs.setAppearance(appearance);
    QTRY_COMPARE(bar->displayTabRect(0).width(), noIcon + bar->fontMetrics().horizontalAdvance("1") + 5);
    auto* controls = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(controls);
    QVERIFY(!controls->isHidden()); appearance.showNewTabButton = false; tabs.setAppearance(appearance);
    QVERIFY(controls->isHidden());
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(scroll->widget()->width(), bar->width());
    appearance.showNewTabButton = true; tabs.setAppearance(appearance); QVERIFY(!controls->isHidden());
    QCOMPARE(scroll->widget()->width(), bar->width() + controls->width());
}

void TabStripLayoutTest::paletteRendersSelectedAndUnselectedStatesAndActiveIcon()
{
    GroupTabs tabs(defaultTheme(), nullptr); const auto ids = populate(tabs, 2, true);
    auto appearance = TabAppearance{};
    auto preset = appearance.colors.front(); preset.id = "test";
    preset.groupActive = {QColor("#d4e5f6"), QColor("#172839"), QColor("#112233"), QColor("#cc55aa")};
    preset.groupInactive = {QColor("#f5dfc9"), QColor("#334455"), QColor("#556677"), QColor("#887766")};
    appearance.colors = {preset}; tabs.setAppearance(appearance);
    for (int index = 0; index < ids.size(); ++index) { TabRecord record; record.id = ids[index]; record.colorId = "test"; tabs.setTabMetadata(index, record); }
    tabs.setCurrentIndex(0); tabs.resize(640, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    for (int index = 0; index < 2; ++index) {
        const auto rect = bar->displayTabRect(index); const auto colors = index == 0 ? preset.groupActive : preset.groupInactive;
        QCOMPARE(sample(bar, {rect.left() + 3, rect.center().y()}), colors.background);
        QCOMPARE(sample(bar, {rect.left() + 3, rect.bottom()}), colors.line);
        QVERIFY(containsColor(bar->grab(rect).toImage(), colors.text));
    }
    QVERIFY(containsColor(bar->grab(bar->displayTabRect(0)).toImage(), preset.groupActive.icon));
    tabs.setCurrentIndex(1);
    QCOMPARE(sample(bar, {bar->displayTabRect(0).left() + 3, bar->displayTabRect(0).center().y()}), preset.groupInactive.background);
    QVERIFY(containsColor(bar->grab(bar->displayTabRect(1)).toImage(), preset.groupActive.icon));
}

void TabStripLayoutTest::newTabButtonsHaveGapAndBothOpenTypeMenuOnContextPress()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 2); tabs.resize(700, 250); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* add = tabs.findChild<QToolButton*>("NewTabButton"); auto* types = tabs.findChild<QToolButton*>("NewTabTypeButton");
    QVERIFY(add && types); QCOMPARE(types->x() - add->geometry().right() - 1, 1);
    QCOMPARE(add->height(), types->height()); QVERIFY(add->height() == tabs.tabBar()->height());
    int requested = 0; QPointer<QMenu> popup;
    tabs.newTabMenuRequested = [&](QWidget* parent) { ++requested; popup = new DismissOnlyMenu(parent); popup->addAction("Create"); applyTheme(popup); return popup.data(); };
    for (auto* button : {add, types}) {
        QContextMenuEvent event(QContextMenuEvent::Mouse, button->rect().center(), button->mapToGlobal(button->rect().center()));
        QApplication::sendEvent(button, &event); QVERIFY(popup); QTRY_VERIFY(popup->isVisible());
        QVERIFY(types->isDown()); popup->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!popup); QVERIFY(!types->isDown());
    }
    QCOMPARE(requested, 2);
}

void TabStripLayoutTest::insertionPreviewAndHitTestingUseThePointerRow_data()
{
    QTest::addColumn<bool>("bottom");
    QTest::newRow("top") << false;
    QTest::newRow("bottom") << true;
}

void TabStripLayoutTest::insertionPreviewAndHitTestingUseThePointerRow()
{
    QFETCH(bool, bottom);
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 9);
    tabs.resize(410, 320); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QTRY_VERIFY(bar->rowCount() >= 3);
    const int rowHeight = defaultTheme().metrics.tabHeight;
    for (int index = 0; index < bar->count(); ++index) {
        const auto label = bar->displayTabRect(index); QVERIFY(!label.isEmpty());
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(label.left() + 2, label.center().y()))), index);
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(label.right() - 2, label.center().y()))), index + 1);
    }
    // Insert at the beginning of a later row: this must not become an x-only
    // offset in the first row, and the gap remains one label high.
    int later = 0;
    while (later < bar->count() && bar->displayTabRect(later).top() == 0) ++later;
    QVERIFY(later < bar->count());
    const auto original = bar->displayTabRect(later);
    tabs.setInsertionPreview(later, original.width());
    const auto gap = tabs.insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.top(), bar->mapToGlobal(original.topLeft()).y());
    QCOMPARE(gap.height(), rowHeight); QCOMPARE(gap.width(), original.width());
    QVERIFY(tabs.viewportRectGlobal().contains(gap));
    QCOMPARE(tabs.insertionIndex(gap.center()), later);
    for (int index = 0; index < bar->count(); ++index) {
        const auto label = bar->displayTabRect(index).translated(bar->mapToGlobal(QPoint()));
        QVERIFY(!label.intersects(gap)); QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
    }
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
}

void TabStripLayoutTest::oversizedInsertionPreviewRemainsOneBoundedRow()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 3);
    tabs.resize(250, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    tabs.setInsertionPreview(1, 2000);
    const auto gap = tabs.insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.width(), tabs.viewportRectGlobal().width());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight);
    QVERIFY(tabs.viewportRectGlobal().contains(gap)); QCOMPARE(tabs.insertionIndex(gap.center()), 1);
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    auto* bar = tabs.tabBar();
    for (int index = 0; index < bar->count(); ++index) {
        const auto label = bar->displayTabRect(index).translated(bar->mapToGlobal(QPoint()));
        QVERIFY(!label.intersects(gap)); QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
    }
}

void TabStripLayoutTest::collapsedRowInsertionKeepsItsOriginalTabOrdinals()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true;
    appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 10);
    tabs.resize(410, 320); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    tabs.scrollByWheel({0, -120}); auto* bar = tabs.tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    int middle = 0;
    while (middle < bar->count() && bar->displayTabRect(middle).top() == 0) ++middle;
    QVERIFY(middle < bar->count()); const int rowTop = bar->displayTabRect(middle).top();
    int rowEnd = middle;
    while (rowEnd < bar->count() && bar->displayTabRect(rowEnd).top() == rowTop) ++rowEnd;
    QVERIFY(rowEnd < bar->count()); // Hidden later rows must not change the insertion after this row.
    tabs.setCurrentIndex(middle); tabs.scrollByWheel({0, 120});
    QTRY_COMPARE(bar->height(), defaultTheme().metrics.tabHeight);
    const auto first = bar->displayTabRect(middle), last = bar->displayTabRect(rowEnd - 1);
    QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(first.left() + 2, first.center().y()))), middle);
    const auto rowRight = tabs.viewportRectGlobal().topRight() + QPoint(-2, last.center().y());
    QCOMPARE(tabs.insertionIndex(rowRight), rowEnd);
    for (int index = 0; index < bar->count(); ++index) {
        QCOMPARE(bar->displayTabRect(index).isEmpty(), index < middle || index >= rowEnd);
        QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
    }
}

void TabStripLayoutTest::multilineDragReordersInPlaceBeforeFirstExit_data()
{
    insertionPreviewAndHitTestingUseThePointerRow_data();
}

void TabStripLayoutTest::multilineDragReordersInPlaceBeforeFirstExit()
{
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
    auto* bar = view->tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    const auto start = bar->mapToGlobal(bar->displayTabRect(1).center() - QPoint(12, 0));
    const auto threshold = start + QPoint(QApplication::startDragDistance() + 3, 0);
    QVERIFY(view->viewportRectGlobal().contains(threshold));
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, threshold);
    QTRY_VERIFY(labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(source), group); QVERIFY(contents[1]->isVisible());
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    const auto later = bar->displayTabRect(6); QVERIFY(later.top() > 0);
    const auto destination = bar->mapToGlobal(QPoint(later.left() + 2, later.center().y()));
    QVERIFY(view->viewportRectGlobal().contains(destination));
    sendMouse(receiver, QEvent::MouseMove, destination);
    QCOMPARE(manager.model().windows().size(), 1); QVERIFY(labelPreview());
    QCOMPARE(manager.model().groupForTab(source), group); QVERIFY(contents[1]->isVisible());
    const int insertion = view->insertionIndex(destination);
    QVERIFY(insertion > 1); QVERIFY(insertion < ids.size());
    const auto gap = view->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight);
    QVERIFY(view->viewportRectGlobal().contains(gap));
    auto expected = ids; expected.removeOne(source); expected.insert(insertion, source);
    sendMouse(receiver, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QTRY_VERIFY(!labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().group(group)->tabs, expected);
    for (int index = 0; index < ids.size(); ++index)
        QCOMPARE(manager.paneWidget(manager.model().tab(ids[index])->activePane), contents[index].data());
}

void TabStripLayoutTest::multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow_data()
{
    insertionPreviewAndHitTestingUseThePointerRow_data();
}

void TabStripLayoutTest::multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow()
{
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
    const auto window = manager.createWindow(WindowKind::Workbench, {"Multiline transition", {480, qMin(420, available.height() - 24)}});
    QVector<TabId> ids; QPointer<QWidget> sourceContent;
    for (int index = 0; index < 9; ++index) {
        auto* content = new QLabel(QString("Content %1").arg(index));
        ids.append(manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}));
        if (index == 1) sourceContent = content;
    }
    const auto source = ids[1], group = manager.model().groupForTab(source);
    manager.windowWidget(window)->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    auto* bar = view->tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    const auto start = bar->mapToGlobal(bar->displayTabRect(1).center() - QPoint(12, 0));
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
    QTRY_VERIFY(labelPreview()); auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    const auto last = bar->displayTabRect(bar->count() - 1);
    sendMouse(receiver, QEvent::MouseMove, bar->mapToGlobal(last.center()));
    QCOMPARE(manager.model().windows().size(), 1); QVERIFY(labelPreview()); QVERIFY(sourceContent->isVisible());
    const auto bounds = view->viewportRectGlobal();
    const QPoint outside(bounds.center().x(), bottom ? bounds.top() - 30 : bounds.bottom() + 30);
    sendMouse(receiver, QEvent::MouseMove, outside);
    QTRY_COMPARE(manager.model().windows().size(), 2); QTRY_VERIFY(!labelPreview());
    const auto floatingGroup = manager.model().groupForTab(source); QVERIFY(floatingGroup != group);
    const auto floating = manager.model().group(floatingGroup)->windowId;
    QCOMPARE(manager.model().window(floating)->kind, WindowKind::Floating);
    QCOMPARE(manager.paneWidget(manager.model().tab(source)->activePane), sourceContent.data());
    receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    // Cancel must restore the exact original order and content, not construct
    // another widget or create a third window during the stage transition.
    QTest::keyClick(receiver, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(manager.model().windows().size(), 1); QCOMPARE(manager.model().group(group)->tabs, ids);
    QCOMPARE(manager.paneWidget(manager.model().tab(source)->activePane), sourceContent.data());
    QVERIFY(sourceContent); QVERIFY(!QWidget::mouseGrabber());
}

void TabStripLayoutTest::detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("activeOnly"); QTest::addColumn<bool>("expandByWheel");
    QTest::addColumn<bool>("shortLabel");
    QTest::newRow("top-expanded") << false << false << false << false;
    QTest::newRow("bottom-expanded") << true << false << false << false;
    QTest::newRow("top-active-row") << false << true << false << false;
    QTest::newRow("bottom-active-row") << true << true << false << false;
    QTest::newRow("top-wheel-expanded") << false << true << true << false;
    QTest::newRow("bottom-wheel-expanded") << true << true << true << false;
    QTest::newRow("top-short-expanded") << false << false << false << true;
    QTest::newRow("bottom-short-expanded") << true << false << false << true;
    QTest::newRow("top-short-active-row") << false << true << false << true;
    QTest::newRow("bottom-short-active-row") << true << true << false << true;
}

void TabStripLayoutTest::detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow()
{
    QFETCH(bool, bottom); QFETCH(bool, activeOnly); QFETCH(bool, expandByWheel);
    QFETCH(bool, shortLabel);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.wheelExpandRows = activeOnly; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const QSize size(qMin(1000, available.width() - 24), qMin(500, available.height() - 24));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Multiline drop", size});
    QPointer<QWidget> sourceContent = new QLabel("Source content");
    const auto source = manager.addTab(window, sourceContent, {shortLabel ? "x" : "MMMM tab 0", "label", {}});
    const auto sourceGroup = manager.model().groupForTab(source);
    const auto sourcePane = manager.model().tab(source)->activePane;
    QVector<TabId> targets; QVector<PaneId> targetPanes; QVector<QPointer<QWidget>> targetContents;
    GroupId targetGroup;
    for (int index = 0; index < 13; ++index) {
        QPointer<QWidget> content = new QLabel(QString("Target content %1").arg(index));
        const auto tab = manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}, targetGroup);
        if (index == 0) {
            QVERIFY(manager.moveTab(tab, sourceGroup, Placement::Right));
            targetGroup = manager.model().groupForTab(tab); QVERIFY(targetGroup != sourceGroup);
        }
        targets.append(tab); targetPanes.append(manager.model().tab(tab)->activePane); targetContents.append(content);
    }
    QCOMPARE(manager.model().group(sourceGroup)->tabs, QVector<TabId>{source});
    QCOMPARE(manager.model().group(targetGroup)->tabs, targets);
    // Keep a later original row current even while the source group owns
    // input focus. Active-only mode must retain this target row on rebuild.
    QVERIFY(manager.activateTab(targets.last())); QVERIFY(manager.activateTab(source));
    auto* main = manager.windowWidget(window); main->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(main));
    if (expandByWheel) {
        auto* targetView = groupView(main, targetGroup); QVERIFY(targetView);
        QCOMPARE(targetView->tabBar()->height(), defaultTheme().metrics.tabHeight);
        targetView->scrollByWheel({0, -120});
        QTRY_VERIFY(targetView->tabBar()->height() > defaultTheme().metrics.tabHeight);
        for (int index = 0; index < targetView->count(); ++index)
            QVERIFY(!targetView->tabBar()->displayTabRect(index).isEmpty());
    }
    auto* sourceView = groupView(main, sourceGroup); QVERIFY(sourceView);
    auto* sourceBar = sourceView->tabBar(); const auto sourceRect = sourceBar->displayTabRect(0); QVERIFY(!sourceRect.isEmpty());
    const auto start = sourceBar->mapToGlobal(sourceRect.center() - QPoint(12, 0));
    const auto threshold = start + QPoint(QApplication::startDragDistance() + 3, 0);
    QVERIFY(sourceView->viewportRectGlobal().contains(threshold));
    sendMouse(sourceBar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(sourceBar, QEvent::MouseMove, threshold);
    QTRY_VERIFY(labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    const auto strip = sourceView->viewportRectGlobal();
    const QPoint outside(strip.center().x(), bottom ? strip.top() - 30 : strip.bottom() + 30);
    sendMouse(receiver, QEvent::MouseMove, outside);
    QTRY_COMPARE(manager.model().windows().size(), 2); QTRY_VERIFY(!labelPreview());
    QVERIFY(!manager.model().group(sourceGroup)); // The emptied split is removed immediately.
    QCOMPARE(manager.model().centralGroups(window), QVector<GroupId>{targetGroup});
    const auto carrierGroup = manager.model().groupForTab(source); QVERIFY(carrierGroup != targetGroup);
    const auto carrierId = manager.model().group(carrierGroup)->windowId;
    QPointer<WorkspaceWindow> carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(carrierId)); QVERIFY(carrier);
    QCOMPARE(manager.model().window(carrierId)->kind, WindowKind::Floating);
    QCOMPARE(QWidget::mouseGrabber(), carrier.data());
    QCOMPARE(carrier->surface()->dragLabelExtent(source, false), sourceRect.width());
    QCOMPARE(manager.model().tab(source)->activePane, sourcePane); QCOMPARE(manager.paneWidget(sourcePane), sourceContent.data());

    // Detaching collapsed a split, so use the target's newly mounted view.
    // Determine the expected insertion from original tab IDs and rectangles
    // before asking any insertion hit-test helper for its result.
    auto* targetView = groupView(main, targetGroup); QVERIFY(targetView);
    auto* targetBar = targetView->tabBar(); QTRY_VERIFY(targetBar->rowCount() > 1);
    QVector<int> visibleIndices;
    int bottomRow = 0;
    for (int index = 0; index < targetBar->count(); ++index)
        if (!targetBar->displayTabRect(index).isEmpty()) bottomRow = qMax(bottomRow, targetBar->displayTabRect(index).top());
    for (int index = 0; index < targetBar->count(); ++index)
        if (!targetBar->displayTabRect(index).isEmpty() && targetBar->displayTabRect(index).top() == bottomRow)
            visibleIndices.append(index);
    QVERIFY(!visibleIndices.isEmpty()); QVERIFY(visibleIndices.first() > 0);
    if (activeOnly && !expandByWheel) QCOMPARE(targetBar->height(), defaultTheme().metrics.tabHeight);
    else QVERIFY(bottomRow > 0);
    const int expectedIndex = visibleIndices.size() > 1 ? visibleIndices[1] : visibleIndices[0];
    const auto destinationLabel = targetBar->displayTabRect(expectedIndex);
    const auto destination = targetBar->mapToGlobal(QPoint(destinationLabel.left() + 2, destinationLabel.center().y()));
    const int expectedRowTop = targetBar->mapToGlobal(destinationLabel.topLeft()).y();
    QVERIFY(targetView->viewportRectGlobal().contains(destination));
    sendMouse(carrier, QEvent::MouseMove, destination);
    targetView = groupView(main, targetGroup); QVERIFY(targetView); targetBar = targetView->tabBar();
    const auto gap = targetView->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight); QCOMPARE(gap.top(), expectedRowTop);
    QVERIFY(targetView->viewportRectGlobal().contains(gap)); QCOMPARE(targetView->insertionIndex(destination), expectedIndex);
    for (int index = 0; index < targetBar->count(); ++index) {
        const auto rect = targetBar->displayTabRect(index);
        if (!rect.isEmpty()) QVERIFY(!rect.translated(targetBar->mapToGlobal(QPoint())).intersects(gap));
        QCOMPARE(targetBar->tabData(index).toUuid(), targets[index]);
    }
    QVERIFY(!labelPreview()); QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
    QCOMPARE(manager.model().group(targetGroup)->tabs, targets);
    auto expected = targets; expected.insert(expectedIndex, source);
    sendMouse(carrier, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QTRY_COMPARE(manager.model().windows().size(), 1);
    QVERIFY(!manager.model().window(carrierId)); QVERIFY(!manager.model().group(carrierGroup));
    QCOMPARE(manager.model().groupForTab(source), targetGroup); QCOMPARE(manager.model().group(targetGroup)->tabs, expected);
    QCOMPARE(manager.model().centralGroups(window), QVector<GroupId>{targetGroup});
    QCOMPARE(manager.model().tab(source)->activePane, sourcePane); QCOMPARE(manager.paneWidget(sourcePane), sourceContent.data());
    QVERIFY(sourceContent); QVERIFY(sourceContent->isVisible());
    for (int index = 0; index < targets.size(); ++index) {
        QCOMPARE(manager.model().tab(targets[index])->activePane, targetPanes[index]);
        QCOMPARE(manager.paneWidget(targetPanes[index]), targetContents[index].data());
    }
    if (expandByWheel) {
        targetView = groupView(main, targetGroup); QVERIFY(targetView);
        QVERIFY(targetView->tabBar()->height() > defaultTheme().metrics.tabHeight);
        for (int index = 0; index < targetView->count(); ++index)
            QVERIFY(!targetView->tabBar()->displayTabRect(index).isEmpty());
        targetView->scrollByWheel({0, 120});
        QTRY_COMPARE(targetView->tabBar()->height(), defaultTheme().metrics.tabHeight);
        QCOMPARE(manager.paneWidget(sourcePane), sourceContent.data());
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!carrier); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}

void TabStripLayoutTest::wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse_data()
{
    insertionPreviewAndHitTestingUseThePointerRow_data();
}

void TabStripLayoutTest::wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse()
{
    QFETCH(bool, bottom);
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
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
    QCOMPARE(view->tabBar()->height(), defaultTheme().metrics.tabHeight);
    view->scrollByWheel({0, -120}); QTRY_VERIFY(view->tabBar()->height() > defaultTheme().metrics.tabHeight);
    const auto created = manager.addTab(window, new QLabel("Created while expanded"), {"MMMM new tab", "label", {}}, group);
    QVERIFY(!created.isNull()); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QTRY_VERIFY(view->tabBar()->height() > defaultTheme().metrics.tabHeight);
    QCOMPARE(view->count(), 10);
    for (int index = 0; index < view->count(); ++index) QVERIFY(!view->tabBar()->displayTabRect(index).isEmpty());
    // A second, independent API mutation replaces the presentation again.
    QVERIFY(manager.setTabIconVisible(ids.first(), false)); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QVERIFY(view->tabBar()->height() > defaultTheme().metrics.tabHeight);
    for (int index = 0; index < ids.size(); ++index) {
        QCOMPARE(manager.model().tab(ids[index])->activePane, panes[index]);
        QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
    }
    view->scrollByWheel({0, 120}); QTRY_COMPARE(view->tabBar()->height(), defaultTheme().metrics.tabHeight);
    const auto collapsedTab = manager.addTab(window, new QLabel("Created while collapsed"), {"MMMM final tab", "label", {}}, group);
    QVERIFY(!collapsedTab.isNull()); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QCOMPARE(view->tabBar()->height(), defaultTheme().metrics.tabHeight);
    QCOMPARE(view->count(), 11); QVERIFY(manager.model().validate());
}

void TabStripLayoutTest::activeOnlyLaterRowDragNeverFallsBackToFirstRow_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("cancel");
    QTest::newRow("top-reorder") << false << false;
    QTest::newRow("bottom-reorder") << true << false;
    QTest::newRow("top-cancel") << false << true;
    QTest::newRow("bottom-cancel") << true << true;
}

void TabStripLayoutTest::activeOnlyLaterRowDragNeverFallsBackToFirstRow()
{
    QFETCH(bool, bottom); QFETCH(bool, cancel);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Active row drag", {480, 340}});
    QVector<TabId> ids; QVector<PaneId> panes; QVector<QPointer<QWidget>> contents;
    for (int index = 0; index < 12; ++index) {
        QPointer<QWidget> content = new QLabel(QString("Content %1").arg(index)); contents.append(content);
        const auto tab = manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}});
        ids.append(tab); panes.append(manager.model().tab(tab)->activePane);
    }
    auto* main = manager.windowWidget(window); manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(main));
    const auto group = manager.model().groupForTab(ids.first()); auto* view = groupView(main, group); QVERIFY(view);
    view->scrollByWheel({0, -120}); auto* bar = view->tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    int firstLater = 0;
    while (firstLater < bar->count() && bar->displayTabRect(firstLater).top() == 0) ++firstLater;
    QVERIFY(firstLater + 1 < bar->count());
    const int sourceIndex = firstLater + 1;
    QCOMPARE(bar->displayTabRect(sourceIndex).top(), bar->displayTabRect(firstLater).top());
    const auto source = ids[sourceIndex]; QVERIFY(manager.activateTab(source));
    view = groupView(main, group); QVERIFY(view); view->scrollByWheel({0, 120}); bar = view->tabBar();
    QTRY_COMPARE(bar->height(), defaultTheme().metrics.tabHeight);
    QVERIFY(bar->displayTabRect(0).isEmpty()); QVERIFY(!bar->displayTabRect(sourceIndex).isEmpty());
    QVector<TabId> originalVisible;
    for (int index = 0; index < bar->count(); ++index)
        if (!bar->displayTabRect(index).isEmpty()) originalVisible.append(bar->tabData(index).toUuid());
    QVERIFY(originalVisible.contains(source)); QVERIFY(originalVisible.contains(ids[firstLater]));
    auto withoutSource = originalVisible; withoutSource.removeOne(source);
    const auto before = manager.model().toJson();
    const auto start = bar->mapToGlobal(bar->displayTabRect(sourceIndex).center() - QPoint(12, 0));
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
    QTRY_VERIFY(labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(source), group); QVERIFY(contents[sourceIndex]->isVisible());
    view = groupView(main, group); QVERIFY(view); bar = view->tabBar();
    QVector<TabId> visibleDuringDrag;
    for (int index = 0; index < bar->count(); ++index)
        if (!bar->displayTabRect(index).isEmpty()) visibleDuringDrag.append(bar->tabData(index).toUuid());
    QCOMPARE(visibleDuringDrag, withoutSource);
    QVERIFY(bar->displayTabRect(sourceIndex).isEmpty()); QVERIFY(bar->displayTabRect(0).isEmpty());
    auto gap = view->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight); QVERIFY(view->viewportRectGlobal().contains(gap));
    const auto firstLabel = bar->displayTabRect(firstLater); QVERIFY(!firstLabel.isEmpty());
    const auto destination = bar->mapToGlobal(QPoint(firstLabel.left() + 2, firstLabel.center().y()));
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    sendMouse(receiver, QEvent::MouseMove, destination);
    view = groupView(main, group); QVERIFY(view); bar = view->tabBar();
    visibleDuringDrag.clear();
    for (int index = 0; index < bar->count(); ++index)
        if (!bar->displayTabRect(index).isEmpty()) visibleDuringDrag.append(bar->tabData(index).toUuid());
    QCOMPARE(visibleDuringDrag, withoutSource); QCOMPARE(view->insertionIndex(destination), firstLater);
    QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.model().windows().size(), 1);
    if (cancel) {
        QTest::keyClick(receiver, Qt::Key_Escape);
        QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.model().group(group)->tabs, ids);
    } else {
        auto expected = ids; expected.removeOne(source); expected.insert(firstLater, source);
        sendMouse(receiver, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(manager.model().group(group)->tabs, expected);
    }
    QTRY_VERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber()); QCOMPARE(manager.model().windows().size(), 1);
    view = groupView(main, group); QVERIFY(view); bar = view->tabBar();
    QCOMPARE(bar->height(), defaultTheme().metrics.tabHeight); QVERIFY(bar->displayTabRect(0).isEmpty());
    if (cancel) {
        QVector<TabId> restoredVisible;
        for (int index = 0; index < bar->count(); ++index)
            if (!bar->displayTabRect(index).isEmpty()) restoredVisible.append(bar->tabData(index).toUuid());
        QCOMPARE(restoredVisible, originalVisible);
    }
    for (int index = 0; index < ids.size(); ++index) {
        QCOMPARE(manager.model().tab(ids[index])->activePane, panes[index]);
        QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
    }
    QVERIFY(contents[sourceIndex]->isVisible()); QVERIFY(manager.model().validate());
}

QTEST_MAIN(TabStripLayoutTest)
#include "tab_strip_layout_test.moc"
