// SPDX-License-Identifier: MIT
#include "tab_appearance_p.hpp"
#include "../window_manager_p.hpp"

#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace window_system {
namespace {

QJsonObject colorsToJson(const TabVisualColors& colors)
{
    return {{"background", colors.background.name(QColor::HexArgb)}, {"text", colors.text.name(QColor::HexArgb)},
        {"line", colors.line.name(QColor::HexArgb)}, {"icon", colors.icon.name(QColor::HexArgb)}};
}

bool parseColor(const QJsonValue& value, QColor& color)
{
    if (!value.isString()) return false;
    const auto text = value.toString();
    if ((text.size() != 7 && text.size() != 9) || !text.startsWith(QLatin1Char('#'))) return false;
    for (qsizetype index = 1; index < text.size(); ++index) {
        const auto digit = text.at(index).toLower();
        if (!(digit >= QLatin1Char('0') && digit <= QLatin1Char('9'))
            && !(digit >= QLatin1Char('a') && digit <= QLatin1Char('f'))) return false;
    }
    color = QColor(text);
    return color.isValid();
}

bool parseColors(const QJsonValue& value, TabVisualColors& colors)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    return object.size() == 4 && parseColor(object.value("background"), colors.background)
        && parseColor(object.value("text"), colors.text) && parseColor(object.value("line"), colors.line)
        && parseColor(object.value("icon"), colors.icon);
}

bool validColors(const TabVisualColors& colors)
{
    return colors.background.isValid() && colors.text.isValid() && colors.line.isValid() && colors.icon.isValid();
}

QString placementName(TabBarPlacement placement)
{
    return placement == TabBarPlacement::Top ? QStringLiteral("top") : QStringLiteral("bottom");
}

bool parsePlacement(const QJsonValue& value, TabBarPlacement& placement)
{
    if (value.isUndefined()) return true;
    if (value == "top") placement = TabBarPlacement::Top;
    else if (value == "bottom") placement = TabBarPlacement::Bottom;
    else return false;
    return true;
}

} // namespace

QVector<TabColorPreset> defaultTabColorPresets()
{
    struct Seed { const char* id; const char* name; const char* accent; const char* soft; };
    constexpr Seed seeds[] = {
        {"gray", "灰色", "#5f6368", "#edf0f2"}, {"blue", "蓝色", "#1967d2", "#e8f0fe"},
        {"red", "红色", "#c5221f", "#fce8e6"}, {"yellow", "金色", "#8b6500", "#fff4ce"},
        {"green", "绿色", "#137333", "#e6f4ea"}, {"pink", "粉色", "#b3267d", "#fce7f3"},
        {"purple", "紫色", "#7b2abf", "#f1e7fc"}, {"cyan", "青色", "#007b83", "#e0f4f5"},
        {"orange", "橙色", "#b05a00", "#fff0df"}};
    QVector<TabColorPreset> colors;
    colors.reserve(std::size(seeds));
    for (const auto& seed : seeds) {
        const QColor accent(seed.accent), soft(seed.soft);
        colors.append({QString::fromLatin1(seed.id), QString::fromUtf8(seed.name),
            {soft, QColor("#202124"), accent, accent},
            {QColor("#f7f7f8"), QColor("#3c4043"), accent, QColor("#5f6368")},
            {accent, QColor(Qt::white), accent, QColor(Qt::white)},
            {QColor("#6A568C"), QColor("#ffffff"), accent, QColor(Qt::white)}});
    }
    return colors;
}

bool validTabAppearance(const TabAppearance& appearance)
{
    const auto validPlacement = [](TabBarPlacement value) { return value == TabBarPlacement::Top || value == TabBarPlacement::Bottom; };
    if (!validPlacement(appearance.centralPlacement) || !validPlacement(appearance.pinnedPlacement)
        || (appearance.wheelExpandRows && !appearance.multiline) || appearance.colors.size() > 128) return false;
    QSet<QString> ids;
    for (const auto& preset : appearance.colors) {
        if (preset.id.isEmpty() || preset.id.size() > 128 || preset.name.isEmpty() || preset.name.size() > 256
            || ids.contains(preset.id) || !validColors(preset.groupActive) || !validColors(preset.groupInactive)
            || !validColors(preset.autoHideActive) || !validColors(preset.autoHideInactive)) return false;
        ids.insert(preset.id);
    }
    return true;
}

QJsonObject tabAppearanceToJson(const TabAppearance& appearance)
{
    QJsonArray colors;
    for (const auto& preset : appearance.colors) colors.append(QJsonObject{
        {"id", preset.id}, {"name", preset.name}, {"groupActive", colorsToJson(preset.groupActive)},
        {"groupInactive", colorsToJson(preset.groupInactive)}, {"autoHideActive", colorsToJson(preset.autoHideActive)},
        {"autoHideInactive", colorsToJson(preset.autoHideInactive)}});
    return {{"centralPlacement", placementName(appearance.centralPlacement)},
        {"pinnedPlacement", placementName(appearance.pinnedPlacement)}, {"multiline", appearance.multiline},
        {"wheelExpandRows", appearance.wheelExpandRows}, {"showIcons", appearance.showIcons},
        {"showNewTabButton", appearance.showNewTabButton}, {"showNumbers", appearance.showNumbers}, {"colors", colors}};
}

OperationResult parseTabAppearance(const QJsonValue& value, TabAppearance& appearance)
{
    TabAppearance candidate;
    if (value.isUndefined()) { appearance = candidate; return {true, {}}; }
    if (!value.isObject()) return {false, QStringLiteral("标签外观设置必须是对象")};
    const auto object = value.toObject();
    if (!parsePlacement(object.value("centralPlacement"), candidate.centralPlacement)
        || !parsePlacement(object.value("pinnedPlacement"), candidate.pinnedPlacement))
        return {false, QStringLiteral("标签栏仅支持顶部或底部")};
    for (const auto& entry : {std::pair{"multiline", &candidate.multiline}, std::pair{"wheelExpandRows", &candidate.wheelExpandRows},
        std::pair{"showIcons", &candidate.showIcons}, std::pair{"showNewTabButton", &candidate.showNewTabButton},
        std::pair{"showNumbers", &candidate.showNumbers}}) {
        const auto field = object.value(entry.first);
        if (!field.isUndefined() && !field.isBool()) return {false, QStringLiteral("标签显示设置类型无效")};
        if (field.isBool()) *entry.second = field.toBool();
    }
    const auto palette = object.value("colors");
    if (!palette.isUndefined()) {
        if (!palette.isArray() || palette.toArray().size() > 128) return {false, QStringLiteral("标签颜色列表无效")};
        candidate.colors.clear();
        for (const auto& value : palette.toArray()) {
            if (!value.isObject()) return {false, QStringLiteral("标签颜色定义无效")};
            const auto definition = value.toObject();
            TabColorPreset preset;
            if (definition.size() != 6 || !definition.value("id").isString() || !definition.value("name").isString()
                || !parseColors(definition.value("groupActive"), preset.groupActive)
                || !parseColors(definition.value("groupInactive"), preset.groupInactive)
                || !parseColors(definition.value("autoHideActive"), preset.autoHideActive)
                || !parseColors(definition.value("autoHideInactive"), preset.autoHideInactive))
                return {false, QStringLiteral("标签颜色须包含 ID、名称和四种完整状态，颜色使用 #RRGGBB 或 #AARRGGBB")};
            preset.id = definition.value("id").toString(); preset.name = definition.value("name").toString();
            candidate.colors.append(std::move(preset));
        }
    }
    if (!validTabAppearance(candidate)) return {false, QStringLiteral("标签外观包含无效颜色、重复 ID 或不适用的滚轮设置")};
    appearance = std::move(candidate);
    return {true, {}};
}

TabAppearance WindowManager::tabAppearance() const { return d->appearance; }

bool WindowManager::setTabAppearance(const TabAppearance& appearance)
{
    if (!validTabAppearance(appearance)) return false;
    if (d->appearance == appearance) return true;
    d->appearance = appearance;
    refresh();
    return true;
}

std::optional<TabColorPreset> WindowManager::tabColorPreset(const QString& id) const
{
    if (id.isEmpty()) return {};
    const auto it = std::find_if(d->appearance.colors.cbegin(), d->appearance.colors.cend(),
        [&id](const TabColorPreset& preset) { return preset.id == id; });
    return it == d->appearance.colors.cend() ? std::optional<TabColorPreset>{} : *it;
}

} // namespace window_system
