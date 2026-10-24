#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace lua_runtime {

// The runtime's own diagnostics use the LuaRuntime translation context. This
// is the Qt resource prefix of its catalog: prefix + "_en.qm" and prefix +
// "_zh_CN.qm". Hosts load it with their other catalogs, for example through
// window_system::TranslationManager::addCatalog(); without it they get the
// English source text.
[[nodiscard]] QString translationCatalog();

struct ScriptResult {
    bool ok = false;
    QVariantList values;
    QString error;
};

struct RuntimeLimits {
    int hostReplyTimeoutMs = 30'000;
    int maximumPendingJobs = 128;
    int maximumCallbacks = 1'000'000;
    int maximumSourceBytes = 64 * 1024 * 1024;
    quint64 maximumQueuedBytes = 256 * 1024 * 1024;
    int maximumValueNodes = 1'000'000;
    int maximumValueDepth = 32;
    int maximumStringBytes = 64 * 1024 * 1024;
    int maximumLogMessagesPerJob = 128;
    int maximumLogMessageBytes = 16 * 1024;
};

// All operations except statistics are called on this object's owning thread.
// The interpreter runs on one private worker thread. Host handlers run on this
// object's thread; no QObject or raw application pointer is exposed to Lua.
// Each accepted job has exactly one asynchronous finished() notification.
class ScriptRuntime final : public QObject {
    Q_OBJECT
  public:
    using HostHandler = std::function<ScriptResult(QString, QVariantList, QVariantMap)>;

    explicit ScriptRuntime(QObject *parent = nullptr);
    explicit ScriptRuntime(const RuntimeLimits &limits, QObject *parent = nullptr);
    ~ScriptRuntime() override;
    ScriptRuntime(const ScriptRuntime &) = delete;
    ScriptRuntime &operator=(const ScriptRuntime &) = delete;

    void setHostHandler(HostHandler handler);
    // A nonzero ID receives one asynchronous result, including validation or
    // capacity errors. If rejection replies are also full, returns 0 without
    // accepting the request or posting finished(). Callers must handle 0.
    // Lua chunks are text-only; successful return values use JSON-like data.
    quint64 execute(QString source, QString name = {}, QVariantMap context = {});
    // _callback(function) retains the function until releaseCallback()/reset().
    // Invocations pass the context as argument 1 and the event as argument 2.
    quint64 invoke(QString callbackId, QVariantMap context = {}, QVariant event = {});
    void releaseCallback(QString callbackId);
    // Reset cancels outstanding jobs, invalidates callback IDs, and drops the
    // complete VM. Hosts may execute their bootstrap after resetFinished().
    void reset();
    // Cooperatively stop existing jobs. A blocking standard-library C call
    // cannot observe this request until it returns; later jobs remain usable.
    void cancelAll();

    // True only while executing Lua or a synchronous standard-library call,
    // not while awaiting a host reply. These accessors are thread-safe.
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isLongRunning() const;

    [[nodiscard]] quint64 memoryBytes() const;
    [[nodiscard]] quint64 peakMemoryBytes() const;
    [[nodiscard]] int pendingJobs() const;
    [[nodiscard]] int pendingRejections() const;
    [[nodiscard]] int retainedCallbacks() const;
    [[nodiscard]] RuntimeLimits limits() const;
    bool measureValue(const QVariant &value, quint64 &retainedBytes, QString &error) const;

  signals:
    void finished(quint64 jobId, lua_runtime::ScriptResult result);
    void logMessage(QString message);
    void resetFinished();
    void runningChanged(bool running);
    // A continuous execution lasting two seconds, with no automatic stop.
    void longRunningChanged(bool running);
    // Reset is still pending after two seconds in a blocking C call. The
    // original resetFinished() is emitted once the call actually returns.
    void resetBlocked();

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace lua_runtime

Q_DECLARE_METATYPE(lua_runtime::ScriptResult)
