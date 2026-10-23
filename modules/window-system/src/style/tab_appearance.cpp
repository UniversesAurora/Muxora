// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "tab_appearance_p.hpp"
#include "../window_manager_p.hpp"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>

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

void upgradeUnmodifiedBuiltinPalette(QVector<TabColorPreset>& colors)
{
    // Layouts include the entire palette. Upgrade only byte-for-byte original
    // built-ins so existing layouts receive the corrected states, while an
    // edited channel or a host-defined preset remains authoritative.
    const auto defaults = defaultTabColorPresets();
    for (auto& preset : colors) {
        const auto current = std::find_if(defaults.cbegin(), defaults.cend(), [&preset](const auto& value) {
            return value.id == preset.id && value.name == preset.name;
        });
        if (current == defaults.cend()) continue;
        const auto accent = current->groupActive.line;
        const TabColorPreset legacy{current->id, current->name,
            {current->groupActive.background, QColor("#202124"), accent, accent},
            {QColor("#f7f7f8"), QColor("#3c4043"), accent, QColor("#5f6368")},
            {accent, QColor(Qt::white), accent, QColor(Qt::white)},
            {QColor("#6A568C"), QColor(Qt::white), accent, QColor(Qt::white)}};
        if (preset == legacy) preset = *current;
    }
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
    struct Seed { const char* id; const char* name; const char* accent; const char* soft; const char* vivid; };
    constexpr Seed seeds[] = {
        {"gray", "灰色", "#5f6368", "#edf0f2", "#a9afb5"}, {"blue", "蓝色", "#1967d2", "#e8f0fe", "#78a4eb"},
        {"red", "红色", "#c5221f", "#fce8e6", "#eb817e"}, {"yellow", "金色", "#8b6500", "#fff4ce", "#dfbc45"},
        {"green", "绿色", "#137333", "#e6f4ea", "#78be91"}, {"pink", "粉色", "#b3267d", "#fce7f3", "#dc8abd"},
        {"purple", "紫色", "#7b2abf", "#f1e7fc", "#b18ade"}, {"cyan", "青色", "#007b83", "#e0f4f5", "#63bcc4"},
        {"orange", "橙色", "#b05a00", "#fff0df", "#e6a65f"}};
    QVector<TabColorPreset> colors;
    colors.reserve(std::size(seeds));
    for (const auto& seed : seeds) {
        const QColor accent(seed.accent), soft(seed.soft), vivid(seed.vivid);
        // The persisted palette is an 8-bit #AARRGGBB contract. QColor's HSV
        // transforms retain 16-bit channels, which would otherwise change on
        // every save/restore comparison even though the displayed RGB matches.
        const TabVisualColors active{soft, QColor("#202124"), accent, QColor(accent.darker(110).name(QColor::HexArgb))};
        const TabVisualColors inactive{vivid, QColor("#202124"), accent, QColor(accent.darker(130).name(QColor::HexArgb))};
        colors.append({QString::fromLatin1(seed.id), QString::fromUtf8(seed.name),
            active, inactive, active, inactive});
    }
    return colors;
}

QString tabColorDisplayName(const TabColorPreset& preset)
{
    struct Name { const char* id; const char* source; };
    static constexpr Name names[] = {
        {"gray", QT_TRANSLATE_NOOP("WindowSystem", "灰色")},
        {"blue", QT_TRANSLATE_NOOP("WindowSystem", "蓝色")},
        {"red", QT_TRANSLATE_NOOP("WindowSystem", "红色")},
        {"yellow", QT_TRANSLATE_NOOP("WindowSystem", "金色")},
        {"green", QT_TRANSLATE_NOOP("WindowSystem", "绿色")},
        {"pink", QT_TRANSLATE_NOOP("WindowSystem", "粉色")},
        {"purple", QT_TRANSLATE_NOOP("WindowSystem", "紫色")},
        {"cyan", QT_TRANSLATE_NOOP("WindowSystem", "青色")},
        {"orange", QT_TRANSLATE_NOOP("WindowSystem", "橙色")}};
    for (const auto& name : names) {
        if (preset.id == QLatin1String(name.id) && preset.name == QString::fromUtf8(name.source))
            return QCoreApplication::translate("WindowSystem", name.source);
    }
    return preset.name;
}

bool validTabAppearance(const TabAppearance& appearance)
{
    const auto validPlacement = [](TabBarPlacement value) { return value == TabBarPlacement::Top || value == TabBarPlacement::Bottom; };
    if (!validPlacement(appearance.centralPlacement) || !validPlacement(appearance.pinnedPlacement)
        || appearance.maximumTitleWidth < minimumTabTitleWidth || appearance.maximumTitleWidth > maximumTabTitleWidth
        || appearance.maximumSidebarTitleWidth < minimumTabTitleWidth || appearance.maximumSidebarTitleWidth > maximumTabTitleWidth
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
        {"showNewTabButton", appearance.showNewTabButton}, {"showNumbers", appearance.showNumbers},
        {"showSidebarIcons", appearance.showSidebarIcons}, {"showSidebarNumbers", appearance.showSidebarNumbers},
        {"clipTitles", appearance.clipTitles}, {"maximumTitleWidth", appearance.maximumTitleWidth},
        {"clipSidebarTitles", appearance.clipSidebarTitles}, {"maximumSidebarTitleWidth", appearance.maximumSidebarTitleWidth},
        {"colors", colors}};
}

OperationResult parseTabAppearance(const QJsonValue& value, TabAppearance& appearance)
{
    TabAppearance candidate;
    if (value.isUndefined()) { appearance = candidate; return {true, {}}; }
    if (!value.isObject()) return {false, QCoreApplication::translate("WindowSystem", "标签外观设置必须是对象")};
    const auto object = value.toObject();
    if (!parsePlacement(object.value("centralPlacement"), candidate.centralPlacement)
        || !parsePlacement(object.value("pinnedPlacement"), candidate.pinnedPlacement))
        return {false, QCoreApplication::translate("WindowSystem", "标签栏仅支持顶部或底部")};
    for (const auto& entry : {std::pair{"multiline", &candidate.multiline}, std::pair{"wheelExpandRows", &candidate.wheelExpandRows},
        std::pair{"showIcons", &candidate.showIcons}, std::pair{"showNewTabButton", &candidate.showNewTabButton},
        std::pair{"showNumbers", &candidate.showNumbers}, std::pair{"showSidebarIcons", &candidate.showSidebarIcons},
        std::pair{"showSidebarNumbers", &candidate.showSidebarNumbers}, std::pair{"clipTitles", &candidate.clipTitles},
        std::pair{"clipSidebarTitles", &candidate.clipSidebarTitles}}) {
        const auto field = object.value(entry.first);
        if (!field.isUndefined() && !field.isBool()) return {false, QCoreApplication::translate("WindowSystem", "标签显示设置类型无效")};
        if (field.isBool()) *entry.second = field.toBool();
    }
    for (const auto& entry : {std::pair{"maximumTitleWidth", &candidate.maximumTitleWidth},
        std::pair{"maximumSidebarTitleWidth", &candidate.maximumSidebarTitleWidth}}) {
        const auto titleWidth = object.value(entry.first);
        if (!titleWidth.isUndefined()) {
            const double width = titleWidth.toDouble();
            if (!titleWidth.isDouble() || !std::isfinite(width) || std::trunc(width) != width
                || width < minimumTabTitleWidth || width > maximumTabTitleWidth)
                return {false, QCoreApplication::translate("WindowSystem", "The maximum tab title width must be an integer number of logical pixels within the allowed range")};
            *entry.second = static_cast<int>(width);
        }
    }
    const auto palette = object.value("colors");
    if (!palette.isUndefined()) {
        if (!palette.isArray() || palette.toArray().size() > 128) return {false, QCoreApplication::translate("WindowSystem", "标签颜色列表无效")};
        candidate.colors.clear();
        for (const auto& color : palette.toArray()) {
            if (!color.isObject()) return {false, QCoreApplication::translate("WindowSystem", "标签颜色定义无效")};
            const auto definition = color.toObject();
            TabColorPreset preset;
            if (definition.size() != 6 || !definition.value("id").isString() || !definition.value("name").isString()
                || !parseColors(definition.value("groupActive"), preset.groupActive)
                || !parseColors(definition.value("groupInactive"), preset.groupInactive)
                || !parseColors(definition.value("autoHideActive"), preset.autoHideActive)
                || !parseColors(definition.value("autoHideInactive"), preset.autoHideInactive))
                return {false, QCoreApplication::translate("WindowSystem", "标签颜色须包含 ID、名称和四种完整状态，颜色使用 #RRGGBB 或 #AARRGGBB")};
            preset.id = definition.value("id").toString(); preset.name = definition.value("name").toString();
            candidate.colors.append(std::move(preset));
        }
    }
    upgradeUnmodifiedBuiltinPalette(candidate.colors);
    if (!validTabAppearance(candidate)) return {false, QCoreApplication::translate("WindowSystem", "标签外观包含无效颜色、重复 ID 或不适用的滚轮设置")};
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
