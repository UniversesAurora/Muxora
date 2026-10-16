#include <QCoreApplication>
#include <window_system/command_registry.hpp>
#include <QMetaType>
#include <QScopeGuard>
#include <algorithm>

namespace window_system {
namespace {
constexpr int maximumCommandDepth = 16;
constexpr int maximumCommandSteps = 256;

bool normalizeBindings(QVector<CommandBinding>& bindings)
{
    QVector<CommandBinding> normalized;
    for (const auto& binding : bindings) {
        if (binding.sequence.isEmpty() || binding.sequence.count() > 4) return false;
        for (int i = 0; i < binding.sequence.count(); ++i) {
            const auto key = binding.sequence[i].key();
            if (!key || key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift
                || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr) return false;
        }
        const auto existing = std::find_if(normalized.cbegin(), normalized.cend(), [&](const auto& item) {
            return item.sequence == binding.sequence;
        });
        if (existing != normalized.cend()) {
            if (existing->args != binding.args) return false;
            continue;
        }
        normalized.append(binding);
    }
    bindings = std::move(normalized);
    return true;
}

struct CommandStep {
    QString id;
    QVariantMap args;
};

// Validate and bound the whole description before executing its first command.
// Nested runCommands are flattened before execution and context resolution.
bool parseSteps(const QVariantMap& args, QVector<CommandStep>& steps, int depth = 0)
{
    if (depth >= maximumCommandDepth || !args.contains(QStringLiteral("commands"))) return false;
    const auto value = args.value(QStringLiteral("commands"));
    if (value.metaType().id() != QMetaType::QVariantList && value.metaType().id() != QMetaType::QStringList) return false;
    const auto entries = value.toList();
    if (entries.isEmpty() || entries.size() > maximumCommandSteps) return false;
    for (const auto& entry : entries) {
        CommandStep step;
        if (entry.metaType().id() == QMetaType::QString) step.id = entry.toString();
        else if (entry.metaType().id() == QMetaType::QVariantMap) {
            const auto object = entry.toMap();
            if (object.value(QStringLiteral("command")).metaType().id() != QMetaType::QString) return false;
            step.id = object.value(QStringLiteral("command")).toString();
            if (object.contains(QStringLiteral("args"))) {
                if (object.value(QStringLiteral("args")).metaType().id() != QMetaType::QVariantMap) return false;
                step.args = object.value(QStringLiteral("args")).toMap();
            }
        } else return false;
        if (step.id.trimmed().isEmpty()) return false;
        if (step.id == QStringLiteral("runCommands")) {
            if (!parseSteps(step.args, steps, depth + 1)) return false;
        } else {
            if (steps.size() >= maximumCommandSteps) return false;
            steps.append(std::move(step));
        }
    }
    return true;
}
}

struct CommandRegistry::State {
    std::function<void()> catalogChanged;
    bool alive = true;
    QVector<Command> commands;
    std::function<void()> bindingsChanged;
    std::function<bool(const CommandContext&)> validator;
    std::function<std::optional<CommandContext>(const CommandContext&)> compositeResolver;
    struct Invocation {
        QStringList active;
        int steps = 0;
    };
    std::shared_ptr<Invocation> invocation;

    const Command* find(const QString& id) const {
        const auto it = std::find_if(commands.cbegin(), commands.cend(),
                                   [&](const auto& command) { return command.id == id; });
        return it == commands.cend() ? nullptr : &*it;
    }

    bool run(const QString& id, const CommandContext& context, const std::shared_ptr<Invocation>& call,
             bool validateContext)
    {
        if (!alive || call->active.size() >= maximumCommandDepth || call->steps >= maximumCommandSteps
            || call->active.contains(id)) return false;
        const auto* command = find(id);
        if (!command) return false;
        const Command retained = *command;
        const bool captured = validateContext || id == QStringLiteral("runCommands")
            || call->active.contains(QStringLiteral("runCommands"));
        const auto validate = captured ? validator : decltype(validator){};
        if (id != QStringLiteral("runCommands")) ++call->steps;
        call->active.append(id);
        const auto pop = qScopeGuard([&] { call->active.removeLast(); });
        if (validate && !validate(context)) return false;
        if (!alive) return false;
        if (retained.enabled && !retained.enabled(context)) return false;
        if (!alive || (validate && !validate(context))) return false;
        if (!alive) return false;
        if (id == QStringLiteral("runCommands")) {
            QVector<CommandStep> steps;
            if (!parseSteps(context.args, steps) || steps.size() > maximumCommandSteps - call->steps) return false;
            const auto resolve = compositeResolver;
            bool first = true;
            for (const auto& step : steps) {
                auto target = context;
                if (!first && resolve) {
                    const auto resolved = resolve(context);
                    if (!alive || !resolved) return false;
                    target = *resolved;
                }
                first = false;
                target.args = step.args;
                if (!run(step.id, target, call, true)) return false;
            }
            return alive;
        }
        const bool result = retained.execute(context);
        return alive && result;
    }
};

CommandRegistry::CommandRegistry() : state_(std::make_shared<State>())
{
    state_->commands.append({QStringLiteral("runCommands"), QCoreApplication::translate("WindowSystem", "运行组合命令"), QString::fromUtf8(QT_TRANSLATE_NOOP("WindowSystem", "命令")),
                             {}, {}, [](const CommandContext&) { return false; }});
}
CommandRegistry::~CommandRegistry() { state_->alive = false; }

bool CommandRegistry::add(Command command)
{
    const auto state = state_;
    if (command.id.isEmpty() || !command.execute || state->find(command.id)) return false;
    if (command.contextMenus.size() > 16) return false;
    for (const auto& menu : command.contextMenus) {
        if (menu.path.size() > 5 || menu.contentType.size() > 256
            || menu.target < ContextMenuTarget::Tab || menu.target > ContextMenuTarget::Window) return false;
        for (const auto& part : menu.path) {
            if (part.trimmed().isEmpty() || part.size() > 256) return false;
            for (const auto ch : part) if (ch.category() == QChar::Other_Control) return false;
        }
    }
    if (command.bindings.isEmpty()) {
        if (!command.shortcut.isEmpty()) command.bindings.append({command.shortcut, {}});
    } else if (!command.shortcut.isEmpty() && command.bindings.first().sequence != command.shortcut) return false;
    if (!normalizeBindings(command.bindings)) return false;
    command.shortcut = command.bindings.isEmpty() ? QKeySequence{} : command.bindings.first().sequence;
    state->commands.append(std::move(command));
    const auto changed = state->catalogChanged;
    if (changed) changed();
    return state->alive;
}
bool CommandRegistry::remove(const QString& id)
{
    if (id == QStringLiteral("runCommands")) return false;
    const auto state = state_;
    const auto found = std::find_if(state->commands.begin(), state->commands.end(),
        [&](const Command& command) { return command.id == id; });
    if (found == state->commands.end()) return false;
    state->commands.erase(found);
    const auto changed = state->catalogChanged;
    if (changed) changed();
    return state->alive;
}
void CommandRegistry::setCatalogChangedHandler(std::function<void()> handler)
{
    state_->catalogChanged = std::move(handler);
}
bool CommandRegistry::setShortcut(const QString& id, QKeySequence shortcut)
{
    QVector<CommandBinding> replacement;
    if (!shortcut.isEmpty()) replacement.append({std::move(shortcut), {}});
    return setBindings(id, std::move(replacement));
}
bool CommandRegistry::setBindings(const QString& id, QVector<CommandBinding> bindings)
{
    return setBindingsBatch({{id, std::move(bindings)}});
}
bool CommandRegistry::setBindingsBatch(QHash<QString, QVector<CommandBinding>> bindings)
{
    const auto state = state_;
    for (auto it = bindings.begin(); it != bindings.end(); ++it)
        if (!state->find(it.key()) || !normalizeBindings(it.value())) return false;
    bool anyChanged = false;
    for (auto& command : state->commands) if (bindings.contains(command.id)) {
        const auto replacement = bindings.value(command.id);
        if (command.bindings == replacement) continue;
        command.bindings = replacement;
        command.shortcut = command.bindings.isEmpty() ? QKeySequence{} : command.bindings.first().sequence;
        anyChanged = true;
    }
    const auto changed = state->bindingsChanged;
    if (anyChanged && changed) changed();
    return state->alive;
}
bool CommandRegistry::addBinding(const QString& id, CommandBinding binding)
{
    auto replacement = bindings(id);
    replacement.append(std::move(binding));
    return setBindings(id, std::move(replacement));
}
bool CommandRegistry::removeBinding(const QString& id, int index)
{
    auto replacement = bindings(id);
    if (index < 0 || index >= replacement.size()) return false;
    replacement.removeAt(index);
    return setBindings(id, std::move(replacement));
}
QVector<CommandBinding> CommandRegistry::bindings(const QString& id) const
{
    const auto* command = find(id);
    return command ? command->bindings : QVector<CommandBinding>{};
}
void CommandRegistry::setBindingsChangedHandler(std::function<void()> handler) { state_->bindingsChanged = std::move(handler); }
void CommandRegistry::setExecutionValidator(std::function<bool(const CommandContext&)> validator) { state_->validator = std::move(validator); }
void CommandRegistry::setCompositeContextResolver(std::function<std::optional<CommandContext>(const CommandContext&)> resolver) { state_->compositeResolver = std::move(resolver); }
const Command* CommandRegistry::find(const QString& id) const { return state_->find(id); }
QVector<Command> CommandRegistry::commands() const { return state_->commands; }
bool CommandRegistry::execute(const QString& id, const CommandContext& context) const
{
    return executeImpl(id, context, false);
}
bool CommandRegistry::executeValidated(const QString& id, const CommandContext& context) const
{
    return executeImpl(id, context, true);
}
bool CommandRegistry::executeImpl(const QString& id, const CommandContext& context, bool validateContext) const
{
    const auto state = state_;
    const bool outermost = !state->invocation;
    if (outermost) state->invocation = std::make_shared<State::Invocation>();
    const auto call = state->invocation;
    const auto release = qScopeGuard([&] { if (outermost) state->invocation.reset(); });
    return state->run(id, context, call, validateContext);
}
} // namespace window_system
