// SPDX-License-Identifier: MIT
#include "strip_layout_state.hpp"
#include "strip_layout.hpp"
#include <QJsonArray>
#include <QSet>
#include <utility>

namespace window_system {

int minimumStripSavedWidth(bool optionsEnabled, bool locked)
{
    return (optionsEnabled ? stripOptionsWidth : 0) + (locked ? 0 : stripHandleWidth) + stripDefaultMinimumContentWidth;
}

int stripRowIndex(std::span<const ToolbarArea> areas, ToolbarArea area, int row)
{
    if (row < 0) return -1;
    for (int index = 0; index < int(areas.size()); ++index)
        if (areas[index] == area && row-- == 0) return index;
    return -1;
}

int ensureStripRow(QVector<QStringList>& rows, QVector<ToolbarArea>& areas, ToolbarArea area, int row)
{
    Q_ASSERT(rows.size() == areas.size());
    if (row < 0) return -1;
    int ordinal = 0;
    for (int index = 0; index < areas.size(); ++index)
        if (areas[index] == area && ordinal++ == row) return index;
    int result = -1;
    while (ordinal <= row) {
        result = int(rows.size());
        rows.append(QStringList{}); areas.append(area);
        ++ordinal;
    }
    return result;
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
        const int target = ensureStripRow(result.rows, result.areas, area, local);
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

namespace {

qsizetype newStripInsertion(const QJsonArray& merged, const QJsonArray& current, qsizetype index)
{
    for (qsizetype before = index; before-- > 0;)
        for (qsizetype position = 0; position < merged.size(); ++position)
            if (merged[position] == current[before]) return position + 1;
    for (qsizetype after = index + 1; after < current.size(); ++after)
        for (qsizetype position = 0; position < merged.size(); ++position)
            if (merged[position] == current[after]) return position;
    return merged.size();
}

QJsonArray mergeStripRow(const QJsonArray& previous, const QJsonArray& current,
                        const QSet<QString>& present, const QSet<QString>& cachedIds)
{
    QJsonArray currentExisting;
    for (const auto& entry : current)
        if (cachedIds.contains(entry.toString())) currentExisting.append(entry);
    QJsonArray merged;
    qsizetype next = 0;
    // Reuse shared slots while keeping dormant provider strips in place.
    for (const auto& entry : previous) {
        if (!present.contains(entry.toString())) merged.append(entry);
        else if (next < currentExisting.size()) merged.append(currentExisting[next++]);
    }
    while (next < currentExisting.size()) merged.append(currentExisting[next++]);
    // New presentations are inserted beside their renderer neighbours after
    // merging. They cannot consume a shared strip's original slot.
    for (qsizetype index = 0; index < current.size(); ++index) {
        const auto entry = current[index];
        if (!cachedIds.contains(entry.toString())) merged.insert(newStripInsertion(merged, current, index), entry);
    }
    return merged;
}

} // namespace

QJsonObject mergeRenderedStripLayout(const QJsonObject& cached, const QJsonObject& rendered)
{
    if (cached.isEmpty()) return rendered;
    QSet<QString> present;
    QSet<QString> cachedIds;
    for (const auto* field : {"rows", "bottomRows"})
        for (const auto& row : cached.value(field).toArray())
            for (const auto& entry : row.toArray()) cachedIds.insert(entry.toString());
    auto settings = cached.value("strips").toObject();
    const auto renderedSettings = rendered.value("strips").toObject();
    for (auto entry = renderedSettings.begin(); entry != renderedSettings.end(); ++entry) {
        present.insert(entry.key()); settings.insert(entry.key(), entry.value());
    }
    auto result = cached;
    for (const auto* field : {"rows", "bottomRows"}) {
        const auto cachedRows = cached.value(field).toArray();
        const auto renderedRows = rendered.value(field).toArray();
        QJsonArray rows;
        for (qsizetype row = 0; row < qMax(cachedRows.size(), renderedRows.size()); ++row) {
            const auto current = row < renderedRows.size() ? renderedRows.at(row).toArray() : QJsonArray{};
            const auto previous = row < cachedRows.size() ? cachedRows.at(row).toArray() : QJsonArray{};
            const auto merged = mergeStripRow(previous, current, present, cachedIds);
            if (!merged.isEmpty()) rows.append(merged);
        }
        if (QString::fromLatin1(field) == "rows") result.insert(field, rows);
        else if (rows.isEmpty()) result.remove(field);
        else result.insert(field, rows);
    }
    result.insert("locked", rendered.value("locked"));
    result.insert("strips", settings);
    return result;
}

QJsonObject mergeToolbarEdit(QJsonObject latest, const QJsonObject& before, const QJsonObject& after)
{
    if (latest.isEmpty()) return after;
    // A row transfer changes both areas as one operation. Unrelated edits in
    // an older live window must retain the newer profile's ordering.
    if (before.value("rows") != after.value("rows") || before.value("bottomRows") != after.value("bottomRows")) {
        for (const auto* field : {"rows", "bottomRows"}) {
            if (after.contains(field)) latest.insert(field, after.value(field));
            else latest.remove(field);
        }
    }
    if (before.value("locked") != after.value("locked")) latest.insert("locked", after.value("locked"));
    auto settings = latest.value("strips").toObject();
    const auto oldSettings = before.value("strips").toObject();
    const auto newSettings = after.value("strips").toObject();
    for (auto strip = newSettings.begin(); strip != newSettings.end(); ++strip) {
        const auto oldFields = oldSettings.value(strip.key()).toObject();
        const auto newFields = strip->toObject();
        auto merged = settings.value(strip.key()).toObject();
        QSet<QString> fields;
        for (auto field = oldFields.begin(); field != oldFields.end(); ++field) fields.insert(field.key());
        for (auto field = newFields.begin(); field != newFields.end(); ++field) fields.insert(field.key());
        for (const auto& field : fields) if (oldFields.value(field) != newFields.value(field)) {
            if (field == "excluded") {
                auto excluded = merged.value(field).toArray();
                const auto oldExcluded = oldFields.value(field).toArray(), newExcluded = newFields.value(field).toArray();
                for (const auto& id : oldExcluded) if (!newExcluded.contains(id))
                    for (qsizetype index = excluded.size(); index-- > 0;) if (excluded[index] == id) excluded.removeAt(index);
                for (const auto& id : newExcluded) if (!oldExcluded.contains(id) && !excluded.contains(id)) excluded.append(id);
                merged.insert(field, excluded); continue;
            }
            if (newFields.contains(field)) merged.insert(field, newFields.value(field));
            else merged.remove(field);
        }
        if (!merged.isEmpty()) settings.insert(strip.key(), merged);
    }
    latest.insert("strips", settings);
    return latest;
}

} // namespace window_system
