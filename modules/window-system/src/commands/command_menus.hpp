// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
#include "menu_definition.hpp"
#include <QPointer>
#include <QObject>
#include <functional>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

namespace window_system {
class WindowManager;
struct MenuItemDescription;

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
    QAction* createAction(QWidget* owner, const MenuItemDescription& item);
    bool updateAction(QAction* action, const MenuItemDescription& item, bool root);
    bool syncMenu(QWidget* owner, const QVector<MenuItemDescription>& items);
};

} // namespace window_system
