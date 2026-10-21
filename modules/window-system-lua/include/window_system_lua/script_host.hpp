// SPDX-License-Identifier: MIT
#pragma once
#include <lua_runtime/script_runtime.hpp>
#include <window_system/command_registry.hpp>
#include <QObject>
#include <functional>
#include <memory>

namespace window_system {
class WindowManager;
}
namespace window_system_lua {

// Owns one script generation and its registrations. Only stable IDs and value
// descriptions cross the worker boundary; Qt objects stay on the GUI thread.
class ScriptHost final : public QObject {
    Q_OBJECT
  public:
    using ServiceHandler = lua_runtime::ScriptRuntime::HostHandler;
    explicit ScriptHost(window_system::WindowManager *manager, QObject *parent = nullptr);
    ~ScriptHost() override;
    void setServiceHandler(ServiceHandler handler);
    // Component-owned methods, e.g. ssh.connect/sftp.list, are removed with
    // their provider. Built-in names cannot be replaced by an extension.
    bool registerService(QString method, ServiceHandler handler, QObject *owner = nullptr);
    bool removeService(const QString &method);
    // On the next initialize(), prepend directory/?.lua and directory/?/init.lua
    // to the standard package.path. Module loading and caching follow Lua.
    void setModuleDirectory(QString directory);
    void initialize(QString source = {}, QString name = QStringLiteral("init.lua"));
    void unload();
    quint64 run(QString source, QString name = {}, window_system::CommandContext context = {});
    bool invokeCommand(QString command, const window_system::CommandContext &context);
    void publishEvent(QString name, QVariant payload = {}, QVariantMap context = {});
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QStringList affectedCommands() const;
    [[nodiscard]] QVariantMap resourceCounts() const;
    [[nodiscard]] lua_runtime::ScriptRuntime *runtime() const;
  signals:
    void initialized(bool ok, QString error);
    void scriptError(QString message);
    void logMessage(QString message);

  private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace window_system_lua
