// SPDX-License-Identifier: MIT
#include "script_host_p.hpp"
#include <QDir>
#include <QSignalBlocker>
#include <QRegularExpression>

namespace window_system_lua {
namespace {
QString luaString(const QString &value) {
    QString literal = QStringLiteral("\"");
    for (const auto byte : value.toUtf8())
        literal += QStringLiteral("\\%1").arg(uint(static_cast<unsigned char>(byte)), 3, 10, QLatin1Char('0'));
    return literal + QLatin1Char('"');
}
// Small Lua adapters keep callbacks in the VM. All effects still pass through
// the same typed host handlers as commands, without exposing C++ object graphs.
const char bootstrap[] = R"lua(
local host, callback, release = muxora._host, muxora._callback, muxora._release
local function registerCallback(fn, register)
  local id = callback(fn)
  local ok, value = pcall(register,id)
  if not ok or value == false then release(id) end
  if not ok then error(value,2) end
  return value
end
local function api(prefix, names)
  local t = {}
  for name in names:gmatch('%S+') do t[name] = function(...) return host(prefix..'.'..name, ...) end end
  return t
end
muxora.context = function() return host('context') end
muxora.call = host
muxora.version = { api = 1, lua = _VERSION }
muxora.runtime = api('runtime','stats')
muxora.clock = api('clock','now')
muxora.commands = api('commands', 'list execute remove')
function muxora.commands.register(spec, fn)
  assert(type(spec)=='table' and type(fn)=='function', 'register expects a descriptor and function')
  return registerCallback(fn,function(id) return host('commands.register',spec,id) end)
end
muxora.keymap = api('keymap','remove')
function muxora.keymap.set(keys, target, options)
  options = options or {}
  if type(target)=='function' then
    local label = type(keys)=='table' and table.concat(keys,'_') or keys
    local command = options.id or ('user.key.'..label:gsub('[^%w]','_'))
    muxora.commands.register({id=command,title=options.title or label}, target)
    local ok, value=pcall(host,'keymap.set',keys,command,options)
    if not ok or value==false then host('commands.remove',command) end
    if not ok then error(value,2) end
    return value
  end
  return host('keymap.set',keys,target,options)
end
muxora.windows = api('windows','list new close activate promote demote set_owner set_visibility visibility set_mode mode set_zen zen resize move set_title state set_state')
muxora.tabs = api('tabs','list new close activate rename color icon move move_to_window reorder detach sort set_bar_sharing set_bar appearance set_appearance into_tab to_group')
muxora.groups = api('groups','list close move merge swap detach to_window to_edge to_center into_tab to_tab')
muxora.panes = api('panes','list split close focus move swap detach set_bar to_tab')
muxora.sidebar = api('sidebar','move pin pin_all set_size size reset_size')
muxora.toolbars = api('toolbars','list register remove state move visible width elastic item_visible locked reset_size')
muxora.status = api('status','set register remove items update visible')
muxora.layout = api('layout','snapshot restore')
muxora.settings = api('settings','get update')
muxora.theme = api('theme','get update')
muxora.ui = api('ui','settings command_palette window_selector notify')
muxora.app = api('app','launch play_sound exit')
muxora.events = api('events','off emit')
function muxora.events.on(name, fn) return registerCallback(fn,function(id)return host('events.on',name,id)end) end
muxora.timers = api('timers','cancel')
function muxora.timers.after(ms, fn) return registerCallback(fn,function(id)return host('timers.create',ms,id,false)end) end
function muxora.timers.every(ms, fn) return registerCallback(fn,function(id)return host('timers.create',ms,id,true)end) end
if moduleDirectory ~= '' then
  package.path = moduleDirectory..'/?.lua;'..moduleDirectory..'/?/init.lua;'..package.path
end
package.cpath = ''
os.exit = function() return host('app.exit') end
)lua";
} // namespace
ScriptHost::Private::Private(ScriptHost *owner, window_system::WindowManager *windows)
    : q(owner), manager(windows), engine(new lua_runtime::ScriptRuntime(owner)) {
    installWindowApi();
    installHierarchyApi();
    installScriptApi();
}
window_system::CommandContext ScriptHost::Private::current(const Context &source) const {
    if (!manager)
        throw std::runtime_error("Window host is no longer available");
    const QUuid window(source.value("window").toString());
    if (!window.isNull() && !manager->model().window(window))
        throw std::runtime_error("Source window no longer exists");
    return manager->context(window);
}
Reply ScriptHost::Private::dispatch(QString method, Args args, Context context) {
    if (unloading || !manager)
        return failure(QStringLiteral("Script host is unavailable"));
    try {
        const auto it = handlers.constFind(method);
        if (it != handlers.cend()) {
            const auto handler = it.value();
            return handler(args, context);
        }
        const auto extension = componentServices.constFind(method);
        if (extension != componentServices.cend()) {
            const auto handler = extension->handler;
            return handler(std::move(method), std::move(args), std::move(context));
        }
        return services ? services(std::move(method), std::move(args), std::move(context))
                        : failure(QStringLiteral("Unknown Lua host method: %1").arg(method));
    } catch (const std::exception &error) {
        return failure(QString::fromUtf8(error.what()));
    }
}
void ScriptHost::Private::clearResources(bool preserveChrome) {
    unloading = true;
    for (auto *timer : std::as_const(timers))
        delete timer;
    timers.clear();
    timerCallbacks.clear();
    subscriptions.clear();
    eventBacklogs.clear();
    retiredEventCallbacks.clear();
    eventBacklogBytes = 0;
    deferredEvents = 0;
    callbackJobs.clear();
    pendingResources.clear();
    // Detach ownership before notifications. A host listener is allowed to
    // destroy this adapter or the manager synchronously during unregistration.
    auto toolbars = std::exchange(ownedToolbars, {});
    auto status = std::exchange(ownedStatusItems, {});
    toolbars.unite(std::exchange(retainedToolbars, {}));
    status.unite(std::exchange(retainedStatusItems, {}));
    if (preserveChrome) {
        retainedToolbars = std::exchange(toolbars, {});
        retainedStatusItems = std::exchange(status, {});
    }
    const auto bindings = std::exchange(previousBindings, {});
    const auto commands = std::exchange(commandCallbacks, {});
    const QPointer<ScriptHost> owner(q);
    const auto windows = manager;
    for (const auto &id : toolbars)
        if (windows)
            windows->removeToolbar(id);
    for (const auto &id : status)
        if (windows)
            windows->removeStatusItem(id);
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it)
        if (windows && windows->commands().find(it.key()))
            windows->commands().setBindings(it.key(), it.value());
    for (const auto &id : commands.keys())
        if (windows)
            windows->commands().remove(id);
    if (owner)
        owner->d->unloading = false;
}
void ScriptHost::Private::finishInitialization(bool ok, QString error) {
    initializing = false;
    isReady = ok;
    const QPointer<ScriptHost> owner(q);
    if (!ok) {
        clearResources();
        if (owner)
            engine->reset();
    } else {
        const auto unusedToolbars = std::exchange(retainedToolbars, {});
        const auto unusedStatus = std::exchange(retainedStatusItems, {});
        const auto windows = manager;
        for (const auto &id : unusedToolbars)
            if (windows)
                windows->removeToolbar(id);
        for (const auto &id : unusedStatus)
            if (windows)
                windows->removeStatusItem(id);
        if (!owner)
            return;
        for (auto *timer : std::as_const(timers))
            timer->start();
        if (manager)
            emit manager->commandsChanged();
    }
    if (owner)
        emit owner->initialized(ok, std::move(error));
}
ScriptHost::ScriptHost(window_system::WindowManager *manager, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, manager)) {
    d->engine->setHostHandler([this](QString method, Args args, Context context) {
        return d->dispatch(std::move(method), std::move(args), std::move(context));
    });
    connect(d->engine, &lua_runtime::ScriptRuntime::logMessage, this, &ScriptHost::logMessage);
    connect(d->engine, &lua_runtime::ScriptRuntime::resetFinished, this, [this] {
        if (d->initializing) {
            const auto directory = QDir::fromNativeSeparators(d->moduleDirectory);
            const auto source = QStringLiteral("local moduleDirectory = %1\n").arg(luaString(directory)) +
                                QString::fromUtf8(bootstrap);
            d->bootstrapJob = d->engine->execute(source, QStringLiteral("@muxora/api"));
            if (!d->bootstrapJob)
                d->finishInitialization(false, QStringLiteral("Lua runtime is overloaded"));
        }
    });
    connect(d->engine, &lua_runtime::ScriptRuntime::finished, this, [this](quint64 job, Reply reply) {
        const QPointer<ScriptHost> owner(this);
        const auto resource = d->callbackJobs.take(job);
        if (!resource.isEmpty())
            d->pendingResources.remove(resource);
        if (const auto retired = d->retiredEventCallbacks.take(resource); !retired.isEmpty())
            d->engine->releaseCallback(retired);
        if (!reply.ok && !resource.isEmpty()) {
            if (auto *timer = d->timers.take(resource)) {
                timer->stop();
                timer->deleteLater();
            }
            d->removeSubscription(resource);
        }
        if (reply.ok && d->subscriptions.contains(resource)) {
            const auto queued = d->eventBacklogs.find(resource);
            if (queued != d->eventBacklogs.end() && !queued->isEmpty()) {
                auto next = queued->dequeue();
                --d->deferredEvents;
                d->eventBacklogBytes -= next.bytes;
                if (queued->isEmpty())
                    d->eventBacklogs.erase(queued);
                d->scheduleResource(resource, d->subscriptions.value(resource).callback,
                                    std::move(next.context), std::move(next.value));
                if (!owner)
                    return;
            }
        }
        if (!resource.isEmpty() && !d->timers.contains(resource) && d->timerCallbacks.contains(resource))
            d->engine->releaseCallback(d->timerCallbacks.take(resource));
        if (d->initializing && job == d->bootstrapJob) {
            if (!reply.ok) {
                d->finishInitialization(false, reply.error);
                return;
            }
            d->initializeJob = d->engine->execute(std::move(d->pendingSource), std::move(d->pendingName));
            if (!d->initializeJob)
                d->finishInitialization(false, QStringLiteral("Lua runtime is overloaded"));
            return;
        }
        if (d->initializing && job == d->initializeJob) {
            d->finishInitialization(reply.ok, reply.error);
            return;
        }
        if (!reply.ok && !d->unloading && d->isReady)
            emit scriptError(reply.error);
    });
    if (manager) {
        connect(manager, &window_system::WindowManager::layoutChanged, this,
                [this] { publishEvent("layout.changed"); });
        connect(manager, &window_system::WindowManager::commandFailed, this, [this](const QString &message) {
            publishEvent("command.failed", QVariantMap{{"message", message}});
        });
    }
}
ScriptHost::~ScriptHost() {
    d->isReady = false;
    d->engine->setHostHandler({});
    d->engine->cancelAll();
    const auto windows = d->manager;
    {
        const QSignalBlocker block(windows.data());
        d->clearResources();
    }
    if (windows)
        QTimer::singleShot(0, windows, [windows] {
            if (windows)
                emit windows->commandsChanged();
        });
}
void ScriptHost::setServiceHandler(ServiceHandler handler) { d->services = std::move(handler); }
bool ScriptHost::registerService(QString method, ServiceHandler handler, QObject *owner) {
    static const QRegularExpression syntax(QStringLiteral("^[a-z][a-z0-9_]*(?:\\.[a-z][a-z0-9_]*)+$"));
    static const QStringList reserved{"commands", "keymap",  "windows",  "tabs",   "groups",
                                      "panes",    "sidebar", "toolbars", "status", "layout",
                                      "settings", "theme",   "ui",       "app",    "events",
                                      "timers",   "module",  "runtime",  "clock"};
    if (!handler || method.size() > 128 || !syntax.match(method).hasMatch() || d->handlers.contains(method) ||
        d->componentServices.contains(method) || reserved.contains(method.section('.', 0, 0)) ||
        d->componentServices.size() >= 128 || (owner && owner->thread() != thread()))
        return false;
    const auto token = ++d->nextResource;
    QMetaObject::Connection destroyed;
    if (owner)
        destroyed = connect(owner, &QObject::destroyed, this, [this, method, token] {
            const auto found = d->componentServices.constFind(method);
            if (found != d->componentServices.cend() && found->token == token)
                removeService(method);
        });
    d->componentServices.insert(std::move(method), {std::move(handler), destroyed, token});
    return true;
}
bool ScriptHost::removeService(const QString &method) {
    const auto found = d->componentServices.find(method);
    if (found == d->componentServices.end())
        return false;
    disconnect(found->destroyed);
    d->componentServices.erase(found);
    return true;
}
void ScriptHost::setModuleDirectory(QString directory) { d->moduleDirectory = std::move(directory); }
void ScriptHost::initialize(QString source, QString name) {
    const QPointer<ScriptHost> owner(this);
    const bool preserveChrome = !d->initializing;
    d->isReady = false;
    d->initializing = false;
    d->clearResources(preserveChrome);
    if (!owner)
        return;
    d->pendingSource = std::move(source);
    d->pendingName = std::move(name);
    d->bootstrapJob = 0;
    d->initializeJob = 0;
    d->initializing = true;
    d->engine->reset();
}
void ScriptHost::unload() {
    const QPointer<ScriptHost> owner(this);
    d->isReady = false;
    d->initializing = false;
    d->clearResources();
    if (owner)
        d->engine->reset();
}
quint64 ScriptHost::run(QString source, QString name, window_system::CommandContext context) {
    if (!d->isReady)
        return 0;
    if (context.window.isNull() && d->manager)
        context = d->manager->context();
    return d->engine->execute(std::move(source), std::move(name), contextMap(context));
}
bool ScriptHost::invokeCommand(QString command, const window_system::CommandContext &context) {
    const auto callback = d->commandCallbacks.value(command);
    if (!d->isReady || callback.isEmpty())
        return false;
    return d->engine->invoke(callback, contextMap(context)) != 0;
}
void ScriptHost::publishEvent(QString name, QVariant payload, QVariantMap context) {
    if (!d->isReady || d->unloading)
        return;
    if (context.isEmpty() && d->manager)
        context = contextMap(d->manager->context());
    const auto subscriptions = d->subscriptions;
    const QPointer<ScriptHost> owner(this);
    const QVariant event = QVariantMap{{"name", name}, {"data", payload}};
    for (auto it = subscriptions.cbegin(); it != subscriptions.cend(); ++it)
        if (it->event == name && d->subscriptions.contains(it.key())) {
            if (!d->pendingResources.contains(it.key())) {
                d->scheduleResource(it.key(), it->callback, context, event);
                if (!owner)
                    return;
                continue;
            }
            if (name == QStringLiteral("layout.changed"))
                continue;
            quint64 bytes = 0;
            QString error;
            const auto limits = d->engine->limits();
            if (!d->engine->measureValue(QVariantList{context, event}, bytes, error) ||
                d->deferredEvents >= limits.maximumPendingJobs ||
                bytes > limits.maximumQueuedBytes - d->eventBacklogBytes) {
                d->removeSubscription(it.key());
                emit scriptError(
                    QStringLiteral("Event subscription %1 (%2) stopped: %3")
                        .arg(it.key(), name,
                             error.isEmpty() ? QStringLiteral("event backlog limit exceeded") : error));
                if (!owner)
                    return;
                continue;
            }
            d->eventBacklogs[it.key()].enqueue({event, context, bytes});
            d->eventBacklogBytes += bytes;
            ++d->deferredEvents;
        }
}
bool ScriptHost::Private::removeSubscription(const QString &id) {
    const auto subscription = subscriptions.find(id);
    if (subscription == subscriptions.end())
        return false;
    const auto callback = subscription->callback;
    subscriptions.erase(subscription);
    const auto queued = eventBacklogs.take(id);
    for (const auto &event : queued) {
        --deferredEvents;
        eventBacklogBytes -= event.bytes;
    }
    if (pendingResources.contains(id))
        retiredEventCallbacks.insert(id, callback);
    else
        engine->releaseCallback(callback);
    return true;
}
void ScriptHost::Private::invokeTracked(const QString &resource, const QString &callback, QVariant event) {
    if (!isReady || pendingResources.contains(resource) || !manager)
        return;
    scheduleResource(resource, callback, contextMap(manager->context()), std::move(event));
}
bool ScriptHost::Private::scheduleResource(const QString &resource, const QString &callback, Context context,
                                           QVariant event) {
    const auto job = engine->invoke(callback, std::move(context), std::move(event));
    if (job) {
        pendingResources.insert(resource);
        callbackJobs.insert(job, resource);
        return true;
    }
    if (auto *timer = timers.take(resource)) {
        timer->stop();
        timer->deleteLater();
    }
    if (const auto timerCallback = timerCallbacks.take(resource); !timerCallback.isEmpty())
        engine->releaseCallback(timerCallback);
    removeSubscription(resource);
    emit q->scriptError(QStringLiteral("Lua runtime is overloaded; stopped resource %1").arg(resource));
    return false;
}
bool ScriptHost::ready() const { return d->isReady; }
QVariantMap ScriptHost::resourceCounts() const {
    return {{"commands", d->commandCallbacks.size()},
            {"bindings", d->previousBindings.size()},
            {"timers", d->timers.size()},
            {"subscriptions", d->subscriptions.size()},
            {"deferred_events", d->deferredEvents},
            {"event_backlog_bytes", QVariant::fromValue(d->eventBacklogBytes)},
            {"toolbars", d->ownedToolbars.size() + d->retainedToolbars.size()},
            {"status_items", d->ownedStatusItems.size() + d->retainedStatusItems.size()}};
}
lua_runtime::ScriptRuntime *ScriptHost::runtime() const { return d->engine; }
} // namespace window_system_lua
