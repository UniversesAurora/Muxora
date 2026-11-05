// SPDX-License-Identifier: MIT
#include "panel_rows.hpp"
#include "localization/display_names.hpp"
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QPointer>
#include <algorithm>

namespace window_system {
namespace {

QString displayTitle(const Command& command) { return command.title.isEmpty() ? command.id : command.title; }

QString shortcutText(const Command& command)
{
    QStringList sequences;
    for (const auto& binding : command.bindings) sequences.append(binding.sequence.toString(QKeySequence::NativeText));
    return sequences.join(QStringLiteral(" / "));
}

bool canExecute(const Command& command, const CommandContext& context)
{
    return command.execute && (!command.enabled || command.enabled(context));
}

} // namespace

bool matchesPanelSearch(const QString& searchable, const QString& query)
{
    const QString folded = searchable.toCaseFolded();
    const auto terms = query.simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(terms.cbegin(), terms.cend(), [&folded](const QString& term) { return folded.contains(term); });
}

QVector<PanelRow> commandPaletteRows(WindowManager* manager, const CommandContext& context, const QString& query)
{
    const QPointer<WindowManager> alive(manager);
    QVector<PanelRow> rows;
    for (const auto& command : manager->commands().commands()) {
        const QString category = commandCategoryDisplayName(command.category);
        if (!matchesPanelSearch(displayTitle(command) + QLatin1Char(' ') + category + QLatin1Char(' ') + command.id, query)) continue;
        const bool enabled = canExecute(command, context);
        if (!alive) return {};
        PanelRow row{command.id, {displayTitle(command), category, shortcutText(command)}, command.id, enabled, {}};
        if (!enabled) row.tooltip += QCoreApplication::translate("WindowSystem", "\nThe current window or content does not meet this command's conditions.");
        rows.append(std::move(row));
    }
    return rows;
}

bool canRunFromPalette(WindowManager* manager, const QString& command, const CommandContext& context)
{
    const auto* found = manager->commands().find(command);
    if (!found) return false;
    const Command copy = *found; // An enabled query may change the catalog.
    return canExecute(copy, context);
}

QVector<PanelRow> windowSelectorRows(WindowManager* manager, const QString& query)
{
    QVector<PanelRow> rows;
    const auto& model = manager->model();
    for (const auto id : model.windowOrder()) {
        const auto* record = model.window(id);
        if (!record) continue;
        const QString title = manager->windowCaption(id);
        const bool workbench = record->kind == WindowKind::Workbench;
        const QString kind = workbench ? QCoreApplication::translate("WindowSystem", "Workbench window", "window type")
                                       : QCoreApplication::translate("WindowSystem", "Floating window");
        const QString owner = workbench ? QStringLiteral("—") : manager->windowCaption(model.familyRoot(record->familyId));
        if (!matchesPanelSearch(title + QLatin1Char(' ') + kind + QLatin1Char(' ') + owner + QLatin1Char(' ') + id.toString(), query)) continue;
        rows.append({id.toString(QUuid::WithoutBraces), {title, kind, owner, QString::number(model.orderedTabs(id).size())},
            id.toString(QUuid::WithoutBraces), true, workbench ? QStringLiteral("window") : QStringLiteral("detach")});
    }
    return rows;
}

} // namespace window_system
