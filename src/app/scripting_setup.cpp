// SPDX-License-Identifier: MIT
#include <QCoreApplication>
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

constexpr auto runningStatusId = "lua.running";
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
                   : lua_runtime::ScriptResult{false, {}, QCoreApplication::translate("Muxora", "Application services were closed")};
    });
    connect(services_, &AppScriptServices::eventPublished, host_,
            [this](const QString &name, const QVariant &payload) { host_->publishEvent(name, payload); });
    connect(host_, &window_system_lua::ScriptHost::scriptError, this, &AppScriptingSetup::report);
    connect(host_, &window_system_lua::ScriptHost::logMessage, this,
            [](const QString &message) { qInfo().noquote() << "Lua:" << message; });
    connect(host_->runtime(), &lua_runtime::ScriptRuntime::longRunningChanged, this,
            [this](bool running) {
                if (!manager_ || shuttingDown_)
                    return;
                manager_->setStatusItemText(QString::fromLatin1(runningStatusId),
                    running ? QCoreApplication::translate("Muxora", "Lua script running") : QString(),
                    QCoreApplication::translate("Muxora", "Click to stop running Lua scripts"));
                manager_->commandsChanged();
            });
    connect(host_->runtime(), &lua_runtime::ScriptRuntime::runningChanged, this, [this] {
        if (manager_ && !shuttingDown_)
            manager_->commandsChanged();
    });
    connect(host_->runtime(), &lua_runtime::ScriptRuntime::resetBlocked, this, [this] {
        report(QCoreApplication::translate("Muxora", "The script is still running. A stop has been requested."));
    });
    connect(host_, &window_system_lua::ScriptHost::initialized, this, [this](bool ok, const QString &error) {
        if (shuttingDown_ || !manager_)
            return;
        preferences_.captureDefaultBindings(manager_->commands());
        if (!preferences_.loadKeybindings(manager_->commands()))
            report(preferences_.diagnostic());
        loading_ = false;
        const auto diagnostic = initializationError_.isEmpty() ? error : initializationError_;
        if (!diagnostic.isEmpty())
            report(QCoreApplication::translate("Muxora", "Failed to load Lua configuration: %1").arg(diagnostic));
        manager_->commandsChanged();
        emit initialized(ok && initializationError_.isEmpty(), diagnostic);
    });
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(300);
    connect(saveTimer_, &QTimer::timeout, this, [this] { flushKeybindings(); });
    if (manager_) {
        manager_->registerStatusItem({QString::fromLatin1(runningStatusId), {},
            QCoreApplication::translate("Muxora", "Click to stop running Lua scripts"),
            QStringLiteral("lua.stop")});
        const QPointer<AppScriptingSetup> self(this);
        const auto available = [self](const window_system::CommandContext &) {
            return self && !self->shuttingDown_;
        };
        manager_->commands().add({QStringLiteral("lua.run"),
                                  QCoreApplication::translate("Muxora", "Run Lua Script…"),
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
            {QStringLiteral("lua.stop"),
             QCoreApplication::translate("Muxora", "Stop Running Lua Scripts"),
             QStringLiteral("工具"),
             {},
             [self](const window_system::CommandContext &) {
                 return self && !self->shuttingDown_ && self->host_->runtime()->isRunning();
             },
             [self](const window_system::CommandContext &) {
                 if (!self || self->shuttingDown_)
                     return false;
                 self->host_->runtime()->cancelAll();
                 return true;
             },
             {QStringLiteral("工具"), QStringLiteral("Lua")},
             15});
        manager_->commands().add(
            {QStringLiteral("lua.reload"),
             QCoreApplication::translate("Muxora", "Reload Lua Configuration"),
             QStringLiteral("工具"),
             {},
             available,
             [self](const window_system::CommandContext &) { return self && self->reload(); },
             {QStringLiteral("工具"), QStringLiteral("Lua")},
             20});
        manager_->commands().add(
            {QStringLiteral("lua.openConfig"),
             QCoreApplication::translate("Muxora", "Open Lua Configuration"),
             QStringLiteral("工具"),
             {},
             available,
             [self](const window_system::CommandContext &) {
                 if (!self || !self->createConfigurationFile())
                     return false;
                 if (QDesktopServices::openUrl(QUrl::fromLocalFile(self->configurationFilePath())))
                     return true;
                 self->report(QCoreApplication::translate("Muxora", "Could not open the Lua configuration file."));
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
    if (!manager_ || shuttingDown_)
        return false;
    if (loading_) {
        if (host_->runtime()->isRunning()) {
            host_->runtime()->cancelAll();
            report(QCoreApplication::translate("Muxora", "The script is still running. A stop has been requested."));
        }
        return false;
    }
    if (started_ && !flushKeybindings())
        return false;
    saveTimer_->stop();
    loading_ = true;
    if (!preferences_.restoreDefaultBindings(manager_->commands())) {
        loading_ = false;
        report(QCoreApplication::translate("Muxora", "Could not restore the default keybindings before loading scripts."));
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
    const auto maximumScriptBytes = host_->runtime()->limits().maximumSourceBytes;
    const auto bytes = file.read(qint64(maximumScriptBytes) + 1);
    if (bytes.size() > maximumScriptBytes) {
        error = QCoreApplication::translate("Muxora", "Lua script exceeds the %1 MiB transport limit.")
                    .arg(maximumScriptBytes / (1024 * 1024));
        return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    source = decoder(bytes);
    if (decoder.hasError()) {
        source.clear();
        error = QCoreApplication::translate("Muxora", "Lua scripts must use UTF-8 encoding.");
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
            report(QCoreApplication::translate("Muxora", "Provide either code or file when running a script, not both."));
            return false;
        }
        const auto value = args.value(args.contains(QStringLiteral("code")) ? QStringLiteral("code")
                                                                            : QStringLiteral("file"));
        if (value.metaType().id() != QMetaType::QString) {
            report(QCoreApplication::translate("Muxora", "Lua script parameters must be strings."));
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
    input->setWindowTitle(QCoreApplication::translate("Muxora", "Run Lua Script"));
    input->setLabelText(QCoreApplication::translate("Muxora", "Enter Lua script:"));
    input->setOption(QInputDialog::UsePlainTextEditForTextInput);
    input->resize(640, 360);
    input->setOkButtonText(QCoreApplication::translate("Muxora", "Run"));
    input->setCancelButtonText(QCoreApplication::translate("Muxora", "Cancel"));
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
        report(QCoreApplication::translate("Muxora", "Could not create the Lua configuration directory."));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        report(file.errorString());
        return false;
    }
    const QByteArray source = QCoreApplication::translate("Muxora",
        "-- User/init.lua runs after the workspace has been restored.\n"
        "-- Put pure Lua modules in User/lua, then use require('module_name').\n"
        "-- Reload from Tools > Lua after saving this file.\n\n"
        "muxora.keymap.set('ctrl+alt+l', function(context)\n"
        "  muxora.ui.notify({ title = 'Muxora', text = 'Lua is ready.', timeout_ms = 3000 })\n"
        "end, { id = 'user.hello', title = 'Lua: Hello' })\n").toUtf8();
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
    if (manager_)
        manager_->removeStatusItem(QString::fromLatin1(runningStatusId));
    host_->unload();
}
} // namespace muxora
