// SPDX-License-Identifier: MIT
#include "settings_page.hpp"
#include "style/themed_controls.hpp"
#include "style/ui_metrics.hpp"
#include <window_system/interface_appearance.hpp>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <algorithm>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

namespace window_system {
void SettingsPage::addInterfaceOptions(QWidget* parent, QVBoxLayout* rows)
{
    auto* controller = InterfaceAppearanceController::applicationInstance();
    if (!controller) return;
    addCategory(parent, rows, QStringLiteral("interface"), QCoreApplication::translate("WindowSystem", "Interface"));
    interfaceFamilies_ = new QLineEdit(parent);
    interfaceFamilies_->setObjectName("InterfaceFontFamily");
    interfaceFamilies_->setPlaceholderText(QStringLiteral("Segoe UI, Microsoft YaHei, sans-serif"));
    interfaceFamilies_->setMaxLength(InterfaceAppearance::maximumFamilyText); uiMaximumWidth(interfaceFamilies_, 440);
    rows->addWidget(addRow(parent, "interface", QCoreApplication::translate("WindowSystem", "Interface font"), "workbench.fontFamily",
        QCoreApplication::translate("WindowSystem", "Font names, separated by commas. Missing characters use the next font in the list. Leave empty to use the default fonts."), interfaceFamilies_));
    interfaceFontSize_ = new UnitSpinBox(parent);
    interfaceFontSize_->setObjectName("InterfaceFontSize"); interfaceFontSize_->setRange(InterfaceAppearance::minimumFontSize, InterfaceAppearance::maximumFontSize);
    interfaceFontSize_->setSuffix(QStringLiteral(" px")); interfaceFontSize_->setKeyboardTracking(false);
    rows->addWidget(addRow(parent, "interface", QCoreApplication::translate("WindowSystem", "Interface font size"), "workbench.fontSize",
        QCoreApplication::translate("WindowSystem", "Text size before interface scaling. Terminal and editor fonts have their own settings."), interfaceFontSize_));
    interfaceZoom_ = new QDoubleSpinBox(parent);
    interfaceZoom_->setObjectName("InterfaceZoom"); interfaceZoom_->setDecimals(1);
    interfaceZoom_->setRange(interfaceScaleForZoom(InterfaceAppearance::minimumZoomLevel) * 100, interfaceScaleForZoom(InterfaceAppearance::maximumZoomLevel) * 100);
    interfaceZoom_->setSingleStep(10); interfaceZoom_->setSuffix(QStringLiteral(" %")); interfaceZoom_->setKeyboardTracking(false);
    rows->addWidget(addRow(parent, "interface", QCoreApplication::translate("WindowSystem", "Interface scale"), "window.zoomLevel",
        QCoreApplication::translate("WindowSystem", "Resize text, icons and controls in all windows. Changes take effect immediately."), interfaceZoom_));
    connect(interfaceFamilies_, &QLineEdit::editingFinished, this, [this, controller] {
        auto value = controller->appearance(); value.fontFamily = interfaceFamilies_->text();
        if (!controller->setAppearance(value)) syncInterfaceOptions();
    });
    connect(interfaceFontSize_, &QSpinBox::valueChanged, this, [controller](int size) {
        auto value = controller->appearance(); value.fontSize = size; controller->setAppearance(value);
    });
    connect(interfaceZoom_, &QDoubleSpinBox::valueChanged, this, [controller](double percent) {
        auto value = controller->appearance(); value.zoomLevel = std::clamp(std::log(percent / 100) / std::log(1.2), InterfaceAppearance::minimumZoomLevel, InterfaceAppearance::maximumZoomLevel);
        controller->setAppearance(value);
    });
    connect(controller, &InterfaceAppearanceController::appearanceChanged, this, &SettingsPage::syncInterfaceOptions);
    syncInterfaceOptions();
}
void SettingsPage::syncInterfaceOptions()
{
    const auto* controller = InterfaceAppearanceController::applicationInstance();
    if (!controller || !interfaceFamilies_) return;
    const auto value = controller->appearance();
    const QSignalBlocker family(interfaceFamilies_), size(interfaceFontSize_), zoom(interfaceZoom_);
    if (!interfaceFamilies_->hasFocus()) interfaceFamilies_->setText(value.fontFamily);
    interfaceFontSize_->setValue(value.fontSize); interfaceZoom_->setValue(interfaceScaleForZoom(value.zoomLevel) * 100);
}
} // namespace window_system
