// SPDX-License-Identifier: MIT
#include "strip_layout_state.hpp"
#include "strip_layout.hpp"
#include <QJsonArray>
#include <QSet>
#include <utility>

namespace window_system {

int minimumStripSavedWidth(bool optionsEnabled, bool locked)
{
    return (optionsEnabled ? stripOptionsWidth : 0) + (locked ? 0 : stripHandleWidth) + 12;
}

QJsonObject encodeStripLayout(const StripLayoutSnapshot& snapshot)
{
    Q_ASSERT(snapshot.rows.size() == snapshot.areas.size());
    QJsonArray rows, bottomRows;
    QJsonObject strips;
    for (int row = 0; row < snapshot.rows.size(); ++row) {
        QJsonArray entries;
        for (const auto& id : snapshot.rows[row]) {
            entries.append(id);
            const auto found = snapshot.strips.constFind(id);
            Q_ASSERT(found != snapshot.strips.cend());
            const auto& value = *found;
            QJsonObject fields{{"visible", value.visible}, {"width", value.width},
                {"excluded", QJsonArray::fromStringList(value.excluded)}, {"contentVisible", value.contentVisible}};
            if (value.elastic) fields.insert("sizing", *value.elastic ? "elastic" : "content");
            if (value.naturalWidthPriority) fields.insert("naturalWidthPriority", true);
            strips.insert(id, fields);
        }
        if (snapshot.areas[row] == ToolbarArea::Top) rows.append(entries);
        else bottomRows.append(entries);
    }
    while (!rows.isEmpty() && rows.last().toArray().isEmpty()) rows.removeLast();
    while (!bottomRows.isEmpty() && bottomRows.last().toArray().isEmpty()) bottomRows.removeLast();
    QJsonObject result{{"locked", snapshot.locked}, {"rows", rows}, {"strips", strips}};
    if (!bottomRows.isEmpty()) result.insert("bottomRows", bottomRows);
    return result;
}

namespace {

void retainNewStrips(StripLayoutSnapshot& result, const QVector<QStringList>& currentRows,
                     const QVector<ToolbarArea>& currentAreas, const QSet<QString>& seen)
{
    int localRows[2] = {};
    for (int row = 0; row < currentRows.size(); ++row) {
        const auto area = currentAreas[row];
        const int local = localRows[int(area)]++;
        QStringList missing;
        for (const auto& id : currentRows[row]) if (!seen.contains(id)) missing.append(id);
        if (missing.isEmpty()) continue;
        int target = -1;
        int ordinal = 0;
        for (int candidate = 0; candidate < result.areas.size(); ++candidate)
            if (result.areas[candidate] == area && ordinal++ == local) { target = candidate; break; }
        while (target < 0) {
            result.rows.append(QStringList{});
            result.areas.append(area);
            if (ordinal++ == local) target = int(result.rows.size()) - 1;
        }
        result.rows[target].append(missing);
    }
}

} // namespace

std::optional<StripLayoutSnapshot> decodeStripLayout(
    const QJsonObject& json, const QVector<QStringList>& currentRows,
    const QVector<ToolbarArea>& currentAreas, const QHash<QString, StripRestorePolicy>& policies)
{
    Q_ASSERT(currentRows.size() == currentAreas.size());
    if (!json.value("rows").isArray() || (json.contains("bottomRows") && !json.value("bottomRows").isArray())) return std::nullopt;
    StripLayoutSnapshot result;
    QSet<QString> seen;
    for (const auto area : {ToolbarArea::Top, ToolbarArea::Bottom}) {
        const auto rows = json.value(area == ToolbarArea::Top ? "rows" : "bottomRows").toArray();
        for (const auto& row : rows) {
            if (!row.isArray()) return std::nullopt;
            QStringList entries;
            for (const auto& entry : row.toArray()) {
                if (!entry.isString() || entry.toString().isEmpty()) return std::nullopt;
                const auto id = entry.toString();
                if (!policies.contains(id)) continue;
                if (seen.contains(id)) return std::nullopt;
                entries.append(id); seen.insert(id);
            }
            result.rows.append(entries); result.areas.append(area);
        }
    }
    for (auto it = policies.cbegin(); it != policies.cend(); ++it)
        if (it->requiredInLayout && !seen.contains(it.key())) return std::nullopt;
    retainNewStrips(result, currentRows, currentAreas, seen);
    for (const auto& row : result.rows) {
        int requiredCount = 0;
        for (const auto& id : row) requiredCount += policies.value(id).elasticRequired ? 1 : 0;
        if (requiredCount > 1) return std::nullopt;
    }
    result.locked = json.value("locked").toBool(true);
    const auto settings = json.value("strips").toObject();
    for (auto it = policies.cbegin(); it != policies.cend(); ++it) {
        if (!settings.contains(it.key())) continue;
        const auto fields = settings.value(it.key()).toObject();
        StripLayoutPreferences value;
        value.width = fields.value("width").toInt(-1);
        if (value.width != -1 && value.width < minimumStripSavedWidth(it->optionsEnabled, result.locked)) return std::nullopt;
        if (fields.contains("sizing")) {
            if (fields.value("sizing") != "elastic" && fields.value("sizing") != "content") return std::nullopt;
            value.elastic = fields.value("sizing") == "elastic";
        }
        if (fields.contains("naturalWidthPriority") && !fields.value("naturalWidthPriority").isBool()) return std::nullopt;
        value.visible = fields.value("visible").toBool(true);
        value.naturalWidthPriority = fields.value("naturalWidthPriority").toBool(false);
        value.contentVisible = fields.value("contentVisible").toBool(true);
        for (const auto& excluded : fields.value("excluded").toArray()) if (excluded.isString()) value.excluded.append(excluded.toString());
        result.strips.insert(it.key(), std::move(value));
    }
    return result;
}

} // namespace window_system
