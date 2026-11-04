// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/toolbar_layout.hpp>
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <optional>

namespace window_system {

// Widths in the stored format are authored lengths, before interface scaling.
struct StripLayoutPreferences {
    bool visible = true;
    int width = -1;
    std::optional<bool> elastic;
    QStringList excluded;
    bool contentVisible = true;
    bool naturalWidthPriority = false;
};

struct StripLayoutSnapshot {
    QVector<QStringList> rows;
    QVector<ToolbarArea> areas;
    QHash<QString, StripLayoutPreferences> strips;
    bool locked = true;
};

struct StripRestorePolicy {
    bool optionsEnabled = true;
    bool elasticRequired = false;
    bool requiredInLayout = false;
};

[[nodiscard]] int minimumStripSavedWidth(bool optionsEnabled, bool locked);
[[nodiscard]] QJsonObject encodeStripLayout(const StripLayoutSnapshot& snapshot);
// Only explicitly saved strip preferences are returned. Omitted settings keep
// their live values, including widths produced by an in-progress layout edit.
[[nodiscard]] std::optional<StripLayoutSnapshot> decodeStripLayout(
    const QJsonObject& json, const QVector<QStringList>& currentRows,
    const QVector<ToolbarArea>& currentAreas, const QHash<QString, StripRestorePolicy>& policies);

} // namespace window_system
