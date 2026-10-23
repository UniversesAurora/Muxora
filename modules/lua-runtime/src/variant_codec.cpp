#include "variant_codec.hpp"

#include <lua.h>
#include <lauxlib.h>
#include <QStringDecoder>
#include <cmath>
#include <limits>

namespace lua_runtime::detail {

RuntimeLimits normalizedLimits(RuntimeLimits limits) {
    limits.hostReplyTimeoutMs = qMax(1, limits.hostReplyTimeoutMs);
    limits.maximumPendingJobs = qMax(1, limits.maximumPendingJobs);
    limits.maximumCallbacks = qMax(1, limits.maximumCallbacks);
    limits.maximumSourceBytes = qMax(1, limits.maximumSourceBytes);
    limits.maximumQueuedBytes = qMax<quint64>(1, limits.maximumQueuedBytes);
    limits.maximumValueNodes = qMax(1, limits.maximumValueNodes);
    limits.maximumValueDepth = qBound(1, limits.maximumValueDepth, 128);
    limits.maximumStringBytes = qMax(1, limits.maximumStringBytes);
    limits.maximumLogMessagesPerJob = qMax(0, limits.maximumLogMessagesPerJob);
    limits.maximumLogMessageBytes = qMax(1, limits.maximumLogMessageBytes);
    return limits;
}

void ValueBudget::visit(int depth) {
    if (depth > limits.maximumValueDepth || ++nodes > limits.maximumValueNodes)
        throw ValueError("Lua value exceeds the configured depth or node limit");
    // Conservative transfer accounting includes a variant, Qt container/node
    // overhead and alignment. The VM allocator is measured separately, exactly.
    bytes += 128;
    if (bytes > limits.maximumQueuedBytes)
        throw ValueError("Lua value exceeds the configured transfer byte limit");
}

void ValueBudget::string(qsizetype size) {
    if (size < 0 || size > limits.maximumStringBytes)
        throw ValueError("Lua string exceeds the configured byte limit");
    bytes += static_cast<quint64>(size) * 2;
    if (bytes > limits.maximumQueuedBytes)
        throw ValueError("Lua value exceeds the configured transfer byte limit");
}

namespace {

void measure(const QVariant &value, ValueBudget &budget, int depth) {
    budget.visit(depth);
    if (!value.isValid())
        return;
    switch (value.metaType().id()) {
    case QMetaType::Nullptr:
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
        return;
    case QMetaType::ULongLong:
        if (value.toULongLong() > static_cast<quint64>(std::numeric_limits<lua_Integer>::max()))
            throw ValueError("Lua integer is outside the signed 64-bit range");
        return;
    case QMetaType::Double:
    case QMetaType::Float:
        if (!std::isfinite(value.toDouble()))
            throw ValueError("Lua values cannot contain NaN or infinity");
        return;
    case QMetaType::QString:
        // QString's UTF-8 encoding is at most three bytes per UTF-16 code unit.
        if (value.toString().size() > budget.limits.maximumStringBytes)
            throw ValueError("Lua string exceeds the configured byte limit");
        budget.string(value.toString().toUtf8().size());
        return;
    case QMetaType::QVariantList:
        for (const auto &child : value.toList())
            measure(child, budget, depth + 1);
        return;
    case QMetaType::QStringList:
        for (const auto &child : value.toStringList())
            measure(child, budget, depth + 1);
        return;
    case QMetaType::QVariantMap: {
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            budget.string(it.key().toUtf8().size());
            measure(it.value(), budget, depth + 1);
        }
        return;
    }
    default:
        throw ValueError(
            "Only nil, JSON null, booleans, finite numbers, UTF-8 strings, lists and maps cross the Lua boundary");
    }
}

struct AncestorGuard {
    QSet<const void *> &ancestors;
    const void *pointer;
    ~AncestorGuard() { ancestors.remove(pointer); }
};

} // namespace

quint64 measureValue(const QVariant &value, const RuntimeLimits &limits) {
    ValueBudget budget{limits};
    measure(value, budget, 0);
    return budget.bytes;
}

void pushValue(lua_State *state, const QVariant &value, ValueBudget &budget, int depth) {
    budget.visit(depth);
    if (!lua_checkstack(state, 4))
        throw ValueError("Lua stack has no space for the supplied value");
    if (!value.isValid()) {
        lua_pushnil(state);
        return;
    }
    switch (value.metaType().id()) {
    case QMetaType::Nullptr:
        // Lua nil removes a table entry. Keep explicit JSON null distinct so
        // lists retain their length and maps retain their present null fields.
        lua_pushlightuserdata(state, nullptr);
        return;
    case QMetaType::Bool:
        lua_pushboolean(state, value.toBool());
        return;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
        lua_pushinteger(state, value.toLongLong());
        return;
    case QMetaType::ULongLong:
        if (value.toULongLong() > static_cast<quint64>(std::numeric_limits<lua_Integer>::max()))
            throw ValueError("Lua integer is outside the signed 64-bit range");
        lua_pushinteger(state, static_cast<lua_Integer>(value.toULongLong()));
        return;
    case QMetaType::Double:
    case QMetaType::Float:
        if (!std::isfinite(value.toDouble()))
            throw ValueError("Lua values cannot contain NaN or infinity");
        lua_pushnumber(state, value.toDouble());
        return;
    case QMetaType::QString: {
        const auto text = value.toString().toUtf8();
        budget.string(text.size());
        lua_pushlstring(state, text.constData(), static_cast<size_t>(text.size()));
        return;
    }
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        const auto list = value.toList();
        lua_createtable(state, static_cast<int>(list.size()), 0);
        for (qsizetype i = 0; i < list.size(); ++i) {
            pushValue(state, list.at(i), budget, depth + 1);
            lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
        }
        // Preserve empty arrays when a host snapshot crosses Lua and returns.
        // Plain user-created {} remains an object; Lua has no native array tag.
        if (luaL_newmetatable(state, "Muxora.ValueArray")) {
            lua_pushliteral(state, "array");
            lua_setfield(state, -2, "__metatable");
        }
        lua_setmetatable(state, -2);
        return;
    }
    case QMetaType::QVariantMap: {
        const auto map = value.toMap();
        lua_createtable(state, 0, static_cast<int>(map.size()));
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const auto key = it.key().toUtf8();
            budget.string(key.size());
            lua_pushlstring(state, key.constData(), static_cast<size_t>(key.size()));
            pushValue(state, it.value(), budget, depth + 1);
            lua_rawset(state, -3);
        }
        return;
    }
    default:
        throw ValueError("Unsupported host value type in Lua arguments");
    }
}

QString readText(lua_State *state, int index, ValueBudget &budget) {
    if (lua_type(state, index) != LUA_TSTRING)
        throw ValueError("Lua argument must be a string");
    size_t length = 0;
    const auto *data = lua_tolstring(state, index, &length);
    if (length > static_cast<size_t>(std::numeric_limits<qsizetype>::max()))
        throw ValueError("Lua string is too large");
    budget.string(static_cast<qsizetype>(length));
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(QByteArrayView(data, static_cast<qsizetype>(length)));
    if (decoder.hasError())
        throw ValueError("Lua strings crossing the host boundary must contain valid UTF-8");
    return text;
}

QVariant readValue(lua_State *state, int index, ValueBudget &budget, int depth) {
    budget.visit(depth);
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return {};
    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(state, index) == nullptr)
            return QVariant::fromValue(nullptr);
        [[fallthrough]];
    case LUA_TUSERDATA:
        throw ValueError("Lua functions, threads and userdata cannot cross the host boundary; register "
                         "callbacks explicitly");
    case LUA_TBOOLEAN:
        return static_cast<bool>(lua_toboolean(state, index));
    case LUA_TNUMBER:
        if (lua_isinteger(state, index))
            return QVariant::fromValue<qlonglong>(lua_tointeger(state, index));
        if (!std::isfinite(lua_tonumber(state, index)))
            throw ValueError("Lua values cannot contain NaN or infinity");
        return lua_tonumber(state, index);
    case LUA_TSTRING:
        return readText(state, index, budget);
    case LUA_TTABLE: {
        const void *pointer = lua_topointer(state, index);
        if (budget.ancestors.contains(pointer))
            throw ValueError("Cyclic Lua tables cannot cross the host boundary");
        budget.ancestors.insert(pointer);
        const AncestorGuard guard{budget.ancestors, pointer};
        if (!lua_checkstack(state, 3))
            throw ValueError("Lua stack has no space for reading a table");
        QVariantMap map;
        QMap<qlonglong, QVariant> sequence;
        lua_pushnil(state);
        while (lua_next(state, index)) {
            if (lua_type(state, -2) == LUA_TSTRING) {
                if (!sequence.isEmpty())
                    throw ValueError("Mixed Lua map/list tables cannot cross the host boundary");
                const auto key = readText(state, -2, budget);
                map.insert(key, readValue(state, -1, budget, depth + 1));
            } else if (lua_isinteger(state, -2)) {
                if (!map.isEmpty())
                    throw ValueError("Mixed Lua map/list tables cannot cross the host boundary");
                const auto key = lua_tointeger(state, -2);
                if (key < 1 || key > budget.limits.maximumValueNodes)
                    throw ValueError("Lua lists must use contiguous positive integer keys");
                sequence.insert(key, readValue(state, -1, budget, depth + 1));
            } else {
                throw ValueError("Lua map keys must be strings; list keys must be contiguous integers");
            }
            lua_pop(state, 1);
        }
        if (sequence.isEmpty()) {
            if (map.isEmpty() && lua_getmetatable(state, index)) {
                luaL_getmetatable(state, "Muxora.ValueArray");
                const bool array = lua_rawequal(state, -1, -2);
                lua_pop(state, 2);
                if (array)
                    return QVariantList{};
            }
            return map;
        }
        if (sequence.lastKey() != sequence.size())
            throw ValueError("Sparse Lua lists cannot cross the host boundary");
        QVariantList list;
        list.reserve(sequence.size());
        for (const auto &child : sequence)
            list.append(child);
        return list;
    }
    default:
        throw ValueError("Lua functions, threads and userdata cannot cross the host boundary; register "
                         "callbacks explicitly");
    }
}

} // namespace lua_runtime::detail
