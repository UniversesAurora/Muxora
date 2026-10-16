#include <lua_runtime/script_runtime.hpp>

#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <limits>
#include <memory>

using lua_runtime::RuntimeLimits;
using lua_runtime::ScriptResult;
using lua_runtime::ScriptRuntime;

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

} // namespace

class ScriptRuntimeTest final : public QObject {
    Q_OBJECT
  private slots:
    void standardValuesAndContext();
    void hostCallsYieldToTheOwnerAndReturnMultipleValues();
    void hostErrorsRemainCatchableAndDoNotPoisonTheInterpreter();
    void callbacksHaveExplicitLifetimeAndContext();
    void callbackCountIsBounded();
    void resetInvalidatesCallbacksAndReleasesAllVmMemory();
    void resetInsideAHostCallDiscardsItsOldReply();
    void hostCanDestroyTheRuntimeDuringItsCallback();
    void cancellationDuringExecutionKeepsTheOwnerResponsive();
    void protectedCallsCannotSwallowTheInstructionBudget_data();
    void protectedCallsCannotSwallowTheInstructionBudget();
    void executionDeadlineDoesNotBlockTheOwner();
    void missingAndUnsafeLibrariesStayUnavailable();
    void compileSupportsTextModulesWithinTheSameTask();
    void compileRejectsBytecodeAndOversizedSources();
    void sourceAndQueueLimitsAreEnforced();
    void rejectionRepliesRemainBoundedUnderFlood_data();
    void rejectionRepliesRemainBoundedUnderFlood();
    void rejectionObserverMayDestroyTheRuntime();
    void transferLimitsAndUnsupportedValues_data();
    void transferLimitsAndUnsupportedValues();
    void invalidHostValuesAreRejected();
    void pendingResultsShareTheTransferBudget();
    void logsHaveCountAndLengthLimits();
    void memoryExhaustionLeavesTheVmUsable();
    void repeatedResetReleasesCallbacksAndJobStorage();
    void cleanupCannotExecuteUnboundedFinalizers();
};

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
    limits.instructionLimit = std::numeric_limits<quint64>::max();
    limits.timeLimitMs = 5000;
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

void ScriptRuntimeTest::protectedCallsCannotSwallowTheInstructionBudget_data() {
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
        "local f=muxora._compile('while true do pcall(function()while true do end end) end'); pcall(f)");
    QTest::newRow("mutated-globals") << QStringLiteral(
        "local protected=pcall; muxora=nil; table=nil; pcall=nil; while true do protected(function()while "
        "true do end end)end");
}

void ScriptRuntimeTest::protectedCallsCannotSwallowTheInstructionBudget() {
    QFETCH(QString, source);
    RuntimeLimits limits;
    limits.instructionLimit = 10'000;
    limits.timeLimitMs = 2000;
    ScriptRuntime runtime(limits);
    const auto result = evaluate(runtime, source);
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("instruction limit")), qPrintable(result.error));
    const auto recovered = evaluate(runtime, QStringLiteral("return 3"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
}

void ScriptRuntimeTest::executionDeadlineDoesNotBlockTheOwner() {
    RuntimeLimits limits;
    limits.instructionLimit = std::numeric_limits<quint64>::max();
    limits.timeLimitMs = 40;
    ScriptRuntime runtime(limits);
    QTimer heartbeat;
    heartbeat.setInterval(1);
    int ticks = 0;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start();
    const auto result = evaluate(runtime, QStringLiteral("while true do end"));
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("time limit")), qPrintable(result.error));
    QVERIFY(ticks > 0);
}

void ScriptRuntimeTest::missingAndUnsafeLibrariesStayUnavailable() {
    ScriptRuntime runtime;
    const auto result = evaluate(
        runtime,
        QStringLiteral("return io==nil,os==nil,debug==nil,coroutine==nil,package==nil,load==nil,"
                       "loadfile==nil,dofile==nil,string.dump==nil,math.floor(1.9)==1,utf8.len('中文')==2"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.size(), 11);
    for (const auto &value : result.values)
        QVERIFY(value.toBool());
}

void ScriptRuntimeTest::compileSupportsTextModulesWithinTheSameTask() {
    ScriptRuntime runtime;
    const auto result = evaluate(
        runtime,
        QStringLiteral(
            "local module=muxora._compile('local value=...; return {answer=value+1}', 'custom/module'); "
            "return module(41).answer"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.values.constFirst().toLongLong(), 42);
}

void ScriptRuntimeTest::compileRejectsBytecodeAndOversizedSources() {
    RuntimeLimits limits;
    limits.maximumSourceBytes = 256;
    ScriptRuntime runtime(limits);
    const auto binary =
        evaluate(runtime, QStringLiteral("return muxora._compile(string.char(27)..'Lua', 'binary')"));
    QVERIFY(!binary.ok);
    QVERIFY(binary.error.contains(QStringLiteral("binary")));
    const auto oversized = evaluate(runtime, QStringLiteral("return muxora._compile(string.rep(' ',257))"));
    QVERIFY(!oversized.ok);
    QVERIFY(oversized.error.contains(QStringLiteral("byte limit")));
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

void ScriptRuntimeTest::memoryExhaustionLeavesTheVmUsable() {
    RuntimeLimits limits;
    limits.memoryBytes = 1024 * 1024;
    limits.instructionLimit = 10'000'000;
    limits.timeLimitMs = 2000;
    ScriptRuntime runtime(limits);
    const auto exhausted =
        evaluate(runtime, QStringLiteral("local t={};while true do t[#t+1]=string.rep('x',4096)..#t end"));
    QVERIFY(!exhausted.ok);
    QVERIFY2(exhausted.error.contains(QStringLiteral("memory budget")), qPrintable(exhausted.error));
    QVERIFY(runtime.memoryBytes() <= limits.memoryBytes);
    QVERIFY(runtime.peakMemoryBytes() <= limits.memoryBytes);
    const auto recovered = evaluate(runtime, QStringLiteral("return 17"));
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values.constFirst().toLongLong(), 17);
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

QTEST_GUILESS_MAIN(ScriptRuntimeTest)
#include "script_runtime_test.moc"
