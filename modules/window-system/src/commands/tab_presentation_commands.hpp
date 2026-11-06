// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <optional>
class QMenu;
namespace window_system {
class WindowManager;
struct CommandContext;
void registerTabPresentationCommands(WindowManager* manager);
// A tab color preset chosen from a menu at the cursor; `reset` names the
// empty choice. Nothing when dismissed or when the manager went away.
std::optional<QString> pickTabColor(WindowManager* manager, const QString& reset);
// The same choices for presentations that show them in their own menu: each
// sets the color argument.
QVector<QPair<QStringList, QVariantMap>> tabColorChoices(const WindowManager* manager, const QString& reset);
void appendTabPresentationActions(QMenu* menu, WindowManager* manager, const CommandContext& captured,
                                  bool autoHide = false);
}
