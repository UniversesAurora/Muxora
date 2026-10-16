// SPDX-License-Identifier: MIT
#pragma once
#include <window_system/command_registry.hpp>
#include <QObject>
#include <QPointer>

class QInputDialog;
class QTimer;
namespace window_system {
class WindowManager;
}
namespace window_system_lua {
class ScriptHost;
}
namespace muxora {
class UserPreferences;
class AppScriptServices;

class AppScriptingSetup final : public QObject {
    Q_OBJECT
  public:
    AppScriptingSetup(window_system::WindowManager *manager, UserPreferences &preferences,
                      QObject *parent = nullptr);
    ~AppScriptingSetup() override;
    // Call after application commands and the initial/restored workspace exist.
    void start();
    bool reload();
    bool flushKeybindings();
    bool createConfigurationFile();
    [[nodiscard]] QString configurationFilePath() const;
    [[nodiscard]] window_system_lua::ScriptHost *host() const { return host_; }
    [[nodiscard]] AppScriptServices *services() const { return services_; }
  signals:
    void initialized(bool ok, QString error);
    void diagnostic(QString message);

  private:
    bool runCommand(const window_system::CommandContext &context);
    bool readScript(QString path, QString &source, QString &error) const;
    void report(const QString &message);
    void stageBindings();
    void shutdown();
    QPointer<window_system::WindowManager> manager_;
    UserPreferences &preferences_;
    window_system_lua::ScriptHost *host_;
    AppScriptServices *services_;
    QTimer *saveTimer_;
    QPointer<QInputDialog> input_;
    QString initializationError_;
    bool started_ = false;
    bool loading_ = false;
    bool shuttingDown_ = false;
};
} // namespace muxora
