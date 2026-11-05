// SPDX-License-Identifier: MIT
#include "command_toolbars_item.hpp"
#include "command_toolbar_item.hpp"
#include "chrome/strip_layout.hpp"
#include <window_system/ui_scale.hpp>
#include <QScopedValueRollback>
#include <QSet>
#include <QVarLengthArray>
#include <QtMath>
#include <algorithm>

namespace window_system::quick {

CommandToolbarsItem::CommandToolbarsItem(QQuickItem* parent) : QQuickItem(parent), bottom_(new QQuickItem(parent))
{
    bottom_->setParent(this);
    setObjectName(QStringLiteral("CommandToolbars"));
    bottom_->setObjectName(QStringLiteral("BottomCommandToolbars"));
}

CommandToolbarsItem::~CommandToolbarsItem()
{
    // The bottom view is visually a sibling, but belongs to this registry.
    delete bottom_;
}

void CommandToolbarsItem::sync(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved, const Theme& theme, bool hideOptionsWhenLocked)
{
    StripLayoutSnapshot defaults;
    QHash<QString, StripRestorePolicy> policies;
    for (const auto& description : descriptions) {
        const auto& definition = description.toolbar;
        const int row = ensureStripRow(defaults.rows, defaults.areas, definition.area, definition.row);
        defaults.rows[row].append(definition.id);
        policies.insert(definition.id, {});
    }
    if (saved.isEmpty()) layout_ = std::move(defaults);
    else {
        const auto restored = decodeStripLayout(saved, defaults.rows, defaults.areas, policies);
        Q_ASSERT(restored);
        layout_ = *restored;
    }
    for (const auto& row : layout_.rows) {
        QVarLengthArray<StripPolicyInput, 8> rowPolicies;
        for (const auto& id : row) {
            const auto preference = layout_.strips.value(id);
            rowPolicies.append({preference.elastic.value_or(false), true, false,
                preference.naturalWidthPriority, preference.width >= 0});
        }
        const std::span<const StripPolicyInput> inputs{rowPolicies.constData(), size_t(rowPolicies.size())};
        const int elastic = stripElasticOwner(inputs), priority = stripWidthPriorityOwner(inputs);
        for (int index = 0; index < row.size(); ++index) {
            auto& preference = layout_.strips[row.at(index)];
            preference.elastic = index == elastic;
            preference.naturalWidthPriority = index == priority;
        }
    }
    QSet<QString> live;
    for (const auto& description : descriptions) {
        const auto& id = description.toolbar.id;
        live.insert(id);
        auto& strip = strips_[id];
        if (!strip) {
            strip = new CommandToolbarItem(this);
            strip->setProperty("stripId", id);
            strip->triggered = [this](const QString& command) { if (const auto callback = triggered) callback(command); };
        }
        const auto preferences = layout_.strips.value(id);
        strip->sync(description.actions, theme, preferences.excluded, layout_.locked && hideOptionsWhenLocked);
        strip->setVisible(preferences.visible);
    }
    for (auto it = strips_.begin(); it != strips_.end();) {
        if (live.contains(it.key())) { ++it; continue; }
        delete it.value().data(); it = strips_.erase(it);
    }
    relayout();
}

void CommandToolbarsItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) relayout();
}

void CommandToolbarsItem::relayout()
{
    if (arranging_) return;
    const QScopedValueRollback guard(arranging_, true);
    QVarLengthArray<StripAreaRowInput, 8> inputs;
    minimumWidth_ = 0;
    for (int row = 0; row < layout_.rows.size(); ++row) {
        int height = 0, count = 0;
        for (const auto& id : layout_.rows.at(row)) {
            const auto preference = layout_.strips.value(id);
            if (!preference.visible) continue;
            height = std::max(height, qCeil(strips_.value(id)->implicitHeight())); ++count;
        }
        minimumWidth_ = std::max(minimumWidth_, count * uiPixels(24) + std::max(0, count - 1) * uiPixels(4));
        inputs.append({layout_.areas.at(row), height, count > 0});
    }
    auto areas = layoutStripAreas({inputs.constData(), size_t(inputs.size())});
    setImplicitHeight(areas.heights[0]);
    bottom_->setImplicitHeight(areas.heights[1]);
    bottom_->setSize({width(), qreal(areas.heights[1])});
    for (int row = 0; row < layout_.rows.size(); ++row) {
        if (!inputs.at(row).occupied) continue;
        auto* parent = layout_.areas.at(row) == ToolbarArea::Top ? this : bottom_;
        const int height = inputs.at(row).height, y = areas.takeRow(layout_.areas.at(row), height);
        QStringList visible;
        QVarLengthArray<StripWidthInput, 8> widths;
        for (const auto& id : layout_.rows.at(row)) {
            const auto preference = layout_.strips.value(id);
            if (!preference.visible) continue;
            const auto* strip = strips_.value(id).data();
            visible.append(id);
            widths.append({uiPixels(24), preference.width < 0 ? qCeil(strip->implicitWidth()) : uiPixels(preference.width),
                preference.elastic.value_or(false), preference.naturalWidthPriority});
        }
        const auto bounds = layoutStripRow({widths.constData(), size_t(widths.size())}, qRound(width()), 0, uiPixels(4));
        for (int index = 0; index < visible.size(); ++index) {
            auto* strip = strips_.value(visible.at(index)).data();
            strip->setParentItem(parent);
            strip->setPosition({qreal(bounds.positions.at(index)), qreal(y)});
            strip->setSize({qreal(bounds.widths.at(index)), qreal(height)});
        }
    }
}

} // namespace window_system::quick
