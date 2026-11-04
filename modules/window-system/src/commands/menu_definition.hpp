// SPDX-License-Identifier: MIT
#pragma once

#include <QStringList>
#include <QVector>
#include <utility>

namespace window_system {

enum class CommandMenuSurface { Application, FloatingApplication, Tab, Pane, Window, Group, TabFolder };

// A stable menu tree. It describes command placement, not presentation objects.
struct CommandMenuItem {
    QString id;
    QString title;
    QVector<CommandMenuItem> children;
    int order = 0;
    bool command = false;
    QString pathKey;

    static CommandMenuItem action(QString id, int order = 0) { return {std::move(id), {}, {}, order, true}; }
};

[[nodiscard]] const QVector<CommandMenuItem>& commandMenuDefinition(CommandMenuSurface surface);
[[nodiscard]] QStringList declaredMenuCommands(CommandMenuSurface surface);
[[nodiscard]] QString applicationMenuExclusion(const QString& commandId);
[[nodiscard]] QString floatingMenuExclusion(const QString& commandId);

} // namespace window_system
