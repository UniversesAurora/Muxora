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

constexpr std::array placementNames{std::pair{TabBarPlacement::Top, "top"}, std::pair{TabBarPlacement::Bottom, "bottom"},
    std::pair{TabBarPlacement::Left, "left"}, std::pair{TabBarPlacement::Right, "right"}};
constexpr std::array expansionNames{std::pair{TabRowExpansion::Push, "push"}, std::pair{TabRowExpansion::Overlay, "overlay"}};

template <typename Value, std::size_t Size>
QString enumName(const std::array<std::pair<Value, const char*>, Size>& names, Value value)
{
    for (const auto& [candidate, name] : names) if (candidate == value) return QString::fromLatin1(name);
    return {};
}

template <typename Value, std::size_t Size>
bool parseEnum(const std::array<std::pair<Value, const char*>, Size>& names, const QJsonValue& value, Value& result)
{
    if (value.isUndefined()) return true;
    for (const auto& [candidate, name] : names) if (value == QLatin1StringView(name)) { result = candidate; return true; }
    return false;
}

QString placementName(TabBarPlacement placement) { return enumName(placementNames, placement); }
bool parsePlacement(const QJsonValue& value, TabBarPlacement& placement) { return parseEnum(placementNames, value, placement); }

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
//
// One tonal scale per hue (tab strip design 9): a pastel for a resting tab,
// a near-white tint for the active tab (it still reads as joined to the
// white content), a dark text of the same hue, and a vivid line color that
// tab folders and auto-hide rails use. The pastels share one lightness, so
// no color shouts over another, and differ in hue from the violet strip.
struct Seed { const char* id; const char* name; const char* line; const char* resting; const char* active;
              const char* text; const char* icon; };
constexpr Seed paletteSeeds[] = {
    {"gray", QT_TRANSLATE_NOOP("WindowSystem", "Gray"), "#6b7280", "#d9dce1", "#f3f4f6", "#1f2329", "#4b5260"},
    {"blue", QT_TRANSLATE_NOOP("WindowSystem", "Blue"), "#2f6fde", "#c9dafa", "#edf3fe", "#0f2a5c", "#2559b8"},
    {"red", QT_TRANSLATE_NOOP("WindowSystem", "Red"), "#d0393a", "#f6cdcc", "#fdeeee", "#5c1314", "#a92b2c"},
    {"yellow", QT_TRANSLATE_NOOP("WindowSystem", "Gold"), "#b8860b", "#f3e0a8", "#fdf7e4", "#4a3600", "#8a6408"},
    {"green", QT_TRANSLATE_NOOP("WindowSystem", "Green"), "#2e9a57", "#c8ead4", "#edf8f1", "#0f3d21", "#237544"},
    {"pink", QT_TRANSLATE_NOOP("WindowSystem", "Pink"), "#cc3f8f", "#f5cde3", "#fdeef6", "#561339", "#a3306f"},
    {"purple", QT_TRANSLATE_NOOP("WindowSystem", "Purple"), "#8a4fd1", "#d6c1f3", "#f6f0fd", "#351766", "#6d3bb0"},
    {"cyan", QT_TRANSLATE_NOOP("WindowSystem", "Cyan"), "#1a9aa6", "#c4e9ec", "#ecf8f9", "#0b3c41", "#147880"},
    {"orange", QT_TRANSLATE_NOOP("WindowSystem", "Orange"), "#db7419", "#f8d9bb", "#fef2e7", "#5a2c05", "#b05b10"}};

} // namespace

QVector<TabColorPreset> defaultTabColorPresets()
{
    QVector<TabColorPreset> colors;
    colors.reserve(std::size(paletteSeeds));
    for (const auto& seed : paletteSeeds) {
        const QColor line(seed.line), text(seed.text), icon(seed.icon);
        const TabVisualColors active{QColor(seed.active), text, line, icon};
        const TabVisualColors resting{QColor(seed.resting), text, line, icon};
        // Auto-hide rails stand on their own over the content edge: the
        // vivid color with white text, and the pastel while expanded.
        const TabVisualColors railResting{line, QColor("#ffffff"), line, QColor("#ffffff")};
        colors.append({QString::fromLatin1(seed.id), QString::fromLatin1(seed.name),
            active, resting, resting, railResting});
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
    const auto validPlacement = [](TabBarPlacement value) { return !placementName(value).isEmpty(); };
    if (!validPlacement(appearance.centralPlacement) || !validPlacement(appearance.pinnedPlacement)
        || enumName(expansionNames, appearance.rowExpansion).isEmpty()
        || appearance.verticalTabWidth < minimumVerticalTabWidth || appearance.verticalTabWidth > maximumVerticalTabWidth
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
        {"rowExpansion", enumName(expansionNames, appearance.rowExpansion)},
        {"showAllRows", appearance.showAllRows}, {"stickyMultiline", appearance.stickyMultiline},
        {"stickyShowAllRows", appearance.stickyShowAllRows},
        {"wheelExpandRows", appearance.wheelExpandRows}, {"verticalTabWidth", appearance.verticalTabWidth},
        {"showIcons", appearance.showIcons},
        {"showNewTabButton", appearance.showNewTabButton}, {"showNumbers", appearance.showNumbers},
        {"showSidebarIcons", appearance.showSidebarIcons}, {"showSidebarNumbers", appearance.showSidebarNumbers},
        {"showGroupNames", appearance.showGroupNames},
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
        return {false, QCoreApplication::translate("WindowSystem", "Tab bars can be placed only top, bottom, left or right")};
    if (!parseEnum(expansionNames, object.value("rowExpansion"), candidate.rowExpansion))
        return {false, QCoreApplication::translate("WindowSystem", "Expanded tab rows must push or overlay the content")};
    if (const auto width = object.value("verticalTabWidth"); !width.isUndefined()) {
        const double number = width.toDouble();
        if (!width.isDouble() || std::trunc(number) != number || number < minimumVerticalTabWidth || number > maximumVerticalTabWidth)
            return {false, QCoreApplication::translate("WindowSystem", "The vertical tab bar width must be an integer from 67 to 480 logical pixels")};
        candidate.verticalTabWidth = int(number);
    }
    for (const auto& entry : {std::pair{"multiline", &candidate.multiline}, std::pair{"showAllRows", &candidate.showAllRows},
        std::pair{"stickyMultiline", &candidate.stickyMultiline}, std::pair{"stickyShowAllRows", &candidate.stickyShowAllRows}, std::pair{"wheelExpandRows", &candidate.wheelExpandRows},
        std::pair{"showIcons", &candidate.showIcons}, std::pair{"showNewTabButton", &candidate.showNewTabButton},
        std::pair{"showNumbers", &candidate.showNumbers}, std::pair{"showSidebarIcons", &candidate.showSidebarIcons},
        std::pair{"showSidebarNumbers", &candidate.showSidebarNumbers}, std::pair{"clipTitles", &candidate.clipTitles},
        std::pair{"showGroupNames", &candidate.showGroupNames},
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
