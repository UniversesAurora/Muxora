// SPDX-License-Identifier: MIT
#pragma once

#include "commands/panel_rows.hpp"
#include <QJsonDocument>

namespace window_system {

struct ShortcutEdit {
    QString command;
    int binding = -1;
    CommandBinding original;
    QKeySequence sequence;
    QString arguments;
};

struct ShortcutEditCheck {
    bool selected = false;
    bool canApply = false;
    bool canRemove = false;
    QVariantMap arguments;
    QString message;
};

// Both settings pages use the same search, argument checks, conflict and
// prefix messages. Checking an edit never changes or executes a command.
[[nodiscard]] QVector<PanelRow> shortcutCommandRows(const CommandRegistry& registry, const QString& query);
[[nodiscard]] QString shortcutArgumentText(const QVariantMap& arguments, QJsonDocument::JsonFormat format);
[[nodiscard]] ShortcutEditCheck checkShortcutEdit(const CommandRegistry& registry, const ShortcutEdit& edit);

} // namespace window_system
