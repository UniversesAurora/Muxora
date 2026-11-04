#include "placeholder_content.hpp"
#include <window_system/interface_appearance.hpp>
#include <window_system/window_manager.hpp>

#include <QCoreApplication>
#include <QGuiApplication>

namespace muxora {
using namespace window_system;
QString notesText()
{
    return QCoreApplication::translate("Muxora",
        "Muxora / Workspace notes\n\nThis is a local editor. Type here and check that content survives moving between windows.\nSSH and a real terminal will follow after the window system is ready.\n\nTry these operations:\n  · Drag a tab onto the center or direction guide of a target group\n  · Drag to the desktop to create a floating window, then drag its title bar to dock it again\n  · Right-click a tab to auto-hide it in any sidebar or split its panes\n\nCtrl+Shift+P     Command palette\nCtrl+Shift+E     Split pane right\nCtrl+Shift+O     Split pane down\nCtrl+Alt+Arrow   Navigate tab groups\nAlt+Arrow        Navigate panes\nCtrl+Shift+D     Move to floating window\n");
}
QFont notesFont()
{
    auto font = QGuiApplication::font(); font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Microsoft YaHei"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace); font.setPointSizeF(11 * interfaceScale());
    return font;
}
QString welcomeSubtitle() { return QCoreApplication::translate("Muxora", "Arrange remote workspaces your way"); }
QString welcomeDescription()
{
    return QCoreApplication::translate("Muxora",
        "This is the first Muxora window system preview.\n\nDrag tabs, arrange tab groups, split panes, and auto-hide tabs in sidebars.\nDetach a floating window and dock it into another window.\n\nSessions, files, and transfers are layout placeholders. Remote operations will follow later.");
}
QString welcomeTip() { return QCoreApplication::translate("Muxora", "Right-click a tab or floating window title bar for more layout operations."); }
QVector<QPair<QString, QString>> welcomeActions()
{
    return {{QCoreApplication::translate("Muxora", "Open a workspace tab"), "host.newTab"},
        {QCoreApplication::translate("Muxora", "Show all commands"), "workbench.showCommandPalette"},
        {QCoreApplication::translate("Muxora", "Adjust window behavior"), "window.settings"}};
}
QString sessionsHeading() { return QCoreApplication::translate("Muxora", "Sessions · Layout preview"); }
QStringList sessionNames()
{
    return {QCoreApplication::translate("Muxora", "Development server"), QCoreApplication::translate("Muxora", "Test server"),
        QCoreApplication::translate("Muxora", "Personal host")};
}
QStringList fileColumns()
{
    return {QCoreApplication::translate("Muxora", "Name"), QCoreApplication::translate("Muxora", "Size"), QCoreApplication::translate("Muxora", "Type")};
}
QVector<QStringList> fileRows()
{
    QVector<QStringList> rows;
    const QStringList names = {"src", "docs", "resources", "CMakeLists.txt", "README.md", "LICENSE", "AGENTS.md"};
    for (int row = 0; row < names.size(); ++row)
        rows.append({names[row], row < 3 ? "—" : QCoreApplication::translate("Muxora", "Preview"),
                     row < 3 ? QCoreApplication::translate("Muxora", "Directory") : QCoreApplication::translate("Muxora", "File")});
    return rows;
}
QString fileAddressText()
{
    return QCoreApplication::translate("Muxora", "/workspace · File view preview");
}
QString fileInformationText(int count)
{
    return QCoreApplication::translate("Muxora", "%n items  ·  File operations are not connected yet", nullptr, count);
}
QString transfersText()
{
    return QCoreApplication::translate("Muxora", "Transfer list\n\nFile transfer is not connected yet.\nThis tab uses the same docking and window operations as other tabs.");
}

TabId addPlaceholderTab(WindowManager& windows, WindowId window, const TabOptions& options, GroupId group)
{
    return windows.addTab(window, options, group);
}
void populateInitialWorkspace(WindowManager& windows, WindowId window)
{
    const auto first = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Welcome"), "welcome", {}});
    addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Workspace 1"), "notes", {}});
    const auto fileTab = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Files"), "files", {}});
    windows.moveTab(fileTab, windows.model().groupForTab(first), Placement::Right);
    const auto sessionTab = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Sessions"), "sessions", {}});
    windows.moveTabToEdge(sessionTab, Edge::Left, true);
    const auto transferTab = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Transfers"), "transfers", {}});
    windows.moveTabToEdge(transferTab, Edge::Bottom);
    windows.activateTab(first);
}
} // namespace muxora
