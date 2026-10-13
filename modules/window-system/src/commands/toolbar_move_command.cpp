// SPDX-License-Identifier: MIT
#include "toolbar_move_command.hpp"
#include "popup_menu.hpp"
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>
#include <QCursor>
#include <QJsonArray>
#include <QMetaType>
#include <QPointer>

namespace window_system {
namespace {

bool integerArgument(const QVariantMap& args, const char* key, int minimum, int& result)
{
    if (!args.contains(key)) return true;
    const auto value = args.value(key);
    bool valid = false;
    const auto candidate = value.toInt(&valid);
    if (!valid || value.metaType().id() == QMetaType::Bool || candidate < minimum
        || value.toDouble() != candidate) return false;
    result = candidate;
    return true;
}

QString chooseStrip(WorkspaceWindow* window, const Theme& theme)
{
    DismissOnlyMenu picker;
    picker.setObjectName("ToolbarMovePicker"); applyTheme(&picker, theme);
    for (const auto& id : window->strips()->stripIds()) {
        auto* action = picker.addAction(window->strips()->stripTitle(id)); action->setData(id);
    }
    QObject::connect(window, &QObject::destroyed, &picker, &QMenu::close);
    const auto* selected = picker.exec(QCursor::pos());
    return selected ? selected->data().toString() : QString{};
}

QVariantMap chooseDestination(WorkspaceWindow* window, const Theme& theme)
{
    DismissOnlyMenu picker;
    picker.setObjectName("ToolbarMoveTargetPicker"); applyTheme(&picker, theme);
    const auto layout = window->strips()->saveLayout();
    for (const auto* area : {"top", "bottom"}) {
        const bool top = QString::fromLatin1(area) == "top";
        auto* menu = picker.addMenu(top ? QStringLiteral("顶部") : QStringLiteral("底部"));
        const auto rows = layout.value(top ? "rows" : "bottomRows").toArray();
        const int count = int(rows.size());
        for (int row = 0; row <= count; ++row) {
            if (row < count && rows.at(row).toArray().isEmpty()) continue;
            auto* action = menu->addAction(row == count ? QStringLiteral("新一行") : QStringLiteral("第 %1 行").arg(row + 1));
            action->setData(QVariantMap{{"area", QString::fromLatin1(area)}, {"row", row}});
        }
    }
    QObject::connect(window, &QObject::destroyed, &picker, &QMenu::close);
    const auto* selected = picker.exec(QCursor::pos());
    return selected ? selected->data().toMap() : QVariantMap{};
}

bool moveToolbar(WindowManager* manager, const CommandContext& context)
{
    const QPointer<WindowManager> owner(manager);
    QPointer<WorkspaceWindow> window = static_cast<WorkspaceWindow*>(manager->windowWidget(context.window));
    if (!window || !window->strips()) return false;
    const auto stillOwned = [&] { return owner && window && owner->windowWidget(context.window) == window.data(); };
    QString strip = context.args.value("strip").toString();
    if (!context.args.contains("strip")) {
        strip = chooseStrip(window, manager->theme());
        if (!stillOwned()) return false;
        if (strip.isEmpty()) return true;
    }
    if (!window->strips()->stripIds().contains(strip)) return false;
    auto area = context.args.value("area").toString();
    int row = 0, index = -1;
    if (!integerArgument(context.args, "row", 0, row) || !integerArgument(context.args, "index", -1, index)) return false;
    if (!context.args.contains("area")) {
        const auto target = chooseDestination(window, manager->theme());
        if (!stillOwned()) return false;
        if (target.isEmpty()) return true;
        area = target.value("area").toString(); row = target.value("row").toInt();
    }
    if (area != "top" && area != "bottom") return false;
    return manager->moveToolbar(context.window, strip, area == "top" ? ToolbarArea::Top : ToolbarArea::Bottom, row, index);
}

} // namespace

void registerToolbarMoveCommand(WindowManager* manager)
{
    manager->commands().add({"window.toolbar.move", QStringLiteral("移动工具条…"), "视图", {},
        [manager](const CommandContext& context) { return manager->model().window(context.window) != nullptr; },
        [manager](const CommandContext& context) { return moveToolbar(manager, context); }});
}

} // namespace window_system
