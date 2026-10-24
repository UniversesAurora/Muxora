// SPDX-License-Identifier: MIT
// Scale benchmark for WindowSystem layout operations. It is not a test: it
// prints timing and memory samples as JSON for before/after comparisons.
#include "benchmark_support.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QSysInfo>
#include <QTest>
#include <QThread>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace window_system;
using namespace window_system::benchmark;

namespace {

constexpr int kTabsPerGroup = 25;
constexpr int kCreateProgressStep = 100;
constexpr int kKeyClicksPerSample = 1000;
constexpr int kCycleIterations = 1000;
constexpr int kCycleSampleStep = 100;

const QStringList kScenarios = {
    QStringLiteral("restore"), QStringLiteral("create"), QStringLiteral("close"), QStringLiteral("move"),
    QStringLiteral("split"), QStringLiteral("ratio"), QStringLiteral("keys"),
    QStringLiteral("save"), QStringLiteral("cycle")};

struct Options {
    QVector<int> tabs;
    int repeat = 30;
    QSet<QString> scenarios;
    QString out;
    QString commit;
};

struct Report {
    QJsonArray results;
    QJsonArray memory;
    QJsonArray cycle;
    QJsonArray save;
    QJsonArray createProgress;
    QVector<int> completedTabs;
};

struct Workspace {
    std::unique_ptr<WindowManager> manager;
    WindowId window;
    QVector<GroupId> groups; // Creation order; each new group splits the previous one.
    QVector<TabId> tabs;
    ~Workspace()
    {
        manager.reset();
        drainDeferredDeletes();
    }
};

[[noreturn]] void abortBenchmark(const char* what)
{
    std::fprintf(stderr, "layout benchmark failed: %s\n", what);
    std::fflush(stderr);
    std::exit(2);
}

void require(bool condition, const char* what)
{
    if (!condition) abortBenchmark(what);
}

// One pass of what a running event loop does after an operation: posted
// layout and paint work, then deleteLater() requests. The benchmark never
// enters exec(), and processEvents() alone does not deliver DeferredDelete
// at that loop level, so discarded views would otherwise pile up.
void settleEventLoop()
{
    QCoreApplication::processEvents();
    drainDeferredDeletes();
}

// The event loop pass is part of every measurement: users see the result
// only after posted layout and paint work has been processed.
template <typename Operation>
double timed(Operation&& operation)
{
    QElapsedTimer timer;
    timer.start();
    operation();
    settleEventLoop();
    return double(timer.nsecsElapsed()) / 1.0e6;
}

TabOptions benchTab(int index)
{
    TabOptions options;
    options.title = QStringLiteral("Tab %1").arg(index);
    options.contentType = QStringLiteral("bench");
    return options;
}

QLabel* benchContent(int index)
{
    return new QLabel(QString::number(index));
}

QVector<int> parseTabs(const QString& text)
{
    QVector<int> result;
    for (const auto& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        require(ok && value > kTabsPerGroup, "--tabs needs sizes above 25 separated by commas");
        result.append(value);
    }
    require(!result.isEmpty(), "--tabs is empty");
    return result;
}

QSet<QString> parseScenarios(const QStringList& values)
{
    QSet<QString> result;
    for (const auto& value : values)
        for (const auto& part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const auto name = part.trimmed();
            if (name == QStringLiteral("all")) {
                for (const auto& scenario : kScenarios) result.insert(scenario);
                continue;
            }
            require(kScenarios.contains(name), "unknown --scenario");
            result.insert(name);
        }
    if (result.isEmpty())
        for (const auto& scenario : kScenarios) result.insert(scenario);
    return result;
}

Options parseOptions(const QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("WindowSystem layout scale benchmark"));
    parser.addHelpOption();
    const QCommandLineOption tabs(QStringLiteral("tabs"), QStringLiteral("Comma-separated tab counts."),
        QStringLiteral("list"), QStringLiteral("100,500,1000"));
    const QCommandLineOption repeat(QStringLiteral("repeat"), QStringLiteral("Samples per scenario."),
        QStringLiteral("count"), QStringLiteral("30"));
    const QCommandLineOption scenario(QStringLiteral("scenario"),
        QStringLiteral("all|restore|create|close|move|split|ratio|keys|save|cycle; repeatable."),
        QStringLiteral("name"));
    const QCommandLineOption out(QStringLiteral("out"), QStringLiteral("JSON output path; stdout when omitted."),
        QStringLiteral("path"));
    const QCommandLineOption commit(QStringLiteral("commit"), QStringLiteral("Source revision label."),
        QStringLiteral("hash"));
    parser.addOptions({tabs, repeat, scenario, out, commit});
    parser.process(app);

    Options options;
    options.tabs = parseTabs(parser.value(tabs));
    bool ok = false;
    options.repeat = parser.value(repeat).toInt(&ok);
    require(ok && options.repeat > 0, "--repeat must be a positive integer");
    options.scenarios = parseScenarios(parser.values(scenario));
    options.out = parser.value(out);
    options.commit = parser.value(commit);
    return options;
}

void addResult(Report& report, const QString& scenario, int tabs, const QVector<double>& samples)
{
    const auto summary = summarize(samples);
    report.results.append(QJsonObject{
        {QStringLiteral("scenario"), scenario}, {QStringLiteral("tabs"), tabs},
        {QStringLiteral("samples"), summary.count}, {QStringLiteral("p50Ms"), summary.p50},
        {QStringLiteral("p95Ms"), summary.p95}, {QStringLiteral("maxMs"), summary.max},
        {QStringLiteral("meanMs"), summary.mean}});
}

std::unique_ptr<Workspace> openWorkspace()
{
    auto workspace = std::make_unique<Workspace>();
    workspace->manager = std::make_unique<WindowManager>();
    WindowOptions options;
    options.title = QStringLiteral("Layout benchmark");
    options.size = QSize(1600, 1000);
    workspace->window = workspace->manager->createWindow(WindowKind::Workbench, options);
    require(!workspace->window.isNull(), "createWindow");
    workspace->manager->showWindow(workspace->window);
    require(QTest::qWaitForWindowExposed(workspace->manager->windowWidget(workspace->window)),
            "window exposure");
    drainDeferredDeletes();
    return workspace;
}

QJsonObject restoreSnapshot(int tabs)
{
    LayoutModel model;
    const auto window = model.createWindow(WindowKind::Workbench);
    require(!window.isNull(), "restore: model window");
    GroupId group;
    QVector<TabId> ids;
    for (int index = 0; index < tabs; ++index) {
        const auto title = QStringLiteral("Tab %1").arg(index);
        const TabId tab = group.isNull() ? model.addTab(window, title)
            : index % kTabsPerGroup == 0
                ? model.addTabToNewGroup(window, title, group,
                    (index / kTabsPerGroup) % 2 == 1 ? Placement::Right : Placement::Bottom)
                : model.addTab(window, title, group);
        require(!tab.isNull(), "restore: model tab");
        group = model.groupForTab(tab);
        ids.append(tab);
    }

    WindowManager seed;
    WindowOptions options;
    options.title = QStringLiteral("Restored layout benchmark");
    options.size = QSize(1600, 1000);
    require(!seed.createWindow(WindowKind::Workbench, options).isNull(), "restore: seed window");
    auto snapshot = seed.saveLayout();
    snapshot.insert(QStringLiteral("model"), model.toJson());
    QJsonArray panes;
    for (const auto tab : ids) {
        const auto* record = model.tab(tab);
        panes.append(QJsonObject{{"id", record->activePane.toString(QUuid::WithoutBraces)},
            {"title", record->title}, {"type", "bench.text"}, {"state", QJsonObject{}},
            {"showIcon", true}, {"topBarSharing", "shared"}, {"bottomBarSharing", "shared"}});
    }
    snapshot.insert(QStringLiteral("panes"), panes);
    auto windows = snapshot.value("windows").toArray();
    auto metadata = windows.first().toObject();
    metadata.insert("id", window.toString(QUuid::WithoutBraces));
    metadata.insert("title", "Restored layout benchmark");
    windows.replace(0, metadata);
    snapshot.insert(QStringLiteral("windows"), windows);
    return snapshot;
}

struct RestoreMeasurement {
    std::unique_ptr<Workspace> workspace;
    double elapsedMs = 0;
    quint64 emptyBytes = 0;
    int emptyWidgets = 0;
};

RestoreMeasurement openRestoredWorkspace(int tabs)
{
    RestoreMeasurement result{openWorkspace()};
    result.emptyBytes = privateBytes();
    result.emptyWidgets = liveWidgetCount();
    auto& manager = *result.workspace->manager;
    manager.setContentFactory([](const TabOptions& options, PaneId) {
        return new QLabel(options.title);
    });
    QElapsedTimer timer;
    timer.start();
    const auto restored = manager.restoreLayout(restoreSnapshot(tabs));
    require(restored.ok, "restoreLayout");
    settleEventLoop();
    result.elapsedMs = double(timer.nsecsElapsed()) / 1.0e6;
    result.workspace->window = manager.model().windowOrder().first();
    result.workspace->groups = manager.model().centralGroups(result.workspace->window);
    result.workspace->tabs = manager.model().orderedTabs(result.workspace->window);
    manager.showWindow(result.workspace->window);
    require(QTest::qWaitForWindowExposed(manager.windowWidget(result.workspace->window)),
            "restored window exposure");
    settleEventLoop();
    return result;
}

// Every 25 tabs start a new group beside the previous one, alternating
// right and bottom so the central tree gains depth.
void appendTab(Workspace& workspace, int index)
{
    auto& manager = *workspace.manager;
    TabId tab;
    if (workspace.groups.isEmpty()) {
        tab = manager.addTab(workspace.window, benchContent(index), benchTab(index));
    } else if (index % kTabsPerGroup == 0) {
        const auto placement = workspace.groups.size() % 2 == 1 ? Placement::Right : Placement::Bottom;
        tab = manager.addTabToNewGroup(workspace.window, benchContent(index), benchTab(index),
                                       workspace.groups.last(), placement);
    } else {
        tab = manager.addTab(workspace.window, benchContent(index), benchTab(index), workspace.groups.last());
    }
    require(!tab.isNull(), "addTab while building the workspace");
    if (workspace.groups.isEmpty() || index % kTabsPerGroup == 0)
        workspace.groups.append(manager.model().groupForTab(tab));
    workspace.tabs.append(tab);
}

void buildWorkspace(Workspace& workspace, int tabs, Report& report, bool record)
{
    QElapsedTimer total;
    total.start();
    for (int index = 0; index < tabs; ++index) {
        appendTab(workspace, index);
        settleEventLoop();
        if (record && (index + 1) % kCreateProgressStep == 0)
            report.createProgress.append(QJsonObject{
                {QStringLiteral("tabs"), tabs}, {QStringLiteral("added"), index + 1},
                {QStringLiteral("elapsedMs"), double(total.nsecsElapsed()) / 1.0e6}});
    }
    const double elapsed = double(total.nsecsElapsed()) / 1.0e6;
    if (record) addResult(report, QStringLiteral("create"), tabs, {elapsed});
}

void recordMemory(Report& report, int tabs, quint64 emptyBytes, int emptyWidgets,
                  const QString& workspace)
{
    drainDeferredDeletes();
    const quint64 bytes = privateBytes();
    const int widgets = liveWidgetCount();
    report.memory.append(QJsonObject{
        {QStringLiteral("workspace"), workspace}, {QStringLiteral("tabs"), tabs},
        {QStringLiteral("privateBytes"), qint64(bytes)},
        {QStringLiteral("widgets"), widgets},
        {QStringLiteral("bytesPerTab"), (double(bytes) - double(emptyBytes)) / tabs},
        {QStringLiteral("widgetsPerTab"), double(widgets - emptyWidgets) / tabs}});
}

int middleIndex(const Workspace& workspace)
{
    return int(workspace.tabs.size() / 2);
}

QVector<double> measureClose(Workspace& workspace, int repeat)
{
    auto& manager = *workspace.manager;
    const int middle = middleIndex(workspace);
    QVector<double> samples;
    for (int sample = 0; sample < repeat; ++sample) {
        const TabId tab = workspace.tabs.at(middle);
        const GroupId group = manager.model().groupForTab(tab);
        const auto* record = manager.model().group(group);
        require(record != nullptr, "close: group lookup");
        const int index = int(record->tabs.indexOf(tab));
        samples.append(timed([&] { require(manager.closeTab(tab), "closeTab"); }));
        const TabId restored = manager.addTab(workspace.window, benchContent(middle), benchTab(middle), group, index);
        require(!restored.isNull(), "close: restore tab");
        workspace.tabs[middle] = restored;
        drainDeferredDeletes();
    }
    return samples;
}

QVector<double> measureMove(Workspace& workspace, int repeat)
{
    auto& manager = *workspace.manager;
    require(workspace.groups.size() >= 2, "move needs two groups");
    const TabId tab = workspace.tabs.at(1);
    const GroupId origin = manager.model().groupForTab(tab);
    const GroupId other = workspace.groups.last();
    QVector<double> samples;
    for (int sample = 0; sample < repeat; ++sample) {
        const GroupId target = sample % 2 == 0 ? other : origin;
        samples.append(timed([&] { require(manager.moveTab(tab, target, Placement::Center), "moveTab"); }));
    }
    return samples;
}

QVector<double> measureSplit(Workspace& workspace, int repeat)
{
    auto& manager = *workspace.manager;
    const TabId tab = workspace.tabs.at(middleIndex(workspace));
    require(manager.activateTab(tab), "split: activateTab");
    settleEventLoop();
    QVector<double> samples;
    for (int sample = 0; sample < repeat; ++sample) {
        const auto* record = manager.model().tab(tab);
        require(record != nullptr, "split: tab lookup");
        const PaneId pane = record->activePane;
        PaneId created;
        samples.append(timed([&] {
            created = manager.splitPane(tab, pane, new QLabel(QStringLiteral("split")), Placement::Right);
            require(!created.isNull(), "splitPane");
        }));
        require(manager.closePane(tab, created), "split: closePane");
        drainDeferredDeletes();
    }
    return samples;
}

QVector<double> measureRatio(Workspace& workspace, int repeat)
{
    auto& manager = *workspace.manager;
    QVector<double> samples;
    for (int sample = 0; sample < repeat; ++sample) {
        const auto* record = manager.model().window(workspace.window);
        require(record && record->central && !record->central->isLeaf(), "ratio: central split root");
        const QVariantMap args{{QStringLiteral("node"), record->central->id},
                               {QStringLiteral("ratio"), sample % 2 == 0 ? 0.3 : 0.7}};
        samples.append(timed([&] {
            require(manager.execute(QStringLiteral("layout.setRatio"), args, workspace.window), "layout.setRatio");
        }));
    }
    return samples;
}

QVector<double> measureKeys(Workspace& workspace, int repeat)
{
    auto& manager = *workspace.manager;
    const TabId tab = workspace.tabs.at(middleIndex(workspace));
    require(manager.activateTab(tab), "keys: activateTab");
    const auto* record = manager.model().tab(tab);
    require(record != nullptr, "keys: tab lookup");
    QWidget* content = manager.paneWidget(record->activePane);
    require(content != nullptr, "keys: pane widget");
    manager.activateWindow(workspace.window);
    content->setFocusPolicy(Qt::StrongFocus);
    content->setFocus(Qt::OtherFocusReason);
    settleEventLoop();
    QVector<double> samples;
    for (int sample = 0; sample < repeat; ++sample) {
        const double total = timed([&] {
            for (int click = 0; click < kKeyClicksPerSample; ++click) QTest::keyClick(content, Qt::Key_X);
        });
        samples.append(total / kKeyClicksPerSample);
    }
    return samples;
}

QVector<double> measureSave(Workspace& workspace, int repeat, Report& report, int tabs)
{
    auto& manager = *workspace.manager;
    QVector<double> samples;
    qsizetype bytes = 0;
    for (int sample = 0; sample < repeat; ++sample)
        samples.append(timed([&] { bytes = QJsonDocument(manager.saveLayout()).toJson().size(); }));
    report.save.append(QJsonObject{{QStringLiteral("tabs"), tabs}, {QStringLiteral("bytes"), qint64(bytes)}});
    return samples;
}

void recordCycle(Report& report, int tabs, int iteration)
{
    drainDeferredDeletes();
    report.cycle.append(QJsonObject{
        {QStringLiteral("tabs"), tabs}, {QStringLiteral("iteration"), iteration},
        {QStringLiteral("privateBytes"), qint64(privateBytes())},
        {QStringLiteral("widgets"), liveWidgetCount()}});
}

QVector<double> measureCycle(Workspace& workspace, Report& report, int tabs)
{
    auto& manager = *workspace.manager;
    const GroupId group = manager.model().groupForTab(workspace.tabs.at(middleIndex(workspace)));
    QVector<double> samples;
    recordCycle(report, tabs, 0);
    for (int iteration = 1; iteration <= kCycleIterations; ++iteration) {
        samples.append(timed([&] {
            const TabId tab = manager.addTab(workspace.window, benchContent(iteration), benchTab(iteration), group);
            require(!tab.isNull(), "cycle: addTab");
            require(manager.closeTab(tab), "cycle: closeTab");
        }));
        if (iteration % kCycleSampleStep == 0) recordCycle(report, tabs, iteration);
    }
    return samples;
}

void progress(const QString& scenario, int tabs)
{
    std::fprintf(stderr, "[layout-benchmark] %d tabs: %s\n", tabs, qPrintable(scenario));
    std::fflush(stderr);
}

void runScale(const Options& options, int tabs, Report& report)
{
    const auto has = [&](const char* name) { return options.scenarios.contains(QLatin1String(name)); };
    const bool needsBuiltWorkspace = has("create") || has("close") || has("move") || has("split")
        || has("ratio") || has("keys") || has("save") || has("cycle");
    if (needsBuiltWorkspace) {
        auto workspace = openWorkspace();
        const quint64 emptyBytes = privateBytes();
        const int emptyWidgets = liveWidgetCount();
        progress(QStringLiteral("create"), tabs);
        buildWorkspace(*workspace, tabs, report, has("create"));
        if (has("create"))
            recordMemory(report, tabs, emptyBytes, emptyWidgets, QStringLiteral("create"));
        const auto run = [&](const char* name, auto&& measure) {
            if (!has(name)) return;
            progress(QLatin1String(name), tabs);
            addResult(report, QLatin1String(name), tabs, measure());
        };
        run("close", [&] { return measureClose(*workspace, options.repeat); });
        run("move", [&] { return measureMove(*workspace, options.repeat); });
        run("split", [&] { return measureSplit(*workspace, options.repeat); });
        run("ratio", [&] { return measureRatio(*workspace, options.repeat); });
        run("keys", [&] { return measureKeys(*workspace, options.repeat); });
        run("save", [&] { return measureSave(*workspace, options.repeat, report, tabs); });
        run("cycle", [&] { return measureCycle(*workspace, report, tabs); });
    }
    if (has("restore")) {
        progress(QStringLiteral("restore"), tabs);
        RestoreMeasurement restored = openRestoredWorkspace(tabs);
        addResult(report, QStringLiteral("restore"), tabs, {restored.elapsedMs});
        recordMemory(report, tabs, restored.emptyBytes, restored.emptyWidgets, QStringLiteral("restore"));
        restored.workspace.reset();
        drainDeferredDeletes();
    }
}

QJsonObject reportJson(const Options& options, const Report& report)
{
    QJsonArray scales;
    for (const int tabs : options.tabs) scales.append(tabs);
    QJsonArray completed;
    for (const int tabs : report.completedTabs) completed.append(tabs);
    return QJsonObject{
        {QStringLiteral("benchmark"), QStringLiteral("layout-scale")},
        {QStringLiteral("commit"), options.commit},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("machine"), QJsonObject{
            {QStringLiteral("cpu"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("logicalCores"), QThread::idealThreadCount()},
            {QStringLiteral("os"), QSysInfo::prettyProductName()}}},
        {QStringLiteral("tabs"), scales},
        {QStringLiteral("completedTabs"), completed},
        {QStringLiteral("complete"), report.completedTabs.size() == options.tabs.size()},
        {QStringLiteral("repeat"), options.repeat},
        {QStringLiteral("results"), report.results},
        {QStringLiteral("memory"), report.memory},
        {QStringLiteral("cycle"), report.cycle},
        {QStringLiteral("save"), report.save},
        {QStringLiteral("createProgress"), report.createProgress}};
}

void writeReport(const Options& options, const QByteArray& json)
{
    if (options.out.isEmpty()) {
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        return;
    }
    QSaveFile file(options.out);
    require(file.open(QIODevice::WriteOnly), "open --out file");
    require(file.write(json) == json.size(), "write --out file");
    require(file.commit(), "commit --out file");
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("window-system-layout-benchmark"));
    const auto options = parseOptions(app);
    Report report;
    for (const int tabs : options.tabs) {
        runScale(options, tabs, report);
        report.completedTabs.append(tabs);
        // Large scales take a long time; keep finished scales on disk even if
        // a later one is interrupted. "complete" stays false until the end.
        if (!options.out.isEmpty() || report.completedTabs.size() == options.tabs.size())
            writeReport(options, QJsonDocument(reportJson(options, report)).toJson(QJsonDocument::Indented));
    }
    return 0;
}
