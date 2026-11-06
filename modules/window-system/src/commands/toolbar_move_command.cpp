// SPDX-License-Identifier: MIT
#include <QCoreApplication>
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
        auto* menu = picker.addMenu(top ? QCoreApplication::translate("WindowSystem", "Top") : QCoreApplication::translate("WindowSystem", "Bottom"));
        const auto rows = layout.value(top ? "rows" : "bottomRows").toArray();
        const int count = int(rows.size());
        for (int row = 0; row <= count; ++row) {
            if (row < count && rows.at(row).toArray().isEmpty()) continue;
            auto* action = menu->addAction(row == count ? QCoreApplication::translate("WindowSystem", "New row") : QCoreApplication::translate("WindowSystem", "Row %1").arg(row + 1));
            action->setData(QVariantMap{{"area", QString::fromLatin1(area)}, {"row", row}});
        }
    }
    QObject::connect(window, &QObject::destroyed, &picker, &QMenu::close);
    const auto* selected = picker.exec(QCursor::pos());
    return selected ? selected->data().toMap() : QVariantMap{};
}

} // namespace

// Without modal pickers, the same command is offered again with the strip
// or with the strip and its destination.
bool offerToolbarMoveChoices(WindowManager* manager, const CommandContext& context)
{
    const auto* editor = manager->toolbarEditor(context.window);
    if (!editor) return false;
    const auto strip = context.args.value("strip").toString();
    WindowManager::CommandChoices choices;
    if (!context.args.contains("strip")) {
        for (const auto& id : editor->toolbarIds()) choices.append({{editor->toolbarTitle(id)}, {{"strip", id}}});
    } else {
        if (!editor->toolbarIds().contains(strip)) return false;
        for (const auto area : {ToolbarArea::Top, ToolbarArea::Bottom}) {
            const auto title = area == ToolbarArea::Top ? QCoreApplication::translate("WindowSystem", "Top")
                                                        : QCoreApplication::translate("WindowSystem", "Bottom");
            const auto rows = editor->toolbarRows(area);
            for (int row = 0; row <= rows.size(); ++row) {
                if (row < rows.size() && rows.at(row).isEmpty()) continue;
                choices.append({{title, row == rows.size() ? QCoreApplication::translate("WindowSystem", "New row")
                                                          : QCoreApplication::translate("WindowSystem", "Row %1").arg(row + 1)},
                    {{"strip", strip}, {"area", area == ToolbarArea::Top ? "top" : "bottom"}, {"row", row}}});
            }
        }
    }
    manager->offerChoices(context, QStringLiteral("window.toolbar.move"), choices);
    return true;
}

namespace {

bool moveToolbar(WindowManager* manager, const CommandContext& context)
{
    int row = 0, index = -1;
    if (!integerArgument(context.args, "row", 0, row) || !integerArgument(context.args, "index", -1, index)) return false;
    if (manager->presentation() == WindowManager::Presentation::External) {
        if (!context.args.contains("strip") || !context.args.contains("area")) return offerToolbarMoveChoices(manager, context);
        const auto area = context.args.value("area").toString();
        if (area != "top" && area != "bottom") return false;
        return manager->moveToolbar(context.window, context.args.value("strip").toString(),
                                    area == "top" ? ToolbarArea::Top : ToolbarArea::Bottom, row, index);
    }
    const QPointer<WindowManager> owner(manager);
    QPointer<WorkspaceWindow> window = static_cast<WorkspaceWindow*>(manager->windowWidget(context.window));
    if (!window || !window->strips()) return false;
    const auto stillOwned = [&] { return owner && window && owner->windowWidget(context.window) == window.data(); };
    QString strip = context.args.value("strip").toString();
    if (!context.args.contains("strip")) {
        strip = chooseStrip(window, manager->interfaceTheme());
        if (!stillOwned()) return false;
        if (strip.isEmpty()) return true;
    }
    if (!window->strips()->stripIds().contains(strip)) return false;
    auto area = context.args.value("area").toString();
    if (!context.args.contains("area")) {
        const auto target = chooseDestination(window, manager->interfaceTheme());
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
    manager->commands().add({"window.toolbar.move", QCoreApplication::translate("WindowSystem", "Move toolbar…"), "view", {},
        [manager](const CommandContext& context) { return manager->model().window(context.window) != nullptr; },
        [manager](const CommandContext& context) { return moveToolbar(manager, context); }});
}

} // namespace window_system
