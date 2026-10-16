// SPDX-License-Identifier: MIT
#pragma once
#include <lua_runtime/script_runtime.hpp>
#include <QObject>
#include <memory>

namespace window_system {
class WindowManager;
}
namespace muxora {

// Application effects stay outside the reusable Lua/window modules. External
// applications are detached; Muxora never terminates them during reload/exit.
class AppScriptServices final : public QObject {
    Q_OBJECT
  public:
    explicit AppScriptServices(window_system::WindowManager *manager, QObject *parent = nullptr);
    ~AppScriptServices() override;
    lua_runtime::ScriptResult invoke(QString method, QVariantList args, QVariantMap context);
    void reset();
    [[nodiscard]] int notificationCount() const;
    [[nodiscard]] int pendingLaunchCount() const;
  signals:
    void launchCompleted(QString request, qint64 pid, QString error);
    void eventPublished(QString name, QVariant payload);
    void launchRequested(quint64 request, quint64 generation, QString program, QStringList arguments,
                         QString directory);

  private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace muxora
