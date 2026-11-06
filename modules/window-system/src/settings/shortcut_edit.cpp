// SPDX-License-Identifier: MIT
#include "shortcut_edit.hpp"
#include "localization/display_names.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace window_system {
namespace {

QString titleFor(const Command& command) { return command.title.isEmpty() ? command.id : command.title; }

bool readArguments(const CommandRegistry& registry, const ShortcutEdit& edit, QVariantMap& args, QString& error)
{
    const auto text = edit.arguments.trimmed();
    if (!text.isEmpty()) {
        QJsonParseError diagnostic;
        const auto document = QJsonDocument::fromJson(text.toUtf8(), &diagnostic);
        if (diagnostic.error != QJsonParseError::NoError || !document.isObject()) {
            error = QCoreApplication::translate("WindowSystem", "Arguments must be a valid JSON object: %1")
                .arg(diagnostic.error == QJsonParseError::NoError
                    ? QCoreApplication::translate("WindowSystem", "The top level must be an object, not an array or another value") : diagnostic.errorString());
            return false;
        }
        args = document.object().toVariantMap();
    }
    if (edit.command != QStringLiteral("runCommands")) return true;
    const auto commands = QJsonObject::fromVariantMap(args).value(QStringLiteral("commands"));
    if (!commands.isArray() || commands.toArray().isEmpty()) {
        error = QCoreApplication::translate("WindowSystem", "A command sequence requires a nonempty commands array and runs in order.");
        return false;
    }
    for (const auto& entry : commands.toArray()) {
        const auto object = entry.toObject();
        const auto id = entry.isString() ? entry.toString() : object.value(QStringLiteral("command")).toString();
        if (id.isEmpty() || !registry.find(id)) {
            error = QCoreApplication::translate("WindowSystem", "The command sequence contains an unknown or missing command ID: %1").arg(id);
            return false;
        }
        if (entry.isObject() && object.contains(QStringLiteral("args")) && !object.value(QStringLiteral("args")).isObject()) {
            error = QCoreApplication::translate("WindowSystem", "Each command step's args must be a JSON object.");
            return false;
        }
    }
    return true;
}

} // namespace

QString shortcutArgumentText(const QVariantMap& arguments, QJsonDocument::JsonFormat format)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(arguments)).toJson(format)).trimmed();
}

QString shortcutBindingText(const QVector<CommandBinding>& bindings)
{
    QStringList sequences;
    for (const auto& binding : bindings) sequences.append(binding.sequence.toString(QKeySequence::NativeText));
    return sequences.join(QStringLiteral(" / "));
}

QVector<PanelRow> shortcutCommandRows(const CommandRegistry& registry, const QString& query)
{
    const auto text = query.trimmed();
    const bool keyQuery = text.size() > 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'));
    const auto recorded = keyQuery ? QKeySequence::fromString(text.mid(1, text.size() - 2), QKeySequence::NativeText) : QKeySequence{};
    const bool exactCommand = registry.find(text);
    QVector<PanelRow> rows;
    for (const auto& command : registry.commands()) {
        const auto bindings = registry.bindings(command.id);
        const auto shortcut = shortcutBindingText(bindings);
        const auto category = commandCategoryDisplayName(command.category);
        if (exactCommand && command.id != text) continue;
        if (keyQuery) {
            if (recorded.isEmpty() || std::none_of(bindings.cbegin(), bindings.cend(), [&recorded](const CommandBinding& binding) {
                return recorded.matches(binding.sequence) != QKeySequence::NoMatch;
            })) continue;
        } else if (!matchesPanelSearch(titleFor(command) + QLatin1Char(' ') + category + QLatin1Char(' ')
                    + command.id + QLatin1Char(' ') + shortcut, text)) continue;
        rows.append({command.id, {titleFor(command), shortcut, category}, command.id});
    }
    return rows;
}

ShortcutEditCheck checkShortcutEdit(const CommandRegistry& registry, const ShortcutEdit& edit)
{
    ShortcutEditCheck result;
    result.selected = registry.find(edit.command);
    if (!result.selected) return result;
    result.canRemove = edit.binding >= 0;
    const auto current = registry.bindings(edit.command);
    if (edit.binding >= 0 && (edit.binding >= current.size() || current[edit.binding] != edit.original)) {
        result.canRemove = false;
        result.message = QCoreApplication::translate("WindowSystem", "This binding changed elsewhere. Select the command again before editing.");
        return result;
    }
    if (!readArguments(registry, edit, result.arguments, result.message)) return result;
    QStringList duplicates, prefixes;
    for (const auto& command : registry.commands()) {
        const auto bindings = registry.bindings(command.id);
        for (int index = 0; index < bindings.size(); ++index) {
            const auto& binding = bindings[index];
            if ((command.id == edit.command && index == edit.binding) || edit.sequence.isEmpty()) continue;
            const auto forward = edit.sequence.matches(binding.sequence);
            const auto backward = binding.sequence.matches(edit.sequence);
            if (forward == QKeySequence::ExactMatch) duplicates.push_back(titleFor(command));
            else if (forward == QKeySequence::PartialMatch || backward == QKeySequence::PartialMatch) prefixes.push_back(titleFor(command));
        }
    }
    duplicates.removeDuplicates();
    prefixes.removeDuplicates();
    result.canApply = duplicates.isEmpty() && (!edit.sequence.isEmpty() || edit.binding >= 0);
    if (!duplicates.isEmpty()) {
        result.message = QCoreApplication::translate("WindowSystem", "This shortcut is already bound to: %1. Remove the other binding first.")
            .arg(duplicates.join(QStringLiteral("、")));
    } else if (!prefixes.isEmpty()) {
        result.message = QCoreApplication::translate("WindowSystem", "This key sequence shares a prefix with: %1.")
            .arg(prefixes.join(QStringLiteral("、")));
    } else if (edit.sequence.isEmpty()) {
        result.message = QCoreApplication::translate("WindowSystem", "Record keys and apply. Clear the keys and apply to remove this binding.");
    } else {
        result.message = QCoreApplication::translate("WindowSystem", "Up to 4 key strokes, such as Ctrl+K, G, 1. Changes take effect immediately.");
    }
    return result;
}

} // namespace window_system
