#include <QCoreApplication>
#include <lua_runtime/script_runtime.hpp>

#include "script_worker.hpp"
#include "variant_codec.hpp"
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <exception>
#include <chrono>

// Resources in a static library are registered only when referenced.
static void initializeLuaRuntimeTranslations() { Q_INIT_RESOURCE(lua_runtime_translations); }

namespace lua_runtime {

QString translationCatalog()
{
    initializeLuaRuntimeTranslations();
    return QStringLiteral(":/lua_runtime/i18n/lua_runtime");
}

struct ScriptRuntime::Private {
    ScriptRuntime *owner;
    RuntimeLimits limits;
    std::shared_ptr<detail::SharedStatistics> statistics = std::make_shared<detail::SharedStatistics>();
    QThread *thread = new QThread;
    detail::ScriptWorker *worker;
    HostHandler hostHandler;
    quint64 nextJob = 0;
    QTimer executionPoll;
    quint64 pendingReset = 0;
    bool reportedRunning = false;
    bool reportedLongRunning = false;

    static qint64 clockMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void reportExecution() {
        const auto started = statistics->executionStarted.load(std::memory_order_acquire);
        const bool running = started != 0;
        const bool longRunning = running && clockMilliseconds() - started >= 2000;
        const QPointer<ScriptRuntime> guard(owner);
        if (reportedRunning != running) {
            reportedRunning = running;
            emit owner->runningChanged(running);
            if (!guard)
                return;
        }
        if (reportedLongRunning != longRunning) {
            reportedLongRunning = longRunning;
            emit owner->longRunningChanged(longRunning);
        }
    }

    Private(ScriptRuntime *owner, RuntimeLimits requested)
        : owner(owner), limits(detail::normalizedLimits(requested)),
          worker(new detail::ScriptWorker(limits, statistics)) {
        worker->moveToThread(thread);
        thread->setObjectName(QStringLiteral("LuaScriptThread"));
        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        executionPoll.setParent(owner);
        executionPoll.setInterval(25);
        QObject::connect(&executionPoll, &QTimer::timeout, owner, [this] { reportExecution(); });
        executionPoll.start();
        QObject::connect(worker, &detail::ScriptWorker::jobFinished, owner,
                         [this](quint64 id, ScriptResult result, quint64 bytes) {
                             statistics->pending.fetch_sub(1, std::memory_order_release);
                             statistics->queuedBytes.fetch_sub(bytes, std::memory_order_release);
                             emit this->owner->finished(id, std::move(result));
                         });
        QObject::connect(worker, &detail::ScriptWorker::logReady, owner, [this](QString message) {
            statistics->pendingLogs.fetch_sub(1, std::memory_order_release);
            emit this->owner->logMessage(std::move(message));
        });
        QObject::connect(worker, &detail::ScriptWorker::didReset, owner, [this](quint64 generation) {
            if (generation == statistics->generation.load(std::memory_order_acquire))
                pendingReset = 0;
            if (generation == statistics->generation.load(std::memory_order_acquire))
                emit this->owner->resetFinished();
        });
        QObject::connect(
            worker, &detail::ScriptWorker::hostRequested, owner,
            [this](quint64 id, quint64 generation, quint64 cancellation, QString method,
                   QVariantList arguments, QVariantMap context) {
                if (generation != statistics->generation.load(std::memory_order_acquire) ||
                    cancellation != statistics->cancellation.load(std::memory_order_acquire) ||
                    id != statistics->activeHostJob.load(std::memory_order_acquire))
                    return;
                const QPointer<ScriptRuntime> guard(this->owner);
                const auto handler = hostHandler;
                const int maximumErrorLength = limits.maximumLogMessageBytes;
                ScriptResult result{false, {}, QCoreApplication::translate("LuaRuntime", "No Lua host handler is installed")};
                try {
                    if (handler)
                        result = handler(std::move(method), std::move(arguments), std::move(context));
                    if (guard && result.ok)
                        detail::measureValue(result.values, guard->d->limits);
                } catch (const std::exception &error) {
                    result = {false, {}, QString::fromUtf8(error.what()).left(maximumErrorLength)};
                } catch (...) {
                    result = {false, {}, QCoreApplication::translate("LuaRuntime", "Lua host handler raised an unknown exception")};
                }
                if (!guard)
                    return;
                auto &current = *guard->d;
                if (!result.ok)
                    result.values.clear();
                result.error = result.error.left(current.limits.maximumLogMessageBytes);
                if (generation != current.statistics->generation.load(std::memory_order_acquire) ||
                    cancellation != current.statistics->cancellation.load(std::memory_order_acquire) ||
                    id != current.statistics->activeHostJob.load(std::memory_order_acquire))
                    return;
                auto *target = current.worker;
                QMetaObject::invokeMethod(
                    target,
                    [target, id, generation, cancellation, result = std::move(result)]() mutable {
                        target->hostReply(id, generation, cancellation, std::move(result));
                    },
                    Qt::QueuedConnection);
            });
        thread->start();
    }

    void requireOwnerThread() const { Q_ASSERT(QThread::currentThread() == owner->thread()); }

    quint64 reject(quint64 id, QString message) {
        if (statistics->pendingRejections.load(std::memory_order_acquire) >= limits.maximumPendingJobs)
            return 0;
        statistics->pendingRejections.fetch_add(1, std::memory_order_release);
        QTimer::singleShot(0, owner,
                           [owner = owner, statistics = statistics, id, message = std::move(message)] {
                               // Public observers may synchronously destroy the runtime. Release
                               // the slot first, and never access its Private after emitting.
                               statistics->pendingRejections.fetch_sub(1, std::memory_order_release);
                               emit owner->finished(id, {false, {}, message});
                           });
        return id;
    }

    quint64 submit(detail::ScriptJob job) {
        const auto id = job.id;
        if (statistics->pending.load(std::memory_order_acquire) >= limits.maximumPendingJobs)
            return reject(id, QCoreApplication::translate("LuaRuntime", "Lua task queue is full"));
        auto queued = statistics->queuedBytes.load(std::memory_order_acquire);
        while (true) {
            if (job.retainedBytes > limits.maximumQueuedBytes - qMin(queued, limits.maximumQueuedBytes))
                return reject(id, QCoreApplication::translate("LuaRuntime", "Lua task queue exceeds its byte limit"));
            if (statistics->queuedBytes.compare_exchange_weak(queued, queued + job.retainedBytes,
                                                              std::memory_order_acq_rel))
                break;
        }
        statistics->pending.fetch_add(1, std::memory_order_release);
        job.generation = statistics->generation.load(std::memory_order_acquire);
        job.cancellation = statistics->cancellation.load(std::memory_order_acquire);
        auto *target = worker;
        QMetaObject::invokeMethod(
            target, [target, job = std::move(job)]() mutable { target->enqueue(std::move(job)); },
            Qt::QueuedConnection);
        return id;
    }
};

ScriptRuntime::ScriptRuntime(QObject *parent) : ScriptRuntime(RuntimeLimits{}, parent) {}

ScriptRuntime::ScriptRuntime(const RuntimeLimits &limits, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, limits)) {
    qRegisterMetaType<ScriptResult>();
}

ScriptRuntime::~ScriptRuntime() {
    d->requireOwnerThread();
    d->executionPoll.stop();
    d->statistics->stopping.store(true, std::memory_order_release);
    d->statistics->cancellation.fetch_add(1, std::memory_order_acq_rel);
    d->statistics->generation.fetch_add(1, std::memory_order_acq_rel);
    auto *worker = d->worker;
    QMetaObject::invokeMethod(worker, [worker] { worker->shutdown(); }, Qt::QueuedConnection);
    // A standard C call cannot be interrupted safely. Keep its worker, VM and
    // allocator statistics alive until it returns; the queued shutdown then
    // closes the VM and stops the thread. Never destroy a running QThread.
    if (d->thread->wait(1900))
        delete d->thread;
}

void ScriptRuntime::setHostHandler(HostHandler handler) {
    d->requireOwnerThread();
    d->hostHandler = std::move(handler);
}

quint64 ScriptRuntime::execute(QString source, QString name, QVariantMap context) {
    d->requireOwnerThread();
    const auto id = ++d->nextJob;
    if (source.size() > d->limits.maximumSourceBytes)
        return d->reject(id, QCoreApplication::translate("LuaRuntime", "Lua source exceeds its configured byte limit"));
    auto encoded = source.toUtf8();
    if (encoded.size() > d->limits.maximumSourceBytes)
        return d->reject(id, QCoreApplication::translate("LuaRuntime", "Lua source exceeds its configured byte limit"));
    if (name.size() > 4096)
        return d->reject(id, QCoreApplication::translate("LuaRuntime", "Lua source name is too long"));
    detail::ScriptJob job;
    job.id = id;
    job.source = std::move(encoded);
    job.name = name.isEmpty() ? QByteArray("=script") : name.toUtf8();
    try {
        job.retainedBytes = static_cast<quint64>(job.source.size() + job.name.size()) +
                            detail::measureValue(context, d->limits);
    } catch (const detail::ValueError &error) {
        return d->reject(id, QString::fromUtf8(error.what()));
    }
    job.context = std::move(context);
    return d->submit(std::move(job));
}

quint64 ScriptRuntime::invoke(QString callbackId, QVariantMap context, QVariant event) {
    d->requireOwnerThread();
    const auto id = ++d->nextJob;
    if (callbackId.isEmpty() || callbackId.size() > 128)
        return d->reject(id, QCoreApplication::translate("LuaRuntime", "Invalid Lua callback ID"));
    detail::ScriptJob job;
    job.id = id;
    job.callbackId = std::move(callbackId);
    try {
        job.retainedBytes = static_cast<quint64>(job.callbackId.size() * sizeof(QChar)) +
                            detail::measureValue(context, d->limits) + detail::measureValue(event, d->limits);
    } catch (const detail::ValueError &error) {
        return d->reject(id, QString::fromUtf8(error.what()));
    }
    job.context = std::move(context);
    job.event = std::move(event);
    return d->submit(std::move(job));
}

void ScriptRuntime::releaseCallback(QString callbackId) {
    d->requireOwnerThread();
    if (callbackId.isEmpty() || callbackId.size() > 128)
        return;
    auto *worker = d->worker;
    QMetaObject::invokeMethod(
        worker, [worker, callbackId = std::move(callbackId)] { worker->releaseCallback(callbackId); },
        Qt::QueuedConnection);
}

void ScriptRuntime::reset() {
    d->requireOwnerThread();
    const auto generation = d->statistics->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    d->pendingReset = generation;
    d->statistics->cancellation.fetch_add(1, std::memory_order_acq_rel);
    auto *worker = d->worker;
    QMetaObject::invokeMethod(
        worker, [worker, generation] { worker->reset(generation); }, Qt::QueuedConnection);
    QTimer::singleShot(2000, this, [this, generation] {
        if (d->pendingReset == generation)
            emit resetBlocked();
    });
}

void ScriptRuntime::cancelAll() {
    d->requireOwnerThread();
    d->statistics->cancellation.fetch_add(1, std::memory_order_acq_rel);
    auto *worker = d->worker;
    QMetaObject::invokeMethod(worker, [worker] { worker->cancel(); }, Qt::QueuedConnection);
}

bool ScriptRuntime::isRunning() const {
    return d->statistics->executionStarted.load(std::memory_order_acquire) != 0;
}

bool ScriptRuntime::isLongRunning() const {
    const auto started = d->statistics->executionStarted.load(std::memory_order_acquire);
    return started != 0 && Private::clockMilliseconds() - started >= 2000;
}

quint64 ScriptRuntime::memoryBytes() const { return d->statistics->memory.load(std::memory_order_acquire); }
quint64 ScriptRuntime::peakMemoryBytes() const { return d->statistics->peak.load(std::memory_order_acquire); }
int ScriptRuntime::pendingJobs() const { return d->statistics->pending.load(std::memory_order_acquire); }
int ScriptRuntime::pendingRejections() const {
    return d->statistics->pendingRejections.load(std::memory_order_acquire);
}
int ScriptRuntime::retainedCallbacks() const {
    return d->statistics->retainedCallbacks.load(std::memory_order_acquire);
}
RuntimeLimits ScriptRuntime::limits() const { return d->limits; }
bool ScriptRuntime::measureValue(const QVariant &value, quint64 &retainedBytes, QString &error) const {
    d->requireOwnerThread();
    try {
        retainedBytes = detail::measureValue(value, d->limits);
        error.clear();
        return true;
    } catch (const detail::ValueError &exception) {
        retainedBytes = 0;
        error = QString::fromUtf8(exception.what());
        return false;
    }
}

} // namespace lua_runtime
