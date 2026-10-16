// SPDX-License-Identifier: MIT
#pragma once
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <QHash>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <cmath>
#include <stdexcept>

namespace window_system_lua {
using Reply = lua_runtime::ScriptResult;
using Args = QVariantList;
using Context = QVariantMap;
using Handler = std::function<Reply(const Args &, const Context &)>;
inline Reply result(QVariant value = {}) {
    return {true, value.isValid() ? QVariantList{value} : QVariantList{}, {}};
}
inline Reply failure(QString message) { return {false, {}, std::move(message)}; }
inline QString text(const Args &a, int i, QString fallback = {}) {
    if (i >= a.size() || !a[i].isValid() || a[i].isNull())
        return fallback;
    if (a[i].metaType().id() != QMetaType::QString)
        throw std::runtime_error("Expected a string");
    return a[i].toString();
}
inline QVariantMap object(const Args &a, int i) {
    if (i >= a.size() || !a[i].isValid() || a[i].isNull())
        return {};
    if (a[i].metaType().id() != QMetaType::QVariantMap)
        throw std::runtime_error("Expected an object table");
    return a[i].toMap();
}
inline bool boolean(const Args &a, int i, bool fallback = false) {
    if (i >= a.size())
        return fallback;
    if (a[i].metaType().id() != QMetaType::Bool)
        throw std::runtime_error("Expected a boolean");
    return a[i].toBool();
}
inline int number(const QVariant &value, int fallback, int minimum, int maximum) {
    if (!value.isValid())
        return fallback;
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Float:
    case QMetaType::Double:
        break;
    default:
        throw std::runtime_error("Expected an integer number");
    }
    bool ok = false;
    const auto n = value.toLongLong(&ok);
    if (!ok || !std::isfinite(value.toDouble()) || value.toDouble() != double(n) || n < minimum ||
        n > maximum)
        throw std::runtime_error("Integer is outside the supported range");
    return int(n);
}
inline QUuid identifier(const Args &a, int i, QUuid fallback = {}) {
    const auto string = text(a, i);
    if (string.isEmpty())
        return fallback;
    const QUuid id(string);
    if (id.isNull())
        throw std::runtime_error("Invalid or null object ID");
    return id;
}
inline QString idText(const QUuid &id) { return id.isNull() ? QString{} : id.toString(QUuid::WithoutBraces); }
inline window_system::Placement placement(QString value, bool center = true) {
    using P = window_system::Placement;
    if (value == "left")
        return P::Left;
    if (value == "right")
        return P::Right;
    if (value == "top" || value == "up")
        return P::Top;
    if (value == "bottom" || value == "down")
        return P::Bottom;
    if (value == "center" && center)
        return P::Center;
    throw std::runtime_error("Invalid placement");
}
inline window_system::Edge edge(QString value) {
    using E = window_system::Edge;
    if (value == "left")
        return E::Left;
    if (value == "right")
        return E::Right;
    if (value == "top")
        return E::Top;
    if (value == "bottom")
        return E::Bottom;
    throw std::runtime_error("Invalid edge");
}
inline Context contextMap(const window_system::CommandContext &c) {
    return {{"window", idText(c.window)},
            {"group", idText(c.group)},
            {"tab", idText(c.tab)},
            {"pane", idText(c.pane)},
            {"args", c.args}};
}
struct ScriptHost::Private {
    enum class Target { Window, Group, Tab, Pane };
    ScriptHost *q;
    QPointer<window_system::WindowManager> manager;
    lua_runtime::ScriptRuntime *engine;
    ServiceHandler services;
    struct ComponentService {
        ServiceHandler handler;
        QMetaObject::Connection destroyed;
        quint64 token;
    };
    QHash<QString, ComponentService> componentServices;
    QHash<QString, Handler> handlers;
    QHash<QString, QString> commandCallbacks;
    QHash<QString, QVector<window_system::CommandBinding>> previousBindings;
    QSet<QString> ownedToolbars, ownedStatusItems;
    QSet<QString> retainedToolbars, retainedStatusItems;
    struct Subscription {
        QString event, callback;
    };
    QHash<QString, Subscription> subscriptions;
    struct PendingEvent {
        QVariant value;
        Context context;
        quint64 bytes;
    };
    QHash<QString, QQueue<PendingEvent>> eventBacklogs;
    QHash<QString, QString> retiredEventCallbacks;
    quint64 eventBacklogBytes = 0;
    int deferredEvents = 0;
    QHash<QString, QTimer *> timers;
    QHash<QString, QString> timerCallbacks;
    QHash<quint64, QString> callbackJobs;
    QSet<QString> pendingResources;
    QString moduleDirectory, pendingSource, pendingName;
    QStringList lastAffected;
    quint64 bootstrapJob = 0, initializeJob = 0, nextResource = 0;
    bool isReady = false, initializing = false, unloading = false;
    explicit Private(ScriptHost *owner, window_system::WindowManager *windows);
    window_system::CommandContext current(const Context &source) const;
    QUuid target(const Args &args, int index, const Context &source, Target kind) const {
        if (index < args.size() && args[index].isValid() && !args[index].isNull() &&
            !(args[index].metaType().id() == QMetaType::QString && args[index].toString().isEmpty()))
            return identifier(args, index);
        const auto context = current(source);
        switch (kind) {
        case Target::Window:
            return context.window;
        case Target::Group:
            return context.group;
        case Target::Tab:
            return context.tab;
        case Target::Pane:
            return context.pane;
        }
        return {};
    }
    window_system::CommandContext captured(const Context &source) const {
        const auto readId = [&source](const char *name) {
            const auto value = text({source.value(QString::fromLatin1(name))}, 0);
            if (value.isEmpty())
                return QUuid{};
            const QUuid id(value);
            if (id.isNull())
                throw std::runtime_error("Invalid context object ID");
            return id;
        };
        return {readId("window"), readId("group"), readId("tab"), readId("pane"),
                object({source.value("args")}, 0)};
    }
    std::pair<window_system::TabId, window_system::PaneId> paneTarget(const Args &args,
                                                                      const Context &source) const {
        const auto tab = target(args, 0, source, Target::Tab);
        if (args.size() > 1 && args[1].isValid() && !args[1].isNull() && !text(args, 1).isEmpty())
            return {tab, identifier(args, 1)};
        const auto *record = manager ? manager->model().tab(tab) : nullptr;
        return {tab, record ? record->activePane : window_system::PaneId{}};
    }
    Reply dispatch(QString method, Args args, Context context);
    void clearResources(bool preserveChrome = false);
    void installWindowApi();
    void installScriptApi();
    void installAppearanceApi();
    void finishInitialization(bool ok, QString error = {});
    void invokeTracked(const QString &resource, const QString &callback, QVariant event = {});
    bool removeSubscription(const QString &id);
    bool scheduleResource(const QString &resource, const QString &callback, Context context, QVariant event);
};
} // namespace window_system_lua
