#pragma once

#include <lua_runtime/script_runtime.hpp>
#include <lua.h>
#include <QElapsedTimer>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <memory>

namespace lua_runtime::detail {

struct SharedStatistics {
    std::atomic<quint64> memory{0};
    std::atomic<quint64> peak{0};
    std::atomic<int> pending{0};
    std::atomic<int> pendingRejections{0};
    std::atomic<int> retainedCallbacks{0};
    std::atomic<quint64> queuedBytes{0};
    std::atomic<int> pendingLogs{0};
    std::atomic<quint64> generation{1};
    std::atomic<quint64> cancellation{1};
    std::atomic<quint64> activeHostJob{0};
    std::atomic<qint64> executionStarted{0};
    std::atomic<bool> stopping{false};
};

struct ScriptJob {
    quint64 id = 0;
    quint64 generation = 0;
    quint64 cancellation = 0;
    quint64 retainedBytes = 0;
    QByteArray source;
    QByteArray name;
    QString callbackId;
    QVariantMap context;
    QVariant event;
};

class ScriptWorker final : public QObject {
    Q_OBJECT
  public:
    ScriptWorker(RuntimeLimits limits, std::shared_ptr<SharedStatistics> statistics);
    ~ScriptWorker() override;
    void enqueue(ScriptJob job);
    void hostReply(quint64 job, quint64 generation, quint64 cancellation, ScriptResult result);
    void releaseCallback(const QString &id);
    void reset(quint64 generation);
    void cancel();
    void shutdown();

  signals:
    void jobFinished(quint64 id, lua_runtime::ScriptResult result, quint64 retainedBytes);
    void logReady(QString message);
    void hostRequested(quint64 id, quint64 generation, quint64 cancellation, QString method,
                       QVariantList arguments, QVariantMap context);
    void didReset(quint64 generation);

  private:
    friend struct ScriptWorkerTestAccess;
    struct ActiveJob {
        ScriptJob job;
        lua_State *coroutine = nullptr;
        int reference = -2;
        lua_State *hostCoroutine = nullptr;
        int hostCoroutineReference = -2;
        int logMessages = 0;
        bool awaitingHost = false;
        bool aborted = false;
        QString abortReason;
        ScriptResult reply;
    };

    RuntimeLimits limits_;
    std::shared_ptr<SharedStatistics> statistics_;
    QQueue<ScriptJob> queue_;
    std::unique_ptr<ActiveJob> active_;
    lua_State *state_ = nullptr;
    QHash<QString, int> callbacks_;
    quint64 nextCallback_ = 0;
    quint64 stateGeneration_ = 0;
    QTimer hostDeadline_;
    bool cleanup_ = false;
    bool shutdown_ = false;
    bool pumpQueued_ = false;
    QString initializationError_;
    QSet<lua_State *> executingUserHooks_;

    static ScriptWorker *self(lua_State *state);
    static void *allocate(void *user, void *pointer, size_t oldSize, size_t newSize);
    static void hook(lua_State *state, lua_Debug *debug);
    static void restoreRuntimeHook(lua_State *state, int userMask, int userCount);
    static int initialize(lua_State *state);
    static int prepare(lua_State *state);
    static int hostCall(lua_State *state);
    static int hostContinuation(lua_State *state, int status, lua_KContext context);
    static int createCallback(lua_State *state);
    static int releaseRetainedCallback(lua_State *state);
    static int print(lua_State *state);
    static int checkStop(lua_State *state);
    static int setUserHook(lua_State *state);
    static int getUserHook(lua_State *state);

    bool ensureState();
    void closeState();
    void schedulePump();
    void pump();
    void resume(int arguments);
    void finish(ScriptResult result);
    void deliver(ScriptJob job, ScriptResult result);
    void finishQueued(ScriptJob job, const QString &error);
    void disposeCoroutine();
    bool stale(const ScriptJob &job) const;
    bool checkCurrentStop();
    void setAbort(QString message);
    QString stackError(lua_State *state, int status) const;
};

} // namespace lua_runtime::detail
