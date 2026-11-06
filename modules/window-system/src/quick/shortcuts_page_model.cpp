// SPDX-License-Identifier: MIT
#include "shortcuts_page_model.hpp"
#include "qml_scene_component.hpp"
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopedValueRollback>
#include <algorithm>

namespace window_system::quick {

ShortcutsPageModel::ShortcutsPageModel(WindowManager& manager, QObject* parent)
    : QObject(parent), manager_(&manager), style_(new SettingsStyle(manager.interfaceTheme(), this))
{
    connect(&manager, &WindowManager::commandsChanged, this, &ShortcutsPageModel::refresh);
    // The manager publishes interface theme and scale changes with layouts.
    connect(&manager, &WindowManager::layoutChanged, this, [this] { if (manager_) style_->setTheme(manager_->interfaceTheme()); });
    search({});
}

QString ShortcutsPageModel::selectedTitle() const
{
    return commandIndex_ < 0 ? QCoreApplication::translate("WindowSystem", "No matching commands")
        : commands_.at(commandIndex_).toMap().value("title").toString() + QStringLiteral("  ·  ") + edit_.command;
}

QString ShortcutsPageModel::commandHelp() const
{
    return edit_.command == QStringLiteral("runCommands") ? QCoreApplication::translate("WindowSystem",
        "runCommands is a regular command. Each binding defines its steps in its own args.commands array. Example: {\"commands\":[\"host.newTab\",{\"command\":\"pane.split.right\",\"args\":{}}]}. The structure follows VS Code; each step's args currently accepts JSON objects only.") : QString();
}

QString ShortcutsPageModel::argumentPlaceholder() const
{
    return edit_.command == QStringLiteral("runCommands")
        ? QStringLiteral("{\n  \"commands\": [\n    \"host.newTab\",\n    {\"command\": \"pane.split.right\", \"args\": {}}\n  ]\n}")
        : QCoreApplication::translate("WindowSystem", "Arguments (JSON object, optional)");
}

void ShortcutsPageModel::rebuildCommands()
{
    QVariantList commands;
    commandIndex_ = -1;
    if (manager_) for (const auto& row : shortcutCommandRows(manager_->commands(), query_)) {
        if (row.id == edit_.command) commandIndex_ = int(commands.size());
        commands.append(QVariantMap{{"id", row.id}, {"title", row.cells.at(0)},
            {"shortcut", row.cells.at(1)}, {"category", row.cells.at(2)}});
    }
    if (commandIndex_ < 0 && !commands.isEmpty()) commandIndex_ = 0;
    if (commands != commands_) { commands_ = std::move(commands); emit commandRowsChanged(); }
    edit_.command = commandIndex_ < 0 ? QString() : commands_.at(commandIndex_).toMap().value("id").toString();
}

void ShortcutsPageModel::search(const QString& query)
{
    if (syncing_) return;
    const QScopedValueRollback guard(syncing_, true);
    query_ = query;
    rebuildCommands();
    loadBinding(0);
}

void ShortcutsPageModel::selectCommand(int index)
{
    if (syncing_ || index < 0 || index >= commands_.size() || index == commandIndex_) return;
    const QScopedValueRollback guard(syncing_, true);
    commandIndex_ = index;
    edit_.command = commands_.at(index).toMap().value("id").toString();
    loadBinding(0);
}

void ShortcutsPageModel::selectBinding(int index)
{
    if (syncing_ || index < 0 || index >= bindings_.size() || index == edit_.binding) return;
    const QScopedValueRollback guard(syncing_, true);
    loadBinding(index);
}

void ShortcutsPageModel::loadBinding(int index)
{
    const auto bindings = manager_ ? manager_->commands().bindings(edit_.command) : QVector<CommandBinding>{};
    QVariantList rows;
    for (const auto& binding : bindings) {
        rows.append(QVariantMap{{"shortcut", binding.sequence.toString(QKeySequence::NativeText)},
            {"arguments", binding.args.isEmpty() ? QString() : shortcutArgumentText(binding.args, QJsonDocument::Compact)}});
    }
    edit_.binding = bindings.isEmpty() || index < 0 ? -1 : std::min(index, int(bindings.size()) - 1);
    edit_.original = edit_.binding < 0 ? CommandBinding{} : bindings.at(edit_.binding);
    edit_.sequence = edit_.original.sequence;
    edit_.arguments = edit_.original.args.isEmpty() ? QString() : shortcutArgumentText(edit_.original.args, QJsonDocument::Indented);
    dirty_ = false;
    if (rows != bindings_) { bindings_ = std::move(rows); emit bindingRowsChanged(); }
    emit editChanged();
    updateDiagnostic();
}

void ShortcutsPageModel::editSequence(const QString& sequence)
{
    if (syncing_) return;
    const auto parsed = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    if (parsed == edit_.sequence) return;
    edit_.sequence = parsed;
    dirty_ = true;
    updateDiagnostic();
}

void ShortcutsPageModel::editArguments(const QString& text)
{
    if (syncing_ || text == edit_.arguments) return;
    edit_.arguments = text;
    dirty_ = true;
    updateDiagnostic();
}

void ShortcutsPageModel::startAdding()
{
    const QScopedValueRollback guard(syncing_, true);
    loadBinding(-1);
}

void ShortcutsPageModel::updateDiagnostic()
{
    check_ = manager_ ? checkShortcutEdit(manager_->commands(), edit_) : ShortcutEditCheck{};
    emit changed();
}

void ShortcutsPageModel::refresh()
{
    if (committing_ || syncing_) return;
    if (!dirty_) { search(query_); return; }
    // Keep the draft and its original binding identity. Only the command
    // table's binding text follows the registry while an edit is unfinished.
    const QScopedValueRollback guard(syncing_, true);
    if (manager_) for (auto& value : commands_) {
        auto row = value.toMap();
        row.insert("shortcut", shortcutBindingText(manager_->commands().bindings(row.value("id").toString())));
        value = row;
    }
    emit commandRowsChanged();
    updateDiagnostic();
}

void ShortcutsPageModel::commit(QVector<CommandBinding> bindings, int preferred)
{
    const QPointer<ShortcutsPageModel> self(this);
    committing_ = true;
    const bool changed = manager_->commands().setBindings(edit_.command, std::move(bindings));
    if (!self) return;
    committing_ = false;
    if (!changed) return;
    const QScopedValueRollback guard(syncing_, true);
    rebuildCommands();
    loadBinding(preferred);
}

void ShortcutsPageModel::apply()
{
    if (!manager_) return;
    const auto check = checkShortcutEdit(manager_->commands(), edit_);
    if (!check.canApply) { updateDiagnostic(); return; }
    auto bindings = manager_->commands().bindings(edit_.command);
    int preferred = edit_.binding;
    if (edit_.sequence.isEmpty()) bindings.removeAt(edit_.binding);
    else if (edit_.binding >= 0) bindings[edit_.binding] = {edit_.sequence, check.arguments};
    else { bindings.append({edit_.sequence, check.arguments}); preferred = int(bindings.size()) - 1; }
    commit(std::move(bindings), preferred);
}

void ShortcutsPageModel::remove()
{
    if (!manager_) return;
    const auto check = checkShortcutEdit(manager_->commands(), edit_);
    if (!check.canRemove) { updateDiagnostic(); return; }
    auto bindings = manager_->commands().bindings(edit_.command);
    bindings.removeAt(edit_.binding);
    commit(std::move(bindings), edit_.binding);
}

QQuickItem* createShortcutsContent(WindowManager& manager)
{
    auto* model = new ShortcutsPageModel(manager);
    auto* view = qobject_cast<QQuickItem*>(sceneComponent(SceneComponent::ShortcutsPage)
        .createWithInitialProperties({{"page", QVariant::fromValue(model)}}));
    Q_ASSERT(view);
    QQmlEngine::setObjectOwnership(view, QQmlEngine::CppOwnership);
    model->setParent(view);
    view->setObjectName(QStringLiteral("ShortcutsPage"));
    return view;
}

} // namespace window_system::quick
