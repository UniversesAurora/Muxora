#pragma once

#include <window_system/layout_model.hpp>
#include <QKeySequence>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <functional>

namespace window_system {

struct CommandContext {
    WindowId window;
    GroupId group;
    TabId tab;
    PaneId pane;
    QVariantMap args;
};

struct Command {
    QString id;
    QString title;
    QString category;
    QKeySequence shortcut;
    std::function<bool(const CommandContext&)> enabled;
    std::function<bool(const CommandContext&)> execute;
    // Optional presentation metadata. The first segment names a top-level
    // menu; later segments name real subcategories. It never changes routing
    // or shortcut execution. Empty paths use the category's extension group.
    QStringList menuPath = {};
    int menuOrder = 0;
};

// A host registers operations here. Menus, the palette and keyboard all invoke
// the same validated handler; the registry has no settings or business dependency.
class CommandRegistry {
public:
    bool add(Command command);
    bool setShortcut(const QString& id, QKeySequence shortcut);
    [[nodiscard]] const Command* find(const QString& id) const;
    [[nodiscard]] QVector<Command> commands() const;
    bool execute(const QString& id, const CommandContext& context) const;
private:
    QVector<Command> commands_;
};

} // namespace window_system
