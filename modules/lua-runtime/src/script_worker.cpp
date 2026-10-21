#include "script_worker.hpp"
#include "variant_codec.hpp"

#include <lauxlib.h>
#include <lualib.h>
// Private dependency on the pinned Lua version: user hooks remain stoppable.
#include <lstate.h>
#include <chrono>
#include <numeric>
#include <cstring>
#include <QRandomGenerator>
#include <QThread>
#include <cstdlib>
#include <limits>

namespace lua_runtime::detail {
namespace {

constexpr int hookInterval = 1000;
constexpr int maximumPendingLogs = 256;
char userHooksKey;
char hostYieldMarker;

qint64 executionClock() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr int runtimeHookMask = LUA_MASKCOUNT;
constexpr int stoppingHookMask = LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET;

struct TraceInput {
    lua_State *source;
    const char *message;
};
int buildTraceback(lua_State *state) {
    const auto *input = static_cast<TraceInput *>(lua_touserdata(state, 1));
    luaL_traceback(state, input->source, input->message, 0);
    return 1;
}

ScriptResult failure(QString message) { return {false, {}, std::move(message)}; }

// Private upvalues preserve protected-call semantics and forward host yields
// through every user coroutine. Stops remain sticky even after caught errors.
constexpr char guardedCalls[] = R"lua(
local raw_pcall, raw_xpcall, check, raw_resume, create, raw_yield, close, marker = ...
local pack, unpack, raise = table.pack, table.unpack, error
pcall = function(...)
    local result = pack(raw_pcall(...))
    check()
    return unpack(result, 1, result.n)
end
xpcall = function(...)
    local result = pack(raw_xpcall(...))
    check()
    return unpack(result, 1, result.n)
end
local function resume(co, ...)
    local result = pack(raw_resume(co, ...))
    check()
    while result[1] and result[2] == marker do
        result = pack(raw_resume(co, raw_yield(marker)))
        check()
    end
    return unpack(result, 1, result.n)
end
coroutine.resume = resume
coroutine.wrap = function(f)
    local co = create(f)
    return function(...)
        local result = pack(resume(co, ...))
        if not result[1] then
            local ok, err = close(co)
            raise(ok and result[2] or err, 2)
        end
        return unpack(result, 2, result.n)
    end
end
)lua";

} // namespace

ScriptWorker::ScriptWorker(RuntimeLimits limits, std::shared_ptr<SharedStatistics> statistics)
    : limits_(limits), statistics_(std::move(statistics)) {
    hostDeadline_.setParent(this);
    hostDeadline_.setSingleShot(true);
    connect(&hostDeadline_, &QTimer::timeout, this, [this] {
        if (!active_ || !active_->awaitingHost)
            return;
        setAbort(QStringLiteral("Lua host operation timed out"));
        finish(failure(active_->abortReason));
        schedulePump();
    });
}

ScriptWorker::~ScriptWorker() { closeState(); }

ScriptWorker *ScriptWorker::self(lua_State *state) {
    return *static_cast<ScriptWorker **>(lua_getextraspace(state));
}

void *ScriptWorker::allocate(void *user, void *pointer, size_t oldSize, size_t newSize) {
    auto *worker = static_cast<ScriptWorker *>(user);
    const auto previous = pointer ? static_cast<quint64>(oldSize) : 0;
    auto &statistics = *worker->statistics_;
    const auto used = statistics.memory.load(std::memory_order_relaxed);
    if (!newSize) {
        std::free(pointer);
        statistics.memory.store(used - previous, std::memory_order_relaxed);
        return nullptr;
    }
    const auto requested = static_cast<quint64>(newSize);
    void *result = std::realloc(pointer, newSize);
    if (!result)
        return nullptr;
    const auto current = used - previous + requested;
    statistics.memory.store(current, std::memory_order_relaxed);
    auto peak = statistics.peak.load(std::memory_order_relaxed);
    while (current > peak &&
           !statistics.peak.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
    }
    return result;
}

bool ScriptWorker::stale(const ScriptJob &job) const {
    return shutdown_ || statistics_->stopping.load(std::memory_order_acquire) || job.generation != statistics_->generation.load(std::memory_order_acquire) ||
           job.cancellation != statistics_->cancellation.load(std::memory_order_acquire);
}

void ScriptWorker::setAbort(QString message) {
    if (!active_ || active_->aborted)
        return;
    active_->aborted = true;
    active_->abortReason = std::move(message);
}

bool ScriptWorker::checkCurrentStop() {
    if (cleanup_)
        return false;
    if (!active_)
        return true;
    if (stale(active_->job))
        setAbort(QStringLiteral("Lua task was cancelled"));
    return !active_->aborted;
}

void ScriptWorker::hook(lua_State *state, lua_Debug *debug) {
    auto *worker = self(state);
    if (!worker->checkCurrentStop()) {
        // Dense checks are needed only after cancellation. Call/return hooks
        // prevent raw pcall, obtained through debug upvalues, from repeatedly
        // catching the count hook's error inside another protected scope.
        if (!worker->cleanup_)
            lua_sethook(state, hook, stoppingHookMask, 1);
        // Only count/line hooks may yield. At C-call boundaries raise an
        // error before entering another protected scope, so raw pcall cannot
        // repeatedly swallow a stop obtained through debug introspection.
        if (!worker->cleanup_ && lua_isyieldable(state) &&
            (debug->event == LUA_HOOKCOUNT || debug->event == LUA_HOOKLINE)) {
            lua_yield(state, 0);
            return;
        }
        luaL_error(state, "Lua task was cancelled");
        return;
    }
    if (!worker->active_ || worker->cleanup_ || worker->executingUserHooks_.contains(state))
        return;
    const int top = lua_gettop(state);
    lua_rawgetp(state, LUA_REGISTRYINDEX, &userHooksKey);
    lua_pushthread(state);
    lua_rawget(state, -2);
    if (!lua_istable(state, -1)) {
        restoreRuntimeHook(state, 0, 0);
        lua_settop(state, top);
        return;
    }
    const int record = lua_gettop(state);
    lua_rawgeti(state, record, 2);
    const int mask = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    lua_rawgeti(state, record, 3);
    const int count = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    restoreRuntimeHook(state, mask, count);
    bool invoke = false;
    if (debug->event == LUA_HOOKCOUNT && (mask & LUA_MASKCOUNT)) {
        lua_rawgeti(state, record, 4);
        const auto remaining = lua_tointeger(state, -1) - lua_gethookcount(state);
        lua_pop(state, 1);
        if (remaining <= 0) {
            lua_rawgeti(state, record, 3);
            lua_rawseti(state, record, 4);
            invoke = true;
        } else {
            lua_pushinteger(state, remaining);
            lua_rawseti(state, record, 4);
        }
    } else {
        const int eventMask = debug->event == LUA_HOOKTAILCALL ? LUA_MASKCALL : (1 << debug->event);
        invoke = (mask & eventMask) != 0;
    }
    if (invoke) {
        constexpr const char *events[] = {"call", "return", "line", "count", "tail call"};
        lua_rawgeti(state, record, 1);
        lua_pushstring(state, events[debug->event]);
        if (debug->currentline >= 0)
            lua_pushinteger(state, debug->currentline);
        else
            lua_pushnil(state);
        // Lua suppresses hooks inside hooks. Re-enable only our C dispatcher
        // here; its re-entry guard suppresses user callbacks, but stop checks
        // still run inside an accidental infinite loop in the user's hook.
        struct HookScope {
            ScriptWorker *worker;
            lua_State *state;
            lu_byte previous;
            HookScope(ScriptWorker *w, lua_State *s) : worker(w), state(s), previous(s->allowhook) {
                worker->executingUserHooks_.insert(state);
                state->allowhook = 1;
            }
            ~HookScope() {
                state->allowhook = previous;
                worker->executingUserHooks_.remove(state);
            }
        };
        int status;
        {
            const HookScope scope(worker, state);
            status = lua_pcall(state, 2, 0, 0);
        }
        if (status != LUA_OK)
            lua_error(state);
        if (!worker->checkCurrentStop())
            luaL_error(state, "Lua task was cancelled");
    }
    lua_settop(state, top);
}

void ScriptWorker::restoreRuntimeHook(lua_State *state, int userMask, int userCount) {
    // A coroutine saved by the script can outlive a cancelled task. When it
    // runs again, discard that task's dense stop checks without losing its
    // user hook or leaving call/return hooks on an otherwise unhooked thread.
    const int mask = runtimeHookMask | userMask;
    const int interval = userCount > 0 ? std::gcd(hookInterval, userCount) : hookInterval;
    if (lua_gethookmask(state) != mask || lua_gethookcount(state) != interval)
        lua_sethook(state, hook, mask, interval);
}

int ScriptWorker::checkStop(lua_State *state) {
    auto *worker = self(state);
    if (worker->checkCurrentStop())
        return 0;
    if (!worker->cleanup_ && lua_isyieldable(state))
        return lua_yield(state, 0);
    return luaL_error(state, "Lua task was cancelled");
}

int ScriptWorker::setUserHook(lua_State *state) {
    int argument = 1;
    auto *target = lua_isthread(state, 1) ? lua_tothread(state, argument++) : state;
    const bool clear = lua_isnoneornil(state, argument);
    int count = 0;
    int mask = 0;
    if (!clear) {
        luaL_checktype(state, argument, LUA_TFUNCTION);
        const auto *flags = luaL_checkstring(state, argument + 1);
        count = static_cast<int>(luaL_optinteger(state, argument + 2, 0));
        if (std::strchr(flags, 'c')) mask |= LUA_MASKCALL;
        if (std::strchr(flags, 'r')) mask |= LUA_MASKRET;
        if (std::strchr(flags, 'l')) mask |= LUA_MASKLINE;
        if (count > 0) mask |= LUA_MASKCOUNT;
    }
    lua_rawgetp(state, LUA_REGISTRYINDEX, &userHooksKey);
    if (target != state && !lua_checkstack(target, 1))
        return luaL_error(state, "stack overflow");
    lua_pushthread(target);
    lua_xmove(target, state, 1);
    if (clear) {
        lua_pushnil(state);
    } else {
        lua_createtable(state, 4, 0);
        lua_pushvalue(state, argument);
        lua_rawseti(state, -2, 1);
        lua_pushinteger(state, mask);
        lua_rawseti(state, -2, 2);
        lua_pushinteger(state, count);
        lua_rawseti(state, -2, 3);
        lua_pushinteger(state, count);
        lua_rawseti(state, -2, 4);
    }
    lua_rawset(state, -3);
    // A divisor gives exact user count intervals, including nonmultiples of
    // our normal checkpoint frequency (for example 1500 instructions).
    const int interval = count > 0 ? std::gcd(hookInterval, count) : hookInterval;
    lua_sethook(target, hook, runtimeHookMask | mask, interval);
    return 0;
}

int ScriptWorker::getUserHook(lua_State *state) {
    auto *target = lua_isthread(state, 1) ? lua_tothread(state, 1) : state;
    lua_rawgetp(state, LUA_REGISTRYINDEX, &userHooksKey);
    if (target != state && !lua_checkstack(target, 1))
        return luaL_error(state, "stack overflow");
    lua_pushthread(target);
    lua_xmove(target, state, 1);
    lua_rawget(state, -2);
    if (!lua_istable(state, -1)) {
        lua_pushnil(state);
        return 1;
    }
    const int record = lua_gettop(state);
    lua_rawgeti(state, record, 2);
    const auto mask = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    if (mask == 0) {
        lua_pushnil(state);
        return 1;
    }
    lua_rawgeti(state, record, 1);
    char flags[4];
    int length = 0;
    if (mask & LUA_MASKCALL) flags[length++] = 'c';
    if (mask & LUA_MASKRET) flags[length++] = 'r';
    if (mask & LUA_MASKLINE) flags[length++] = 'l';
    lua_pushlstring(state, flags, static_cast<size_t>(length));
    lua_rawgeti(state, record, 3);
    return 3;
}

int ScriptWorker::initialize(lua_State *state) {
    luaL_openlibs(state);
    // This statically linked C++ Lua has no DLL ABI for native Lua modules.
    // Keep loadlib available for its standard diagnostic return contract.
    lua_getglobal(state, LUA_LOADLIBNAME);
    lua_pushliteral(state, "");
    lua_setfield(state, -2, "cpath");
    lua_pop(state, 1);
    lua_newtable(state);
    lua_newtable(state);
    lua_pushliteral(state, "k");
    lua_setfield(state, -2, "__mode");
    lua_setmetatable(state, -2);
    lua_rawsetp(state, LUA_REGISTRYINDEX, &userHooksKey);
    lua_getglobal(state, LUA_DBLIBNAME);
    lua_pushcfunction(state, setUserHook);
    lua_setfield(state, -2, "sethook");
    lua_pushcfunction(state, getUserHook);
    lua_setfield(state, -2, "gethook");
    lua_pop(state, 1);

    lua_newtable(state);
    lua_pushcfunction(state, hostCall);
    lua_setfield(state, -2, "_host");
    lua_pushcfunction(state, createCallback);
    lua_setfield(state, -2, "_callback");
    lua_pushcfunction(state, releaseRetainedCallback);
    lua_setfield(state, -2, "_release");
    lua_setglobal(state, "muxora");
    lua_pushcfunction(state, print);
    lua_setglobal(state, "print");

    if (luaL_loadbufferx(state, guardedCalls, sizeof(guardedCalls) - 1, "=runtime/protected-calls", "t") !=
        LUA_OK)
        return lua_error(state);
    lua_getglobal(state, "pcall");
    lua_getglobal(state, "xpcall");
    lua_pushcfunction(state, checkStop);
    lua_getglobal(state, LUA_COLIBNAME);
    const int coroutineLibrary = lua_gettop(state);
    for (const auto *name : {"resume", "create", "yield", "close"})
        lua_getfield(state, coroutineLibrary, name);
    lua_remove(state, coroutineLibrary);
    lua_pushlightuserdata(state, &hostYieldMarker);
    lua_call(state, 8, 0);
    return 0;
}

bool ScriptWorker::ensureState() {
    if (state_)
        return true;
    stateGeneration_ = statistics_->generation.load(std::memory_order_acquire);
    state_ = lua_newstate(allocate, this, QRandomGenerator::global()->generate());
    if (!state_) {
        initializationError_ =
            QStringLiteral("Unable to allocate the Lua interpreter");
        return false;
    }
    *static_cast<ScriptWorker **>(lua_getextraspace(state_)) = this;
    lua_sethook(state_, hook, runtimeHookMask, hookInterval);
    lua_pushcfunction(state_, initialize);
    const int status = lua_pcall(state_, 0, 0, 0);
    if (status == LUA_OK)
        return true;
    initializationError_ = stackError(state_, status);
    closeState();
    return false;
}

void ScriptWorker::closeState() {
    if (!state_)
        return;
    cleanup_ = true;
    // Finalizers cannot run unbounded Lua while a reset/destructor closes the
    // state. In this scope even a single Lua instruction raises an error.
    lua_sethook(state_, hook, LUA_MASKCOUNT, 1);
    lua_close(state_);
    state_ = nullptr;
    cleanup_ = false;
    executingUserHooks_.clear();
    callbacks_.clear();
    statistics_->retainedCallbacks.store(0, std::memory_order_release);
}

void ScriptWorker::enqueue(ScriptJob job) {
    if (stale(job)) {
        finishQueued(std::move(job), QStringLiteral("Lua task was cancelled"));
        return;
    }
    queue_.enqueue(std::move(job));
    schedulePump();
}

void ScriptWorker::schedulePump() {
    if (pumpQueued_ || shutdown_)
        return;
    pumpQueued_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            pumpQueued_ = false;
            pump();
        },
        Qt::QueuedConnection);
}

void ScriptWorker::pump() {
    if (active_ || queue_.isEmpty() || shutdown_)
        return;
    ScriptJob job = queue_.dequeue();
    if (stale(job)) {
        finishQueued(std::move(job), QStringLiteral("Lua task was cancelled"));
        schedulePump();
        return;
    }
    if (!ensureState()) {
        finishQueued(std::move(job), initializationError_);
        schedulePump();
        return;
    }
    active_ = std::make_unique<ActiveJob>();
    active_->job = std::move(job);
    lua_pushcfunction(state_, prepare);
    statistics_->executionStarted.store(executionClock(), std::memory_order_release);
    const int status = lua_pcall(state_, 0, 0, 0);
    if (status != LUA_OK) {
        finish(failure(stackError(state_, status)));
        lua_settop(state_, 0);
        schedulePump();
        return;
    }
    resume(active_->job.callbackId.isEmpty() ? 1 : 2);
}

int ScriptWorker::prepare(lua_State *state) {
    auto *worker = self(state);
    auto &active = *worker->active_;
    active.coroutine = lua_newthread(state);
    active.reference = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_sethook(active.coroutine, hook, runtimeHookMask, hookInterval);
    try {
        if (active.job.callbackId.isEmpty()) {
            const auto &source = active.job.source;
            const int status =
                luaL_loadbufferx(active.coroutine, source.constData(), static_cast<size_t>(source.size()),
                                 active.job.name.constData(), "t");
            if (status != LUA_OK) {
                lua_xmove(active.coroutine, state, 1);
                return lua_error(state);
            }
        } else {
            const auto it = worker->callbacks_.constFind(active.job.callbackId);
            if (it == worker->callbacks_.cend())
                return luaL_error(state, "Lua callback no longer exists");
            lua_rawgeti(active.coroutine, LUA_REGISTRYINDEX, it.value());
        }
        ValueBudget budget{worker->limits_};
        pushValue(active.coroutine, active.job.context, budget);
        if (!active.job.callbackId.isEmpty())
            pushValue(active.coroutine, active.job.event, budget);
    } catch (const ValueError &error) {
        return luaL_error(state, "%s", error.what());
    }
    return 0;
}

void ScriptWorker::resume(int arguments) {
    if (!active_)
        return;
    if (!checkCurrentStop()) {
        finish(failure(active_->abortReason));
        schedulePump();
        return;
    }
    active_->awaitingHost = false;
    statistics_->executionStarted.store(executionClock(), std::memory_order_release);
    int results = 0;
    const int status = lua_resume(active_->coroutine, nullptr, arguments, &results);
    statistics_->executionStarted.store(0, std::memory_order_release);
    checkCurrentStop();
    if (active_->aborted) {
        finish(failure(active_->abortReason));
    } else if (status == LUA_YIELD && active_->awaitingHost && results == 1 &&
               lua_touserdata(active_->coroutine, -1) == &hostYieldMarker) {
        lua_pop(active_->coroutine, results);
        hostDeadline_.start(limits_.hostReplyTimeoutMs);
        return;
    } else if (status != LUA_OK) {
        finish(failure(status == LUA_YIELD ? QStringLiteral("Unexpected Lua coroutine suspension")
                                           : stackError(active_->coroutine, status)));
    } else {
        try {
            ValueBudget budget{limits_};
            QVariantList values;
            for (int i = results; i > 0; --i)
                values.append(readValue(active_->coroutine, -i, budget));
            finish({true, std::move(values), {}});
        } catch (const ValueError &error) {
            finish(failure(QString::fromUtf8(error.what())));
        }
    }
    schedulePump();
}

int ScriptWorker::hostCall(lua_State *state) {
    auto *worker = self(state);
    if (!worker->active_ || worker->cleanup_ || !lua_isyieldable(state))
        return luaL_error(state, "Host operations require a yieldable Lua task");
    if (!worker->checkCurrentStop())
        return checkStop(state);
    try {
        ValueBudget budget{worker->limits_};
        const auto method = readText(state, 1, budget);
        if (method.isEmpty())
            return luaL_error(state, "Host method cannot be empty");
        QVariantList arguments;
        for (int i = 2; i <= lua_gettop(state); ++i)
            arguments.append(readValue(state, i, budget));
        auto &active = *worker->active_;
        if (active.hostCoroutineReference >= 0)
            luaL_unref(state, LUA_REGISTRYINDEX, active.hostCoroutineReference);
        active.hostCoroutine = state;
        lua_pushthread(state);
        active.hostCoroutineReference = luaL_ref(state, LUA_REGISTRYINDEX);
        active.awaitingHost = true;
        worker->statistics_->activeHostJob.store(active.job.id, std::memory_order_release);
        emit worker->hostRequested(active.job.id, active.job.generation, active.job.cancellation, method,
                                   std::move(arguments), active.job.context);
    } catch (const ValueError &error) {
        return luaL_error(state, "%s", error.what());
    }
    lua_pushlightuserdata(state, &hostYieldMarker);
    return lua_yieldk(state, 1, static_cast<lua_KContext>(worker->active_->job.id), hostContinuation);
}

void ScriptWorker::hostReply(quint64 job, quint64 generation, quint64 cancellation, ScriptResult result) {
    if (!active_ || !active_->awaitingHost || active_->job.id != job ||
        active_->job.generation != generation || active_->job.cancellation != cancellation ||
        stale(active_->job))
        return;
    hostDeadline_.stop();
    statistics_->activeHostJob.store(0, std::memory_order_release);
    active_->reply = std::move(result);
    resume(0);
}

int ScriptWorker::hostContinuation(lua_State *state, int, lua_KContext context) {
    auto *worker = self(state);
    if (!worker->active_ || worker->active_->job.id != static_cast<quint64>(context))
        return luaL_error(state, "Lua task no longer exists");
    if (!worker->checkCurrentStop())
        return checkStop(state);
    auto result = std::move(worker->active_->reply);
    luaL_unref(state, LUA_REGISTRYINDEX, worker->active_->hostCoroutineReference);
    worker->active_->hostCoroutineReference = LUA_NOREF;
    worker->active_->hostCoroutine = nullptr;
    if (!result.ok) {
        const auto error = result.error.left(worker->limits_.maximumStringBytes).toUtf8();
        lua_pushlstring(state, error.constData(), static_cast<size_t>(error.size()));
        return lua_error(state);
    }
    try {
        ValueBudget budget{worker->limits_};
        if (!lua_checkstack(state, static_cast<int>(result.values.size())))
            return luaL_error(state, "Lua stack has no space for host results");
        for (const auto &value : result.values)
            pushValue(state, value, budget);
        return static_cast<int>(result.values.size());
    } catch (const ValueError &error) {
        return luaL_error(state, "%s", error.what());
    }
}

int ScriptWorker::createCallback(lua_State *state) {
    auto *worker = self(state);
    if (!worker->active_ || worker->cleanup_)
        return luaL_error(state, "Cannot register a Lua callback during cleanup");
    if (lua_type(state, 1) != LUA_TFUNCTION)
        return luaL_error(state, "Callback must be a Lua function");
    if (worker->callbacks_.size() >= worker->limits_.maximumCallbacks)
        return luaL_error(state, "Lua callback count exceeds its configured limit");
    const auto id = QString::number(worker->stateGeneration_) + QLatin1Char(':') +
                    QString::number(++worker->nextCallback_);
    const auto encoded = id.toUtf8();
    // Allocate the returned string before retaining the callback, so OOM cannot
    // leave an unreachable retained function behind.
    lua_pushlstring(state, encoded.constData(), static_cast<size_t>(encoded.size()));
    lua_pushvalue(state, 1);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    worker->callbacks_.insert(id, reference);
    worker->statistics_->retainedCallbacks.fetch_add(1, std::memory_order_release);
    return 1;
}

int ScriptWorker::releaseRetainedCallback(lua_State *state) {
    if (lua_type(state, 1) != LUA_TSTRING)
        return luaL_error(state, "Callback ID must be a string");
    size_t size = 0;
    const auto *data = lua_tolstring(state, 1, &size);
    if (size > 128)
        return luaL_error(state, "Invalid callback ID");
    self(state)->releaseCallback(QString::fromUtf8(data, static_cast<qsizetype>(size)));
    return 0;
}

int ScriptWorker::print(lua_State *state) {
    auto *worker = self(state);
    if (!worker->active_ || worker->cleanup_)
        return 0;
    if (!worker->checkCurrentStop())
        return checkStop(state);
    auto &active = *worker->active_;
    if (active.logMessages++ >= worker->limits_.maximumLogMessagesPerJob)
        return 0;
    QByteArray text;
    const int limit = worker->limits_.maximumLogMessageBytes;
    for (int index = 1, count = lua_gettop(state); index <= count && text.size() < limit; ++index) {
        if (index > 1)
            text.append('\t');
        size_t length = 0;
        const auto *value = luaL_tolstring(state, index, &length);
        const auto available = static_cast<size_t>(qMax<qsizetype>(0, limit - text.size()));
        text.append(value, static_cast<qsizetype>(qMin(length, available)));
        lua_pop(state, 1);
    }
    const int queued = worker->statistics_->pendingLogs.fetch_add(1, std::memory_order_relaxed);
    if (queued >= maximumPendingLogs) {
        worker->statistics_->pendingLogs.fetch_sub(1, std::memory_order_relaxed);
        return 0;
    }
    emit worker->logReady(QString::fromUtf8(text));
    return 0;
}

QString ScriptWorker::stackError(lua_State *state, int status) const {
    if (status == LUA_ERRMEM)
        return QStringLiteral("Lua allocator could not allocate memory");
    if (lua_type(state, -1) != LUA_TSTRING)
        return QStringLiteral("Lua task failed with a non-string error");
    size_t size = 0;
    const auto *message = lua_tolstring(state, -1, &size);
    const auto length = qMin(size, static_cast<size_t>(limits_.maximumLogMessageBytes));
    const QByteArray original(message, static_cast<qsizetype>(length));
    const auto fallback = QString::fromUtf8(original);
    if (!state_ || !lua_checkstack(state_, 4))
        return fallback;
    const int top = lua_gettop(state_);
    TraceInput input{state, original.constData()};
    lua_pushcfunction(state_, buildTraceback);
    lua_pushlightuserdata(state_, &input);
    const int traceStatus = lua_pcall(state_, 1, 1, 0);
    QString trace = fallback;
    if (traceStatus == LUA_OK && lua_type(state_, -1) == LUA_TSTRING) {
        size_t traceSize = 0;
        const auto *data = lua_tolstring(state_, -1, &traceSize);
        trace = QString::fromUtf8(
            data, qMin<qsizetype>(static_cast<qsizetype>(traceSize), limits_.maximumLogMessageBytes));
    }
    lua_settop(state_, top);
    return trace;
}

void ScriptWorker::disposeCoroutine() {
    if (!active_ || !active_->coroutine)
        return;
    const bool previousCleanup = cleanup_;
    cleanup_ = true;
    if (active_->hostCoroutineReference >= 0) {
        if (active_->hostCoroutine != active_->coroutine) {
            lua_sethook(active_->hostCoroutine, hook, LUA_MASKCOUNT, 1);
            lua_closethread(active_->hostCoroutine, state_);
        }
        luaL_unref(state_, LUA_REGISTRYINDEX, active_->hostCoroutineReference);
    }
    lua_sethook(active_->coroutine, hook, LUA_MASKCOUNT, 1);
    lua_closethread(active_->coroutine, state_);
    luaL_unref(state_, LUA_REGISTRYINDEX, active_->reference);
    active_->coroutine = nullptr;
    cleanup_ = previousCleanup;
}

void ScriptWorker::finish(ScriptResult result) {
    if (!active_)
        return;
    hostDeadline_.stop();
    statistics_->executionStarted.store(0, std::memory_order_release);
    statistics_->activeHostJob.store(0, std::memory_order_release);
    disposeCoroutine();
    auto job = std::move(active_->job);
    active_.reset();
    deliver(std::move(job), std::move(result));
}

void ScriptWorker::finishQueued(ScriptJob job, const QString &error) {
    deliver(std::move(job), failure(error));
}

void ScriptWorker::deliver(ScriptJob job, ScriptResult result) {
    quint64 outputBytes = 0;
    if (result.ok) {
        try {
            outputBytes = measureValue(result.values, limits_);
        } catch (const ValueError &error) {
            result = failure(QString::fromUtf8(error.what()));
        }
    }
    // Pending result events share the same transfer budget as queued inputs.
    // A blocked owner thread therefore cannot accumulate unbounded result data.
    auto retained = statistics_->queuedBytes.load(std::memory_order_acquire);
    while (true) {
        const auto others = retained - job.retainedBytes;
        if (outputBytes > limits_.maximumQueuedBytes - qMin(others, limits_.maximumQueuedBytes)) {
            result = failure(QStringLiteral("Lua result exceeds the pending transfer byte limit"));
            outputBytes = 0;
        }
        if (statistics_->queuedBytes.compare_exchange_weak(retained, others + outputBytes,
                                                           std::memory_order_acq_rel))
            break;
    }
    if (!result.ok)
        result.values.clear();
    result.error = result.error.left(limits_.maximumLogMessageBytes);
    emit jobFinished(job.id, std::move(result), outputBytes);
}

void ScriptWorker::releaseCallback(const QString &id) {
    const auto it = callbacks_.find(id);
    if (it == callbacks_.end())
        return;
    luaL_unref(state_, LUA_REGISTRYINDEX, it.value());
    callbacks_.erase(it);
    statistics_->retainedCallbacks.fetch_sub(1, std::memory_order_release);
}

void ScriptWorker::cancel() {
    if (active_ && stale(active_->job))
        finish(failure(QStringLiteral("Lua task was cancelled")));
    QQueue<ScriptJob> retained;
    while (!queue_.isEmpty()) {
        auto job = queue_.dequeue();
        if (stale(job))
            finishQueued(std::move(job), QStringLiteral("Lua task was cancelled"));
        else
            retained.enqueue(std::move(job));
    }
    queue_ = std::move(retained);
    schedulePump();
}

void ScriptWorker::reset(quint64 generation) {
    cancel();
    if (generation != statistics_->generation.load(std::memory_order_acquire))
        return;
    closeState();
    initializationError_.clear();
    emit didReset(generation);
}

void ScriptWorker::shutdown() {
    shutdown_ = true;
    cancel();
    closeState();
    QThread::currentThread()->quit();
}

} // namespace lua_runtime::detail
