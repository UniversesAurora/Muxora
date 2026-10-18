// SPDX-License-Identifier: MIT
#include "scripting_setup.hpp"
#include "script_services.hpp"
#include "user_preferences.hpp"
#include <window_system/window_manager.hpp>
#include <window_system_lua/script_host.hpp>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>

namespace muxora {
namespace {

constexpr qint64 maximumScriptBytes = 1024 * 1024;
}

AppScriptingSetup::AppScriptingSetup(window_system::WindowManager *manager, UserPreferences &preferences,
                                     QObject *parent)
    : QObject(parent), manager_(manager), preferences_(preferences),
      host_(new window_system_lua::ScriptHost(manager, this)),
      services_(new AppScriptServices(manager, this)), saveTimer_(new QTimer(this)) {
    host_->setModuleDirectory(QDir(preferences_.directory()).filePath(QStringLiteral("lua")));
    const QPointer<AppScriptServices> services(services_);
    host_->setServiceHandler([services](QString method, QVariantList args, QVariantMap context) {
        return services
                   ? services->invoke(std::move(method), std::move(args), std::move(context))
                   : lua_runtime::ScriptResult{false, {}, QStringLiteral("Application services were closed")};
    });
    connect(services_, &AppScriptServices::eventPublished, host_,
            [this](const QString &name, const QVariant &payload) { host_->publishEvent(name, payload); });
    connect(host_, &window_system_lua::ScriptHost::scriptError, this, &AppScriptingSetup::report);
    connect(host_, &window_system_lua::ScriptHost::logMessage, this,
            [](const QString &message) { qInfo().noquote() << "Lua:" << message; });
    connect(host_, &window_system_lua::ScriptHost::initialized, this, [this](bool ok, const QString &error) {
        if (shuttingDown_ || !manager_)
            return;
        preferences_.captureDefaultBindings(manager_->commands());
        if (!preferences_.loadKeybindings(manager_->commands()))
            report(preferences_.diagnostic());
        loading_ = false;
        const auto diagnostic = initializationError_.isEmpty() ? error : initializationError_;
        if (!diagnostic.isEmpty())
            report(QCoreApplication::translate("Muxora", "Lua 配置加载失败：%1").arg(diagnostic));
        manager_->commandsChanged();
        emit initialized(ok && initializationError_.isEmpty(), diagnostic);
    });
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(300);
    connect(saveTimer_, &QTimer::timeout, this, [this] { flushKeybindings(); });
    if (manager_) {
        const QPointer<AppScriptingSetup> self(this);
        const auto available = [self](const window_system::CommandContext &) {
            return self && !self->shuttingDown_;
        };
        manager_->commands().add({QStringLiteral("lua.run"),
                                  QCoreApplication::translate("Muxora", "运行 Lua 脚本…"),
                                  QStringLiteral("工具"),
                                  {},
                                  [self](const window_system::CommandContext &) {
                                      return self && self->host_->ready() && !self->loading_ &&
                                             !self->shuttingDown_;
                                  },
                                  [self](const window_system::CommandContext &context) {
                                      return self && self->runCommand(context);
                                  },
                                  {QStringLiteral("工具"), QStringLiteral("Lua")},
                                  10});
        manager_->commands().add(
            {QStringLiteral("lua.reload"),
             QCoreApplication::translate("Muxora", "重新加载 Lua 配置"),
             QStringLiteral("工具"),
             {},
             available,
             [self](const window_system::CommandContext &) { return self && self->reload(); },
             {QStringLiteral("工具"), QStringLiteral("Lua")},
             20});
        manager_->commands().add(
            {QStringLiteral("lua.openConfig"),
             QCoreApplication::translate("Muxora", "打开 Lua 配置文件"),
             QStringLiteral("工具"),
             {},
             available,
             [self](const window_system::CommandContext &) {
                 if (!self || !self->createConfigurationFile())
                     return false;
                 if (QDesktopServices::openUrl(QUrl::fromLocalFile(self->configurationFilePath())))
                     return true;
                 self->report(QCoreApplication::translate("Muxora", "无法打开 Lua 配置文件。"));
                 return false;
             },
             {QStringLiteral("工具"), QStringLiteral("Lua")},
             30});
        connect(manager_, &window_system::WindowManager::commandsChanged, this,
                &AppScriptingSetup::stageBindings);
    }
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AppScriptingSetup::shutdown);
}

AppScriptingSetup::~AppScriptingSetup() {
    shutdown();
    host_->setServiceHandler({});
    delete host_;
    host_ = nullptr;
}

void AppScriptingSetup::start() {
    if (started_)
        return;
    started_ = true;
    reload();
}

bool AppScriptingSetup::reload() {
    if (!manager_ || loading_ || shuttingDown_)
        return false;
    if (started_ && !flushKeybindings())
        return false;
    saveTimer_->stop();
    loading_ = true;
    if (!preferences_.restoreDefaultBindings(manager_->commands())) {
        loading_ = false;
        report(QCoreApplication::translate("Muxora", "无法恢复脚本加载前的默认快捷键。"));
        return false;
    }
    services_->reset();
    initializationError_.clear();
    QString source;
    const auto path = configurationFilePath();
    if (QFileInfo::exists(path))
        readScript(path, source, initializationError_);
    host_->initialize(source, QStringLiteral("@") + path);
    return true;
}

QString AppScriptingSetup::configurationFilePath() const {
    return QDir(preferences_.directory()).filePath(QStringLiteral("init.lua"));
}

bool AppScriptingSetup::readScript(QString path, QString &source, QString &error) const {
    if (QDir::isRelativePath(path))
        path = QDir(preferences_.directory()).filePath(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const auto bytes = file.read(maximumScriptBytes + 1);
    if (bytes.size() > maximumScriptBytes) {
        error = QCoreApplication::translate("Muxora", "Lua 脚本超过 1 MiB 限制。");
        return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    source = decoder(bytes);
    if (decoder.hasError()) {
        source.clear();
        error = QCoreApplication::translate("Muxora", "Lua 脚本必须使用 UTF-8 编码。");
        return false;
    }
    return true;
}

bool AppScriptingSetup::runCommand(const window_system::CommandContext &context) {
    if (!manager_ || !host_->ready())
        return false;
    const auto &args = context.args;
    if (args.contains(QStringLiteral("code")) || args.contains(QStringLiteral("file"))) {
        if (args.contains(QStringLiteral("code")) && args.contains(QStringLiteral("file"))) {
            report(QCoreApplication::translate("Muxora", "运行脚本时只能提供 code 或 file 其中一个。"));
            return false;
        }
        const auto value = args.value(args.contains(QStringLiteral("code")) ? QStringLiteral("code")
                                                                            : QStringLiteral("file"));
        if (value.metaType().id() != QMetaType::QString) {
            report(QCoreApplication::translate("Muxora", "Lua 脚本参数必须是字符串。"));
            return false;
        }
        QString source = value.toString();
        QString name = QStringLiteral("@user-command");
        if (args.contains(QStringLiteral("file"))) {
            QString error;
            if (!readScript(value.toString(), source, error)) {
                report(error);
                return false;
            }
            name = QStringLiteral("@") + value.toString();
        }
        return host_->run(source, name, context) != 0;
    }
    if (input_) {
        input_->raise();
        input_->activateWindow();
        return true;
    }
    auto *input = new QInputDialog(manager_->windowWidget(context.window));
    input_ = input;
    input->setObjectName(QStringLiteral("LuaScriptInput"));
    input->setAttribute(Qt::WA_DeleteOnClose);
    input->setWindowTitle(QCoreApplication::translate("Muxora", "运行 Lua 脚本"));
    input->setLabelText(QCoreApplication::translate("Muxora", "输入 Lua 脚本："));
    input->setOption(QInputDialog::UsePlainTextEditForTextInput);
    input->resize(640, 360);
    input->setOkButtonText(QCoreApplication::translate("Muxora", "运行"));
    input->setCancelButtonText(QCoreApplication::translate("Muxora", "取消"));
    window_system::applyTheme(input, manager_->theme());
    connect(input, &QInputDialog::textValueSelected, this, [this, context](const QString &source) {
        if (host_ && !shuttingDown_)
            host_->run(source, QStringLiteral("@interactive"), context);
    });
    input->open();
    return true;
}

void AppScriptingSetup::stageBindings() {
    if (!started_ || loading_ || shuttingDown_ || !manager_)
        return;
    if (!preferences_.stageKeybindings(manager_->commands()))
        return;
    if (preferences_.hasPendingKeybindingChanges())
        saveTimer_->start();
    else
        saveTimer_->stop();
}

bool AppScriptingSetup::flushKeybindings() {
    saveTimer_->stop();
    if (!manager_ || loading_ || !preferences_.hasPendingKeybindingChanges())
        return true;
    if (preferences_.saveKeybindings(manager_->commands()))
        return true;
    report(preferences_.diagnostic());
    return false;
}

bool AppScriptingSetup::createConfigurationFile() {
    const auto path = configurationFilePath();
    if (QFileInfo::exists(path))
        return true;
    if (!QDir().mkpath(preferences_.directory())) {
        report(QCoreApplication::translate("Muxora", "无法创建 Lua 配置目录。"));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        report(file.errorString());
        return false;
    }
    const QByteArray source =
        "-- User/init.lua runs after the workspace has been restored.\n"
        "-- Put pure Lua modules in User/lua, then use require('module_name').\n"
        "-- Reload from Tools > Lua after saving this file.\n\n"
        "muxora.keymap.set('ctrl+alt+l', function(context)\n"
        "  muxora.ui.notify({ title = 'Muxora', text = 'Lua is ready.', timeout_ms = 3000 })\n"
        "end, { id = 'user.hello', title = 'Lua: Hello' })\n";
    if (file.write(source) == source.size())
        return true;
    report(file.errorString());
    return false;
}

void AppScriptingSetup::report(const QString &message) {
    if (message.isEmpty() || shuttingDown_)
        return;
    qWarning().noquote() << message;
    if (manager_)
        manager_->setStatus(manager_->context().window, message);
    emit diagnostic(message);
}

void AppScriptingSetup::shutdown() {
    if (shuttingDown_)
        return;
    if (started_ && !loading_)
        flushKeybindings();
    shuttingDown_ = true;
    saveTimer_->stop();
    if (input_)
        delete input_.data();
    services_->reset();
    host_->unload();
}
} // namespace muxora
