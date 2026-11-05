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

void CommandStripHostItem::setContent(QString id, QString title, QQuickItem* content, StripConstraints constraints,
                                      StripSizing sizing, std::function<QStringList()> items)
{
    for (auto& existing : contents_) if (existing.item == content) {
        if (existing.id != id) {
            auto view = strips_.take(existing.id); strips_.insert(id, view); view->setProperty("stripId", id);
            titles_.remove(existing.id);
            existing.id = id;
        }
        titles_.insert(std::move(id), std::move(title));
        return;
    }
    titles_.insert(id, std::move(title));
    auto* strip = new CommandStripItem(this, false);
    strip->setProperty("stripId", id);
    // Menus keep their existing extension anchor; no second options tail.
    strip->setContent(content, constraints);
    strips_.insert(id, strip); contents_.append({std::move(id), content, constraints, sizing, std::move(items)});
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
    for (const auto& description : descriptions) titles_.insert(description.toolbar.id, description.toolbar.title);
    restoreLayout(descriptions, saved);
    syncViews(descriptions, theme, hideOptionsWhenLocked);
    relayout();
}

const CommandStripHostItem::Content* CommandStripHostItem::content(const QString& id) const
{
    for (const auto& content : contents_) if (content.id == id) return &content;
    return nullptr;
}

StripConstraints CommandStripHostItem::constraintsFor(const QString& id) const
{
    const auto* found = content(id);
    return found ? found->constraints : StripConstraints{};
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
    // A sync after this window's own edit, or one that changes nothing else,
    // keeps the layout as it is.
    if (saved == applied_ && defaults.rows == defaultRows_ && defaults.areas == defaultAreas_) return;
    applied_ = saved; defaultRows_ = defaults.rows; defaultAreas_ = defaults.areas;
    if (saved.isEmpty()) layout_ = defaults;
    else {
        // The layout file's validation rejects what decoding would.
        const auto restored = decodeStripLayout(saved, defaults.rows, defaults.areas, policies);
        Q_ASSERT(restored);
        layout_ = *restored;
    }
    normalizeLayout(defaults.strips);
    committed_ = currentLayout();
}

void CommandStripHostItem::normalizeLayout(const QHash<QString, StripLayoutPreferences>& defaults)
{
    for (const auto& row : layout_.rows) {
        QVarLengthArray<StripPolicyInput, 8> rowPolicies;
        for (const auto& id : row) {
            auto preference = layout_.strips.find(id);
            if (preference == layout_.strips.end()) preference = layout_.strips.insert(id, defaults.value(id));
            // Content with items hides them one by one; the switch for the
            // whole content is only for content without items.
            if (const auto* found = content(id); found && found->items) preference->contentVisible = true;
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
            strip->triggered = [this](const QString& command, const QVariantMap& arguments) {
                if (const auto callback = triggered) callback(command, arguments);
            };
            strip->options = [this, id, view = QPointer<CommandStripItem>(strip)] {
                return view ? describeToolbarOptions(*this, id, view->buttons()) : QVector<MenuItemDescription>{};
            };
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

QJsonObject CommandStripHostItem::currentLayout() const
{
    return mergeRenderedStripLayout(applied_, encodeStripLayout(layout_));
}

void CommandStripHostItem::commit()
{
    for (auto it = strips_.cbegin(); it != strips_.cend(); ++it) if (it.value()) {
        it.value()->setLocked(layout_.locked);
        it.value()->setVisible(layout_.strips.value(it.key()).visible);
    }
    relayout();
    auto after = currentLayout();
    if (after == committed_) return;
    const auto before = std::exchange(committed_, after);
    applied_ = after;
    if (const auto callback = committed) callback(before, after);
}

QStringList CommandStripHostItem::toolbarIds() const
{
    QStringList result;
    for (const auto& row : layout_.rows) result += row;
    return result;
}

int CommandStripHostItem::rowOf(const QString& id) const
{
    for (int row = 0; row < layout_.rows.size(); ++row) if (layout_.rows.at(row).contains(id)) return row;
    return -1;
}

std::optional<ToolbarState> CommandStripHostItem::toolbarState(const QString& id) const
{
    const int row = rowOf(id);
    if (row < 0) return {};
    ToolbarState result;
    result.area = layout_.areas.at(row);
    for (int previous = 0; previous < row; ++previous) result.row += layout_.areas.at(previous) == result.area ? 1 : 0;
    result.index = int(layout_.rows.at(row).indexOf(id));
    const auto preference = layout_.strips.value(id);
    result.width = preference.width;
    result.visible = preference.visible;
    result.elastic = preference.elastic.value_or(false);
    result.locked = layout_.locked;
    result.hiddenItems = preference.excluded;
    return result;
}

QVector<QStringList> CommandStripHostItem::toolbarRows(ToolbarArea area) const
{
    QVector<QStringList> result;
    for (int row = 0; row < layout_.rows.size(); ++row) if (layout_.areas.at(row) == area) result.append(layout_.rows.at(row));
    return result;
}

QString CommandStripHostItem::requiredElasticInRow(int row, const QString& except) const
{
    for (const auto& id : layout_.rows.at(row)) if (id != except && constraintsFor(id).elasticRequired) return id;
    return {};
}

bool CommandStripHostItem::isToolbarSizingEditable(const QString& id) const
{
    const auto constraints = constraintsFor(id);
    const int row = rowOf(id);
    return row >= 0 && constraints.elasticAllowed && !constraints.elasticRequired && requiredElasticInRow(row, id).isEmpty();
}

void CommandStripHostItem::clearWidthPriority(int row)
{
    for (const auto& id : layout_.rows.at(row)) layout_.strips[id].naturalWidthPriority = false;
}

// As the Widgets host: one owner of a row's spare space, the preferred
// strip when the row's constraints allow it.
void CommandStripHostItem::normalizeElastic(int row, const QString& preferred)
{
    const auto& entries = layout_.rows.at(row);
    QVarLengthArray<StripPolicyInput, 8> policies;
    for (const auto& id : entries) {
        const auto constraints = constraintsFor(id);
        const auto& preference = layout_.strips[id];
        policies.append({preference.elastic.value_or(false), constraints.elasticAllowed, constraints.elasticRequired,
            preference.naturalWidthPriority, preference.width >= 0});
    }
    const int owner = stripElasticOwner({policies.constData(), size_t(policies.size())}, int(entries.indexOf(preferred)));
    for (int index = 0; index < entries.size(); ++index) layout_.strips[entries.at(index)].elastic = index == owner;
}

// The neighbour on the anchored side takes the vacated width, measured in
// the current scale and stored as an authored width.
void CommandStripHostItem::removeFromRows(const QString& id)
{
    for (auto& row : layout_.rows) {
        const int index = int(row.indexOf(id));
        if (index < 0) continue;
        QVarLengthArray<StripSlotInput, 8> slotInputs;
        for (const auto& entry : row) {
            const auto* strip = strips_.value(entry).data();
            const auto& preference = layout_.strips[entry];
            slotInputs.append({strip ? qRound(strip->width()) : 0, preference.visible, preference.elastic.value_or(false),
                constraintsFor(entry).fixedWidth});
        }
        if (const auto adjustment = stripVacancyAdjustment({slotInputs.constData(), size_t(slotInputs.size())}, index, uiPixels(stripSpacing))) {
            const auto& neighbour = row.at(adjustment->index);
            // Rounding to the stored length must not fall below what a saved
            // layout can restore.
            layout_.strips[neighbour].width = adjustment->requestedWidth < 0 ? -1
                : std::max(qRound(adjustment->requestedWidth / uiPixelsF(1)), minimumSavedWidth(neighbour));
        }
        row.removeAt(index);
    }
}

bool CommandStripHostItem::setToolbarVisible(const QString& id, bool visible)
{
    if (rowOf(id) < 0) return false;
    layout_.strips[id].visible = visible;
    commit();
    return true;
}

int CommandStripHostItem::minimumSavedWidth(const QString& id) const
{
    return (content(id) ? 0 : stripOptionsWidth) + (layout_.locked ? 0 : stripHandleWidth) + constraintsFor(id).minimumContentWidth;
}

bool CommandStripHostItem::setToolbarWidth(const QString& id, int width)
{
    const int row = rowOf(id);
    if (row < 0 || constraintsFor(id).fixedWidth || width < minimumSavedWidth(id)) return false;
    clearWidthPriority(row);
    layout_.strips[id].width = width;
    commit();
    return true;
}

bool CommandStripHostItem::resetToolbarWidth(const QString& id)
{
    const int row = rowOf(id);
    if (row < 0) return false;
    auto& preference = layout_.strips[id];
    preference.width = -1;
    preference.naturalWidthPriority = false;
    // A strip reset to its natural width keeps it when the row is short.
    if (preference.visible && !preference.elastic.value_or(false)) {
        clearWidthPriority(row);
        layout_.strips[id].naturalWidthPriority = true;
    }
    commit();
    return true;
}

bool CommandStripHostItem::setToolbarElastic(const QString& id, bool elastic)
{
    const int row = rowOf(id);
    const auto constraints = constraintsFor(id);
    if (row < 0 || (!elastic && constraints.elasticRequired) || (elastic && !constraints.elasticAllowed)
        || (elastic && !requiredElasticInRow(row, id).isEmpty())) return false;
    clearWidthPriority(row);
    layout_.strips[id].elastic = elastic;
    if (elastic) normalizeElastic(row, id);
    commit();
    return true;
}

bool CommandStripHostItem::setToolbarItemVisible(const QString& id, const QString& item, bool visible)
{
    if (rowOf(id) < 0) return false;
    auto& preference = layout_.strips[id];
    const auto* found = content(id);
    if (found && !found->items) {
        if (item != QStringLiteral("content")) return false;
        preference.contentVisible = visible;
    } else {
        const auto items = found ? found->items() : strips_.value(id)->itemIds();
        if (!items.contains(item)) return false;
        preference.excluded.removeAll(item);
        if (!visible) preference.excluded.append(item);
    }
    commit();
    return true;
}

bool CommandStripHostItem::moveToolbar(const QString& id, ToolbarArea area, int row, int index)
{
    if (rowOf(id) < 0) return false;
    const int existing = stripRowIndex({layout_.areas.constData(), size_t(layout_.areas.size())}, area, row);
    if (existing >= 0 && constraintsFor(id).elasticRequired && !requiredElasticInRow(existing, id).isEmpty()) return false;
    const int target = ensureStripRow(layout_.rows, layout_.areas, area, row);
    // An unchanged placement must not run the vacancy rule a second time.
    const int current = int(layout_.rows.at(target).indexOf(id));
    if (current >= 0 && current == qBound(0, index, int(layout_.rows.at(target).size()) - 1)) return true;
    for (int source = 0; source < layout_.rows.size(); ++source)
        if (source == target || layout_.rows.at(source).contains(id)) clearWidthPriority(source);
    removeFromRows(id);
    auto& entries = layout_.rows[target];
    entries.insert(qBound(0, index, int(entries.size())), id);
    normalizeElastic(target, layout_.strips.value(id).elastic.value_or(false) ? id : QString{});
    commit();
    return true;
}

void CommandStripHostItem::setToolbarsLocked(bool locked)
{
    if (layout_.locked == locked) return;
    layout_.locked = locked;
    commit();
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
        minimumWidth_ = std::max(minimumWidth_, minimum + std::max(0, count - 1) * uiPixels(stripSpacing));
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
        const auto bounds = layoutStripRow({widths.constData(), size_t(widths.size())}, qRound(width()), 0, uiPixels(stripSpacing));
        for (int index = 0; index < visible.size(); ++index) {
            auto* strip = strips_.value(visible.at(index)).data();
            strip->setParentItem(parent);
            strip->setPosition({qreal(bounds.positions.at(index)), qreal(y)});
            strip->setSize({qreal(bounds.widths.at(index)), qreal(height)});
        }
    }
}

} // namespace window_system::quick
