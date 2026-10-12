// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    window_system::WindowManager windows;
    const auto root = windows.createWindow();
    if (!windows.resetToolbarWidth(root, "title")
        || !windows.execute("window.toolbar.resetSize", {{"strip", "caption-controls"}}, root)
        || windows.resetToolbarWidth(root, "missing")) return 12;
    auto* text = new QLabel("External installed-package contract");
    QPointer<QLabel> observer(text);
    const auto tab = windows.addTab(root, text, {"External content", "contract.text", {}});
    if (tab.isNull()) { delete text; return 1; }
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
    const auto secondFloating = windows.createWindow(window_system::WindowKind::Floating, {}, root);
    auto* secondText = new QLabel("Whole-window side insertion");
    QPointer<QLabel> secondObserver(secondText);
    const auto secondTab = windows.addTab(secondFloating, secondText, {"Second", "contract.text", {}});
    if (secondTab.isNull()) { delete secondText; return 9; }
    if (!windows.moveAllTabsToEdgeBar(secondFloating, root, window_system::Edge::Right, 0)) return 10;
    if (windows.model().autoHiddenTabs(root, window_system::Edge::Right) != QVector<window_system::TabId>{secondTab, tab}
        || windows.paneWidget(pane) != text) return 11;
    if (!windows.model().validate()) return 4;
    if (!windows.requestCloseWindow(root)) return 5;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return observer.isNull() && secondObserver.isNull() && windows.model().windows().isEmpty() ? 0 : 6;
}
