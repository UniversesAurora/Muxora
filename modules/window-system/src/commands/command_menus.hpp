// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
#include <QList>
#include <QHash>
#include <QPointer>
#include <QObject>
#include <functional>

class QAction;
class QMenu;
class QMenuBar;

namespace window_system {
class WindowManager;

enum class CommandMenuSurface { Application, FloatingApplication, Tab, Pane, Window, Group, TabFolder };

// Built-in menu taxonomy, also used to audit complete command coverage.
[[nodiscard]] QStringList declaredMenuCommands(CommandMenuSurface surface);
[[nodiscard]] QString applicationMenuExclusion(const QString& commandId);
[[nodiscard]] QString floatingMenuExclusion(const QString& commandId);
void refreshCommandAction(WindowManager* manager, QAction* action, const CommandContext& context);
void populateContextMenu(QMenu* menu, CommandMenuSurface surface, WindowManager* manager,
                         const CommandContext& captured);

// Owns presentation objects only. Commands stay in the host's registry; late
// registrations are added incrementally without destroying a displayed menu.
class ApplicationCommandMenus final : public QObject {
public:
    ApplicationCommandMenus(QMenuBar* bar, WindowManager* manager, WindowId window,
                            CommandMenuSurface surface = CommandMenuSurface::Application);
    ~ApplicationCommandMenus() override = default;
    void refresh();
    std::function<void()> structureChanged;
private:
    QPointer<QMenuBar> bar_;
    QPointer<WindowManager> manager_;
    WindowId window_;
    CommandMenuSurface surface_;
    QList<QPointer<QAction>> actions_;
    QList<QPointer<QMenu>> menus_;
    QPointer<QAction> windowListSeparator_;
    QHash<WindowId, QPointer<QAction>> windowActions_;
    QMenu* menuAtPath(const QStringList& path);
    bool removeUnusedContributions();
    void addContributions();
    void refreshWindowList();
};

} // namespace window_system
