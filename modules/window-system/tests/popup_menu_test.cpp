// SPDX-License-Identifier: MIT
#include "commands/popup_menu.hpp"
#include "commands/command_menus.hpp"
#include "style/themed_controls.hpp"
#include "docking/tab_strip.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QMenuBar>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>
#include <QToolButton>
#include <QWidgetAction>
#include <QImage>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

using namespace window_system;

namespace {
QMenu* menuAt(QMenuBar* bar, const QStringList& path)
{
    for (auto* menu : bar->findChildren<QMenu*>())
        if (menu->property("commandMenuPath").toStringList() == path) return menu;
    return nullptr;
}
class MutationProbe final : public QObject {
public:
    int added = 0, removed = 0;
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::ActionAdded) ++added;
        if (event->type() == QEvent::ActionRemoved) ++removed;
        return false;
    }
};
}

class PopupMenuTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void positionPrefersLeftBelowAndFallsBackRightAbove_data();
    void positionPrefersLeftBelowAndFallsBackRightAbove();
    void dropdownUsesItsActualScreenAndNativeSquareCorners();
    void comboDropdownUsesTheSameSquareAnchoredPopupPolicy();
    void cascadingMenusDoNotRemoveItemsOrToggleParentSeparators();
    void menuBarContextUsesCapturedGroupAndGlobalAppearanceCommands();
    void popupHasOneSquareClientBorderAndNoNativeRim();
    void tabListShowsEachAssignedColorBeforeItsContent();
    void tabBarContextExposesMultilineAndItsOwnPlacement_data();
    void tabBarContextExposesMultilineAndItsOwnPlacement();
    void rightClickUsesItsTargetWithoutActivatingIt_data();
    void rightClickUsesItsTargetWithoutActivatingIt();
};

void PopupMenuTest::rightClickUsesItsTargetWithoutActivatingIt_data()
{
    QTest::addColumn<bool>("pinned");
    QTest::newRow("central") << false; QTest::newRow("pinned-sidebar") << true;
}
void PopupMenuTest::rightClickUsesItsTargetWithoutActivatingIt()
{
    QFETCH(bool, pinned);
    WindowManager manager; const auto window = manager.createWindow();
    const auto a = manager.addTab(window, new QLabel("Active"), {"Active", "label", {}});
    const auto b = manager.addTab(window, new QLabel("Context target"), {"Context target", "label", {}});
    if (pinned) {
        QVERIFY(manager.moveTabToEdge(a, Edge::Left, true));
        QVERIFY(manager.moveTab(b, manager.model().groupForTab(a)));
    }
    QVERIFY(manager.activateTab(a)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    GroupTabs* view = nullptr;
    for (auto* widget : manager.windowWidget(window)->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget); group && group->isVisible()
            && group->property("groupId").toUuid() == manager.model().groupForTab(a)) view = group;
    QVERIFY(view); auto* bar = view->tabBar();
    const auto point = bar->displayTabRect(1).center(); const auto global = bar->mapToGlobal(point);
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &press);
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->activeTab, a);
    QCOMPARE(view->currentIndex(), 0);
    bool sawMenu = false; QVector<TabId> closed;
    manager.setCloseGuard([&](const QVector<TabId>& ids) { closed = ids; return true; });
    QTimer::singleShot(0, &manager, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) return;
        sawMenu = true;
        QCOMPARE(manager.model().group(manager.model().groupForTab(a))->activeTab, a);
        auto* action = menu->findChild<QAction*>("workspace.closeTab");
        if (!action) { menu->close(); return; }
        menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Return);
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, point, global);
    QApplication::sendEvent(bar, &context);
    QVERIFY(sawMenu); QTRY_VERIFY(!manager.model().tab(b));
    QCOMPARE(closed, QVector<TabId>{b});
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->activeTab, a);
}

void PopupMenuTest::positionPrefersLeftBelowAndFallsBackRightAbove_data()
{
    QTest::addColumn<QRect>("anchor"); QTest::addColumn<QSize>("size");
    QTest::addColumn<QRect>("available"); QTest::addColumn<QPoint>("expected");
    const QRect screen(0, 0, 1000, 800);
    QTest::newRow("left-below") << QRect(120, 90, 80, 24) << QSize(240, 170) << screen << QPoint(120, 114);
    QTest::newRow("right-below") << QRect(950, 90, 40, 24) << QSize(240, 170) << screen << QPoint(750, 114);
    QTest::newRow("left-above") << QRect(120, 740, 80, 24) << QSize(240, 170) << screen << QPoint(120, 570);
    QTest::newRow("right-above") << QRect(950, 740, 40, 24) << QSize(240, 170) << screen << QPoint(750, 570);
    QTest::newRow("negative-monitor") << QRect(-1900, -720, 80, 24) << QSize(240, 170)
        << QRect(-1920, -800, 1920, 800) << QPoint(-1900, -696);
    QTest::newRow("oversize-clamp") << QRect(120, 90, 80, 24) << QSize(1240, 1170) << screen << QPoint(0, 0);
    QTest::newRow("trigger-outside-workarea") << QRect(-10, -10, 5, 5) << QSize(240, 170) << screen << QPoint(0, 0);
}

void PopupMenuTest::positionPrefersLeftBelowAndFallsBackRightAbove()
{
    QFETCH(QRect, anchor); QFETCH(QSize, size); QFETCH(QRect, available); QFETCH(QPoint, expected);
    QCOMPARE(popupMenuPosition(anchor, size, available), expected);
}

void PopupMenuTest::dropdownUsesItsActualScreenAndNativeSquareCorners()
{
    QWidget root; root.resize(320, 200); root.show(); QVERIFY(QTest::qWaitForWindowExposed(&root));
    DismissOnlyMenu menu(&root); applyTheme(&menu);
    menu.addAction("First"); menu.addAction("Second");
    const auto area = root.screen()->availableGeometry();
    const QRect trigger(area.right() - 32, area.bottom() - 30, 30, 24);
    setPopupAnchor(&menu, trigger); menu.popup(trigger.bottomLeft() + QPoint(0, 1));
    QVERIFY(QTest::qWaitForWindowExposed(&menu));
    QCOMPARE(menu.pos(), QPoint(trigger.right() + 1 - menu.width(), trigger.top() - menu.height()));
    QVERIFY(area.contains(menu.frameGeometry())); QVERIFY(menu.mask().isEmpty());
#ifdef Q_OS_WIN
    DWORD corner = 0;
    const auto result = DwmGetWindowAttribute(reinterpret_cast<HWND>(menu.effectiveWinId()), DWORD(33), &corner, sizeof(corner));
    if (result == S_OK) QCOMPARE(corner, DWORD(1));
    else QCOMPARE(result, E_INVALIDARG);
#endif
    menu.close(); QVERIFY(!menu.isVisible());
}

void PopupMenuTest::comboDropdownUsesTheSameSquareAnchoredPopupPolicy()
{
    QWidget root(nullptr, Qt::Tool | Qt::FramelessWindowHint); applyTheme(&root); root.resize(320, 200);
    ThemedComboBox combo(&root); combo.addItems({"First", "Second", "Third", "Fourth"});
    combo.setGeometry(230, 170, 80, 24);
    auto* popup = combo.view()->window(); QVERIFY(popup && popup != &root);
    popup->setMinimumWidth(240);
    const auto available = root.screen()->availableGeometry();
    root.move(available.right() + 1 - root.width(), available.bottom() + 1 - root.height());
    root.show(); QVERIFY(QTest::qWaitForWindowExposed(&root));
    combo.showPopup(); QVERIFY(QTest::qWaitForWindowExposed(popup));
    // The list's window is a child of the combo box: preparing its frame must
    // not turn the combo box or its siblings into native child windows.
    for (auto* child : root.findChildren<QWidget*>())
        if (!child->isWindow()) QVERIFY2(!child->internalWinId() && !child->testAttribute(Qt::WA_NativeWindow), child->metaObject()->className());
    const QRect trigger(combo.mapToGlobal(QPoint()), combo.size());
    QVERIFY(popup->width() > trigger.width()); QVERIFY(popup->height() > available.bottom() - trigger.bottom());
    QCOMPARE(popup->pos(), QPoint(trigger.right() + 1 - popup->width(), trigger.top() - popup->height()));
    QVERIFY(available.contains(popup->frameGeometry())); QVERIFY(popup->mask().isEmpty());
#ifdef Q_OS_WIN
    DWORD corner = 0;
    const auto result = DwmGetWindowAttribute(reinterpret_cast<HWND>(popup->effectiveWinId()), DWORD(33), &corner, sizeof(corner));
    if (result == S_OK) QCOMPARE(corner, DWORD(1));
    else QCOMPARE(result, E_INVALIDARG);
#endif
    combo.hidePopup(); QVERIFY(!popup->isVisible());
    root.move(available.topLeft() + QPoint(20, 20)); combo.move(0, 0);
    combo.showPopup(); QVERIFY(QTest::qWaitForWindowExposed(popup));
    QCOMPARE(popup->pos(), combo.mapToGlobal(QPoint(0, combo.height())));
    QVERIFY(popup->mask().isEmpty());
    combo.hidePopup(); QCOMPARE(combo.currentIndex(), 0);
}

void PopupMenuTest::cascadingMenusDoNotRemoveItemsOrToggleParentSeparators()
{
    WindowManager manager;
    const auto first = manager.createWindow(); manager.addTab(first, new QLabel("A"), {"A", "label", {}});
    const auto second = manager.createWindow(); manager.addTab(second, new QLabel("B"), {"B", "label", {}});
    const auto floating = manager.createWindow(WindowKind::Floating, {}, first);
    manager.addTab(floating, new QLabel("C"), {"C", "label", {}});
    manager.showWindow(first); auto* window = manager.windowWidget(first);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* bar = window->findChild<QMenuBar*>("MenuStrip"); QVERIFY(bar);
    auto* parent = menuAt(bar, {"窗口"}); auto* child = menuAt(bar, {"窗口", "切换窗口"});
    auto* grandchild = menuAt(bar, {"窗口", "切换窗口", "所属窗口族"});
    QVERIFY(parent && child && grandchild);
    parent->popup(bar->mapToGlobal(QPoint(0, bar->height())));
    QVERIFY(QTest::qWaitForWindowExposed(parent));
    const auto geometry = parent->geometry(); const auto actions = parent->actions();
    MutationProbe probe; parent->installEventFilter(&probe);
    std::vector<std::unique_ptr<QSignalSpy>> separatorChanges;
    std::vector<std::unique_ptr<QSignalSpy>> windowChanges;
    for (auto* action : actions) {
        if (action->isSeparator()) separatorChanges.push_back(std::make_unique<QSignalSpy>(action, &QAction::changed));
        if (action->objectName() == "WindowListItem") windowChanges.push_back(std::make_unique<QSignalSpy>(action, &QAction::changed));
    }
    QCOMPARE(windowChanges.size(), std::size_t(3));
    for (int iteration = 0; iteration < 3; ++iteration) {
        parent->setActiveAction(child->menuAction()); child->popup(parent->mapToGlobal(parent->actionGeometry(child->menuAction()).topRight()));
        QVERIFY(QTest::qWaitForWindowExposed(child));
        child->setActiveAction(grandchild->menuAction()); grandchild->popup(child->mapToGlobal(child->actionGeometry(grandchild->menuAction()).topRight()));
        QVERIFY(QTest::qWaitForWindowExposed(grandchild));
        QCOMPARE(parent->actions(), actions); QCOMPARE(parent->geometry(), geometry);
        QCOMPARE(parent->activeAction(), child->menuAction());
        grandchild->hide(); child->hide();
    }
    QCOMPARE(probe.added, 0); QCOMPARE(probe.removed, 0);
    for (const auto& spy : separatorChanges) QCOMPARE(spy->count(), 0);
    for (const auto& spy : windowChanges) QCOMPARE(spy->count(), 0);
    parent->removeEventFilter(&probe); parent->close(); QVERIFY(manager.model().validate().ok);
}

void PopupMenuTest::menuBarContextUsesCapturedGroupAndGlobalAppearanceCommands()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("A"), {"A", "label", {}});
    manager.showWindow(window); auto* view = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(view));
    auto* bar = view->findChild<QMenuBar*>("MenuStrip"); QVERIFY(bar);
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5), bar->mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(bar, &event);
    auto* menu = view->findChild<QMenu*>("TabPresentationMenu"); QVERIFY(menu && menu->isVisible());
    const auto group = manager.model().groupForTab(tab);
    auto* icons = menu->findChild<QAction*>("presentation.toggleIcons");
    auto* plus = menu->findChild<QAction*>("presentation.toggleNewTabButton");
    auto* numbers = menu->findChild<QAction*>("presentation.toggleNumbers");
    auto* close = menu->findChild<QAction*>("group.close");
    QVERIFY(icons && plus && numbers && close);
    QVERIFY(icons->isChecked()); QVERIFY(plus->isChecked()); QVERIFY(!numbers->isChecked()); QVERIFY(close->isEnabled());
    icons->trigger(); QTRY_VERIFY(!manager.tabAppearance().showIcons);
    QVERIFY(manager.model().group(group)); QCOMPARE(manager.model().groupForTab(tab), group);
    menu->close(); QVERIFY(manager.model().validate().ok);
}

void PopupMenuTest::popupHasOneSquareClientBorderAndNoNativeRim()
{
    DismissOnlyMenu menu;
    auto theme = defaultTheme(); theme.surface = QColor("#eef3e4"); theme.border = QColor("#514465");
    applyTheme(&menu, theme); menu.addAction("First"); menu.addAction("Second");
    menu.popup(QPoint(100, 100)); QVERIFY(QTest::qWaitForWindowExposed(&menu));
    const auto image = menu.grab().toImage(); const auto ratio = image.devicePixelRatio();
    const auto sample = [&](QPoint point) { return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio)); };
    QCOMPARE(sample(QPoint(0, menu.height() / 2)), theme.border);
    QCOMPARE(sample(QPoint(menu.width() - 1, menu.height() / 2)), theme.border);
    QCOMPARE(sample(QPoint(menu.width() / 2, 0)), theme.border);
    QCOMPARE(sample(QPoint(menu.width() / 2, menu.height() - 1)), theme.border);
    QCOMPARE(sample(QPoint(1, menu.height() / 2)), theme.surface);
    QCOMPARE(sample(QPoint(menu.width() - 2, menu.height() / 2)), theme.surface);
    QCOMPARE(sample(QPoint(menu.width() / 2, 1)), theme.surface);
    QCOMPARE(sample(QPoint(menu.width() / 2, menu.height() - 2)), theme.surface);
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(menu.effectiveWinId());
    QVERIFY(!(GetWindowLongPtrW(handle, GWL_STYLE) & (WS_BORDER | WS_DLGFRAME | WS_THICKFRAME)));
    QVERIFY(!(GetWindowLongPtrW(handle, GWL_EXSTYLE) & (WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE)));
    RECT outer{}, client{}; QVERIFY(GetWindowRect(handle, &outer)); QVERIFY(GetClientRect(handle, &client));
    POINT origin{}; QVERIFY(ClientToScreen(handle, &origin));
    QCOMPARE(origin.x, outer.left); QCOMPARE(origin.y, outer.top);
    QCOMPARE(client.right, outer.right - outer.left); QCOMPARE(client.bottom, outer.bottom - outer.top);
    COLORREF border = 0;
    const auto result = DwmGetWindowAttribute(handle, DWORD(34), &border, sizeof(border));
    if (result == S_OK) QCOMPARE(border, COLORREF(0xfffffffe));
    else QCOMPARE(result, E_INVALIDARG);
#endif
    menu.close();
}

void PopupMenuTest::tabListShowsEachAssignedColorBeforeItsContent()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{};
    auto custom = appearance.colors[0]; custom.id = "custom"; custom.name = "Custom";
    custom.groupInactive.line = QColor("#14a083"); appearance.colors.append(custom);
    tabs.setAppearance(appearance);
    const QStringList colors{"red", {}, "custom"};
    for (int index = 0; index < colors.size(); ++index) {
        const auto id = QUuid::createUuid();
        tabs.addTab(new QLabel("Content"), {}, QStringLiteral("Entry %1").arg(index), id);
        TabRecord record; record.id = id; record.colorId = colors[index]; tabs.setTabMetadata(index, record);
    }
    auto theme = defaultTheme(); theme.surface = QColor("#edf2f7"); theme.selection = QColor("#bdd7ef");
    applyTheme(&tabs, theme); tabs.resize(620, 240); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* button = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    auto* menu = tabs.findChild<QMenu*>(); QVERIFY(menu);
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    int index = 0;
    for (auto* action : menu->actions()) {
        auto* item = qobject_cast<QWidgetAction*>(action); QVERIFY(item);
        auto* row = item->defaultWidget(); QVERIFY(row);
        QColor expected;
        for (const auto& preset : appearance.colors) if (preset.id == colors[index]) expected = preset.groupInactive.line;
        QCOMPARE(row->property("tabColorMarker").value<QColor>(), expected);
        auto checkMarker = [&] {
            const auto pixels = row->grab().toImage(); const auto scale = pixels.devicePixelRatio();
            const auto background = row->property("emphasized").toBool() ? theme.selection : theme.surface;
            for (int y = 0; y < row->height(); ++y) {
                QCOMPARE(pixels.pixelColor(qRound(3 * scale), qRound(y * scale)),
                    expected.isValid() ? expected : background);
                QCOMPARE(pixels.pixelColor(qRound(4 * scale), qRound(y * scale)), background);
            }
        };
        checkMarker();
        menu->setActiveAction(action); QCoreApplication::processEvents(); checkMarker();
        ++index;
    }
    QCOMPARE(index, 3); QCOMPARE(tabs.currentIndex(), 0); menu->close();
}

void PopupMenuTest::tabBarContextExposesMultilineAndItsOwnPlacement_data()
{
    QTest::addColumn<bool>("pinned");
    QTest::newRow("ordinary") << false; QTest::newRow("pinned") << true;
}

void PopupMenuTest::tabBarContextExposesMultilineAndItsOwnPlacement()
{
    QFETCH(bool, pinned);
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Target"), {"Target", "label", {}});
    if (pinned) QVERIFY(manager.moveTabToEdge(tab, Edge::Left, true));
    const auto group = manager.model().groupForTab(tab);
    manager.showWindow(window); auto* view = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(view));
    const auto open = [&] {
        manager.showGroupMenu(group, view->mapToGlobal(QPoint(100, 100)));
        return view->findChild<QMenu*>("TabBarContextMenu");
    };
    QPointer<QMenu> menu = open(); QVERIFY(menu && menu->isVisible());
    const auto iconCommand = pinned ? QStringLiteral("presentation.toggleSidebarIcons") : QStringLiteral("presentation.toggleIcons");
    const auto numberCommand = pinned ? QStringLiteral("presentation.toggleSidebarNumbers") : QStringLiteral("presentation.toggleNumbers");
    auto* icons = menu->findChild<QAction*>(iconCommand);
    auto* numbers = menu->findChild<QAction*>(numberCommand);
    QVERIFY(icons && numbers); QCOMPARE(icons->isChecked(), !pinned); QVERIFY(!numbers->isChecked());
    QVERIFY(!menu->findChild<QAction*>(pinned ? QStringLiteral("presentation.toggleIcons") : QStringLiteral("presentation.toggleSidebarIcons")));
    auto* multiline = menu->findChild<QAction*>("presentation.toggleMultiline");
    auto* wheel = menu->findChild<QAction*>("presentation.toggleWheelExpandRows");
    QVERIFY(multiline && wheel); QVERIFY(!multiline->isChecked()); QVERIFY(!wheel->isEnabled());
    multiline->trigger(); QTRY_VERIFY(manager.tabAppearance().multiline); menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    menu = open(); QVERIFY(menu && menu->isVisible());
    wheel = menu->findChild<QAction*>("presentation.toggleWheelExpandRows");
    QVERIFY(wheel && wheel->isEnabled()); wheel->trigger(); QTRY_VERIFY(manager.tabAppearance().wheelExpandRows);
    const auto prefix = pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement");
    auto* top = menu->findChild<QAction*>(prefix + ".top");
    auto* bottom = menu->findChild<QAction*>(prefix + ".bottom");
    QVERIFY(top && bottom); QVERIFY(top->isChecked()); QVERIFY(!bottom->isChecked());
    bottom->trigger();
    QTRY_COMPARE(pinned ? manager.tabAppearance().pinnedPlacement : manager.tabAppearance().centralPlacement, TabBarPlacement::Bottom);
    QCOMPARE(pinned ? manager.tabAppearance().centralPlacement : manager.tabAppearance().pinnedPlacement, TabBarPlacement::Top);
    QCOMPARE(manager.model().groupForTab(tab), group); QVERIFY(manager.model().validate());
    menu->close();
}

QTEST_MAIN(PopupMenuTest)
#include "popup_menu_test.moc"
