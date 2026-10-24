#include <lua_runtime/script_runtime.hpp>
#include "../src/script_worker.hpp"
#include "../src/variant_codec.hpp"
#include <lauxlib.h>
#include <lstate.h>

#include <QPointer>
#include <QElapsedTimer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QCoreApplication>
#include <QTranslator>
#include <limits>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>

using lua_runtime::RuntimeLimits;
using lua_runtime::ScriptResult;
using lua_runtime::ScriptRuntime;

namespace lua_runtime::detail {

struct ScriptWorkerTestAccess {
    static lua_State *initializedState(ScriptWorker &worker) {
        return worker.ensureState() ? worker.state_ : nullptr;
    }

    static void registerHookProbe(lua_State *state) {
        lua_pushcfunction(state, [](lua_State *caller) {
            auto *target = lua_isthread(caller, 1) ? lua_tothread(caller, 1) : caller;
            lua_pushinteger(caller, lua_gethookmask(target));
            lua_pushinteger(caller, lua_gethookcount(target));
            return 2;
        });
        lua_setglobal(state, "test_hook_configuration");
    }
};

} // namespace lua_runtime::detail

namespace {

ScriptResult waitFor(const QSignalSpy &spy, quint64 id) {
    ScriptResult result{false, {}, QStringLiteral("Test timed out waiting for the Lua task result")};
    const bool completed = QTest::qWaitFor(
        [&] {
            for (const auto &entry : spy) {
                if (entry.at(0).toULongLong() == id) {
                    result = qvariant_cast<ScriptResult>(entry.at(1));
                    return true;
                }
            }
            return false;
        },
        5000);
    if (!completed)
        return {false, {}, QStringLiteral("Runtime test job timed out")};
    return result;
}

ScriptResult evaluate(ScriptRuntime &runtime, const QString &source, QVariantMap context = {}) {
    QSignalSpy spy(&runtime, &ScriptRuntime::finished);
    return waitFor(spy, runtime.execute(source, QStringLiteral("=runtime-test"), std::move(context)));
}

QString sleepingChildCommand(const QString &readyPath, int milliseconds) {
    auto command = QStringLiteral("\"%1\" --lua-sleep %2 \"%3\"")
                       .arg(QCoreApplication::applicationFilePath())
                       .arg(milliseconds)
                       .arg(readyPath);
#ifdef Q_OS_WIN
    // cmd.exe /c removes its outer pair when the command contains more than
    // one quoted argument, so system() needs a second enclosing pair.
    command = QLatin1Char('"') + command + QLatin1Char('"');
#endif
    return command;
}

} // namespace

class ScriptRuntimeTest final : public QObject {
    Q_OBJECT
  private slots:
    void translationCatalogCoversBothLanguages();
    void standardValuesAndContext();
    void hostCallsYieldToTheOwnerAndReturnMultipleValues();
    void explicitNullValuesSurviveHostRoundTrips_data();
    void explicitNullValuesSurviveHostRoundTrips();
    void explicitNullSentinelDoesNotPermitOtherUserdata();
    void hostErrorsRemainCatchableAndDoNotPoisonTheInterpreter();
    void callbacksHaveExplicitLifetimeAndContext();
    void callbackCountIsBounded();
    void resetInvalidatesCallbacksAndReleasesAllVmMemory();
    void resetInsideAHostCallDiscardsItsOldReply();
    void hostCanDestroyTheRuntimeDuringItsCallback();
    void cancellationDuringExecutionKeepsTheOwnerResponsive();
    void protectedCallsCannotSwallowAStopRequest_data();
    void protectedCallsCannotSwallowAStopRequest();
    void stopKeepsTheOwnerResponsive();
    void allStandardLibrariesAreAvailable();
    void loadSupportsTextModulesWithinTheSameTask();
    void loadAcceptsBinaryAndLargeChunks();
    void sourceAndQueueLimitsAreEnforced();
    void rejectionRepliesRemainBoundedUnderFlood_data();
    void rejectionRepliesRemainBoundedUnderFlood();
    void rejectionObserverMayDestroyTheRuntime();
    void transferLimitsAndUnsupportedValues_data();
    void transferLimitsAndUnsupportedValues();
    void invalidHostValuesAreRejected();
    void pendingResultsShareTheTransferBudget();
    void logsHaveCountAndLengthLimits();
    void largeAllocationsAreMeasuredAndReleased();
    void repeatedResetReleasesCallbacksAndJobStorage();
    void cleanupCannotExecuteUnboundedFinalizers();
    void supersededResetRepliesDoNotReachTheNewGeneration();
    void userHooksKeepStandardSemantics();
    void nestedHostCallsPreserveYieldSemantics();
    void resetsStopNestedCoroutines();
    void executionHasNoAutomaticBudget();
    void runningSignalsAndBlockedReset();
    void blockingStandardCallHasNoExecutionBudget();
    void blockedDestructorRetainsWorkerUntilItReturns();
    void weakHookEntriesDoNotRetainCoroutines();
    void runningObserverMayDestroyTheRuntime();
    void explicitCoroutineHooksReserveStackSpace_data();
    void explicitCoroutineHooksReserveStackSpace();
    void runtimeHooksOnlyRequestUserEvents();
    void cancellationRestoresRetainedCoroutineHooks();
    void retainedCoroutineCountHookKeepsItsInterval();
};

void ScriptRuntimeTest::supersededResetRepliesDoNotReachTheNewGeneration() {
    ScriptRuntime runtime;
    QVERIFY(evaluate(runtime, "return 1").ok);
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    const auto waitForAllocation = [&](bool allocated) {
        QElapsedTimer deadline;
        deadline.start();
        // Keep the owner event queue paused while observing the worker's
        // atomic allocator counter. This deliberately retains old replies.
        while ((runtime.memoryBytes() > 0) != allocated && deadline.elapsed() < 5000)
            QThread::msleep(1);
        return (runtime.memoryBytes() > 0) == allocated;
    };
    for (int generation = 0; generation < 16; ++generation) {
        runtime.reset();
        QVERIFY(waitForAllocation(false));
        QVERIFY(runtime.execute("return 1") != 0);
        // Recreating the VM proves the preceding reset completed on its
        // worker before this owner processes any of those completion replies.
        QVERIFY(waitForAllocation(true));
    }
    QTRY_COMPARE(reset.size(), 1);
    QTRY_COMPARE(runtime.pendingJobs(), 0);
    QVERIFY(evaluate(runtime, "return 42").ok);
}

void ScriptRuntimeTest::translationCatalogCoversBothLanguages() {
    // Without the catalog the diagnostics are the English sources.
    QCOMPARE(QCoreApplication::translate("LuaRuntime", "Lua task was cancelled"), QStringLiteral("Lua task was cancelled"));
    for (const auto &[language, expected] : {std::pair{"_zh_CN.qm", "Lua 任务已取消"}, std::pair{"_en.qm", "Lua task was cancelled"}}) {
        QTranslator translator;
        QVERIFY(translator.load(lua_runtime::translationCatalog() + QLatin1String(language)));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCOMPARE(QCoreApplication::translate("LuaRuntime", "Lua task was cancelled"), QString::fromUtf8(expected));
        QVERIFY(QCoreApplication::removeTranslator(&translator));
    }
}

void ScriptRuntimeTest::standardValuesAndContext() {
    ScriptRuntime runtime;
    const auto result = evaluate(
        runtime,
        QStringLiteral("local ctx = ...; return 42, true, '简体中文', nil, {name=ctx.name, list={2,3}}, "
                       "9223372036854775807"),
        {{QStringLiteral("name"), QStringLiteral("context")}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.size(), 6);
    QCOMPARE(result.values.at(0).toLongLong(), 42);
    QCOMPARE(result.values.at(1).toBool(), true);
    QCOMPARE(result.values.at(2).toString(), QStringLiteral("简体中文"));
    QVERIFY(!result.values.at(3).isValid());
    QCOMPARE(result.values.at(4).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("context"));
    QCOMPARE(result.values.at(4).toMap().value(QStringLiteral("list")).toList().size(), 2);
    QCOMPARE(result.values.at(5).toLongLong(), std::numeric_limits<qlonglong>::max());
    QCOMPARE(runtime.pendingJobs(), 0);
    QVERIFY(runtime.memoryBytes() > 0);
    QVERIFY(runtime.peakMemoryBytes() >= runtime.memoryBytes());
}

void ScriptRuntimeTest::hostCallsYieldToTheOwnerAndReturnMultipleValues() {
    ScriptRuntime runtime;
    int calls = 0;
    bool ownerThread = false;
    runtime.setHostHandler([&](QString method, QVariantList arguments, QVariantMap context) {
        ++calls;
        ownerThread = QThread::currentThread() == runtime.thread();
        if (method != QStringLiteral("echo") ||
            context.value(QStringLiteral("window")).toString() != QStringLiteral("w1"))
            return ScriptResult{false, {}, QStringLiteral("Unexpected host context")};
        return ScriptResult{true, {arguments.at(0), arguments.at(1), calls}, {}};
    });
    const auto result = evaluate(runtime,
                                 QStringLiteral("local a,b,n=muxora._host('echo', 'hello', {answer=42}); "
                                                "local c,d,m=muxora._host('echo', a, b); return c,d,n,m"),
                                 {{QStringLiteral("window"), QStringLiteral("w1")}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(calls, 2);
    QVERIFY(ownerThread);
    QCOMPARE(result.values.at(0).toString(), QStringLiteral("hello"));
    QCOMPARE(result.values.at(1).toMap().value(QStringLiteral("answer")).toLongLong(), 42);
    QCOMPARE(result.values.at(2).toLongLong(), 1);
    QCOMPARE(result.values.at(3).toLongLong(), 2);
}

void ScriptRuntimeTest::explicitNullValuesSurviveHostRoundTrips_data() {
    QTest::addColumn<QVariant>("payload");
    QTest::addColumn<QString>("inspect");
    const auto null = QVariant::fromValue(nullptr);
    QTest::newRow("complete-list")
        << QVariant(QVariantList{qlonglong(11), qlonglong(22), qlonglong(33)})
        << QStringLiteral("assert(#value==3 and value[1]==11 and value[2]==22 and value[3]==33)");
    QTest::newRow("empty-list")
        << QVariant(QVariantList{}) << QStringLiteral("assert(#value==0 and next(value)==nil)");
    QTest::newRow("all-null-list")
        << QVariant(QVariantList{null, null, null, null})
        << QStringLiteral("assert(#value==4); for i=1,4 do assert(value[i]==null) end");
    QTest::newRow("mixed-null-list")
        << QVariant(QVariantList{null, qlonglong(42), null, QStringLiteral("last")})
        << QStringLiteral("assert(#value==4 and value[1]==null and value[2]==42 and "
                          "value[3]==null and value[4]=='last')");
    QTest::newRow("trailing-null-list")
        << QVariant(QVariantList{QStringLiteral("first"), null, null})
        << QStringLiteral("assert(#value==3 and value[1]=='first' and value[2]==null and value[3]==null)");
    QTest::newRow("nested-map-null")
        << QVariant(QVariantMap{{QStringLiteral("nested"),
                                 QVariantMap{{QStringLiteral("present"), null},
                                             {QStringLiteral("values"), QVariantList{null, qlonglong(7), null}}}},
                                {QStringLiteral("present"), null}})
        << QStringLiteral("assert(value.present==null and value.nested.present==null); "
                          "local list=value.nested.values; "
                          "assert(#list==3 and list[1]==null and list[2]==7 and list[3]==null)");
    QTest::newRow("top-level-null") << null << QStringLiteral("assert(value==null)");
}

void ScriptRuntimeTest::explicitNullValuesSurviveHostRoundTrips() {
    QFETCH(QVariant, payload);
    QFETCH(QString, inspect);
    ScriptRuntime runtime;
    const auto null = QVariant::fromValue(nullptr);
    const QVariantList expected{payload, QVariant{}, null};
    QVariantList echoed;
    int calls = 0;
    runtime.setHostHandler([&](QString method, QVariantList arguments, QVariantMap) {
        ++calls;
        if (method == QStringLiteral("values"))
            return ScriptResult{true, expected, {}};
        if (method == QStringLiteral("echo")) {
            echoed = arguments;
            return ScriptResult{true, arguments, {}};
        }
        return ScriptResult{false, {}, QStringLiteral("Unexpected host method")};
    });
    const auto result = evaluate(runtime, QStringLiteral(
        "local value,missing,null=muxora._host('values'); "
        "assert(missing==nil and null~=nil and type(null)=='userdata'); ") + inspect +
        QStringLiteral("; return muxora._host('echo',value,missing,null)"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(calls, 2);
    QCOMPARE(echoed, expected);
    QCOMPARE(result.values, expected);
    QCOMPARE(result.values.at(0).metaType(), payload.metaType());
    QVERIFY(!echoed.at(1).isValid());
    QVERIFY(!result.values.at(1).isValid());
    QCOMPARE(echoed.at(2).metaType().id(), int(QMetaType::Nullptr));
    QCOMPARE(result.values.at(2).metaType().id(), int(QMetaType::Nullptr));
}

void ScriptRuntimeTest::explicitNullSentinelDoesNotPermitOtherUserdata() {
    using namespace lua_runtime::detail;
    const std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    QVERIFY(state);
    const RuntimeLimits limits;
    int pointer = 0;
    lua_pushlightuserdata(state.get(), &pointer);
    ValueBudget lightBudget{limits};
    QVERIFY_EXCEPTION_THROWN(readValue(state.get(), -1, lightBudget), ValueError);
    lua_pop(state.get(), 1);
    lua_newuserdatauv(state.get(), 1, 0);
    ValueBudget fullBudget{limits};
    QVERIFY_EXCEPTION_THROWN(readValue(state.get(), -1, fullBudget), ValueError);
    lua_pop(state.get(), 1);
    const auto pointerNull = QVariant::fromValue(static_cast<void *>(nullptr));
    QVERIFY_EXCEPTION_THROWN(measureValue(pointerNull, limits), ValueError);
    ValueBudget pointerBudget{limits};
    QVERIFY_EXCEPTION_THROWN(pushValue(state.get(), pointerNull, pointerBudget), ValueError);
    QCOMPARE(lua_gettop(state.get()), 0);
}

void ScriptRuntimeTest::hostErrorsRemainCatchableAndDoNotPoisonTheInterpreter() {
    ScriptRuntime runtime;
    runtime.setHostHandler([](QString, QVariantList, QVariantMap) {
        return ScriptResult{false, {}, QStringLiteral("expected host error")};
    });
    const auto caught =
        evaluate(runtime, QStringLiteral("local ok,err=pcall(muxora._host,'fail'); return ok,err"));
    QVERIFY2(caught.ok, qPrintable(caught.error));
    QVERIFY(!caught.values.at(0).toBool());
    QVERIFY(caught.values.at(1).toString().contains(QStringLiteral("expected host error")));
    const auto failed = evaluate(runtime, QStringLiteral("return muxora._host('fail')"));
    QVERIFY(!failed.ok);
    QVERIFY(failed.error.contains(QStringLiteral("expected host error")));
    const auto recovered = evaluate(runtime, QStringLiteral("return 11"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values.constFirst().toLongLong(), 11);
}

void ScriptRuntimeTest::callbacksHaveExplicitLifetimeAndContext() {
    ScriptRuntime runtime;
    const auto created =
        evaluate(runtime, QStringLiteral("local count=0; return muxora._callback(function(ctx,event) "
                                         "count=count+1; return ctx.window,event.value,count end)"));
    QVERIFY2(created.ok, qPrintable(created.error));
    const auto callback = created.values.constFirst().toString();
    QSignalSpy spy(&runtime, &ScriptRuntime::finished);
    const auto result =
        waitFor(spy, runtime.invoke(callback, {{QStringLiteral("window"), QStringLiteral("w2")}},
                                    QVariantMap{{QStringLiteral("value"), 7}}));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.at(0).toString(), QStringLiteral("w2"));
    QCOMPARE(result.values.at(1).toLongLong(), 7);
    QCOMPARE(result.values.at(2).toLongLong(), 1);
    const auto again = waitFor(spy, runtime.invoke(callback));
    QVERIFY(!again.ok); // Context validation belongs to the script, not a stale QObject.
    runtime.releaseCallback(callback);
    const auto released = waitFor(spy, runtime.invoke(callback));
    QVERIFY(!released.ok);
    QVERIFY(released.error.contains(QStringLiteral("no longer exists")));
}

void ScriptRuntimeTest::callbackCountIsBounded() {
    RuntimeLimits limits;
    limits.maximumCallbacks = 2;
    ScriptRuntime runtime(limits);
    const auto created = evaluate(
        runtime, QStringLiteral("return muxora._callback(function()end),muxora._callback(function()end)"));
    QVERIFY2(created.ok, qPrintable(created.error));
    const auto overflow = evaluate(runtime, QStringLiteral("return muxora._callback(function()end)"));
    QVERIFY(!overflow.ok);
    QVERIFY(overflow.error.contains(QStringLiteral("callback count")));
    runtime.releaseCallback(created.values.constFirst().toString());
    const auto replacement = evaluate(runtime, QStringLiteral("return muxora._callback(function()end)"));
    QVERIFY2(replacement.ok, qPrintable(replacement.error));
    QVERIFY(replacement.values.constFirst() != created.values.constFirst());
}

void ScriptRuntimeTest::resetInvalidatesCallbacksAndReleasesAllVmMemory() {
    ScriptRuntime runtime;
    const auto created =
        evaluate(runtime, QStringLiteral("kept=99; return muxora._callback(function()return kept end)"));
    QVERIFY2(created.ok, qPrintable(created.error));
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    runtime.reset();
    QTRY_COMPARE(reset.size(), 1);
    QCOMPARE(runtime.memoryBytes(), 0);
    QSignalSpy finished(&runtime, &ScriptRuntime::finished);
    const auto stale = waitFor(finished, runtime.invoke(created.values.constFirst().toString()));
    QVERIFY(!stale.ok);
    const auto clean = evaluate(runtime, QStringLiteral("return kept==nil,muxora._callback(function()end)"));
    QVERIFY2(clean.ok, qPrintable(clean.error));
    QVERIFY(clean.values.constFirst().toBool());
    QVERIFY(clean.values.at(1) != created.values.constFirst());
}

void ScriptRuntimeTest::resetInsideAHostCallDiscardsItsOldReply() {
    ScriptRuntime runtime;
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    runtime.setHostHandler([&](QString, QVariantList, QVariantMap) {
        runtime.reset();
        return ScriptResult{true, {QStringLiteral("must never resume")}, {}};
    });
    const auto result = evaluate(runtime, QStringLiteral("local value=muxora._host('reset'); error(value)"));
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("cancelled")));
    QTRY_COMPARE(reset.size(), 1);
    QCOMPARE(runtime.memoryBytes(), 0);
    const auto clean = evaluate(runtime, QStringLiteral("return 8"));
    QVERIFY2(clean.ok, qPrintable(clean.error));
}

void ScriptRuntimeTest::hostCanDestroyTheRuntimeDuringItsCallback() {
    auto runtime = std::make_unique<ScriptRuntime>();
    const QPointer<ScriptRuntime> guard(runtime.get());
    bool called = false;
    runtime->setHostHandler([&](QString, QVariantList, QVariantMap) {
        called = true;
        runtime.reset();
        return ScriptResult{true, {}, {}};
    });
    runtime->execute(QStringLiteral("muxora._host('destroy')"));
    QTRY_VERIFY(called);
    QVERIFY(guard.isNull());
}

void ScriptRuntimeTest::cancellationDuringExecutionKeepsTheOwnerResponsive() {
    RuntimeLimits limits;
    ScriptRuntime runtime(limits);
    bool began = false;
    connect(&runtime, &ScriptRuntime::logMessage, &runtime, [&](const QString &) {
        began = true;
        runtime.cancelAll();
    });
    const auto result = evaluate(runtime, QStringLiteral("print('ready'); while true do end"));
    QVERIFY(began);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("cancelled")));
    const auto recovered = evaluate(runtime, QStringLiteral("return 9"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
}

void ScriptRuntimeTest::protectedCallsCannotSwallowAStopRequest_data() {
    QTest::addColumn<QString>("source");
    QTest::newRow("loop") << QStringLiteral("while true do end");
    QTest::newRow("pcall") << QStringLiteral("while true do pcall(function() while true do end end) end");
    QTest::newRow("xpcall") << QStringLiteral(
        "while true do xpcall(function() while true do end end,function(e)return e end) end");
    QTest::newRow("nonyieldable-comparator") << QStringLiteral(
        "while true do pcall(function() table.sort({2,1},function() while true do end end) end) end");
    QTest::newRow("nonyieldable-tostring")
        << QStringLiteral("local v=setmetatable({},{__tostring=function()while true do end end}); while true "
                          "do pcall(tostring,v) end");
    QTest::newRow("compiled-nested-pcall") << QStringLiteral(
        "local f=load('while true do pcall(function()while true do end end) end'); pcall(f)");
    QTest::newRow("mutated-globals") << QStringLiteral(
        "local protected=pcall; muxora=nil; table=nil; pcall=nil; while true do protected(function()while "
        "true do end end)end");
    QTest::newRow("coroutine") << QStringLiteral("coroutine.resume(coroutine.create(function()while true do end end))");
    QTest::newRow("wrap") << QStringLiteral("coroutine.wrap(function()while true do end end)()");
    QTest::newRow("nested-coroutines") << QStringLiteral(
        "coroutine.wrap(function()coroutine.resume(coroutine.create(function()"
        "coroutine.wrap(function()while true do end end)() end))end)()");
    QTest::newRow("user-line-hook") << QStringLiteral("debug.sethook(function()end,'l');while true do end");
    QTest::newRow("user-count-hook") << QStringLiteral("debug.sethook(function()end,'',7);while true do end");
    QTest::newRow("cleared-user-hook") << QStringLiteral("debug.sethook(function()end,'l');debug.sethook();while true do end");
    QTest::newRow("hook-self-loop") << QStringLiteral("debug.sethook(function()while true do end end,'',5);while true do end");
    QTest::newRow("hook-pcall-self-loop") << QStringLiteral(
        "debug.sethook(function()while true do pcall(function()while true do end end)end end,'',5);while true do end");
    QTest::newRow("raw-pcall") << QStringLiteral(
        "local raw;for i=1,10 do local name,value=debug.getupvalue(pcall,i);"
        "if name=='raw_pcall'then raw=value end end;assert(raw);"
        "while true do raw(function()while true do end end)end");
    QTest::newRow("raw-pcall-in-user-hook") << QStringLiteral(
        "local raw;for i=1,10 do local name,value=debug.getupvalue(pcall,i);"
        "if name=='raw_pcall'then raw=value end end;assert(raw);"
        "debug.sethook(function()while true do raw(function()while true do end end)end end,'',5);"
        "while true do end");
    QTest::newRow("raw-pcall-nonyieldable-comparator") << QStringLiteral(
        "local raw;for i=1,10 do local name,value=debug.getupvalue(pcall,i);"
        "if name=='raw_pcall'then raw=value end end;assert(raw);"
        "while true do raw(function()table.sort({2,1},function()while true do end end)end)end");

}

void ScriptRuntimeTest::protectedCallsCannotSwallowAStopRequest() {
    QFETCH(QString, source);
    ScriptRuntime runtime;
    bool requested = false;
    connect(&runtime, &ScriptRuntime::logMessage, &runtime, [&](const QString &) {
        QTimer::singleShot(20, &runtime, [&] {
            requested = true;
            runtime.cancelAll();
        });
    });
    const auto result = evaluate(runtime, QStringLiteral("print('ready'); ") + source);
    QVERIFY(requested);
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("cancelled")), qPrintable(result.error));
    const auto recovered = evaluate(runtime, QStringLiteral("return 3"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
}

void ScriptRuntimeTest::stopKeepsTheOwnerResponsive() {
    RuntimeLimits limits;
    ScriptRuntime runtime(limits);
    QTimer heartbeat;
    heartbeat.setInterval(1);
    int ticks = 0;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start();
    QTimer::singleShot(40, &runtime, &ScriptRuntime::cancelAll);
    const auto result = evaluate(runtime, QStringLiteral("while true do end"));
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("cancelled")), qPrintable(result.error));
    QVERIFY(ticks > 0);
}

void ScriptRuntimeTest::allStandardLibrariesAreAvailable() {
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "return type(io.open)=='function',type(os.execute)=='function',type(io.popen)=='function',"
        "type(debug.sethook)=='function',type(coroutine.wrap)=='function',type(package.loadlib)=='function',"
        "type(require)=='function',type(loadfile)=='function',type(dofile)=='function',"
        "type(string.dump)=='function',type(collectgarbage('count'))=='number'"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.size(), 11);
    for (const auto &value : result.values)
        QVERIFY(value.toBool());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto io = evaluate(runtime, QStringLiteral(
        "local p=(...).path;local f=assert(io.open(p,'wb'));f:write('standard IO');f:close();"
        "f=assert(io.open(p,'rb'));local value=f:read('*a');f:close();return value"),
        {{QStringLiteral("path"), directory.filePath(QStringLiteral("runtime.txt"))}});
    QVERIFY2(io.ok, qPrintable(io.error));
    QCOMPARE(io.values.constFirst().toString(), QStringLiteral("standard IO"));
}

void ScriptRuntimeTest::loadSupportsTextModulesWithinTheSameTask() {
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "local module=assert(load('local value=...; return {answer=value+1}', 'custom/module')); "
        "return module(41).answer"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.constFirst().toLongLong(), 42);
}

void ScriptRuntimeTest::loadAcceptsBinaryAndLargeChunks() {
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "local binary=string.dump(function()return 42 end);"
        "local large=string.rep(' ',2*1024*1024)..'return 17';"
        "return assert(load(binary))(),assert(load(large))()"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.at(0).toInt(), 42);
    QCOMPARE(result.values.at(1).toInt(), 17);
}

void ScriptRuntimeTest::sourceAndQueueLimitsAreEnforced() {
    RuntimeLimits limits;
    limits.maximumSourceBytes = 32;
    limits.maximumPendingJobs = 2;
    ScriptRuntime runtime(limits);
    QSignalSpy spy(&runtime, &ScriptRuntime::finished);
    const auto first = runtime.execute(QStringLiteral("return 1"));
    const auto second = runtime.execute(QStringLiteral("return 2"));
    const auto third = runtime.execute(QStringLiteral("return 3"));
    const auto queued = waitFor(spy, third);
    QVERIFY(!queued.ok);
    QVERIFY(queued.error.contains(QStringLiteral("queue is full")));
    QVERIFY(waitFor(spy, first).ok);
    QVERIFY(waitFor(spy, second).ok);
    const auto source = evaluate(runtime, QString(33, QLatin1Char(' ')));
    QVERIFY(!source.ok);
    QVERIFY(source.error.contains(QStringLiteral("source")));
}

void ScriptRuntimeTest::rejectionRepliesRemainBoundedUnderFlood_data() {
    QTest::addColumn<QString>("reason");
    QTest::newRow("source-validation") << QStringLiteral("source");
    QTest::newRow("callback-validation") << QStringLiteral("callback");
    QTest::newRow("task-capacity") << QStringLiteral("capacity");
}

void ScriptRuntimeTest::rejectionRepliesRemainBoundedUnderFlood() {
    QFETCH(QString, reason);
    RuntimeLimits limits;
    limits.maximumSourceBytes = 32;
    limits.maximumPendingJobs = 4;
    ScriptRuntime runtime(limits);
    QSignalSpy finished(&runtime, &ScriptRuntime::finished);
    QSet<quint64> accepted;
    if (reason == QStringLiteral("capacity"))
        for (int i = 0; i < limits.maximumPendingJobs; ++i) {
            const auto id = runtime.execute(QStringLiteral("return 1"));
            QVERIFY(id);
            accepted.insert(id);
        }
    int rejectionResults = 0;
    connect(&runtime, &ScriptRuntime::finished, this, [&](quint64, const ScriptResult &result) {
        if (!result.ok) {
            ++rejectionResults;
            QCOMPARE(runtime.pendingRejections(), limits.maximumPendingJobs - rejectionResults);
        }
    });
    int silentlyUnaccepted = 0;
    for (int i = 0; i < 10'000; ++i) {
        const auto id =
            reason == QStringLiteral("callback")
                ? runtime.invoke(QString{})
                : runtime.execute(reason == QStringLiteral("source") ? QString(33, QLatin1Char(' '))
                                                                     : QStringLiteral("return 2"));
        if (id)
            QVERIFY(!accepted.contains(id));
        if (id)
            accepted.insert(id);
        else
            ++silentlyUnaccepted;
        QVERIFY(runtime.pendingJobs() <= limits.maximumPendingJobs);
        QVERIFY(runtime.pendingRejections() <= limits.maximumPendingJobs);
    }
    QCOMPARE(finished.size(), 0);
    QCOMPARE(runtime.pendingRejections(), limits.maximumPendingJobs);
    QCOMPARE(silentlyUnaccepted, 10'000 - limits.maximumPendingJobs);
    const auto expected = limits.maximumPendingJobs * (reason == QStringLiteral("capacity") ? 2 : 1);
    QCOMPARE(accepted.size(), expected);
    QTRY_COMPARE(finished.size(), expected);
    QCOMPARE(rejectionResults, limits.maximumPendingJobs);
    QCOMPARE(runtime.pendingJobs(), 0);
    QCOMPARE(runtime.pendingRejections(), 0);
    for (const auto &result : finished)
        QVERIFY(accepted.remove(result[0].toULongLong()));
    QVERIFY(accepted.isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), expected);
    const auto recovered = evaluate(runtime, QStringLiteral("return 42"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values[0].toInt(), 42);
}

void ScriptRuntimeTest::rejectionObserverMayDestroyTheRuntime() {
    RuntimeLimits limits;
    limits.maximumSourceBytes = 1;
    auto runtime = std::make_unique<ScriptRuntime>(limits);
    const QPointer<ScriptRuntime> retained(runtime.get());
    bool called = false;
    int pendingAtDelivery = -1;
    connect(runtime.get(), &ScriptRuntime::finished, this, [&](quint64, const ScriptResult &result) {
        called = !result.ok;
        pendingAtDelivery = runtime->pendingRejections();
        runtime.reset();
    });
    QVERIFY(runtime->execute(QStringLiteral("too long")));
    QTRY_VERIFY(called);
    QCOMPARE(pendingAtDelivery, 0);
    QVERIFY(!retained);
}

void ScriptRuntimeTest::transferLimitsAndUnsupportedValues_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("expectedError");
    QTest::newRow("cycle") << QStringLiteral("local t={};t.t=t;return t") << QStringLiteral("Cyclic");
    QTest::newRow("sparse") << QStringLiteral("return {[2]=3}") << QStringLiteral("Sparse");
    QTest::newRow("mixed") << QStringLiteral("return {1,name='x'}") << QStringLiteral("Mixed");
    QTest::newRow("function") << QStringLiteral("return function()end") << QStringLiteral("functions");
    QTest::newRow("infinity") << QStringLiteral("return 1/0") << QStringLiteral("NaN or infinity");
    QTest::newRow("nan") << QStringLiteral("return 0/0") << QStringLiteral("NaN or infinity");
    QTest::newRow("depth") << QStringLiteral("return {{{{{{{{{{}}}}}}}}}}") << QStringLiteral("depth");
    QTest::newRow("nodes") << QStringLiteral("return {1,2,3,4,5,6,7,8,9,10,11}")
                           << QStringLiteral("node limit");
    QTest::newRow("string") << QStringLiteral("return string.rep('a',65)") << QStringLiteral("byte limit");
    QTest::newRow("invalid-utf8") << QStringLiteral("return string.char(255)")
                                  << QStringLiteral("valid UTF-8");
}

void ScriptRuntimeTest::transferLimitsAndUnsupportedValues() {
    QFETCH(QString, source);
    QFETCH(QString, expectedError);
    RuntimeLimits limits;
    limits.maximumValueDepth = 4;
    limits.maximumValueNodes = 10;
    limits.maximumStringBytes = 64;
    ScriptRuntime runtime(limits);
    const auto result = evaluate(runtime, source);
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(expectedError), qPrintable(result.error));
    const auto recovered = evaluate(runtime, QStringLiteral("return 5"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
}

void ScriptRuntimeTest::invalidHostValuesAreRejected() {
    ScriptRuntime runtime;
    QObject object;
    const auto rejected = evaluate(runtime, QStringLiteral("return 1"),
                                   {{QStringLiteral("object"), QVariant::fromValue(&object)}});
    QVERIFY(!rejected.ok);
    runtime.setHostHandler([&](QString, QVariantList, QVariantMap) {
        return ScriptResult{true, {QVariant::fromValue(&object)}, {}};
    });
    const auto reply = evaluate(runtime, QStringLiteral("return muxora._host('invalid')"));
    QVERIFY(!reply.ok);
    QVERIFY2(reply.error.contains(QStringLiteral("Only nil")), qPrintable(reply.error));
}

void ScriptRuntimeTest::pendingResultsShareTheTransferBudget() {
    RuntimeLimits limits;
    limits.maximumQueuedBytes = 4096;
    ScriptRuntime runtime(limits);
    const auto result = evaluate(runtime, QStringLiteral("return string.rep('a',3000)"));
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("byte limit")));
    const auto recovered = evaluate(runtime, QStringLiteral("return 6"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
}

void ScriptRuntimeTest::logsHaveCountAndLengthLimits() {
    RuntimeLimits limits;
    limits.maximumLogMessagesPerJob = 3;
    limits.maximumLogMessageBytes = 8;
    ScriptRuntime runtime(limits);
    QSignalSpy logs(&runtime, &ScriptRuntime::logMessage);
    const auto result =
        evaluate(runtime, QStringLiteral("for i=1,20 do print(string.rep('a',100)) end; return 1"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(logs.size(), 3);
    for (const auto &message : logs)
        QCOMPARE(message.constFirst().toString().size(), 8);
}

void ScriptRuntimeTest::largeAllocationsAreMeasuredAndReleased() {
    ScriptRuntime runtime;
    const auto baseline = evaluate(runtime, QStringLiteral("collectgarbage('collect');return true"));
    QVERIFY2(baseline.ok, qPrintable(baseline.error));
    const auto before = runtime.memoryBytes();
    const auto large = evaluate(runtime, QStringLiteral(
        "local value=string.rep('x',100*1024*1024);local size=#value;"
        "value=nil;collectgarbage('collect');return size"));
    QVERIFY2(large.ok, qPrintable(large.error));
    QCOMPARE(large.values.constFirst().toInt(), 100 * 1024 * 1024);
    QVERIFY(runtime.peakMemoryBytes() > 100 * 1024 * 1024);
    QVERIFY(runtime.memoryBytes() < before + 256 * 1024);
    const auto recovered = evaluate(runtime, QStringLiteral("return 17"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
}

void ScriptRuntimeTest::repeatedResetReleasesCallbacksAndJobStorage() {
    ScriptRuntime runtime;
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    for (int round = 1; round <= 5; ++round) {
        const auto result = evaluate(
            runtime,
            QStringLiteral(
                "for i=1,50 do local retained=string.rep('x',1024);muxora._callback(function()return "
                "retained end)end;return true"));
        QVERIFY2(result.ok, qPrintable(result.error));
        runtime.reset();
        QTRY_COMPARE(reset.size(), round);
        QCOMPARE(runtime.memoryBytes(), 0);
        QCOMPARE(runtime.pendingJobs(), 0);
    }
}

void ScriptRuntimeTest::cleanupCannotExecuteUnboundedFinalizers() {
    ScriptRuntime runtime;
    const auto result =
        evaluate(runtime, QStringLiteral("retained=setmetatable({},{__gc=function()while true do "
                                         "pcall(function()while true do end end)end end});return true"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    runtime.reset();
    QTRY_COMPARE(reset.size(), 1);
    QCOMPARE(runtime.memoryBytes(), 0);
}

void ScriptRuntimeTest::userHooksKeepStandardSemantics() {
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "local lines,counts,calls,returns=0,0,0,0;local f=function(e)"
        "if e=='line'then lines=lines+1 elseif e=='count'then counts=counts+1 "
        "elseif e=='call'or e=='tail call'then calls=calls+1 elseif e=='return'then returns=returns+1 end end;"
        "debug.sethook(f,'crl',7);local h,m,n=debug.gethook();local sum=0;for i=1,100 do sum=sum+i end;"
        "debug.sethook();local h2,m2,n2=debug.gethook();"
        "debug.sethook(f,'',0);local empty=table.pack(debug.gethook());assert(empty.n==1 and empty[1]==nil);debug.sethook();"
        "local co=coroutine.create(function()local x=0;for i=1,100 do x=x+i end;return x end);"
        "debug.sethook(co,f,'l',9);local h3,m3,n3=debug.gethook(co);local ok,v=coroutine.resume(co);"
        "return h==f,m=='crl',n==7,lines>0,counts>0,calls>0,returns>0,h2==nil,m2==nil,n2==nil,"
        "h3==f,m3=='l',n3==9,ok,v==5050"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.size(), 15);
    for (const auto &value : result.values)
        QVERIFY(value.toBool());
}

void ScriptRuntimeTest::nestedHostCallsPreserveYieldSemantics() {
    ScriptRuntime runtime;
    runtime.setHostHandler([](QString method, QVariantList values, QVariantMap) {
        return ScriptResult{true, {method, values.constFirst(), 42}, {}};
    });
    const auto result = evaluate(runtime, QStringLiteral(
        "local co=coroutine.create(function()"
        "local a,b=coroutine.yield(nil,17);"
        "local ok,name,n,answer=pcall(function()return coroutine.wrap(function()"
        "return muxora._host('nested',a+b)end)()end);return ok,name,n,answer end);"
        "local a,b,c=coroutine.resume(co);assert(a and b==nil and c==17);"
        "local resumed,ok,name,n,answer=coroutine.resume(co,20,22);return resumed,ok,name,n,answer"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values, QVariantList({true,true,QStringLiteral("nested"),42,42}));
}

void ScriptRuntimeTest::resetsStopNestedCoroutines() {
    ScriptRuntime runtime;
    QSignalSpy finished(&runtime, &ScriptRuntime::finished);
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    connect(&runtime, &ScriptRuntime::logMessage, &runtime, [&](const QString &) {
        QTimer::singleShot(20, &runtime, &ScriptRuntime::reset);
    });
    const auto id = runtime.execute(QStringLiteral(
        "print('ready');coroutine.wrap(function()coroutine.wrap(function()while true do end end)()end)()"));
    const auto stopped = waitFor(finished, id);
    QVERIFY2(stopped.error.contains(QStringLiteral("cancelled")), qPrintable(stopped.error));
    QTRY_COMPARE(reset.count(), 1);
    QCOMPARE(runtime.memoryBytes(), 0);
    QVERIFY(evaluate(runtime, QStringLiteral("return 42")).ok);
}

void ScriptRuntimeTest::executionHasNoAutomaticBudget() {
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "local n=0;debug.sethook(function(e)if e=='count'then n=n+1 end end,'',1000);"
        "while n<30001 do end;debug.sethook();return n"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.values.constFirst().toInt() >= 30001);
}

void ScriptRuntimeTest::runningSignalsAndBlockedReset() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto ready = directory.filePath(QStringLiteral("ready"));
    ScriptRuntime runtime;
    QSignalSpy finished(&runtime, &ScriptRuntime::finished);
    QSignalSpy reset(&runtime, &ScriptRuntime::resetFinished);
    QSignalSpy blocked(&runtime, &ScriptRuntime::resetBlocked);
    QSignalSpy running(&runtime, &ScriptRuntime::runningChanged);
    QSignalSpy longRunning(&runtime, &ScriptRuntime::longRunningChanged);
    const auto id = runtime.execute(QStringLiteral("os.execute((...).command);return true"), {},
                                   {{QStringLiteral("command"), sleepingChildCommand(ready, 3300)}});
    QTRY_VERIFY(QFile::exists(ready));
    QVERIFY(runtime.isRunning());
    runtime.reset();
    QTRY_VERIFY(runtime.isLongRunning());
    QTRY_COMPARE(blocked.count(), 1);
    QVERIFY(!waitFor(finished, id).ok);
    QTRY_COMPARE(reset.count(), 1);
    QTRY_VERIFY(!runtime.isRunning());
    QTRY_VERIFY(!longRunning.isEmpty() && !longRunning.last().at(0).toBool());
    QVERIFY(!running.isEmpty());
    QVERIFY(evaluate(runtime, QStringLiteral("return 1")).ok);
}

void ScriptRuntimeTest::blockingStandardCallHasNoExecutionBudget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "local ok= os.execute((...).command);return ok==true,17"),
        {{QStringLiteral("command"), sleepingChildCommand(directory.filePath(QStringLiteral("ready")), 3000)}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values, QVariantList({true,17}));
}

void ScriptRuntimeTest::blockedDestructorRetainsWorkerUntilItReturns() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto ready = directory.filePath(QStringLiteral("ready"));
    auto runtime = std::make_unique<ScriptRuntime>();
    runtime->execute(QStringLiteral("os.execute((...).command)"), {},
                     {{QStringLiteral("command"), sleepingChildCommand(ready, 3200)}});
    QTRY_VERIFY(QFile::exists(ready));
    QVERIFY(runtime->isRunning());
    QElapsedTimer elapsed;
    elapsed.start();
    runtime.reset();
    QVERIFY2(elapsed.elapsed() < 2000, "Blocked C code delayed runtime destruction beyond two seconds");
    // The original thread returns and frees its VM after its facade is gone.
    // A subsequent event-processing interval must produce no QThread warnings.
    QTest::qWait(1600);
    ScriptRuntime recovered;
    QVERIFY(evaluate(recovered, QStringLiteral("return 42")).ok);
}

void ScriptRuntimeTest::weakHookEntriesDoNotRetainCoroutines() {
    ScriptRuntime runtime;
    const auto result = evaluate(runtime, QStringLiteral(
        "local weak=setmetatable({},{__mode='v'});do local co=coroutine.create(function()end);"
        "debug.sethook(co,function()end,'l',10);weak[1]=co end;"
        "collectgarbage('collect');collectgarbage('collect');return weak[1]==nil"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.values.constFirst().toBool());
}

void ScriptRuntimeTest::runningObserverMayDestroyTheRuntime() {
    auto runtime = std::make_unique<ScriptRuntime>();
    const QPointer<ScriptRuntime> guard(runtime.get());
    connect(runtime.get(), &ScriptRuntime::runningChanged, this, [&](bool running) {
        if (running)
            runtime.reset();
    });
    QVERIFY(runtime->execute(QStringLiteral("while true do end")));
    QTRY_VERIFY(guard.isNull());
}

void ScriptRuntimeTest::explicitCoroutineHooksReserveStackSpace_data() {
    QTest::addColumn<QByteArray>("operation");
    QTest::newRow("sethook") << QByteArray("sethook");
    QTest::newRow("gethook") << QByteArray("gethook");
}

void ScriptRuntimeTest::explicitCoroutineHooksReserveStackSpace() {
    QFETCH(QByteArray, operation);
    using namespace lua_runtime::detail;
    ScriptWorker worker(RuntimeLimits{}, std::make_shared<SharedStatistics>());
    auto *caller = ScriptWorkerTestAccess::initializedState(worker);
    QVERIFY(caller);
    auto *target = lua_newthread(caller);
    const int threadIndex = lua_gettop(caller);
    QVERIFY(lua_checkstack(target, 64));
    const int available = static_cast<int>(target->stack_last.p - target->top.p);
    // lua_checkstack uses a strict 'free > requested' comparison, so asking
    // for the exact free capacity would grow it again. This internal fixture
    // permits the initial C frame to use the allocation we already reserved,
    // then fills every slot without changing that allocation.
    target->ci->top.p = target->stack_last.p;
    for (int i = 0; i < available; ++i)
        lua_pushnil(target);
    const auto capacity = target->stack_last.p - target->stack.p;
    const int originalValues = lua_gettop(target);
    QCOMPARE(target->stack_last.p - target->top.p, 0);

    lua_getglobal(caller, "debug");
    lua_getfield(caller, -1, operation.constData());
    lua_pushvalue(caller, threadIndex);
    int arguments = 1;
    if (operation == "sethook") {
        lua_pushcfunction(caller, [](lua_State *) { return 0; });
        lua_pushliteral(caller, "l");
        lua_pushinteger(caller, 7);
        arguments = 4;
    }
    const auto status = lua_pcall(caller, arguments, LUA_MULTRET, 0);
    const auto error = status == LUA_OK ? QByteArray{} : QByteArray(lua_tostring(caller, -1));
    QVERIFY2(status == LUA_OK, error.constData());
    QCOMPARE(lua_gettop(target), originalValues);
    QVERIFY2(target->stack_last.p - target->stack.p > capacity,
             "Cross-coroutine debug hooks must reserve space before pushing on the target stack");
}

void ScriptRuntimeTest::runtimeHooksOnlyRequestUserEvents() {
    using namespace lua_runtime::detail;
    ScriptWorker worker(RuntimeLimits{}, std::make_shared<SharedStatistics>());
    auto *state = ScriptWorkerTestAccess::initializedState(worker);
    QVERIFY(state);
    ScriptWorkerTestAccess::registerHookProbe(state);
    QSignalSpy finished(&worker, &ScriptWorker::jobFinished);
    ScriptJob job;
    job.id = 1;
    job.generation = 1;
    job.cancellation = 1;
    job.name = "=runtime-hook-configuration-test";
    job.context = {{QStringLiteral("countMask"), LUA_MASKCOUNT},
                   {QStringLiteral("callMask"), LUA_MASKCALL},
                   {QStringLiteral("returnMask"), LUA_MASKRET},
                   {QStringLiteral("lineMask"), LUA_MASKLINE}};
    job.source = R"lua(
local expected = ...
local function verify(mask, count, co)
    local actualMask, actualCount = test_hook_configuration(co)
    assert(actualMask == mask, ('hook mask %d, expected %d'):format(actualMask, mask))
    assert(actualCount == count, ('hook count %d, expected %d'):format(actualCount, count))
end
assert(debug.gethook() == nil)
verify(expected.countMask, 1000)
local co = coroutine.create(function()
    assert(debug.gethook() == nil)
    verify(expected.countMask, 1000)
end)
verify(expected.countMask, 1000, co)
assert(coroutine.resume(co))
local userHook = function() end
debug.sethook(userHook, 'c')
local hook, flags, count = debug.gethook()
assert(hook == userHook and flags == 'c' and count == 0)
verify(expected.countMask | expected.callMask, 1000)
debug.sethook(userHook, 'r')
verify(expected.countMask | expected.returnMask, 1000)
debug.sethook(userHook, 'crl', 1500)
verify(expected.countMask | expected.callMask | expected.returnMask | expected.lineMask, 500)
debug.sethook(userHook, '', 7)
verify(expected.countMask, 1)
debug.sethook()
assert(debug.gethook() == nil)
verify(expected.countMask, 1000)
debug.sethook(co, userHook, 'cr', 1500)
verify(expected.countMask | expected.callMask | expected.returnMask, 500, co)
debug.sethook(co)
assert(debug.gethook(co) == nil)
verify(expected.countMask, 1000, co)
return true
)lua";
    worker.enqueue(std::move(job));
    QTRY_COMPARE(finished.size(), 1);
    const auto result = qvariant_cast<ScriptResult>(finished.constFirst().at(1));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values, QVariantList{true});
    QCOMPARE(lua_gethookmask(state), LUA_MASKCOUNT);
    QCOMPARE(lua_gethookcount(state), 1000);
}

void ScriptRuntimeTest::cancellationRestoresRetainedCoroutineHooks() {
    using namespace lua_runtime::detail;
    const auto statistics = std::make_shared<SharedStatistics>();
    ScriptWorker worker(RuntimeLimits{}, statistics);
    auto *state = ScriptWorkerTestAccess::initializedState(worker);
    QVERIFY(state);
    ScriptWorkerTestAccess::registerHookProbe(state);
    QSignalSpy finished(&worker, &ScriptWorker::jobFinished);
    std::mutex readyMutex;
    std::condition_variable_any readyCondition;
    bool enteredCoroutine = false;
    connect(&worker, &ScriptWorker::logReady, &worker, [&](const QString &message) {
        if (message != QStringLiteral("retained coroutine ready"))
            return;
        {
            const std::lock_guard lock(readyMutex);
            enteredCoroutine = true;
        }
        readyCondition.notify_all();
    });
    // The worker runs on this test thread so it can expose its private state.
    // The helper touches only the same atomic cancellation field as the facade.
    std::jthread cancellation([&](std::stop_token stop) {
        std::unique_lock lock(readyMutex);
        if (readyCondition.wait(lock, stop, [&] { return enteredCoroutine; }))
            statistics->cancellation.fetch_add(1, std::memory_order_acq_rel);
    });
    ScriptJob first;
    first.id = 1;
    first.generation = 1;
    first.cancellation = 1;
    first.name = "=retained-coroutine-cancellation-test";
    first.source = R"lua(
retained = coroutine.create(function()
    print('retained coroutine ready')
    while not continueAfterStop do end
    return test_hook_configuration()
end)
coroutine.resume(retained)
)lua";
    worker.enqueue(std::move(first));
    QTRY_COMPARE(finished.size(), 1);
    const auto stopped = qvariant_cast<ScriptResult>(finished.constFirst().at(1));
    QVERIFY(!stopped.ok);
    QVERIFY2(stopped.error.contains(QStringLiteral("cancelled")), qPrintable(stopped.error));
    QVERIFY(enteredCoroutine);
    cancellation.join();

    ScriptJob second;
    second.id = 2;
    second.generation = 1;
    second.cancellation = statistics->cancellation.load(std::memory_order_acquire);
    second.name = "=retained-coroutine-recovery-test";
    second.source = R"lua(
assert(coroutine.status(retained) == 'suspended')
continueAfterStop = true
local ok, mask, count = coroutine.resume(retained)
assert(ok, mask)
assert(debug.gethook(retained) == nil)
return mask, count
)lua";
    worker.enqueue(std::move(second));
    QTRY_COMPARE(finished.size(), 2);
    const auto recovered = qvariant_cast<ScriptResult>(finished.at(1).at(1));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values, (QVariantList{LUA_MASKCOUNT, 1000}));
}

void ScriptRuntimeTest::retainedCoroutineCountHookKeepsItsInterval() {
    // Cancellation leaves the retained coroutine on one-instruction checks with
    // its user count hook due again in 1000 instructions. The first event after
    // resuming covers one instruction, not the restored interval, so the user
    // hook must not run before the loop below has started counting.
    using namespace lua_runtime::detail;
    const auto statistics = std::make_shared<SharedStatistics>();
    ScriptWorker worker(RuntimeLimits{}, statistics);
    QVERIFY(ScriptWorkerTestAccess::initializedState(worker));
    QSignalSpy finished(&worker, &ScriptWorker::jobFinished);
    bool cancellationRequested = false;
    connect(&worker, &ScriptWorker::logReady, &worker, [&](const QString &message) {
        if (message != QStringLiteral("counted coroutine ready"))
            return;
        // Request the stop before any user count callback can run. A separate
        // thread may instead cancel inside that callback, where Lua cannot
        // yield and the coroutine dies rather than remaining suspended.
        cancellationRequested = true;
        statistics->cancellation.fetch_add(1, std::memory_order_acq_rel);
    });
    ScriptJob first;
    first.id = 1;
    first.generation = 1;
    first.cancellation = 1;
    first.name = "=retained-count-hook-cancellation-test";
    first.source = R"lua(
counted = coroutine.create(function()
    debug.sethook(function()
        if resumed and not firstHit then firstHit = iterations or -1 end
    end, '', 1000)
    print('counted coroutine ready')
    while not continueAfterStop do end
    iterations = 0
    while not firstHit and iterations < 1000000 do iterations = iterations + 1 end
    return firstHit
end)
coroutine.resume(counted)
)lua";
    worker.enqueue(std::move(first));
    QTRY_COMPARE(finished.size(), 1);
    const auto stopped = qvariant_cast<ScriptResult>(finished.constFirst().at(1));
    QVERIFY(!stopped.ok);
    QVERIFY2(stopped.error.contains(QStringLiteral("cancelled")), qPrintable(stopped.error));
    QVERIFY(cancellationRequested);

    ScriptJob second;
    second.id = 2;
    second.generation = 1;
    second.cancellation = statistics->cancellation.load(std::memory_order_acquire);
    second.name = "=retained-count-hook-recovery-test";
    second.source = R"lua(
assert(coroutine.status(counted) == 'suspended')
continueAfterStop, resumed = true, true
local ok, hit = coroutine.resume(counted)
assert(ok, hit)
return hit
)lua";
    worker.enqueue(std::move(second));
    QTRY_COMPARE(finished.size(), 2);
    const auto recovered = qvariant_cast<ScriptResult>(finished.at(1).at(1));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values.size(), 1);
    QVERIFY2(recovered.values.constFirst().toInt() > 0, qPrintable(recovered.values.constFirst().toString()));
}

int main(int argc, char **argv) {
    if (argc == 4 && QByteArray(argv[1]) == "--lua-sleep") {
        QFile ready(QString::fromLocal8Bit(argv[3]));
        if (!ready.open(QIODevice::WriteOnly))
            return 2;
        ready.close();
        QThread::msleep(QByteArray(argv[2]).toUInt());
        return 0;
    }
    QCoreApplication application(argc, argv);
    ScriptRuntimeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "script_runtime_test.moc"
