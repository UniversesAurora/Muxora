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
    if (!command || (command->enabled && !command->enabled(context))) return false;
    // Copy: a command may register another command and reallocate the catalog.
    const auto handler = command->execute;
    return handler(context);
}
} // namespace window_system
