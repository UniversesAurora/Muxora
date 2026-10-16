#include "script_worker.hpp"
#include "variant_codec.hpp"

#include <lauxlib.h>
#include <lualib.h>
#include <QRandomGenerator>
#include <QThread>
#include <cstdlib>
#include <limits>

namespace lua_runtime::detail {
namespace {

constexpr int hookInterval = 1000;
constexpr int maximumPendingLogs = 256;

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

// Lua calls these wrappers after each protected call, including protected calls
// whose body entered a non-yieldable library callback. The original functions
// and budget checker are private upvalues: replacing global names cannot bypass
// a sticky cancellation, instruction limit, or deadline.
constexpr char guardedCalls[] = R"lua(
local raw_pcall, raw_xpcall, check = ...
local pack, unpack = table.pack, table.unpack
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
    if (requested > previous &&
        requested - previous > worker->limits_.memoryBytes - qMin(used, worker->limits_.memoryBytes))
        return nullptr;
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
    return shutdown_ || job.generation != statistics_->generation.load(std::memory_order_acquire) ||
           job.cancellation != statistics_->cancellation.load(std::memory_order_acquire);
}

void ScriptWorker::setAbort(QString message) {
    if (!active_ || active_->aborted)
        return;
    active_->aborted = true;
    active_->abortReason = std::move(message);
}

bool ScriptWorker::checkCurrentBudget() {
    if (cleanup_)
        return false;
    if (!active_)
        return true;
    if (stale(active_->job))
        setAbort(QStringLiteral("Lua task was cancelled"));
    if (active_->instructions >= limits_.instructionLimit)
        setAbort(QStringLiteral("Lua task exceeded its instruction limit"));
    const auto current = resumeTimer_.isValid() ? resumeTimer_.nsecsElapsed() : 0;
    if (active_->elapsedNanoseconds + current >= static_cast<qint64>(limits_.timeLimitMs) * 1'000'000)
        setAbort(QStringLiteral("Lua task exceeded its execution time limit"));
    return !active_->aborted;
}

void ScriptWorker::hook(lua_State *state, lua_Debug *) {
    auto *worker = self(state);
    if (worker->active_ && !worker->cleanup_)
        worker->active_->instructions +=
            static_cast<quint64>(qMin<quint64>(hookInterval, worker->limits_.instructionLimit));
    if (worker->checkCurrentBudget())
        return;
    // A yield crosses pcall/xpcall without being caught. Library callbacks that
    // cannot yield raise first; the private protected-call wrappers check the
    // sticky abort immediately after that boundary returns.
    if (!worker->cleanup_ && lua_isyieldable(state)) {
        lua_yield(state, 0);
        return;
    }
    luaL_error(state, "Lua task stopped by the runtime budget");
}

int ScriptWorker::checkBudget(lua_State *state) {
    auto *worker = self(state);
    if (worker->checkCurrentBudget())
        return 0;
    if (!worker->cleanup_ && lua_isyieldable(state))
        return lua_yield(state, 0);
    return luaL_error(state, "Lua task stopped by the runtime budget");
}

int ScriptWorker::initialize(lua_State *state) {
    const luaL_Reg libraries[] = {{"_G", luaopen_base},
                                  {LUA_MATHLIBNAME, luaopen_math},
                                  {LUA_STRLIBNAME, luaopen_string},
                                  {LUA_TABLIBNAME, luaopen_table},
                                  {LUA_UTF8LIBNAME, luaopen_utf8},
                                  {nullptr, nullptr}};
    for (const auto *library = libraries; library->func; ++library) {
        luaL_requiref(state, library->name, library->func, 1);
        lua_pop(state, 1);
    }
    for (const auto *name : {"load", "loadfile", "dofile", "collectgarbage"}) {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
    lua_getglobal(state, LUA_STRLIBNAME);
    lua_pushnil(state);
    lua_setfield(state, -2, "dump");
    lua_pop(state, 1);

    lua_newtable(state);
    lua_pushcfunction(state, hostCall);
    lua_setfield(state, -2, "_host");
    lua_pushcfunction(state, createCallback);
    lua_setfield(state, -2, "_callback");
    lua_pushcfunction(state, releaseRetainedCallback);
    lua_setfield(state, -2, "_release");
    lua_pushcfunction(state, compile);
    lua_setfield(state, -2, "_compile");
    lua_setglobal(state, "muxora");
    lua_pushcfunction(state, print);
    lua_setglobal(state, "print");

    if (luaL_loadbufferx(state, guardedCalls, sizeof(guardedCalls) - 1, "=runtime/protected-calls", "t") !=
        LUA_OK)
        return lua_error(state);
    lua_getglobal(state, "pcall");
    lua_getglobal(state, "xpcall");
    lua_pushcfunction(state, checkBudget);
    lua_call(state, 3, 0);
    return 0;
}

bool ScriptWorker::ensureState() {
    if (state_)
        return true;
    stateGeneration_ = statistics_->generation.load(std::memory_order_acquire);
    state_ = lua_newstate(allocate, this, QRandomGenerator::global()->generate());
    if (!state_) {
        initializationError_ =
            QStringLiteral("Unable to allocate the Lua interpreter within its memory budget");
        return false;
    }
    *static_cast<ScriptWorker **>(lua_getextraspace(state_)) = this;
    lua_sethook(state_, hook, LUA_MASKCOUNT,
                static_cast<int>(qMin<quint64>(hookInterval, limits_.instructionLimit)));
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
    resumeTimer_.start();
    const int status = lua_pcall(state_, 0, 0, 0);
    active_->elapsedNanoseconds += resumeTimer_.nsecsElapsed();
    resumeTimer_.invalidate();
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
    const auto count = static_cast<int>(qMin<quint64>(hookInterval, worker->limits_.instructionLimit));
    lua_sethook(active.coroutine, hook, LUA_MASKCOUNT, count);
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
    if (!checkCurrentBudget()) {
        finish(failure(active_->abortReason));
        schedulePump();
        return;
    }
    active_->awaitingHost = false;
    resumeTimer_.start();
    int results = 0;
    const int status = lua_resume(active_->coroutine, nullptr, arguments, &results);
    active_->elapsedNanoseconds += resumeTimer_.nsecsElapsed();
    resumeTimer_.invalidate();
    checkCurrentBudget();
    if (active_->aborted) {
        finish(failure(active_->abortReason));
    } else if (status == LUA_YIELD && active_->awaitingHost) {
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
    if (!worker->checkCurrentBudget())
        return checkBudget(state);
    try {
        ValueBudget budget{worker->limits_};
        const auto method = readText(state, 1, budget);
        if (method.isEmpty())
            return luaL_error(state, "Host method cannot be empty");
        QVariantList arguments;
        for (int i = 2; i <= lua_gettop(state); ++i)
            arguments.append(readValue(state, i, budget));
        auto &active = *worker->active_;
        active.awaitingHost = true;
        worker->statistics_->activeHostJob.store(active.job.id, std::memory_order_release);
        emit worker->hostRequested(active.job.id, active.job.generation, active.job.cancellation, method,
                                   std::move(arguments), active.job.context);
    } catch (const ValueError &error) {
        return luaL_error(state, "%s", error.what());
    }
    return lua_yieldk(state, 0, 0, hostContinuation);
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

int ScriptWorker::hostContinuation(lua_State *state, int, lua_KContext) {
    auto *worker = self(state);
    if (!worker->active_)
        return luaL_error(state, "Lua task no longer exists");
    if (!worker->checkCurrentBudget())
        return checkBudget(state);
    auto result = std::move(worker->active_->reply);
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

int ScriptWorker::compile(lua_State *state) {
    auto *worker = self(state);
    if (!worker->active_ || worker->cleanup_)
        return luaL_error(state, "Cannot compile Lua during cleanup");
    if (lua_type(state, 1) != LUA_TSTRING)
        return luaL_error(state, "Lua module source must be text");
    size_t length = 0;
    const auto *source = lua_tolstring(state, 1, &length);
    if (length > static_cast<size_t>(worker->limits_.maximumSourceBytes))
        return luaL_error(state, "Lua module source exceeds its configured byte limit");
    try {
        ValueBudget budget{worker->limits_};
        const auto name =
            lua_gettop(state) > 1 ? readText(state, 2, budget).left(4096).toUtf8() : QByteArray("=module");
        if (luaL_loadbufferx(state, source, length, name.constData(), "t") != LUA_OK)
            return lua_error(state);
    } catch (const ValueError &error) {
        return luaL_error(state, "%s", error.what());
    }
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
    if (!worker->checkCurrentBudget())
        return checkBudget(state);
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
        return QStringLiteral("Lua task exceeded its memory budget");
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
    emit didReset();
}

void ScriptWorker::shutdown() {
    shutdown_ = true;
    cancel();
    closeState();
}

} // namespace lua_runtime::detail
