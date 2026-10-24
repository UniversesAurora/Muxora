// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include "chrome/command_strip.hpp"
#include "chrome/status_bar.hpp"
#include "chrome/workspace_window.hpp"
#include "commands/command_menus.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QScopeGuard>
#include <QSet>
#include <QTimer>
#include <algorithm>

namespace window_system {
namespace {
constexpr qsizetype maximumDefinitions = 128;
constexpr qsizetype maximumToolbarCommands = 256;
constexpr int maximumToolbarRow = 256;
constexpr qsizetype maximumTextLength = 65536;

bool validId(const QString &id) {
    if (id.isEmpty() || id.size() > 256 || id.trimmed() != id)
        return false;
    for (const auto ch : id)
        if (ch.isSpace() || ch.isNull() || ch.category() == QChar::Other_Control)
            return false;
    return true;
}

bool builtinToolbar(const QString &id) {
    return id == QStringLiteral("menu") || id == QStringLiteral("floating-menu") ||
           id == QStringLiteral("workspace") || id == QStringLiteral("title") ||
           id == QStringLiteral("caption-controls");
}

bool validToolbar(const CommandToolbar &toolbar, const CommandRegistry &commands) {
    if (!validId(toolbar.id) || builtinToolbar(toolbar.id) || toolbar.title.size() > maximumTextLength ||
        toolbar.commands.isEmpty() || toolbar.commands.size() > maximumToolbarCommands || toolbar.row < 0 ||
        toolbar.row > maximumToolbarRow ||
        (toolbar.area != ToolbarArea::Top && toolbar.area != ToolbarArea::Bottom))
        return false;
    QSet<QString> seen;
    for (const auto &command : toolbar.commands) {
        if (!commands.find(command) || seen.contains(command))
            return false;
        seen.insert(command);
    }
    return true;
}

CommandStripHost *stripsFor(const WindowManager *manager, WindowId window) {
    const auto *view = static_cast<WorkspaceWindow *>(manager->windowWidget(window));
    return view ? view->strips() : nullptr;
}

QJsonObject withoutToolbar(QJsonObject layout, const QString &id) {
    auto settings = layout.value(QStringLiteral("strips")).toObject();
    settings.remove(id);
    layout.insert(QStringLiteral("strips"), settings);
    for (const auto *field : {"rows", "bottomRows"}) {
        if (!layout.contains(field))
            continue;
        auto rows = layout.value(field).toArray();
        for (qsizetype row = 0; row < rows.size(); ++row) {
            auto entries = rows[row].toArray();
            for (qsizetype index = entries.size(); index-- > 0;)
                if (entries[index].toString() == id)
                    entries.removeAt(index);
            rows[row] = entries;
        }
        const int retainedSlots = QString::fromLatin1(field) == QStringLiteral("rows") ? 2 : 0;
        while (rows.size() > retainedSlots && rows.last().toArray().isEmpty())
            rows.removeLast();
        if (rows.isEmpty() && !retainedSlots)
            layout.remove(field);
        else
            layout.insert(field, rows);
    }
    return layout;
}
} // namespace

bool WindowManager::registerToolbar(CommandToolbar toolbar) {
    if (!validToolbar(toolbar, d->commands) || d->customToolbars.size() >= maximumDefinitions)
        return false;
    for (const auto &existing : d->customToolbars)
        if (existing.id == toolbar.id)
            return false;
    const auto id = toolbar.id;
    d->customToolbars.append(std::move(toolbar));
    const QPointer<WindowManager> self(this);
    const auto windows = d->windows;
    for (const auto &view : windows) {
        if (view)
            view->syncCustomChrome();
        if (!self)
            return false;
    }
    emit layoutChanged();
    return self && std::any_of(d->customToolbars.cbegin(), d->customToolbars.cend(),
                               [&](const auto &item) { return item.id == id; });
}

bool WindowManager::updateToolbar(CommandToolbar toolbar) {
    if (!validToolbar(toolbar, d->commands))
        return false;
    const auto found = std::find_if(d->customToolbars.begin(), d->customToolbars.end(),
                                    [&](const auto &entry) { return entry.id == toolbar.id; });
    if (found == d->customToolbars.end())
        return false;
    if (*found == toolbar)
        return true;
    *found = std::move(toolbar);
    const QPointer<WindowManager> self(this);
    const auto windows = d->windows;
    for (const auto &view : windows) {
        if (view)
            view->syncCustomChrome();
        if (!self)
            return false;
    }
    emit layoutChanged();
    return !self.isNull();
}

bool WindowManager::removeToolbar(const QString &id) {
    const auto found = std::find_if(d->customToolbars.begin(), d->customToolbars.end(),
                                    [&](const auto &item) { return item.id == id; });
    if (found == d->customToolbars.end())
        return false;
    d->customToolbars.erase(found);
    const QPointer<WindowManager> self(this);
    const auto windows = d->windows;
    for (const auto &view : windows) {
        if (view)
            view->syncCustomChrome();
        if (!self)
            return false;
        if (view)
            view->forgetCustomToolbarLayout(id);
    }
    for (auto &profile : d->toolbarProfiles)
        profile = withoutToolbar(profile, id);
    emit layoutChanged();
    return !self.isNull();
}

QVector<CommandToolbar> WindowManager::toolbars() const { return d->customToolbars; }

std::optional<ToolbarState> WindowManager::toolbarState(WindowId window, const QString &id) const {
    const auto *strips = stripsFor(this, window);
    if (!strips || !strips->stripWidget(id))
        return {};
    const auto layout = strips->saveLayout();
    const auto fields = layout.value(QStringLiteral("strips")).toObject().value(id).toObject();
    ToolbarState result;
    result.area =
        strips->stripArea(id) == CommandStripHost::Area::Top ? ToolbarArea::Top : ToolbarArea::Bottom;
    const auto rows = layout.value(result.area == ToolbarArea::Top ? "rows" : "bottomRows").toArray();
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const auto entries = rows[row].toArray();
        for (qsizetype index = 0; index < entries.size(); ++index)
            if (entries[index].toString() == id) {
                result.row = int(row);
                result.index = int(index);
            }
    }
    result.width = fields.value(QStringLiteral("width")).toInt(-1);
    result.visible = strips->isStripVisible(id);
    result.elastic = strips->stripSizing(id) == CommandStripHost::StripSizing::Elastic;
    result.locked = strips->isLocked();
    for (const auto &value : fields.value(QStringLiteral("excluded")).toArray())
        result.hiddenItems.append(value.toString());
    return result;
}

bool WindowManager::setToolbarVisible(WindowId window, const QString &id, bool visible) {
    auto *strips = stripsFor(this, window);
    return strips && strips->setStripVisible(id, visible);
}
bool WindowManager::setToolbarWidth(WindowId window, const QString &id, int width) {
    if (width < -1 || width > QWIDGETSIZE_MAX)
        return false;
    auto *strips = stripsFor(this, window);
    return strips && (width == -1 ? strips->resetStripWidth(id) : strips->setStripWidth(id, width));
}
bool WindowManager::setToolbarElastic(WindowId window, const QString &id, bool elastic) {
    auto *strips = stripsFor(this, window);
    return strips && strips->setStripSizing(id, elastic ? CommandStripHost::StripSizing::Elastic
                                                        : CommandStripHost::StripSizing::Content);
}
bool WindowManager::setToolbarItemVisible(WindowId window, const QString &id, const QString &item,
                                          bool visible) {
    auto *strips = stripsFor(this, window);
    return strips && strips->setStripItemVisible(id, item, visible);
}
bool WindowManager::setToolbarsLocked(WindowId window, bool locked) {
    const QPointer<WindowManager> self(this);
    auto *strips = stripsFor(this, window);
    if (!strips)
        return false;
    strips->setLocked(locked);
    return !self.isNull();
}

bool WindowManager::registerStatusItem(StatusItem item) {
    if (!validId(item.id) || statusItemIds().contains(item.id) ||
        d->customStatusItems.size() >= maximumDefinitions || item.text.size() > maximumTextLength ||
        item.tooltip.size() > maximumTextLength || (!item.command.isEmpty() && !validId(item.command)))
        return false;
    d->customStatusItems.append(std::move(item));
    const QPointer<WindowManager> self(this);
    const auto windows = d->windows;
    for (const auto &view : windows) {
        if (view)
            view->syncCustomChrome();
        if (!self)
            return false;
    }
    return true;
}
bool WindowManager::removeStatusItem(const QString &id) {
    const auto found = std::find_if(d->customStatusItems.begin(), d->customStatusItems.end(),
                                    [&](const auto &item) { return item.id == id; });
    if (found == d->customStatusItems.end())
        return false;
    d->customStatusItems.erase(found);
    for (auto &options : d->options)
        options.hiddenStatusItems.removeAll(id);
    const QPointer<WindowManager> self(this);
    const auto windows = d->windows;
    for (const auto &view : windows) {
        if (view)
            view->setOptions(d->options.value(view->id()));
        if (!self)
            return false;
        if (view)
            view->syncCustomChrome();
        if (!self)
            return false;
    }
    emit layoutChanged();
    return !self.isNull();
}
QVector<StatusItem> WindowManager::statusItems() const { return d->customStatusItems; }
QStringList WindowManager::statusItemIds() const {
    auto result = WorkspaceWindow::statusItemIds();
    for (const auto &item : d->customStatusItems)
        result.append(item.id);
    return result;
}
bool WindowManager::setStatusItemText(const QString &id, QString text, QString tooltip) {
    if (text.size() > maximumTextLength || tooltip.size() > maximumTextLength)
        return false;
    const auto found = std::find_if(d->customStatusItems.begin(), d->customStatusItems.end(),
                                    [&](const auto &item) { return item.id == id; });
    if (found == d->customStatusItems.end())
        return false;
    found->text = std::move(text);
    found->tooltip = std::move(tooltip);
    const QPointer<WindowManager> self(this);
    const auto windows = d->windows;
    for (const auto &view : windows) {
        if (view)
            view->syncCustomChrome();
        if (!self)
            return false;
    }
    return true;
}

bool WindowManager::renameTab(TabId tab, QString title) {
    if (title.size() > maximumTextLength)
        return false;
    const auto panes = d->model.panes(tab);
    const auto result = d->model.renameTab(tab, title);
    if (!result)
        return false;
    for (const auto pane : panes)
        if (d->descriptors.contains(pane))
            d->descriptors[pane].title = title;
    refresh();
    return true;
}

bool WindowManager::setWindowTitle(WindowId window, QString title) {
    if (!d->model.window(window) || title.size() > maximumTextLength)
        return false;
    auto options = d->options.value(window);
    options.title = std::move(title);
    d->options.insert(window, options);
    const QPointer<WindowManager> self(this);
    const auto view = d->windows.value(window);
    if (view)
        view->setOptions(options);
    if (!self)
        return false;
    if (view)
        view->updateCaption();
    if (!self)
        return false;
    emit layoutChanged();
    return !self.isNull();
}

void WindowManager::registerCustomChromeCommands() {
    const QPointer<WindowManager> owner(this);
    const auto hasWindow = [owner](const CommandContext &c) {
        return owner && owner->model().window(c.window);
    };
    const auto registerCommand = [this, &hasWindow](QString id, QString title,
                                                    std::function<bool(const CommandContext &)> run) {
        d->commands.add({std::move(id),
                         std::move(title),
                         QStringLiteral("视图"),
                         {},
                         hasWindow,
                         std::move(run),
                         {QStringLiteral("视图"), QStringLiteral("工具条")}});
    };
    registerCommand(QStringLiteral("window.toolbar.setVisible"),
                    QCoreApplication::translate("WindowSystem", "Set toolbar visibility"),
                    [owner](const CommandContext &c) {
                        if (!owner || c.args.value("strip").metaType().id() != QMetaType::QString ||
                            c.args.value("visible").metaType().id() != QMetaType::Bool)
                            return false;
                        return owner->setToolbarVisible(c.window, c.args.value("strip").toString(),
                                                        c.args.value("visible").toBool());
                    });
    registerCommand(QStringLiteral("window.setTitle"),
                    QCoreApplication::translate("WindowSystem", "Set window title"),
                    [owner](const CommandContext &c) {
                        return owner && c.args.value("title").metaType().id() == QMetaType::QString &&
                               owner->setWindowTitle(c.window, c.args.value("title").toString());
                    });
    registerCommand(
        QStringLiteral("window.toolbar.setWidth"),
        QCoreApplication::translate("WindowSystem", "Set toolbar width"), [owner](const CommandContext &c) {
            if (!owner || c.args.value("strip").metaType().id() != QMetaType::QString)
                return false;
            const auto value = c.args.value("width");
            if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString)
                return false;
            bool valid = false;
            const int width = value.toInt(&valid);
            if (!valid || value.toDouble() != double(width))
                return false;
            return owner->setToolbarWidth(c.window, c.args.value("strip").toString(), width);
        });
    registerCommand(QStringLiteral("window.toolbar.setItemVisible"),
                    QCoreApplication::translate("WindowSystem", "Set toolbar button visibility"),
                    [owner](const CommandContext &c) {
                        if (!owner || c.args.value("strip").metaType().id() != QMetaType::QString ||
                            c.args.value("item").metaType().id() != QMetaType::QString ||
                            c.args.value("visible").metaType().id() != QMetaType::Bool)
                            return false;
                        return owner->setToolbarItemVisible(c.window, c.args.value("strip").toString(),
                                                            c.args.value("item").toString(),
                                                            c.args.value("visible").toBool());
                    });
    d->commands.add({QStringLiteral("window.statusItem.setText"),
                     QCoreApplication::translate("WindowSystem", "Set status item text"),
                     QStringLiteral("视图"),
                     {},
                     {},
                     [owner](const CommandContext &c) {
                         if (!owner || c.args.value("item").metaType().id() != QMetaType::QString ||
                             c.args.value("text").metaType().id() != QMetaType::QString ||
                             (c.args.contains("tooltip") &&
                              c.args.value("tooltip").metaType().id() != QMetaType::QString))
                             return false;
                         return owner->setStatusItemText(c.args.value("item").toString(),
                                                         c.args.value("text").toString(),
                                                         c.args.value("tooltip").toString());
                     }});
    d->commands.add({QStringLiteral("tab.rename"),
                     QCoreApplication::translate("WindowSystem", "Rename tab…"),
                     QStringLiteral("布局"),
                     {},
                     [owner](const CommandContext &c) { return owner && owner->model().tab(c.tab); },
                     [owner](const CommandContext &c) {
                         if (!owner)
                             return false;
                         if (c.args.contains("title")) {
                             if (c.args.value("title").metaType().id() != QMetaType::QString)
                                 return false;
                             return owner->renameTab(c.tab, c.args.value("title").toString());
                         }
                         const auto *tab = owner->model().tab(c.tab);
                         if (!tab)
                             return false;
                         const auto current = tab->title;
                         bool accepted = false;
                         const auto title =
                             QInputDialog::getText(owner->windowWidget(c.window),
                                                   QCoreApplication::translate("WindowSystem", "Rename tab…"),
                                                   QCoreApplication::translate("WindowSystem", "Tab name"),
                                                   QLineEdit::Normal, current, &accepted);
                         if (!owner)
                             return false;
                         return !accepted || owner->renameTab(c.tab, title);
                     },
                     {QStringLiteral("布局"), QStringLiteral("标签外观")}});
}

void WorkspaceWindow::syncCustomChrome() {
    if (!manager_ || !strips_)
        return;
    if (syncingCustomChrome_) {
        customChromePending_ = true;
        return;
    }
    syncingCustomChrome_ = true;
    const QPointer<WorkspaceWindow> retained(this);
    const auto reset = qScopeGuard([retained] {
        if (!retained)
            return;
        retained->syncingCustomChrome_ = false;
        if (!retained->customChromePending_)
            return;
        retained->customChromePending_ = false;
        QTimer::singleShot(0, retained, [retained] {
            if (retained)
                retained->syncCustomChrome();
        });
    });
    const auto definitions = manager_->toolbars();
    const auto previousLayout = saveStripLayout();
    bool added = false;
    QSet<QString> live;
    for (const auto &definition : definitions)
        live.insert(definition.id);
    const auto previous = customActions_;
    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
        if (!live.contains(it.key())) {
            const auto actions = customActions_.take(it.key());
            strips_->removeStrip(it.key());
            if (!retained || !manager_ || !strips_)
                return;
            for (const auto &action : actions)
                if (action)
                    delete action.data();
        }
    for (const auto &definition : definitions) {
        if (customActions_.contains(definition.id)) {
            QStringList commands;
            for (const auto &action : customActions_.value(definition.id))
                if (action)
                    commands.append(action->data().toString());
            if (commands == definition.commands && strips_->stripTitle(definition.id) == definition.title)
                continue;
            const auto replaced = customActions_.take(definition.id);
            strips_->removeStrip(definition.id);
            if (!retained || !manager_ || !strips_)
                return;
            for (const auto &action : replaced)
                if (action)
                    delete action.data();
        }
        QList<QAction *> actions;
        QList<QPointer<QAction>> observed;
        for (const auto &id : definition.commands) {
            const auto *command = manager_->commands().find(id);
            auto *action =
                new QAction(iconFor("command", manager_->theme()), command ? command->title : id, strips_);
            action->setObjectName(id);
            action->setData(id);
            action->setProperty("stripCommand", true);
            action->setProperty("customToolbar", definition.id);
            actions.append(action);
            observed.append(action);
            QObject::connect(action, &QAction::triggered, manager_, [owner = manager_, window = id_, id] {
                if (owner)
                    QTimer::singleShot(0, owner, [owner, window, id] {
                        if (owner)
                            owner->execute(id, {}, window);
                    });
            });
        }
        customActions_.insert(definition.id, observed);
        strips_->addActionStrip(definition.id, definition.title, actions, definition.row);
        added = true;
        if (!retained || !manager_ || !strips_)
            return;
        if (definition.area == ToolbarArea::Bottom)
            strips_->moveStrip(definition.id, CommandStripHost::Area::Bottom, definition.row, 0);
        if (!retained || !manager_ || !strips_)
            return;
    }
    // Merge live edits with dormant provider preferences before adding views.
    // Reading only savedStripLayout_ here would restore an old startup layout
    // over toolbar changes made since startup.
    if (added && !previousLayout.isEmpty())
        strips_->restoreLayout(previousLayout);
    if (!retained || !manager_ || !strips_)
        return;
    if (status_) {
        auto *bar = static_cast<WorkspaceStatusBar *>(status_.data());
        bar->setCustomItems(manager_->statusItems());
        if (!retained || !manager_)
            return;
        bar->setHiddenItems(options_.hiddenStatusItems);
        if (!retained || !manager_)
            return;
    }
    updateCommandStates();
}

void WorkspaceWindow::forgetCustomToolbarLayout(const QString &id) {
    savedStripLayout_ = withoutToolbar(savedStripLayout_, id);
    committedStripLayout_ = withoutToolbar(committedStripLayout_, id);
    for (auto &layout : savedStripLayouts_)
        layout = withoutToolbar(layout, id);
}

} // namespace window_system
