#pragma once

#include <lua_runtime/script_runtime.hpp>
#include <QSet>
#include <stdexcept>

struct lua_State;

namespace lua_runtime::detail {

class ValueError final : public std::runtime_error {
  public:
    explicit ValueError(const QString &message) : std::runtime_error(message.toUtf8().constData()) {}
};

// One budget covers an entire argument/result list, rather than resetting for
// each sibling. It bounds both recursive work and non-Lua transfer allocations.
struct ValueBudget {
    const RuntimeLimits &limits;
    explicit ValueBudget(const RuntimeLimits &requested) : limits(requested) {}
    int nodes = 0;
    quint64 bytes = 0;
    QSet<const void *> ancestors;
    void visit(int depth);
    void string(qsizetype size);
};

void pushValue(lua_State *state, const QVariant &value, ValueBudget &budget, int depth = 0);
QVariant readValue(lua_State *state, int index, ValueBudget &budget, int depth = 0);
quint64 measureValue(const QVariant &value, const RuntimeLimits &limits);
RuntimeLimits normalizedLimits(RuntimeLimits limits);
QString readText(lua_State *state, int index, ValueBudget &budget);

} // namespace lua_runtime::detail
