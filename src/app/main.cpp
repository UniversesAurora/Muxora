#include <QCoreApplication>
#include <window_system/window_manager.hpp>
#include <window_system/localization.hpp>
#include "workspace_host/placeholder_content.hpp"
#include "state_paths.hpp"
#include "user_preferences.hpp"
#include "scripting_setup.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    app.setApplicationName("Muxora"); app.setOrganizationName("Muxora"); app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);
    // Locate user preferences before constructing localized CLI/UI descriptions.
    // The parser still performs validation once all translated options exist.
    QString explicitStateDirectory;
    const auto arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == "--") break;
        if (arguments[i].startsWith("--state-dir=")) explicitStateDirectory = arguments[i].mid(12);
        else if (arguments[i] == "--state-dir" && i + 1 < arguments.size()) explicitStateDirectory = arguments[++i];
        else if (arguments[i].startsWith("-state-dir=")) explicitStateDirectory = arguments[i].mid(11);
        else if (arguments[i] == "-state-dir" && i + 1 < arguments.size()) explicitStateDirectory = arguments[++i];
    }
    const auto stateRoot = muxora::resolveStateDirectory(explicitStateDirectory, QCoreApplication::applicationDirPath());
    const auto userDirectory = muxora::resolveUserDirectory(explicitStateDirectory, QCoreApplication::applicationDirPath());
    const auto requestedLanguage = window_system::uiLanguageFromId(muxora::UserPreferences(userDirectory).language());
    window_system::TranslationManager translations(&app);
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    if (!translations.installLanguage(requestedLanguage)) return 5;
    // Load the owned preferences after translation installation so any cached
    // configuration diagnostics use the selected display language as well.
    muxora::UserPreferences preferences(userDirectory);
    QCommandLineParser parser; parser.setApplicationDescription(QCoreApplication::translate("Muxora", "Muxora 窗口系统预览")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"fresh", QCoreApplication::translate("Muxora", "使用初始布局，不读取保存状态")});
    parser.addOption({"smoke", QCoreApplication::translate("Muxora", "启动检查后自动退出")});
    parser.addOption({"screenshot", QCoreApplication::translate("Muxora", "启动后保存窗口截图"), "path"});
    parser.addOption({"command", QCoreApplication::translate("Muxora", "启动后执行已注册命令"), "id"});
    parser.addOption({"state-dir", QCoreApplication::translate("Muxora", "指定独立布局状态目录"), "path"}); parser.process(app);
    window_system::WindowManager windows;
    windows.setContentFactory([&windows](const window_system::TabOptions& options, window_system::PaneId) {
        return muxora::createPlaceholder(options, &windows);
    });
    QObject::connect(&windows, &window_system::WindowManager::allWindowsClosed, &app, [&] {
        QTimer::singleShot(0, &app, [&] { if (windows.model().windows().isEmpty()) app.quit(); });
    });
    const auto stateFile = stateRoot + "/window-layout.json";
    auto save = [&] {
        if (windows.model().windows().isEmpty()) return true;
        QDir().mkpath(stateRoot); QSaveFile file(stateFile);
        if (!file.open(QIODevice::WriteOnly)) return false;
        if (file.write(QJsonDocument(windows.saveLayout()).toJson()) < 0) return false;
        return file.commit();
    };
    int count = 1;
    windows.commands().add({"host.newTab", QCoreApplication::translate("Muxora", "新建工作区标签"), "文件", QKeySequence("Ctrl+T"), {},
        [&windows, &count](const window_system::CommandContext& ctx) {
            window_system::TabOptions options{QCoreApplication::translate("Muxora", "工作区 %1").arg(++count), "notes", {}};
            auto* content = muxora::createPlaceholder(options, &windows);
            const auto id = windows.addTab(ctx.window, content, options, ctx.group);
            if (id.isNull()) { delete content; return false; } return true;
        }, {"文件", "新建标签"}, 10});
    struct TabKind { const char* command; const char* type; const char* title; };
    const TabKind previewKinds[] = {
        {"host.newFilesTab", "files", QT_TRANSLATE_NOOP("Muxora", "文件浏览")},
        {"host.newSessionsTab", "sessions", QT_TRANSLATE_NOOP("Muxora", "会话")},
        {"host.newTransfersTab", "transfers", QT_TRANSLATE_NOOP("Muxora", "传输队列")},
    };
    int creationOrder = 20;
    for (const auto& kind : previewKinds) {
        const auto title = QCoreApplication::translate("Muxora", kind.title);
        const auto type = QString::fromLatin1(kind.type);
        windows.commands().add({kind.command, QCoreApplication::translate("Muxora", "新建%1标签").arg(title), "文件", {}, {},
            [&windows, title, type](const window_system::CommandContext& ctx) {
                const window_system::TabOptions options{title, type, {}};
                auto* content = muxora::createPlaceholder(options, &windows);
                if (!windows.addTab(ctx.window, content, options, ctx.group).isNull()) return true;
                delete content;
                return false;
            }, {"文件", "新建标签"}, creationOrder++});
    }
    windows.setTabCreationCommands("host.newTab",
        {"host.newTab", "host.newFilesTab", "host.newSessionsTab", "host.newTransfersTab"});
    windows.commands().add({"layout.save", QCoreApplication::translate("Muxora", "保存工作区布局"), "文件", QKeySequence("Ctrl+S"), {},
        [&windows, &save](const window_system::CommandContext& ctx) { const auto ok = save(); windows.setStatus(ctx.window, ok ? QCoreApplication::translate("Muxora", "布局已保存") : QCoreApplication::translate("Muxora", "布局保存失败")); return ok; }, {"文件"}, 40});
    windows.commands().add({"application.exit", QCoreApplication::translate("Muxora", "退出"), "文件", QKeySequence(QKeySequence::Quit),
        [&windows](const window_system::CommandContext&) { return !windows.model().windows().isEmpty(); },
        [&windows, &save](const window_system::CommandContext& ctx) {
            if (!save()) { windows.setStatus(ctx.window, QCoreApplication::translate("Muxora", "布局保存失败，未退出")); return false; }
            return windows.requestCloseAllWindows();
        }, {"文件"}, 100});
    windows.commands().add({"layout.reset", QCoreApplication::translate("Muxora", "恢复初始工作区布局"), "布局", {}, {},
        [&windows](const window_system::CommandContext&) {
            const auto current = windows.model().windowOrder();
            for (const auto id : current) if (windows.model().window(id)) windows.requestCloseWindow(id);
            const auto id = windows.createWindow(window_system::WindowKind::Workbench, {"Muxora", QSize(1320, 850)});
            muxora::populateInitialWorkspace(windows, id); windows.showWindow(id); return true;
        }, {"布局"}, 1000});
    windows.commands().add({"help.about", QCoreApplication::translate("Muxora", "关于 Muxora"), "帮助", {}, {},
        [&windows](const window_system::CommandContext& ctx) {
            QMessageBox::about(windows.windowWidget(ctx.window), QStringLiteral("Muxora"),
                QCoreApplication::translate("Muxora", "Muxora 0.1.0\n窗口系统首个预览\n\nQt / 原生 C++，MIT 开源。\n远程连接与文件功能将在窗口系统确定后接入。")); return true;
        }, {"帮助"}, 10});
    // Persist provider layouts before script owners unregister their chrome.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { if (!parser.isSet("smoke")) save(); });
    muxora::AppScriptingSetup scripting(&windows, preferences);
    QObject::connect(&translations, &window_system::TranslationManager::requestedLanguageChanged, &app,
        [&](window_system::UiLanguage language) {
            const auto previous = window_system::uiLanguageFromId(preferences.language());
            if (preferences.saveLanguage(window_system::uiLanguageId(language))) return;
            const QSignalBlocker rollback(&translations);
            translations.setRequestedLanguage(previous);
            qWarning().noquote() << preferences.diagnostic();
            windows.setStatus(windows.context().window, preferences.diagnostic());
        });
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
    QObject::connect(&scripting, &muxora::AppScriptingSetup::initialized, &app, [&](bool ok, const QString&) {
        if (parser.isSet("command") && (!ok || !windows.execute(parser.value("command")))) app.exit(4);
    });
    scripting.start();
    QObject::connect(&windows, &window_system::WindowManager::lastWindowAboutToClose, &app, [&] { save(); });
    QTimer persistTimer; persistTimer.setSingleShot(true); persistTimer.setInterval(700);
    QObject::connect(&windows, &window_system::WindowManager::layoutChanged, &persistTimer, [&persistTimer] { persistTimer.start(); });
    QObject::connect(&persistTimer, &QTimer::timeout, &app, save);
    persistTimer.start();
    if (parser.isSet("screenshot")) QTimer::singleShot(700, &app, [&] {
        auto* widget = windows.windowWidget(windows.context().window);
        if (widget && !widget->grab().save(parser.value("screenshot"))) app.exit(2);
    });
    if (parser.isSet("smoke")) QTimer::singleShot(1300, &app, [&] {
        const auto valid = windows.model().validate(); app.exit(valid.ok && !windows.model().windows().isEmpty() ? 0 : 3);
    });
    return app.exec();
}
