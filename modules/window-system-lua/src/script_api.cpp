// SPDX-License-Identifier: MIT
#include "script_host_p.hpp"
#include <QKeySequence>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QDateTime>
#include <algorithm>

namespace window_system_lua {
using namespace window_system;
namespace {
bool ownedId(const QString &id) {
    static const QRegularExpression syntax(QStringLiteral("^user\\.[A-Za-z0-9_.-]{1,120}$"));
    return syntax.match(id).hasMatch();
}
QStringList stringList(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QString)
        return {value.toString()};
    if (value.metaType().id() == QMetaType::QVariantMap && value.toMap().isEmpty())
        return {};
    QStringList list;
    if (value.metaType().id() != QMetaType::QVariantList && value.metaType().id() != QMetaType::QStringList)
        throw std::runtime_error("Expected a string or array of strings");
    for (const auto &item : value.toList()) {
        if (item.metaType().id() != QMetaType::QString)
            throw std::runtime_error("Array item must be a string");
        list.append(item.toString());
    }
    return list;
}
QKeySequence sequence(QString input) {
    input = input.trimmed();
    if (input.count(QRegularExpression(QStringLiteral("(?<!\\+),"))) >= 4)
        return {};
    if (!input.contains(", ") && input.contains(QRegularExpression("\\s"))) {
        const auto strokes = input.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (strokes.size() > 4)
            return {};
        input = strokes.join(", ");
    }
    input.replace(QRegularExpression("\\b(?:cmd|win)\\+", QRegularExpression::CaseInsensitiveOption),
                  "Meta+");
    const auto key = QKeySequence::fromString(input, QKeySequence::PortableText);
    for (int i = 0; i < key.count(); ++i) {
        const auto value = key[i].key();
        if (value == Qt::Key_unknown || value == 0 || value == Qt::Key_Control || value == Qt::Key_Shift ||
            value == Qt::Key_Alt || value == Qt::Key_Meta || value == Qt::Key_AltGr)
            return {};
    }
    return key;
}
} // namespace
void ScriptHost::Private::installScriptApi() {
    handlers["clock.now"] = [](const Args &, const Context &) {
        const auto now = QDateTime::currentDateTime();
        const auto date = now.date();
        const auto time = now.time();
        return result(QVariantMap{{"unix_ms", now.toMSecsSinceEpoch()},
                                  {"utc_time", now.toUTC().toString(Qt::ISODateWithMs)},
                                  {"local_time", now.toString(Qt::ISODateWithMs)},
                                  {"year", date.year()},
                                  {"month", date.month()},
                                  {"day", date.day()},
                                  {"weekday", date.dayOfWeek()},
                                  {"hour", time.hour()},
                                  {"minute", time.minute()},
                                  {"second", time.second()}});
    };
    handlers["runtime.stats"] = [this](const Args &, const Context &) {
        auto counts = q->resourceCounts();
        counts["lua_bytes"] = engine->memoryBytes();
        counts["lua_peak_bytes"] = engine->peakMemoryBytes();
        counts["pending_jobs"] = engine->pendingJobs();
        counts["retained_callbacks"] = engine->retainedCallbacks();
        return result(counts);
    };
    handlers["commands.register"] = [this](const Args &a, const Context &) {
        const auto callback = text(a, 1);
        const QPointer<lua_runtime::ScriptRuntime> runtime(engine);
        bool transferred = false;
        const auto release = qScopeGuard([&] {
            if (!transferred && runtime)
                runtime->releaseCallback(callback);
        });
        const auto spec = object(a, 0);
        const auto id = text({spec.value("id")}, 0);
        if (!ownedId(id) || spec.value("title").toString().size() > 256 || commandCallbacks.size() >= 256 ||
            manager->commands().find(id))
            return failure("Invalid, duplicate or excessive user command");
        Command command;
        command.id = id;
        command.title = text({spec.value("title")}, 0, id);
        command.category = "工具";
        command.menuPath = {QStringLiteral("工具"), QStringLiteral("Lua")};
        if (spec.contains("menu")) {
            command.menuPath = stringList(spec["menu"]);
            if (command.menuPath.isEmpty() || command.menuPath.size() > 5)
                return failure("Menu path must contain one to five names");
            for (const auto &part : command.menuPath)
                if (part.isEmpty() || part.size() > 256)
                    return failure("Invalid menu path name");
        }
        command.menuOrder = number(spec.value("order"), 0, -100000, 100000);
        if (spec.contains("context_menus")) {
            const auto entries = spec.value("context_menus");
            if (entries.metaType().id() != QMetaType::QVariantList)
                return failure("Context menus must be an array");
            for (const auto &entry : entries.toList()) {
                const auto menu = object({entry}, 0);
                const auto target = text({menu.value("target")}, 0);
                ContextMenuPlacement location;
                if (target == "tab")
                    location.target = ContextMenuTarget::Tab;
                else if (target == "pane")
                    location.target = ContextMenuTarget::Pane;
                else if (target == "group")
                    location.target = ContextMenuTarget::Group;
                else if (target == "window")
                    location.target = ContextMenuTarget::Window;
                else
                    return failure("Unknown context menu target");
                if (menu.contains("path"))
                    location.path = stringList(menu["path"]);
                location.contentType = text({menu.value("content_type")}, 0);
                location.order = number(menu.value("order"), 0, -100000, 100000);
                command.contextMenus.append(location);
            }
        }
        const QPointer<ScriptHost> owner(q);
        command.enabled =
            [owner, needsTab = boolean({spec.value("requires_tab", false)}, 0)](const CommandContext &c) {
                return owner && owner->ready() && (!needsTab || !c.tab.isNull());
            };
        command.execute = [owner, id](const CommandContext &c) {
            return owner && owner->invokeCommand(id, c);
        };
        commandCallbacks.insert(id, callback);
        const bool added = manager->commands().add(std::move(command));
        if (!owner)
            return result(false);
        if (!added || !manager || commandCallbacks.value(id) != callback) {
            commandCallbacks.remove(id);
            return result(false);
        }
        transferred = true;
        return result(id);
    };
    handlers["commands.remove"] = [this](const Args &a, const Context &) {
        const auto id = text(a, 0);
        const auto callback = commandCallbacks.take(id);
        if (callback.isEmpty())
            return result(false);
        previousBindings.remove(id);
        const QPointer<lua_runtime::ScriptRuntime> runtime(engine);
        const bool ok = manager->commands().remove(id);
        if (runtime)
            runtime->releaseCallback(callback);
        return result(ok);
    };
    handlers["keymap.set"] = [this](const Args &a, const Context &) {
        const auto command = text(a, 1);
        if (!manager->commands().find(command))
            return failure("Unknown command");
        const auto options = object(a, 2);
        const auto keys = stringList(a.value(0));
        const bool replace = boolean({options.value("replace", false)}, 0);
        const auto arguments = object({options.value("args")}, 0);
        if (keys.isEmpty() || keys.size() > 32)
            return failure("Too many script bindings");
        const auto strictPrefix = [](const QKeySequence &first, const QKeySequence &second) {
            return first.matches(second) == QKeySequence::PartialMatch ||
                   second.matches(first) == QKeySequence::PartialMatch;
        };
        QVector<CommandBinding> additions;
        for (const auto &key : keys) {
            const auto parsed = sequence(key);
            if (parsed.isEmpty())
                return failure("Invalid shortcut sequence");
            for (const auto &addition : additions)
                if (strictPrefix(parsed, addition.sequence))
                    return failure("Shortcut aliases contain a prefix conflict");
            if (std::any_of(additions.cbegin(), additions.cend(),
                            [&](const CommandBinding &binding) { return binding.sequence == parsed; }))
                continue;
            additions.append({parsed, arguments});
        }
        auto replacement = manager->commands().bindings(command);
        QHash<QString, QVector<CommandBinding>> conflicts;
        for (const auto &other : manager->commands().commands())
            if (other.id != command) {
                auto bindings = other.bindings;
                for (const auto &addition : additions) {
                    for (auto it = bindings.begin(); it != bindings.end();) {
                        if (it->sequence.matches(addition.sequence) != QKeySequence::NoMatch ||
                            addition.sequence.matches(it->sequence) != QKeySequence::NoMatch) {
                            if (!replace)
                                return failure("Shortcut conflicts with " + other.id);
                            it = bindings.erase(it);
                        } else
                            ++it;
                    }
                }
                if (bindings != other.bindings)
                    conflicts.insert(other.id, bindings);
            }
        for (const auto &addition : additions) {
            if (!replace &&
                std::any_of(replacement.cbegin(), replacement.cend(), [&](const CommandBinding &binding) {
                    return strictPrefix(binding.sequence, addition.sequence);
                }))
                return failure("Shortcut has a prefix conflict with " + command);
            replacement.erase(std::remove_if(replacement.begin(), replacement.end(),
                                             [&](const CommandBinding &binding) {
                                                 return binding.sequence == addition.sequence ||
                                                        strictPrefix(binding.sequence, addition.sequence);
                                             }),
                              replacement.end());
            replacement.append(addition);
        }
        conflicts.insert(command, replacement);
        auto saved = previousBindings;
        for (auto it = conflicts.cbegin(); it != conflicts.cend(); ++it)
            if (!saved.contains(it.key()))
                saved.insert(it.key(), manager->commands().bindings(it.key()));
        if (saved.size() > 512)
            return failure("Too many script binding targets");
        // Validate in a detached registry before capturing owner resources.
        // The real registry commits the whole batch and notifies only once.
        CommandRegistry validator;
        for (auto it = conflicts.cbegin(); it != conflicts.cend(); ++it) {
            if (it.key() == QStringLiteral("runCommands")) {
                if (!validator.setBindings(it.key(), it.value()))
                    return failure("Invalid shortcut binding");
                continue;
            }
            Command candidate;
            candidate.id = it.key();
            candidate.execute = [](const CommandContext &) { return true; };
            candidate.bindings = it.value();
            if (!validator.add(std::move(candidate)))
                return failure("Invalid shortcut binding");
        }
        const QPointer<ScriptHost> owner(q);
        const auto previous = previousBindings;
        previousBindings = std::move(saved);
        const bool ok = manager->commands().setBindingsBatch(std::move(conflicts));
        if (!owner)
            return result(false);
        if (!ok)
            previousBindings = previous;
        return result(ok);
    };
    handlers["keymap.remove"] = [this](const Args &a, const Context &) {
        const auto command = text(a, 1);
        const auto key = sequence(text(a, 0));
        if (key.isEmpty() || !manager->commands().find(command))
            return result(false);
        auto bindings = manager->commands().bindings(command);
        if (!previousBindings.contains(command)) {
            if (previousBindings.size() >= 512)
                return failure("Too many script binding targets");
            previousBindings.insert(command, bindings);
        }
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                      [&](const CommandBinding &binding) { return binding.sequence == key; }),
                       bindings.end());
        return result(manager->commands().setBindings(command, bindings));
    };
    handlers["toolbars.register"] = [this](const Args &a, const Context &) {
        const auto spec = object(a, 0);
        const auto id = text({spec.value("id")}, 0);
        if (!ownedId(id) || ownedToolbars.contains(id) || ownedToolbars.size() >= 32)
            return failure("Invalid or excessive user toolbar");
        const auto commands = stringList(spec.value("commands"));
        if (commands.size() > 128)
            return failure("Too many toolbar commands");
        const auto area = text({spec.value("area")}, 0, "top");
        if (area != "top" && area != "bottom")
            return failure("Unknown toolbar area");
        CommandToolbar toolbar{id, text({spec.value("title")}, 0, id), commands,
                               area == "top" ? ToolbarArea::Top : ToolbarArea::Bottom,
                               number(spec.value("row"), 1, 0, 255)};
        const QPointer<ScriptHost> owner(q);
        const bool retained = retainedToolbars.remove(id);
        ownedToolbars.insert(id);
        const bool ok = retained ? manager->updateToolbar(std::move(toolbar))
                                 : manager->registerToolbar(std::move(toolbar));
        if (!owner)
            return result(false);
        if (!ok) {
            ownedToolbars.remove(id);
            if (retained)
                retainedToolbars.insert(id);
        }
        return result(ok && manager && ownedToolbars.contains(id));
    };
    handlers["toolbars.remove"] = [this](const Args &a, const Context &) {
        const auto id = text(a, 0);
        if (!ownedToolbars.remove(id))
            return result(false);
        return result(manager->removeToolbar(id));
    };
    handlers["status.register"] = [this](const Args &a, const Context &) {
        const auto spec = object(a, 0);
        const auto id = text({spec.value("id")}, 0);
        if (!ownedId(id) || ownedStatusItems.contains(id) || ownedStatusItems.size() >= 64)
            return failure("Invalid or excessive status item");
        const StatusItem item{id, text({spec.value("text")}, 0), text({spec.value("tooltip")}, 0)};
        const QPointer<ScriptHost> owner(q);
        const bool retained = retainedStatusItems.remove(id);
        ownedStatusItems.insert(id);
        const bool ok = retained ? manager->setStatusItemText(id, item.text, item.tooltip)
                                 : manager->registerStatusItem(item);
        if (!owner)
            return result(false);
        if (!ok) {
            ownedStatusItems.remove(id);
            if (retained)
                retainedStatusItems.insert(id);
        }
        return result(ok && manager && ownedStatusItems.contains(id));
    };
    handlers["status.remove"] = [this](const Args &a, const Context &) {
        const auto id = text(a, 0);
        if (!ownedStatusItems.remove(id))
            return result(false);
        return result(manager->removeStatusItem(id));
    };
    handlers["status.update"] = [this](const Args &a, const Context &) {
        const auto id = text(a, 0);
        if (!ownedStatusItems.contains(id))
            return result(false);
        return result(manager->setStatusItemText(id, text(a, 1), text(a, 2)));
    };
    handlers["events.on"] = [this](const Args &a, const Context &) {
        const auto callback = text(a, 1);
        const QPointer<lua_runtime::ScriptRuntime> runtime(engine);
        bool transferred = false;
        const auto release = qScopeGuard([&] {
            if (!transferred && runtime)
                runtime->releaseCallback(callback);
        });
        const auto name = text(a, 0);
        if (name.isEmpty() || name.size() > 128 || subscriptions.size() >= 128)
            return failure("Invalid or excessive event subscription");
        const auto id = QStringLiteral("subscription.%1").arg(++nextResource);
        subscriptions.insert(id, {name, callback});
        transferred = true;
        return result(id);
    };
    handlers["events.off"] = [this](const Args &a, const Context &) {
        return result(removeSubscription(text(a, 0)));
    };
    handlers["events.emit"] = [this](const Args &a, const Context &c) {
        const auto name = text(a, 0);
        if (!name.startsWith("user."))
            return failure("Scripts may publish user.* events only");
        q->publishEvent(name, a.value(1), c);
        return result(true);
    };
    handlers["timers.create"] = [this](const Args &a, const Context &) {
        const auto callback = text(a, 1);
        const QPointer<lua_runtime::ScriptRuntime> runtime(engine);
        bool transferred = false;
        const auto release = qScopeGuard([&] {
            if (!transferred && runtime)
                runtime->releaseCallback(callback);
        });
        if (timers.size() >= 128)
            return failure("Too many script timers");
        const int delay = number(a.value(0), 1000, 10, 2147483647);
        const bool repeat = boolean(a, 2);
        const auto id = QStringLiteral("timer.%1").arg(++nextResource);
        auto *timer = new QTimer(q);
        timer->setInterval(delay);
        timer->setSingleShot(!repeat);
        timers.insert(id, timer);
        timerCallbacks.insert(id, callback);
        QObject::connect(timer, &QTimer::timeout, q, [this, id, callback, repeat] {
            if (!timers.contains(id))
                return;
            const QPointer<ScriptHost> owner(q);
            invokeTracked(id, callback, QVariantMap{{"name", "timer"}, {"id", id}});
            if (!owner)
                return;
            if (!repeat) {
                if (auto *finished = timers.take(id))
                    finished->deleteLater();
            }
        });
        transferred = true;
        if (isReady)
            timer->start();
        return result(id);
    };
    handlers["timers.cancel"] = [this](const Args &a, const Context &) {
        const auto id = text(a, 0);
        auto *timer = timers.take(id);
        if (!timer)
            return result(false);
        const bool pending = pendingResources.contains(id);
        const auto callback = pending ? QString{} : timerCallbacks.take(id);
        const QPointer<lua_runtime::ScriptRuntime> runtime(engine);
        delete timer;
        if (!pending && runtime)
            runtime->releaseCallback(callback);
        return result(true);
    };
}
} // namespace window_system_lua
