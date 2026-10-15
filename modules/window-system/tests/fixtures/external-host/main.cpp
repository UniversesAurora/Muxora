// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <window_system/toolbar_layout.hpp>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QJsonArray>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    window_system::WindowManager windows;
    const auto root = windows.createWindow();
    {
        window_system::FloatingPanel panel(windows.windowWidget(root), {false, false});
        if (panel.options().dismissOnOutsidePress || panel.isModal()) return 101;
        panel.setOptions({true, true});
        if (!panel.options().dismissOnOutsidePress || !panel.isModal()) return 102;
    }
    if (!windows.setStatusItemVisible(root, "workspace", false)
        || windows.statusItemVisible(root, "workspace")
        || !windows.setStatusItemVisible(root, "workspace", true)) return 103;
    if (!windows.commands().add({"external.new", "External new content", "Host", {}, {},
        [&windows](const window_system::CommandContext& context) {
            auto* content = new QLabel("Created through installed API");
            if (!windows.addTab(context.window, content, {"New", "contract.text", {}}, context.group).isNull()) return true;
            delete content; return false;
        }}) || !windows.setTabCreationCommands("external.new", {"external.new"})) return 104;
    auto behavior = windows.behavior(); behavior.newTabPosition = window_system::NewTabPosition::Beginning;
    windows.setBehavior(behavior);
    if (!windows.requestNewTab(root)) return 105;
    const auto firstCreated = windows.context(root).tab;
    if (!windows.requestNewTab(root)) return 106;
    const auto nextCreated = windows.context(root).tab;
    if (windows.model().group(windows.context(root).group)->tabs != QVector<window_system::TabId>{nextCreated, firstCreated}
        || !windows.closeTabs({firstCreated, nextCreated})) return 107;
    behavior.newTabPosition = window_system::NewTabPosition::AfterActive; windows.setBehavior(behavior);
    if (!windows.setTabCreationCommands({}, {})) return 108;
    auto findToolbar = [&windows](window_system::WindowId id) -> QWidget* {
        for (auto* widget : windows.windowWidget(id)->findChildren<QWidget*>("CommandStrip"))
            if (widget->property("stripId").toString() == "workspace") return widget;
        return nullptr;
    };
    QPointer<QWidget> toolbar = findToolbar(root);
    if (!toolbar || !windows.moveToolbar(root, "workspace", window_system::ToolbarArea::Bottom, 0)) return 90;
    if (!toolbar || findToolbar(root) != toolbar.data()
        || toolbar->parentWidget()->parentWidget()->property("toolbarArea").toString() != "bottom") return 91;
    int menuExecutions = 0;
    if (!windows.commands().add({"external.menu", "External menu action", "Host", {}, {},
        [&menuExecutions](const window_system::CommandContext&) { ++menuExecutions; return true; }, {"工具", "外部宿主"}, 20})) return 80;
    auto* menuBar = windows.windowWidget(root)->findChild<QMenuBar*>("MenuStrip");
    if (!menuBar) return 81;
    auto* fileMenu = menuBar->findChild<QMenu*>("文件");
    if (!fileMenu || !QMetaObject::invokeMethod(fileMenu, "aboutToShow")) return 82;
    auto* externalAction = menuBar->findChild<QAction*>("external.menu");
    if (!externalAction || !externalAction->isVisible() || !externalAction->shortcut().isEmpty()) return 83;
    externalAction->trigger(); QCoreApplication::processEvents();
    if (menuExecutions != 1) return 84;
    if (!windows.resetToolbarWidth(root, "title")
        || !windows.execute("window.toolbar.resetSize", {{"strip", "caption-controls"}}, root)
        || windows.resetToolbarWidth(root, "missing")) return 12;
    auto* text = new QLabel("External installed-package contract");
    QPointer<QLabel> observer(text);
    const auto tab = windows.addTab(root, text, {"External content", "contract.text", {}});
    if (tab.isNull()) { delete text; return 1; }
    if (!windows.setTabColor(tab, "red") || windows.setTabColor(tab, "missing")) return 110;
    const auto alpha = windows.addTab(root, new QLabel("Alpha"), {"Alpha", "contract.text", {}});
    const auto omega = windows.addTab(root, new QLabel("Omega"), {"Omega", "contract.text", {}});
    const auto originalGroup = windows.model().groupForTab(tab);
    if (alpha.isNull() || omega.isNull() || !windows.sortTabs(originalGroup, window_system::TabSortOrder::AlphabeticalAscending)
        || windows.model().group(originalGroup)->tabs != QVector<window_system::TabId>{alpha, tab, omega}) return 111;
    const auto staging = windows.createWindow();
    if (!windows.moveGroupToWindow(originalGroup, staging) || windows.paneWidget(windows.model().tab(tab)->activePane) != text
        || windows.model().group(originalGroup)->windowId != staging
        || !windows.moveGroupToWindow(originalGroup, root) || windows.model().groupForTab(tab) != originalGroup
        || !windows.requestCloseWindow(staging) || !windows.closeTabs({alpha, omega})) return 112;
    if (windows.model().tab(tab)->colorId != "red" || !windows.setTabColor(tab, {})) return 113;
    if (!windows.setTabTopBarSharing(tab, window_system::PaneBarSharing::PerPane)
        || !windows.setTabBottomBarSharing(tab, window_system::PaneBarSharing::Shared)
        || !windows.setTabBarsVisible(root, false) || windows.tabBarsVisible(root)
        || !windows.execute("window.tabBars.toggle", {}, root) || !windows.tabBarsVisible(root)) return 13;
    if (!windows.setSideBarsVisible(root, false) || windows.sideBarsVisible(root)
        || !windows.execute("window.sideBars.toggle", {}, root) || !windows.sideBarsVisible(root)) return 14;
    const auto floating = windows.detachTab(tab);
    if (floating.isNull() || !observer || !windows.promoteWindow(floating)) return 2;
    if (!windows.demoteWindow(floating, root)) return 3;
    const auto pane = windows.model().tab(tab)->activePane;
    if (!windows.moveTabToEdgeBar(tab, root, window_system::Edge::Right, 0)) return 7;
    if (windows.paneWidget(pane) != text || windows.model().autoHiddenTabs(root, window_system::Edge::Right)
        != QVector<window_system::TabId>{tab}) return 8;
    if (windows.peekSize(tab) != QSize(250, 170) || !windows.setPeekSize(tab, QSize(360, 210))
        || !windows.execute("tab.peek.setSize", {{"tab", tab}, {"width", 420}, {"height", 240}}, root)
        || windows.peekSize(tab) != QSize(420, 240) || windows.setPeekSize(tab, QSize(0, 0))) return 95;
    if (!windows.setPeekSize(tab, window_system::Edge::Left, QSize(430, 260))
        || windows.peekSize(tab, window_system::Edge::Right) != QSize(430, 260)
        || !windows.setPeekSizePolicy(tab, window_system::Edge::Top, {510, std::nullopt})
        || windows.peekSizePolicy(tab, window_system::Edge::Bottom).width != std::optional<int>(510)
        || windows.peekSizePolicy(tab, window_system::Edge::Bottom).height.has_value()) return 109;
    const auto secondFloating = windows.createWindow(window_system::WindowKind::Floating, {}, root);
    auto* secondText = new QLabel("Whole-window side insertion");
    QPointer<QLabel> secondObserver(secondText);
    const auto secondTab = windows.addTab(secondFloating, secondText, {"Second", "contract.text", {}});
    if (secondTab.isNull()) { delete secondText; return 9; }
    if (!windows.moveAllTabsToEdgeBar(secondFloating, root, window_system::Edge::Right, 0)) return 10;
    if (windows.model().autoHiddenTabs(root, window_system::Edge::Right) != QVector<window_system::TabId>{secondTab, tab}
        || windows.paneWidget(pane) != text) return 11;
    const auto secondPane = windows.model().tab(secondTab)->activePane;
    if (!windows.movePane(secondTab, secondPane, tab)
        || windows.model().tab(secondTab) || windows.model().tabForPane(secondPane) != tab
        || windows.paneWidget(secondPane) != secondText
        || !windows.swapPanes(tab, pane, secondPane)) return 15;
    const auto movementTarget = windows.createWindow();
    if (!windows.movePaneToWindow(tab, secondPane, movementTarget)
        || windows.model().tabForPane(secondPane) == tab || windows.paneWidget(secondPane) != secondText
        || !windows.execute("workspace.moveTabToWindow", {{"targetWindow", root}}, movementTarget)) return 16;
    if (!windows.requestCloseWindow(movementTarget)) return 17;
    if (!windows.model().validate()) return 4;
    const auto saved = windows.saveLayout();
    QPointer<QLabel> restoredFirst, restoredSecond;
    windows.setContentFactory([&](const window_system::TabOptions& options, window_system::PaneId id) -> QWidget* {
        auto* label = new QLabel(options.title);
        if (id == pane) restoredFirst = label;
        if (id == secondPane) restoredSecond = label;
        return label;
    });
    if (!windows.restoreLayout(saved) || !toolbar || !restoredFirst || !restoredSecond
        || findToolbar(root) != toolbar.data() || windows.paneWidget(pane) != restoredFirst.data()
        || windows.paneWidget(secondPane) != restoredSecond.data()) return 92;
    if (toolbar->parentWidget()->parentWidget()->property("toolbarArea").toString() != "bottom") return 93;
    auto toolbarLayout = [](const QJsonObject& snapshot, window_system::WindowId id) {
        for (const auto& entry : snapshot.value("windows").toArray()) {
            const auto object = entry.toObject();
            if (object.value("id").toString() == id.toString(QUuid::WithoutBraces)) return object.value("strips").toObject();
        }
        return QJsonObject{};
    };
    if (toolbarLayout(windows.saveLayout(), root) != toolbarLayout(saved, root)) return 94;
    if (windows.peekSize(tab, window_system::Edge::Right) != QSize(430, 260)
        || windows.peekSizePolicy(tab, window_system::Edge::Bottom).width != std::optional<int>(510)
        || !windows.resetPeekSize(tab)
        || windows.peekSize(tab) != QSize(250, 170) || windows.paneWidget(pane) != restoredFirst.data()) return 96;
    if (!windows.requestCloseWindow(root)) return 5;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return observer.isNull() && secondObserver.isNull() && restoredFirst.isNull() && restoredSecond.isNull()
        && toolbar.isNull() && windows.model().windows().isEmpty() ? 0 : 6;
}
