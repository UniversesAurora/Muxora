// SPDX-License-Identifier: MIT
#include "setting_description.hpp"
#include <window_system/interface_appearance.hpp>
#include <window_system/localization.hpp>
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QPointer>
#include <algorithm>
#include <cmath>

namespace window_system {
namespace {

// Changes one field of the window behavior, as the Widgets page does.
template <class Apply>
std::function<bool(const QVariant&)> behaviorWriter(QPointer<WindowManager> manager, Apply apply)
{
    return [manager, apply](const QVariant& value) {
        if (!manager) return false;
        auto next = manager->behavior();
        apply(next, value);
        manager->setBehavior(next);
        return true;
    };
}

} // namespace

QVector<SettingCategory> settingCategories(WindowManager*)
{
    QVector<SettingCategory> result;
    if (TranslationManager::applicationInstance()) result.append({QStringLiteral("application"), QCoreApplication::translate("WindowSystem", "Application")});
    if (InterfaceAppearanceController::applicationInstance()) result.append({QStringLiteral("interface"), QCoreApplication::translate("WindowSystem", "Interface")});
    result.append({QStringLiteral("window"), QCoreApplication::translate("WindowSystem", "Window")});
    result.append({QStringLiteral("window/drag"), QCoreApplication::translate("WindowSystem", "Dragging")});
    return result;
}

QVector<SettingDescription> describeSettings(WindowManager* manager)
{
    const QPointer<WindowManager> owner(manager);
    QVector<SettingDescription> result;
    using Kind = SettingDescription::Kind;

    if (const QPointer<TranslationManager> translations = TranslationManager::applicationInstance()) {
        SettingDescription language{QStringLiteral("application"), QStringLiteral("workbench.language"), QCoreApplication::translate("WindowSystem", "Display language"),
            QCoreApplication::translate("WindowSystem", "Restart the application to apply. This setting is independent of the window layout."), Kind::Choice};
        for (const auto value : supportedUiLanguages()) language.choices.append({uiLanguageName(value), uiLanguageId(value)});
        language.defaultValue = uiLanguageId(UiLanguage::System);
        language.read = [translations] { return translations ? QVariant(uiLanguageId(translations->requestedLanguage())) : QVariant(); };
        language.write = [translations](const QVariant& value) {
            if (!translations) return false;
            translations->setRequestedLanguage(uiLanguageFromId(value.toString()));
            return true;
        };
        result.append(std::move(language));
    }

    if (const QPointer<InterfaceAppearanceController> appearance = InterfaceAppearanceController::applicationInstance()) {
        const auto change = [appearance](auto apply) {
            return [appearance, apply](const QVariant& value) {
                if (!appearance) return false;
                auto next = appearance->appearance();
                apply(next, value);
                return appearance->setAppearance(next);
            };
        };
        SettingDescription family{QStringLiteral("interface"), QStringLiteral("workbench.fontFamily"), QCoreApplication::translate("WindowSystem", "Interface font"),
            QCoreApplication::translate("WindowSystem", "Font names, separated by commas. Missing characters use the next font in the list. Leave empty to use the default fonts."), Kind::Text};
        family.maximumLength = InterfaceAppearance::maximumFamilyText;
        family.placeholder = QStringLiteral("Segoe UI, Microsoft YaHei, sans-serif");
        family.read = [appearance] { return appearance ? QVariant(appearance->appearance().fontFamily) : QVariant(); };
        family.write = change([](InterfaceAppearance& next, const QVariant& value) { next.fontFamily = value.toString(); });
        result.append(std::move(family));

        SettingDescription size{QStringLiteral("interface"), QStringLiteral("workbench.fontSize"), QCoreApplication::translate("WindowSystem", "Interface font size"),
            QCoreApplication::translate("WindowSystem", "Text size before interface scaling. Terminal and editor fonts have their own settings."), Kind::Integer};
        size.minimum = InterfaceAppearance::minimumFontSize;
        size.maximum = InterfaceAppearance::maximumFontSize;
        size.suffix = QStringLiteral(" px");
        size.read = [appearance] { return appearance ? QVariant(appearance->appearance().fontSize) : QVariant(); };
        size.write = change([](InterfaceAppearance& next, const QVariant& value) { next.fontSize = value.toInt(); });
        result.append(std::move(size));

        SettingDescription zoom{QStringLiteral("interface"), QStringLiteral("window.zoomLevel"), QCoreApplication::translate("WindowSystem", "Interface scale"),
            QCoreApplication::translate("WindowSystem", "Resize text, icons and controls in all windows. Changes take effect immediately."), Kind::Real};
        zoom.minimum = interfaceScaleForZoom(InterfaceAppearance::minimumZoomLevel) * 100;
        zoom.maximum = interfaceScaleForZoom(InterfaceAppearance::maximumZoomLevel) * 100;
        zoom.step = 10;
        zoom.decimals = 1;
        zoom.suffix = QStringLiteral(" %");
        zoom.read = [appearance] { return appearance ? QVariant(interfaceScaleForZoom(appearance->appearance().zoomLevel) * 100) : QVariant(); };
        zoom.write = change([](InterfaceAppearance& next, const QVariant& value) {
            next.zoomLevel = std::clamp(std::log(value.toDouble() / 100) / std::log(1.2),
                                        InterfaceAppearance::minimumZoomLevel, InterfaceAppearance::maximumZoomLevel);
        });
        result.append(std::move(zoom));
    }

    const WindowBehavior defaults;
    const auto behavior = [owner] { return owner ? owner->behavior() : WindowBehavior{}; };

    SettingDescription scope{QStringLiteral("window"), QStringLiteral("window.inputMethodScope"), QCoreApplication::translate("WindowSystem", "Remember input method for"),
        QCoreApplication::translate("WindowSystem", "Restore the input method and its modes when switching between these areas. Some input methods only allow the keyboard layout to be restored. States are not kept after restarting."), Kind::Choice};
    scope.choices = {{QCoreApplication::translate("WindowSystem", "Application"), int(InputMethodScope::Application)},
        {QCoreApplication::translate("WindowSystem", "Workbench window and its floating windows"), int(InputMethodScope::Window)},
        {QCoreApplication::translate("WindowSystem", "Each top-level window"), int(InputMethodScope::FloatingWindow)}, {QCoreApplication::translate("WindowSystem", "Tab group"), int(InputMethodScope::TabGroup)},
        {QCoreApplication::translate("WindowSystem", "Tab"), int(InputMethodScope::Tab)}, {QCoreApplication::translate("WindowSystem", "Pane"), int(InputMethodScope::Pane)}};
    scope.defaultValue = int(defaults.inputMethodScope);
    scope.read = [behavior] { return int(behavior().inputMethodScope); };
    scope.write = behaviorWriter(owner, [](WindowBehavior& next, const QVariant& value) { next.inputMethodScope = InputMethodScope(value.toInt()); });
    result.append(std::move(scope));

    SettingDescription position{QStringLiteral("window"), QStringLiteral("window.newTabPosition"), QCoreApplication::translate("WindowSystem", "New tab position"),
        QCoreApplication::translate("WindowSystem", "Applies to tabs created using the plus button, type menu, or commands."), Kind::Choice};
    position.choices = {{QCoreApplication::translate("WindowSystem", "After the active tab"), int(NewTabPosition::AfterActive)}, {QCoreApplication::translate("WindowSystem", "Before the active tab"), int(NewTabPosition::BeforeActive)},
        {QCoreApplication::translate("WindowSystem", "Start of tab group"), int(NewTabPosition::Beginning)}, {QCoreApplication::translate("WindowSystem", "End of tab group"), int(NewTabPosition::End)}};
    position.defaultValue = int(defaults.newTabPosition);
    position.read = [behavior] { return int(behavior().newTabPosition); };
    position.write = behaviorWriter(owner, [](WindowBehavior& next, const QVariant& value) { next.newTabPosition = NewTabPosition(value.toInt()); });
    result.append(std::move(position));

    SettingDescription detach{QStringLiteral("window"), QStringLiteral("window.dragOutsideKind"), QCoreApplication::translate("WindowSystem", "Window type for detached tabs"),
        QCoreApplication::translate("WindowSystem", "Choose a floating or workbench window when detaching a tab."), Kind::Choice};
    detach.choices = {{QCoreApplication::translate("WindowSystem", "Floating window (keeps its owner)"), int(WindowKind::Floating)}, {QCoreApplication::translate("WindowSystem", "Workbench window"), int(WindowKind::Workbench)}};
    detach.defaultValue = int(defaults.dragOutsideKind);
    detach.read = [behavior] { return int(behavior().dragOutsideKind); };
    detach.write = behaviorWriter(owner, [](WindowBehavior& next, const QVariant& value) { next.dragOutsideKind = WindowKind(value.toInt()); });
    result.append(std::move(detach));

    SettingDescription empty{QStringLiteral("window"), QStringLiteral("window.closeEmptyWorkbench"), QCoreApplication::translate("WindowSystem", "When the window becomes empty"),
        QCoreApplication::translate("WindowSystem", "The workbench window remains open while it owns floating windows."), Kind::Choice};
    empty.choices = {{QCoreApplication::translate("WindowSystem", "Keep the empty workbench window"), false}, {QCoreApplication::translate("WindowSystem", "Automatically close the empty workbench window"), true}};
    empty.defaultValue = defaults.closeEmptyWorkbench;
    empty.read = [behavior] { return behavior().closeEmptyWorkbench; };
    empty.write = behaviorWriter(owner, [](WindowBehavior& next, const QVariant& value) { next.closeEmptyWorkbench = value.toBool(); });
    result.append(std::move(empty));

    SettingDescription reveal{QStringLiteral("window/drag"), QStringLiteral("window.revealDuringDrag"), QCoreApplication::translate("WindowSystem", "Reveal target window"),
        QCoreApplication::translate("WindowSystem", "Hover a dragged tab over a target window to bring it forward for dropping."), Kind::Toggle};
    reveal.checkLabel = QCoreApplication::translate("WindowSystem", "Bring an obscured target window forward on hover");
    reveal.read = [behavior] { return behavior().revealDuringDrag; };
    reveal.write = behaviorWriter(owner, [](WindowBehavior& next, const QVariant& value) { next.revealDuringDrag = value.toBool(); });
    result.append(std::move(reveal));

    SettingDescription delay{QStringLiteral("window/drag"), QStringLiteral("window.revealDelayMs"), QCoreApplication::translate("WindowSystem", "Hover delay"),
        QCoreApplication::translate("WindowSystem", "Delay before bringing an obscured window forward."), Kind::Integer};
    delay.minimum = 200;
    delay.maximum = 2000;
    delay.step = 100;
    delay.suffix = QCoreApplication::translate("WindowSystem", " ms");
    delay.read = [behavior] { return behavior().revealDelayMs; };
    delay.write = behaviorWriter(owner, [](WindowBehavior& next, const QVariant& value) { next.revealDelayMs = value.toInt(); });
    delay.enabled = [behavior] { return behavior().revealDuringDrag; };
    result.append(std::move(delay));
    return result;
}

} // namespace window_system
