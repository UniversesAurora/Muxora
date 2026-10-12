#include <window_system/window_manager.hpp>
#include "workspace_host/placeholder_content.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    app.setApplicationName("Muxora"); app.setOrganizationName("Muxora"); app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Muxora 窗口系统预览")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"fresh", QStringLiteral("使用初始布局，不读取保存状态")});
    parser.addOption({"smoke", QStringLiteral("启动检查后自动退出")});
    parser.addOption({"screenshot", QStringLiteral("启动后保存窗口截图"), "path"});
    parser.addOption({"command", QStringLiteral("启动后执行已注册命令"), "id"});
    parser.addOption({"state-dir", QStringLiteral("指定独立布局状态目录"), "path"}); parser.process(app);
    window_system::WindowManager windows;
    windows.setContentFactory([&windows](const window_system::TabOptions& options, window_system::PaneId) {
        return muxora::createPlaceholder(options, &windows);
    });
    QObject::connect(&windows, &window_system::WindowManager::allWindowsClosed, &app, [&] {
        QTimer::singleShot(0, &app, [&] { if (windows.model().windows().isEmpty()) app.quit(); });
    });
    const auto stateRoot = parser.value("state-dir").isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::StateLocation) + "/window-preview"
        : parser.value("state-dir");
    const auto stateFile = stateRoot + "/window-layout.json";
    auto save = [&] {
        if (windows.model().windows().isEmpty()) return true;
        QDir().mkpath(stateRoot); QSaveFile file(stateFile);
        if (!file.open(QIODevice::WriteOnly)) return false;
        if (file.write(QJsonDocument(windows.saveLayout()).toJson()) < 0) return false;
        return file.commit();
    };
    int count = 1;
    windows.commands().add({"host.newTab", QStringLiteral("新建工作区标签"), "文件", QKeySequence("Ctrl+T"), {},
        [&windows, &count](const window_system::CommandContext& ctx) {
            window_system::TabOptions options{QStringLiteral("工作区 %1").arg(++count), "notes", {}};
            auto* content = muxora::createPlaceholder(options, &windows);
            const auto id = windows.addTab(ctx.window, content, options, ctx.group);
            if (id.isNull()) { delete content; return false; } return true;
        }});
    windows.commands().add({"layout.save", QStringLiteral("保存当前窗口布局"), "文件", QKeySequence("Ctrl+S"), {},
        [&windows, &save](const window_system::CommandContext& ctx) { const auto ok = save(); windows.setStatus(ctx.window, ok ? QStringLiteral("布局已保存") : QStringLiteral("布局保存失败")); return ok; }});
    windows.commands().add({"layout.reset", QStringLiteral("恢复初始工作区布局"), "视图", {}, {},
        [&windows](const window_system::CommandContext&) {
            const auto current = windows.model().windowOrder();
            for (const auto id : current) if (windows.model().window(id)) windows.requestCloseWindow(id);
            const auto id = windows.createWindow(window_system::WindowKind::Workbench, {"Muxora", QSize(1320, 850)});
            muxora::populateInitialWorkspace(windows, id); windows.showWindow(id); return true;
        }});
    windows.commands().add({"help.about", QStringLiteral("关于 Muxora"), "帮助", {}, {},
        [&windows](const window_system::CommandContext& ctx) {
            QMessageBox::about(windows.windowWidget(ctx.window), QStringLiteral("Muxora"),
                QStringLiteral("Muxora 0.1.0\n窗口系统首个预览\n\nQt / 原生 C++，MIT 开源。\n远程连接与文件功能将在窗口系统确定后接入。")); return true;
        }});
    QObject::connect(&windows, &window_system::WindowManager::commandFailed, &windows, [&windows](const QString& text) { windows.setStatus(windows.context().window, text); });
    bool restored = false;
    if (!parser.isSet("fresh")) {
        QFile file(stateFile);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024) {
            QJsonParseError error; const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                auto snapshot = doc.object();
                auto windowStates = snapshot.value("windows").toArray();
                // Early preview builds saved the module fallback title after a
                // restore. Keep their layout while correcting the app caption.
                for (qsizetype i = 0; i < windowStates.size(); ++i) {
                    auto record = windowStates.at(i).toObject();
                    if (record.value("title").toString() == "WindowSystem") {
                        record.insert("title", app.applicationName()); windowStates[i] = record;
                    }
                }
                snapshot.insert("windows", windowStates);
                restored = windows.restoreLayout(snapshot).ok;
            }
        }
    }
    if (!restored || windows.model().windows().isEmpty()) {
        const auto window = windows.createWindow(window_system::WindowKind::Workbench, {"Muxora", QSize(1320, 850)});
        muxora::populateInitialWorkspace(windows, window); windows.showWindow(window);
    }
    if (parser.isSet("command")) QTimer::singleShot(0, &app, [&] {
        if (!windows.execute(parser.value("command"))) app.exit(4);
    });
    QTimer persistTimer; persistTimer.setSingleShot(true); persistTimer.setInterval(700);
    QObject::connect(&windows, &window_system::WindowManager::layoutChanged, &persistTimer, [&persistTimer] { persistTimer.start(); });
    QObject::connect(&persistTimer, &QTimer::timeout, &app, save);
    persistTimer.start();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { if (!parser.isSet("smoke")) save(); });
    if (parser.isSet("screenshot")) QTimer::singleShot(700, &app, [&] {
        auto* widget = windows.windowWidget(windows.context().window);
        if (widget && !widget->grab().save(parser.value("screenshot"))) app.exit(2);
    });
    if (parser.isSet("smoke")) QTimer::singleShot(1300, &app, [&] {
        const auto valid = windows.model().validate(); app.exit(valid.ok && !windows.model().windows().isEmpty() ? 0 : 3);
    });
    return app.exec();
}
