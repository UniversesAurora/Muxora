// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "tab_appearance_p.hpp"
#include "../window_manager_p.hpp"
#include "../chrome/workspace_window.hpp"
#include "../docking/dock_surface.hpp"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <array>
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

constexpr std::array stickySizingNames{std::pair{StickyTabSizing::Normal, "normal"},
    std::pair{StickyTabSizing::Compact, "compact"}, std::pair{StickyTabSizing::Shrink, "shrink"}};
constexpr std::array stickyCloseGuardNames{std::pair{StickyTabCloseGuard::KeyboardAndMouse, "keyboardAndMouse"},
    std::pair{StickyTabCloseGuard::Keyboard, "keyboard"}, std::pair{StickyTabCloseGuard::Mouse, "mouse"},
    std::pair{StickyTabCloseGuard::Never, "never"}};

} // namespace

QString stickyCloseGuardName(StickyTabCloseGuard guard)
{
    for (const auto& [value, name] : stickyCloseGuardNames) if (value == guard) return QString::fromLatin1(name);
    return {};
}

std::optional<StickyTabCloseGuard> stickyCloseGuardFromName(const QString& name)
{
    for (const auto& [value, text] : stickyCloseGuardNames) if (name == QLatin1String(text)) return value;
    return {};
}

QString stickySizingName(StickyTabSizing sizing)
{
    for (const auto& [value, name] : stickySizingNames) if (value == sizing) return QString::fromLatin1(name);
    return {};
}

std::optional<StickyTabSizing> stickySizingFromName(const QString& name)
{
    for (const auto& [value, text] : stickySizingNames) if (name == QLatin1String(text)) return value;
    return {};
}

namespace {

// The stored names are English sources; the display name is translated only
// while a built-in preset still has its own ID and name.
struct Seed { const char* id; const char* name; const char* accent; const char* soft; const char* vivid; };
constexpr Seed paletteSeeds[] = {
    {"gray", QT_TRANSLATE_NOOP("WindowSystem", "Gray"), "#5f6368", "#edf0f2", "#a9afb5"},
    {"blue", QT_TRANSLATE_NOOP("WindowSystem", "Blue"), "#1967d2", "#e8f0fe", "#78a4eb"},
    {"red", QT_TRANSLATE_NOOP("WindowSystem", "Red"), "#c5221f", "#fce8e6", "#eb817e"},
    {"yellow", QT_TRANSLATE_NOOP("WindowSystem", "Gold"), "#8b6500", "#fff4ce", "#dfbc45"},
    {"green", QT_TRANSLATE_NOOP("WindowSystem", "Green"), "#137333", "#e6f4ea", "#78be91"},
    {"pink", QT_TRANSLATE_NOOP("WindowSystem", "Pink"), "#b3267d", "#fce7f3", "#dc8abd"},
    {"purple", QT_TRANSLATE_NOOP("WindowSystem", "Purple"), "#7b2abf", "#f1e7fc", "#b18ade"},
    {"cyan", QT_TRANSLATE_NOOP("WindowSystem", "Cyan"), "#007b83", "#e0f4f5", "#63bcc4"},
    {"orange", QT_TRANSLATE_NOOP("WindowSystem", "Orange"), "#b05a00", "#fff0df", "#e6a65f"}};

} // namespace

QVector<TabColorPreset> defaultTabColorPresets()
{
    QVector<TabColorPreset> colors;
    colors.reserve(std::size(paletteSeeds));
    for (const auto& seed : paletteSeeds) {
        const QColor accent(seed.accent), soft(seed.soft), vivid(seed.vivid);
        // The persisted palette is an 8-bit #AARRGGBB contract. QColor's HSV
        // transforms retain 16-bit channels, which would otherwise change on
        // every save/restore comparison even though the displayed RGB matches.
        const TabVisualColors active{soft, QColor("#202124"), accent, QColor(accent.darker(110).name(QColor::HexArgb))};
        const TabVisualColors inactive{vivid, QColor("#202124"), accent, QColor(accent.darker(130).name(QColor::HexArgb))};
        colors.append({QString::fromLatin1(seed.id), QString::fromLatin1(seed.name),
            active, inactive, active, inactive});
    }
    return colors;
}

QString tabColorDisplayName(const TabColorPreset& preset)
{
    for (const auto& seed : paletteSeeds) {
        if (preset.id == QLatin1String(seed.id) && preset.name == QLatin1String(seed.name))
            return QCoreApplication::translate("WindowSystem", seed.name);
    }
    return preset.name;
}

bool validTabAppearance(const TabAppearance& appearance)
{
    const auto validPlacement = [](TabBarPlacement value) { return value == TabBarPlacement::Top || value == TabBarPlacement::Bottom; };
    if (!validPlacement(appearance.centralPlacement) || !validPlacement(appearance.pinnedPlacement)
        || appearance.maximumTitleWidth < minimumTabTitleWidth || appearance.maximumTitleWidth > maximumTabTitleWidth
        || appearance.maximumSidebarTitleWidth < minimumTabTitleWidth || appearance.maximumSidebarTitleWidth > maximumTabTitleWidth
        || stickySizingName(appearance.stickySizing).isEmpty()
        || appearance.shrinkStickyTitleWidth < minimumTabTitleWidth
        || appearance.shrinkStickyTitleWidth > maximumTabTitleWidth
        || stickyCloseGuardName(appearance.preventStickyClose).isEmpty()
        || appearance.colors.size() > 128) return false;
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
        {"stickySizing", stickySizingName(appearance.stickySizing)},
        {"shrinkStickyTitleWidth", appearance.shrinkStickyTitleWidth},
        {"stickyTabsOnSeparateRow", appearance.stickyTabsOnSeparateRow},
        {"preventStickyClose", stickyCloseGuardName(appearance.preventStickyClose)}, {"colors", colors}};
}

OperationResult parseTabAppearance(const QJsonValue& value, TabAppearance& appearance)
{
    TabAppearance candidate;
    if (value.isUndefined()) { appearance = candidate; return {true, {}}; }
    if (!value.isObject()) return {false, QCoreApplication::translate("WindowSystem", "Tab appearance settings must be an object")};
    const auto object = value.toObject();
    if (!parsePlacement(object.value("centralPlacement"), candidate.centralPlacement)
        || !parsePlacement(object.value("pinnedPlacement"), candidate.pinnedPlacement))
        return {false, QCoreApplication::translate("WindowSystem", "Tab bars support only top or bottom placement")};
    for (const auto& entry : {std::pair{"multiline", &candidate.multiline}, std::pair{"wheelExpandRows", &candidate.wheelExpandRows},
        std::pair{"showIcons", &candidate.showIcons}, std::pair{"showNewTabButton", &candidate.showNewTabButton},
        std::pair{"showNumbers", &candidate.showNumbers}, std::pair{"showSidebarIcons", &candidate.showSidebarIcons},
        std::pair{"showSidebarNumbers", &candidate.showSidebarNumbers}, std::pair{"clipTitles", &candidate.clipTitles},
        std::pair{"clipSidebarTitles", &candidate.clipSidebarTitles},
        std::pair{"stickyTabsOnSeparateRow", &candidate.stickyTabsOnSeparateRow}}) {
        const auto field = object.value(entry.first);
        if (!field.isUndefined() && !field.isBool()) return {false, QCoreApplication::translate("WindowSystem", "Invalid tab visibility setting type")};
        if (field.isBool()) *entry.second = field.toBool();
    }
    for (const auto& entry : {std::pair{"maximumTitleWidth", &candidate.maximumTitleWidth},
        std::pair{"maximumSidebarTitleWidth", &candidate.maximumSidebarTitleWidth},
        std::pair{"shrinkStickyTitleWidth", &candidate.shrinkStickyTitleWidth}}) {
        const auto titleWidth = object.value(entry.first);
        if (!titleWidth.isUndefined()) {
            const double width = titleWidth.toDouble();
            if (!titleWidth.isDouble() || !std::isfinite(width) || std::trunc(width) != width
                || width < minimumTabTitleWidth || width > maximumTabTitleWidth)
                return {false, QCoreApplication::translate("WindowSystem", "The maximum tab title width must be an integer number of logical pixels within the allowed range")};
            *entry.second = static_cast<int>(width);
        }
    }
    if (const auto sizing = object.value("stickySizing"); !sizing.isUndefined()) {
        const auto parsed = stickySizingFromName(sizing.toString());
        if (!sizing.isString() || !parsed)
            return {false, QCoreApplication::translate("WindowSystem", "Sticky tab sizing must be normal, compact or shrink")};
        candidate.stickySizing = *parsed;
    }
    if (const auto guard = object.value("preventStickyClose"); !guard.isUndefined()) {
        const auto parsed = stickyCloseGuardFromName(guard.toString());
        if (!guard.isString() || !parsed)
            return {false, QCoreApplication::translate("WindowSystem", "Sticky tab close protection must be keyboardAndMouse, keyboard, mouse or never")};
        candidate.preventStickyClose = *parsed;
    }
    const auto palette = object.value("colors");
    if (!palette.isUndefined()) {
        if (!palette.isArray() || palette.toArray().size() > 128) return {false, QCoreApplication::translate("WindowSystem", "Invalid tab color list")};
        candidate.colors.clear();
        for (const auto& color : palette.toArray()) {
            if (!color.isObject()) return {false, QCoreApplication::translate("WindowSystem", "Invalid tab color definition")};
            const auto definition = color.toObject();
            TabColorPreset preset;
            if (definition.size() != 6 || !definition.value("id").isString() || !definition.value("name").isString()
                || !parseColors(definition.value("groupActive"), preset.groupActive)
                || !parseColors(definition.value("groupInactive"), preset.groupInactive)
                || !parseColors(definition.value("autoHideActive"), preset.autoHideActive)
                || !parseColors(definition.value("autoHideInactive"), preset.autoHideInactive))
                return {false, QCoreApplication::translate("WindowSystem", "Tab colors require an ID, name, and four complete states, using #RRGGBB or #AARRGGBB")};
            preset.id = definition.value("id").toString(); preset.name = definition.value("name").toString();
            candidate.colors.append(std::move(preset));
        }
    }
    if (!validTabAppearance(candidate)) return {false, QCoreApplication::translate("WindowSystem", "Tab appearance contains invalid colors or duplicate IDs")};
    appearance = std::move(candidate);
    return {true, {}};
}

TabAppearance WindowManager::tabAppearance() const { return d->appearance; }

bool WindowManager::setTabAppearance(const TabAppearance& appearance)
{
    if (!validTabAppearance(appearance)) return false;
    if (d->appearance == appearance) return true;
    d->appearance = appearance;
    // Relabel the built strips in place when they still match the model; a
    // full refresh would also close peeks and revisit every tab's view.
    bool inPlace = !d->refreshing && !d->destroying;
    for (auto window : std::as_const(d->windows))
        if (inPlace && window) inPlace = window->surface()->updateTabAppearance();
    if (!inPlace) { refresh(); return true; }
    emit layoutChanged();
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
