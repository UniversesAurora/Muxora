// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "script_host_p.hpp"
#include <window_system/localization.hpp>
#include <QLabel>
#include <array>

namespace window_system_lua {
using namespace window_system;
namespace {
constexpr std::array behaviorBooleans{
    std::pair{"close_empty_window", &WindowBehavior::closeEmptyWorkbench},
    std::pair{"reveal_during_drag", &WindowBehavior::revealDuringDrag},
    std::pair{"hide_single_tab_main", &WindowBehavior::hideSingleTabWorkbench},
    std::pair{"hide_toolbar_options_when_locked", &WindowBehavior::hideToolbarOptionsWhenLocked},
    std::pair{"hide_single_tab_floating", &WindowBehavior::hideSingleTabFloating}};
constexpr std::array appearanceBooleans{std::pair{"multiline", &TabAppearance::multiline},
                                        std::pair{"wheel_expand_rows", &TabAppearance::wheelExpandRows},
                                        std::pair{"show_tab_icons", &TabAppearance::showIcons},
                                        std::pair{"show_tab_numbers", &TabAppearance::showNumbers},
                                        std::pair{"show_sidebar_icons", &TabAppearance::showSidebarIcons},
                                        std::pair{"show_sidebar_numbers", &TabAppearance::showSidebarNumbers},
                                        std::pair{"clip_tab_titles", &TabAppearance::clipTitles},
                                        std::pair{"clip_sidebar_titles", &TabAppearance::clipSidebarTitles},
                                        std::pair{"show_new_tab_button", &TabAppearance::showNewTabButton},
                                        std::pair{"sticky_tabs_on_separate_row", &TabAppearance::stickyTabsOnSeparateRow}};
constexpr std::array themeColors{std::pair{"background", &Theme::background},
                                 std::pair{"surface", &Theme::surface},
                                 std::pair{"raisedSurface", &Theme::raisedSurface},
                                 std::pair{"headerBackground", &Theme::headerBackground},
                                 std::pair{"headerText", &Theme::headerText},
                                 std::pair{"text", &Theme::text},
                                 std::pair{"mutedText", &Theme::mutedText},
                                 std::pair{"border", &Theme::border},
                                 std::pair{"accent", &Theme::accent},
                                 std::pair{"accentHover", &Theme::accentHover},
                                 std::pair{"accentSoft", &Theme::accentSoft},
                                 std::pair{"buttonHover", &Theme::buttonHover},
                                 std::pair{"activeTab", &Theme::activeTab},
                                 std::pair{"inactiveTab", &Theme::inactiveTab},
                                 std::pair{"edgeTabBackground", &Theme::edgeTabBackground},
                                 std::pair{"edgeTabHover", &Theme::edgeTabHover},
                                 std::pair{"edgeTabText", &Theme::edgeTabText},
                                 std::pair{"statusBackground", &Theme::statusBackground},
                                 std::pair{"statusText", &Theme::statusText},
                                 std::pair{"selection", &Theme::selection},
                                 std::pair{"selectionText", &Theme::selectionText},
                                 std::pair{"focus", &Theme::focus},
                                 std::pair{"danger", &Theme::danger},
                                 std::pair{"checkedIndicatorText", &Theme::checkedIndicatorText}};
struct Metric {
    const char *name;
    int ThemeMetrics::*field;
    int minimum;
    int maximum;
};
constexpr std::array metrics{Metric{"control_height", &ThemeMetrics::controlHeight, 8, 256},
                             Metric{"header_height", &ThemeMetrics::headerHeight, 8, 256},
                             Metric{"tab_height", &ThemeMetrics::tabHeight, 8, 256},
                             Metric{"status_height", &ThemeMetrics::statusHeight, 8, 256},
                             Metric{"edge_thickness", &ThemeMetrics::edgeThickness, 8, 256},
                             Metric{"pane_header_height", &ThemeMetrics::paneHeaderHeight, 8, 256},
                             Metric{"splitter_width", &ThemeMetrics::splitterWidth, 1, 16},
                             Metric{"group_splitter_width", &ThemeMetrics::groupSplitterWidth, 1, 16},
                             Metric{"corner_radius", &ThemeMetrics::cornerRadius, 0, 32},
                             Metric{"padding", &ThemeMetrics::padding, 0, 128},
                             Metric{"spacing", &ThemeMetrics::spacing, 0, 128},
                             Metric{"icon_size", &ThemeMetrics::iconSize, 1, 256}};
constexpr std::array visualFields{
    std::pair{"background", &TabVisualColors::background}, std::pair{"text", &TabVisualColors::text},
    std::pair{"line", &TabVisualColors::line}, std::pair{"icon", &TabVisualColors::icon}};
constexpr std::array presetStates{std::pair{"group_active", &TabColorPreset::groupActive},
                                  std::pair{"group_inactive", &TabColorPreset::groupInactive},
                                  std::pair{"sidebar_active", &TabColorPreset::autoHideActive},
                                  std::pair{"sidebar_inactive", &TabColorPreset::autoHideInactive}};
// Indexed by CaptionButtonOrder.
const QStringList captionOrders{"windows", "reversed", "macos"};
QString mode(ViewMode value) {
    switch (value) {
    case ViewMode::Normal:
        return "normal";
    case ViewMode::WindowFullscreen:
        return "window";
    case ViewMode::GroupFullscreen:
        return "group";
    case ViewMode::PaneFullscreen:
        return "pane";
    }
    return {};
}
ViewMode fullscreen(const QString &name) {
    if (name == "window")
        return ViewMode::WindowFullscreen;
    if (name == "group")
        return ViewMode::GroupFullscreen;
    if (name == "pane")
        return ViewMode::PaneFullscreen;
    throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Invalid fullscreen mode").toUtf8().constData());
}
TabBarPlacement tabPlacement(const QVariant &value) {
    const auto name = text({value}, 0);
    if (name == "top")
        return TabBarPlacement::Top;
    if (name == "bottom")
        return TabBarPlacement::Bottom;
    throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Tab position must be top or bottom").toUtf8().constData());
}
// Indexed by StickyTabSizing and StickyTabCloseGuard.
const QStringList stickySizings{"normal", "compact", "shrink"};
const QStringList stickyCloseGuards{"keyboard_and_mouse", "keyboard", "mouse", "never"};
QVariantMap appearanceValue(const TabAppearance &value) {
    QVariantMap map;
    for (const auto &[name, field] : appearanceBooleans)
        map[name] = value.*field;
    map["tab_position"] = value.centralPlacement == TabBarPlacement::Top ? "top" : "bottom";
    map["pinned_tab_position"] = value.pinnedPlacement == TabBarPlacement::Top ? "top" : "bottom";
    map["maximum_tab_title_width"] = value.maximumTitleWidth;
    map["maximum_sidebar_title_width"] = value.maximumSidebarTitleWidth;
    map["sticky_tab_sizing"] = stickySizings.value(int(value.stickySizing));
    map["shrink_sticky_tab_width"] = value.shrinkStickyTabWidth;
    map["prevent_sticky_close"] = stickyCloseGuards.value(int(value.preventStickyClose));
    QVariantList colors;
    for (const auto &preset : value.colors) {
        QVariantMap item{{"id", preset.id}, {"name", preset.name}};
        for (const auto &[name, field] : presetStates) {
            QVariantMap state;
            for (const auto &[key, color] : visualFields)
                state[key] = ((preset.*field).*color).name(QColor::HexArgb);
            item[name] = state;
        }
        colors.append(item);
    }
    map["colors"] = colors;
    return map;
}
void readAppearance(const QVariantMap &map, TabAppearance &value, bool requireKnown = true) {
    QSet<QString> known{"tab_position", "pinned_tab_position", "maximum_tab_title_width", "maximum_sidebar_title_width",
                        "sticky_tab_sizing", "shrink_sticky_tab_width", "prevent_sticky_close", "colors"};
    for (const auto &[name, field] : appearanceBooleans) {
        known.insert(name);
        if (map.contains(name))
            value.*field = boolean({map[name]}, 0);
    }
    if (requireKnown)
        for (const auto &key : map.keys())
            if (!known.contains(key))
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Unknown tab appearance field").toUtf8().constData());
    if (map.contains("tab_position"))
        value.centralPlacement = tabPlacement(map["tab_position"]);
    if (map.contains("pinned_tab_position"))
        value.pinnedPlacement = tabPlacement(map["pinned_tab_position"]);
    if (map.contains("maximum_tab_title_width"))
        value.maximumTitleWidth = number(map["maximum_tab_title_width"], value.maximumTitleWidth,
            minimumTabTitleWidth, maximumTabTitleWidth);
    if (map.contains("maximum_sidebar_title_width"))
        value.maximumSidebarTitleWidth = number(map["maximum_sidebar_title_width"], value.maximumSidebarTitleWidth,
            minimumTabTitleWidth, maximumTabTitleWidth);
    if (map.contains("sticky_tab_sizing")) {
        const auto index = stickySizings.indexOf(text({map["sticky_tab_sizing"]}, 0));
        if (index < 0)
            throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Sticky tab sizing must be normal, compact or shrink").toUtf8().constData());
        value.stickySizing = StickyTabSizing(index);
    }
    if (map.contains("shrink_sticky_tab_width"))
        value.shrinkStickyTabWidth = number(map["shrink_sticky_tab_width"], value.shrinkStickyTabWidth,
            minimumShrinkStickyTabWidth, maximumShrinkStickyTabWidth);
    if (map.contains("prevent_sticky_close")) {
        const auto index = stickyCloseGuards.indexOf(text({map["prevent_sticky_close"]}, 0));
        if (index < 0)
            throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Sticky tab close protection must be keyboard_and_mouse, keyboard, mouse or never").toUtf8().constData());
        value.preventStickyClose = StickyTabCloseGuard(index);
    }
    if (map.contains("colors")) {
        const auto presets = map["colors"];
        if (presets.metaType().id() != QMetaType::QVariantList)
            throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Colors must be an array").toUtf8().constData());
        if (presets.toList().size() > 64)
            throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Too many color presets").toUtf8().constData());
        QVector<TabColorPreset> colors;
        for (const auto &entry : presets.toList()) {
            const auto item = object({entry}, 0);
            TabColorPreset preset;
            preset.id = text({item.value("id")}, 0);
            preset.name = text({item.value("name")}, 0, preset.id);
            for (const auto &[name, field] : presetStates) {
                const auto state = object({item.value(name)}, 0);
                for (const auto &[key, color] : visualFields) {
                    QColor parsed(text({state.value(key)}, 0));
                    if (!parsed.isValid())
                        throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Invalid preset color").toUtf8().constData());
                    (preset.*field).*color = parsed;
                }
            }
            colors.append(preset);
        }
        value.colors = colors;
    }
}
} // namespace
void ScriptHost::Private::installAppearanceApi() {
    handlers["settings.get"] = [this](const Args &, const Context &) {
        const auto behavior = manager->behavior();
        auto values = appearanceValue(manager->tabAppearance());
        values.remove("colors");
        for (const auto &[name, field] : behaviorBooleans)
            values[name] = behavior.*field;
        values["reveal_delay_ms"] = behavior.revealDelayMs;
        values["zen_fullscreen"] = mode(behavior.zenFullscreenMode);
        values["drag_outside_kind"] =
            behavior.dragOutsideKind == WindowKind::Floating ? "floating" : "workbench";
        const QStringList positions{"beginning", "before_active", "after_active", "end"};
        values["new_tab_position"] = positions[int(behavior.newTabPosition)];
        values["caption_button_order"] = captionOrders[int(behavior.captionButtonOrder)];
        if (const auto *language = TranslationManager::applicationInstance())
            values["language"] = uiLanguageId(language->requestedLanguage());
        return result(values);
    };
    handlers["settings.update"] = [this](const Args &a, const Context &) {
        const auto map = object(a, 0);
        auto behavior = manager->behavior();
        auto appearance = manager->tabAppearance();
        QSet<QString> known{"reveal_delay_ms", "zen_fullscreen", "drag_outside_kind",  "new_tab_position",
                            "language",        "tab_position",   "pinned_tab_position", "caption_button_order",
                            "maximum_tab_title_width", "maximum_sidebar_title_width", "sticky_tab_sizing", "prevent_sticky_close"};
        for (const auto &[name, field] : behaviorBooleans) {
            known.insert(name);
            if (map.contains(name))
                behavior.*field = boolean({map[name]}, 0);
        }
        for (const auto &[name, field] : appearanceBooleans) {
            Q_UNUSED(field);
            known.insert(name);
        }
        for (const auto &key : map.keys())
            if (!known.contains(key))
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Unknown setting").toUtf8().constData());
        readAppearance(map, appearance, false);
        behavior.revealDelayMs = number(map.value("reveal_delay_ms"), behavior.revealDelayMs, 200, 2000);
        if (map.contains("zen_fullscreen"))
            behavior.zenFullscreenMode = fullscreen(text({map["zen_fullscreen"]}, 0));
        if (map.contains("drag_outside_kind")) {
            const auto kind = text({map["drag_outside_kind"]}, 0);
            if (kind != "floating" && kind != "workbench")
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Invalid window kind").toUtf8().constData());
            behavior.dragOutsideKind = kind == "floating" ? WindowKind::Floating : WindowKind::Workbench;
        }
        if (map.contains("new_tab_position")) {
            const QStringList positions{"beginning", "before_active", "after_active", "end"};
            const auto index = positions.indexOf(text({map["new_tab_position"]}, 0));
            if (index < 0)
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Invalid new tab position").toUtf8().constData());
            behavior.newTabPosition = NewTabPosition(index);
        }
        if (map.contains("caption_button_order")) {
            const auto index = captionOrders.indexOf(text({map["caption_button_order"]}, 0));
            if (index < 0)
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Caption button order must be windows, reversed or macos").toUtf8().constData());
            behavior.captionButtonOrder = CaptionButtonOrder(index);
        }
        QPointer<TranslationManager> language = TranslationManager::applicationInstance();
        UiLanguage requested = UiLanguage::System;
        if (map.contains("language")) {
            const auto name = text({map["language"]}, 0);
            if (!language || (name != "system" && name != "en" && name != "zh_CN"))
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Unsupported language").toUtf8().constData());
            requested = uiLanguageFromId(name);
        }
        const QPointer<ScriptHost> owner(q);
        const auto windows = manager;
        if (!windows->setTabAppearance(appearance) || !owner || !windows)
            return result(false);
        windows->setBehavior(behavior);
        if (!owner || !windows)
            return result(false);
        if (map.contains("language") && language)
            language->setRequestedLanguage(requested);
        return result(true);
    };
    handlers["tabs.appearance"] = [this](const Args &, const Context &) {
        return result(appearanceValue(manager->tabAppearance()));
    };
    handlers["tabs.set_appearance"] = [this](const Args &a, const Context &) {
        auto appearance = manager->tabAppearance();
        readAppearance(object(a, 0), appearance);
        return result(manager->setTabAppearance(appearance));
    };
    handlers["theme.get"] = [this](const Args &, const Context &) {
        const auto theme = manager->theme();
        QVariantMap values{{"name", theme.name}};
        for (const auto &[name, field] : themeColors)
            values[name] = (theme.*field).name(QColor::HexArgb);
        QVariantMap sizes;
        for (const auto &m : metrics)
            sizes[m.name] = theme.metrics.*(m.field);
        values["metrics"] = sizes;
        return result(values);
    };
    handlers["theme.update"] = [this](const Args &a, const Context &) {
        auto theme = manager->theme();
        const auto values = object(a, 0);
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (it.key() == "name") {
                theme.name = text({it.value()}, 0);
                if (theme.name.size() > 128)
                    throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Theme name is too long").toUtf8().constData());
                continue;
            }
            if (it.key() == "metrics") {
                const auto sizes = object({it.value()}, 0);
                for (auto size = sizes.cbegin(); size != sizes.cend(); ++size) {
                    bool found = false;
                    for (const auto &metric : metrics)
                        if (size.key() == metric.name) {
                            theme.metrics.*(metric.field) =
                                number(size.value(), 0, metric.minimum, metric.maximum);
                            found = true;
                            break;
                        }
                    if (!found)
                        throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Unknown theme metric").toUtf8().constData());
                }
                continue;
            }
            bool found = false;
            for (const auto &[name, field] : themeColors)
                if (it.key() == name) {
                    const QColor color(text({it.value()}, 0));
                    if (!color.isValid())
                        throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Invalid color").toUtf8().constData());
                    theme.*field = color;
                    found = true;
                    break;
                }
            if (!found)
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Unknown theme field").toUtf8().constData());
        }
        manager->setTheme(theme);
        return result(true);
    };
    const auto setBar = [this](const Args &a, const Context &c, bool pane) {
        const auto id = target(a, 0, c, pane ? Target::Pane : Target::Tab);
        const auto side = text(a, 1);
        if (side != "top" && side != "bottom")
            throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Bar must be top or bottom").toUtf8().constData());
        QPointer<QLabel> bar;
        if (a.size() > 2 && a[2].isValid() && !a[2].isNull()) {
            const auto spec =
                a[2].metaType().id() == QMetaType::QString ? QVariantMap{{"text", a[2]}} : object(a, 2);
            const auto label = text({spec.value("text")}, 0);
            if (label.size() > 8192)
                throw std::runtime_error(QCoreApplication::translate("WindowSystem", "Bar text is too long").toUtf8().constData());
            bar = new QLabel(label);
            bar->setTextFormat(Qt::PlainText);
            bar->setToolTip(text({spec.value("tooltip")}, 0));
            bar->setContentsMargins(6, 0, 6, 0);
        }
        const bool ok =
            pane ? (side == "top" ? manager->setPaneTopBar(id, bar) : manager->setPaneBottomBar(id, bar))
                 : (side == "top" ? manager->setTabTopBar(id, bar) : manager->setTabBottomBar(id, bar));
        if (!ok && bar)
            delete bar.data();
        return result(ok);
    };
    handlers["tabs.set_bar"] = [setBar](const Args &a, const Context &c) { return setBar(a, c, false); };
    handlers["panes.set_bar"] = [setBar](const Args &a, const Context &c) { return setBar(a, c, true); };
}
} // namespace window_system_lua
