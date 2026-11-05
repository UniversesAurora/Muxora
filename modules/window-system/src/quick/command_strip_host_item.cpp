// SPDX-License-Identifier: MIT
#include "command_strip_host_item.hpp"
#include "command_strip_item.hpp"
#include "chrome/strip_layout.hpp"
#include <window_system/ui_scale.hpp>
#include <QScopedValueRollback>
#include <QSet>
#include <QVarLengthArray>
#include <QtMath>
#include <algorithm>

namespace window_system::quick {

CommandStripHostItem::CommandStripHostItem(QQuickItem* parent) : WindowDragAreaItem(parent), bottom_(new WindowDragAreaItem(parent))
{
    bottom_->setParent(this);
    setObjectName(QStringLiteral("CommandToolbars"));
    bottom_->setObjectName(QStringLiteral("BottomCommandToolbars"));
    auto* bottom = static_cast<WindowDragAreaItem*>(bottom_);
    bottom->maximizeRequested = [this] { if (const auto callback = maximizeRequested) callback(); };
    bottom->carriesTabs = [this] { return carriesTabs && carriesTabs(); };
    bottom->carryRequested = [this](QPoint point) { if (const auto callback = carryRequested) callback(point); };
    bottom->menuRequested = [this](QPoint point) { if (const auto callback = menuRequested) callback(point); };
}

CommandStripHostItem::~CommandStripHostItem()
{
    // The bottom view is visually a sibling, but belongs to this registry.
    delete bottom_;
}

void CommandStripHostItem::watchStrip(CommandStripItem* strip)
{
    connect(strip, &QQuickItem::implicitWidthChanged, this, &CommandStripHostItem::requestLayout);
    connect(strip, &QQuickItem::implicitHeightChanged, this, &CommandStripHostItem::requestLayout);
}

void CommandStripHostItem::setContent(QString id, QQuickItem* content, StripConstraints constraints, StripSizing sizing)
{
    for (auto& existing : contents_) if (existing.item == content) {
        if (existing.id != id) {
            auto view = strips_.take(existing.id); strips_.insert(id, view); view->setProperty("stripId", id);
            existing.id = std::move(id);
        }
        return;
    }
    auto* strip = new CommandStripItem(this, false);
    strip->setProperty("stripId", id);
    // Menus keep their existing extension anchor; no second options tail.
    strip->setContent(content, constraints);
    strips_.insert(id, strip); contents_.append({std::move(id), content, constraints, sizing});
    watchStrip(strip);
}

void CommandStripHostItem::requestLayout()
{
    if (layoutPending_) return;
    layoutPending_ = true;
    QMetaObject::invokeMethod(this, [this] { layoutPending_ = false; relayout(); }, Qt::QueuedConnection);
}

void CommandStripHostItem::sync(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved, const Theme& theme, bool hideOptionsWhenLocked)
{
    restoreLayout(descriptions, saved);
    syncViews(descriptions, theme, hideOptionsWhenLocked);
    relayout();
}

StripConstraints CommandStripHostItem::constraintsFor(const QString& id) const
{
    for (const auto& content : contents_) if (content.id == id) return content.constraints;
    return {};
}

void CommandStripHostItem::restoreLayout(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved)
{
    StripLayoutSnapshot defaults;
    QHash<QString, StripRestorePolicy> policies;
    if (!contents_.isEmpty()) {
        const int row = ensureStripRow(defaults.rows, defaults.areas, ToolbarArea::Top, 0);
        for (const auto& content : contents_) {
            defaults.rows[row].append(content.id);
            defaults.strips[content.id].elastic = content.sizing == StripSizing::Elastic;
            policies.insert(content.id, {false, content.constraints.elasticRequired, content.id == QStringLiteral("caption-controls")});
        }
    }
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
    normalizeLayout(defaults.strips);
}

void CommandStripHostItem::normalizeLayout(const QHash<QString, StripLayoutPreferences>& defaults)
{
    for (const auto& row : layout_.rows) {
        QVarLengthArray<StripPolicyInput, 8> rowPolicies;
        for (const auto& id : row) {
            auto preference = layout_.strips.find(id);
            if (preference == layout_.strips.end()) preference = layout_.strips.insert(id, defaults.value(id));
            // Menus customize individual roots through excluded; the generic
            // content switch is only for strips without individual items.
            if (id == QStringLiteral("menu") || id == QStringLiteral("floating-menu")) preference->contentVisible = true;
            const auto constraints = constraintsFor(id);
            rowPolicies.append({preference->elastic.value_or(false), constraints.elasticAllowed, constraints.elasticRequired,
                preference->naturalWidthPriority, preference->width >= 0});
        }
        const std::span<const StripPolicyInput> inputs{rowPolicies.constData(), size_t(rowPolicies.size())};
        const int elastic = stripElasticOwner(inputs), priority = stripWidthPriorityOwner(inputs);
        for (int index = 0; index < row.size(); ++index) {
            const auto& id = row.at(index);
            auto& preference = layout_.strips[id];
            preference.elastic = index == elastic;
            preference.naturalWidthPriority = index == priority;
        }
    }
}

void CommandStripHostItem::syncViews(const QVector<CommandToolbarDescription>& descriptions, const Theme& theme, bool hideOptionsWhenLocked)
{
    QSet<QString> live;
    for (const auto& content : contents_) {
        live.insert(content.id);
        auto* strip = strips_.value(content.id).data();
        const auto preferences = layout_.strips.value(content.id);
        strip->setLocked(layout_.locked);
        strip->syncContent(theme, preferences, layout_.locked && hideOptionsWhenLocked);
        strip->setVisible(preferences.visible);
    }
    for (const auto& description : descriptions) {
        const auto& id = description.toolbar.id;
        live.insert(id);
        auto& strip = strips_[id];
        if (!strip) {
            strip = new CommandStripItem(this);
            strip->setProperty("stripId", id);
            strip->triggered = [this](const QString& command) { if (const auto callback = triggered) callback(command); };
            watchStrip(strip);
        }
        const auto preferences = layout_.strips.value(id);
        strip->setLocked(layout_.locked);
        strip->sync(description.actions, theme, preferences, layout_.locked && hideOptionsWhenLocked);
        strip->setVisible(preferences.visible);
    }
    for (auto it = strips_.begin(); it != strips_.end();) {
        if (live.contains(it.key())) { ++it; continue; }
        delete it.value().data(); it = strips_.erase(it);
    }
}

void CommandStripHostItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) relayout();
}

void CommandStripHostItem::relayout()
{
    if (arranging_) return;
    const QScopedValueRollback guard(arranging_, true);
    QVarLengthArray<StripAreaRowInput, 8> inputs;
    minimumWidth_ = 0;
    for (int row = 0; row < layout_.rows.size(); ++row) {
        int height = 0, count = 0, minimum = 0;
        for (const auto& id : layout_.rows.at(row)) {
            const auto preference = layout_.strips.value(id);
            if (!preference.visible) continue;
            const auto* strip = strips_.value(id).data();
            height = std::max(height, qCeil(strip->implicitHeight())); ++count;
            minimum += strip->minimumWidth();
        }
        minimumWidth_ = std::max(minimumWidth_, minimum + std::max(0, count - 1) * uiPixels(4));
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
            const bool fixed = constraintsFor(id).fixedWidth;
            widths.append({strip->minimumWidth(), preference.width < 0 || fixed ? qCeil(strip->implicitWidth()) : uiPixels(preference.width),
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
