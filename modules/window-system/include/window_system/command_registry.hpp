#pragma once

#include <window_system/layout_model.hpp>
#include <QKeySequence>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>

namespace window_system {

struct CommandContext {
    WindowId window;
    GroupId group;
    TabId tab;
    PaneId pane;
    QVariantMap args;
};

struct CommandBinding {
    QKeySequence sequence;
    QVariantMap args;
    bool operator==(const CommandBinding&) const = default;
};

enum class ContextMenuTarget { Tab, Pane, Group, Window };
struct ContextMenuPlacement {
    ContextMenuTarget target = ContextMenuTarget::Pane;
    QStringList path;
    QString contentType; // Empty matches any host content type.
    int order = 0;
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
    // Canonical bindings, including the primary shortcut. The registry mirrors
    // the first sequence to shortcut for existing hosts and presentation code.
    QVector<CommandBinding> bindings = {};
    QVector<ContextMenuPlacement> contextMenus = {};
};

// A host registers operations here. Menus, the palette and keyboard all invoke
// the same validated handler; the registry has no settings or business dependency.
class CommandRegistry {
public:
    CommandRegistry();
    ~CommandRegistry();
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    bool add(Command command);
    // Built-in runCommands is structural and cannot be removed. Removing a
    // running command does not invalidate the retained invocation callback.
    bool remove(const QString& id);
    // Replaces every binding; an empty sequence removes them all.
    bool setShortcut(const QString& id, QKeySequence shortcut);
    bool setBindings(const QString& id, QVector<CommandBinding> bindings);
    // Validate all entries first, then publish one change notification. No
    // observer can see a partially replaced set of conflicting shortcuts.
    bool setBindingsBatch(QHash<QString, QVector<CommandBinding>> bindings);
    bool addBinding(const QString& id, CommandBinding binding);
    bool removeBinding(const QString& id, int index);
    [[nodiscard]] QVector<CommandBinding> bindings(const QString& id) const;
    void setBindingsChangedHandler(std::function<void()> handler);
    // Catalog observers are separate from binding edits. Window hosts may
    // coalesce presentation refresh without running enabled handlers in add().
    void setCatalogChangedHandler(std::function<void()> handler);
    // Applied to captured-context execution and every composite step. Direct
    // execute() keeps each handler's own target/argument validation contract.
    void setExecutionValidator(std::function<bool(const CommandContext&)> validator);
    // The first composite step uses its captured target. Before later steps a
    // host may resolve the current active content within that original target's
    // window; nullopt aborts. Without a resolver every step retains the target.
    void setCompositeContextResolver(std::function<std::optional<CommandContext>(const CommandContext&)> resolver);
    [[nodiscard]] const Command* find(const QString& id) const;
    [[nodiscard]] QVector<Command> commands() const;
    bool execute(const QString& id, const CommandContext& context) const;
    bool executeValidated(const QString& id, const CommandContext& context) const;
private:
    bool executeImpl(const QString& id, const CommandContext& context, bool validateContext) const;
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace window_system
