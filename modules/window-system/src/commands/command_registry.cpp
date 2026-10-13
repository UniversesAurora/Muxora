#include <window_system/command_registry.hpp>
#include <algorithm>

namespace window_system {
bool CommandRegistry::add(Command command)
{
    if (command.id.isEmpty() || !command.execute || find(command.id)) return false;
    commands_.append(std::move(command));
    return true;
}
bool CommandRegistry::setShortcut(const QString& id, QKeySequence shortcut)
{
    for (auto& command : commands_) {
        if (command.id == id) { command.shortcut = std::move(shortcut); return true; }
    }
    return false;
}
const Command* CommandRegistry::find(const QString& id) const
{
    const auto it = std::find_if(commands_.cbegin(), commands_.cend(),
                               [&](const Command& command) { return command.id == id; });
    return it == commands_.cend() ? nullptr : &*it;
}
QVector<Command> CommandRegistry::commands() const { return commands_; }
bool CommandRegistry::execute(const QString& id, const CommandContext& context) const
{
    const auto* command = find(id);
    if (!command) return false;
    // The enabled callback may register more commands and reallocate the list.
    // Retain both callbacks before invoking either of them.
    const Command retained = *command;
    if (retained.enabled && !retained.enabled(context)) return false;
    return retained.execute(context);
}
} // namespace window_system
