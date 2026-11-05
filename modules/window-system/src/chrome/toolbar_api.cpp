// SPDX-License-Identifier: MIT
#include "command_strip.hpp"

#include <QJsonArray>

namespace window_system {

std::optional<ToolbarState> CommandStripHost::toolbarState(const QString& id) const
{
    if (!stripWidget(id)) return {};
    const auto layout = saveLayout();
    const auto fields = layout.value(QStringLiteral("strips")).toObject().value(id).toObject();
    ToolbarState result;
    result.area = stripArea(id);
    const auto rows = toolbarRows(result.area);
    for (int row = 0; row < rows.size(); ++row)
        if (const int index = int(rows.at(row).indexOf(id)); index >= 0) { result.row = row; result.index = index; }
    result.width = fields.value(QStringLiteral("width")).toInt(-1);
    result.visible = isStripVisible(id);
    result.elastic = stripSizing(id) == StripSizing::Elastic;
    result.locked = isLocked();
    for (const auto& value : fields.value(QStringLiteral("excluded")).toArray()) result.hiddenItems.append(value.toString());
    return result;
}

QVector<QStringList> CommandStripHost::toolbarRows(ToolbarArea area) const
{
    QVector<QStringList> result;
    for (const auto& row : saveLayout().value(area == ToolbarArea::Top ? "rows" : "bottomRows").toArray()) {
        QStringList ids;
        for (const auto& entry : row.toArray()) ids.append(entry.toString());
        result.append(ids);
    }
    return result;
}

} // namespace window_system
