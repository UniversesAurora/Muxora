// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "peek_size_command.hpp"
#include <window_system/window_manager.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMetaType>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QWindow>
#include <cmath>

namespace window_system {
namespace {

TabId targetTab(const CommandContext& context)
{
    return context.args.value("tab", context.tab).toUuid();
}

bool isAutoHideTab(const WindowManager* manager, TabId tab)
{
    const auto* group = manager->model().group(manager->model().groupForTab(tab));
    return group && group->location == GroupLocation::AutoHide;
}

Edge targetEdge(const WindowManager* manager, TabId tab)
{
    const auto* group = manager->model().group(manager->model().groupForTab(tab));
    return group ? group->edge : Edge::Left;
}

bool dimension(const QVariant& value, int minimum, int& result)
{
    const auto type = value.metaType().id();
    switch (type) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Double: case QMetaType::Float: break;
    default: return false;
    }
    bool valid = false;
    const double number = value.toDouble(&valid);
    if (!valid || !std::isfinite(number) || (number != 0 && number < minimum) || number > QWIDGETSIZE_MAX
        || std::floor(number) != number) return false;
    result = int(number);
    return true;
}

bool chooseSize(WindowManager* manager, const CommandContext& context, TabId tab)
{
    const QPointer<WindowManager> owner(manager);
    const QPointer<QWidget> window(manager->windowWidget(context.window));
    if (!window) return false;
    const auto edge = targetEdge(manager, tab);
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    // Keep the stack dialog out of Qt parent ownership. Closing its window or
    // manager rejects it without deleting a stack object during the modal loop.
    QDialog dialog;
    dialog.setObjectName("PeekSizeDialog");
    dialog.setWindowTitle(QCoreApplication::translate("WindowSystem", "Auto-hide panel size"));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.winId();
    if (window->windowHandle()) dialog.windowHandle()->setTransientParent(window->windowHandle());
    auto* form = new QFormLayout(&dialog);
    auto* width = new QSpinBox(&dialog); width->setObjectName("PeekWidthInput");
    auto* height = new QSpinBox(&dialog); height->setObjectName("PeekHeightInput");
    width->setRange(0, QWIDGETSIZE_MAX); height->setRange(0, QWIDGETSIZE_MAX);
    width->setSpecialValueText(vertical ? QCoreApplication::translate("WindowSystem", "Default width") : QCoreApplication::translate("WindowSystem", "Fill width"));
    height->setSpecialValueText(vertical ? QCoreApplication::translate("WindowSystem", "Fill height") : QCoreApplication::translate("WindowSystem", "Default height"));
    const auto current = manager->peekSizePolicy(tab, edge);
    width->setValue(current.width.value_or(0)); height->setValue(current.height.value_or(0));
    width->setSuffix(QCoreApplication::translate("WindowSystem", " px")); height->setSuffix(QCoreApplication::translate("WindowSystem", " px"));
    form->addRow(QCoreApplication::translate("WindowSystem", "&Width"), width); form->addRow(QCoreApplication::translate("WindowSystem", "&Height"), height);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("PeekSizeButtons"); form->addRow(buttons);
    const auto updateValidity = [width, height, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled((width->value() == 0 || width->value() >= 140)
            && (height->value() == 0 || height->value() >= 100));
    };
    QObject::connect(width, &QSpinBox::valueChanged, &dialog, updateValidity);
    QObject::connect(height, &QSpinBox::valueChanged, &dialog, updateValidity);
    updateValidity();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(window, &QObject::destroyed, &dialog, &QDialog::reject);
    QObject::connect(manager, &QObject::destroyed, &dialog, &QDialog::reject);
    QObject::connect(manager, &WindowManager::layoutChanged, &dialog, [&] {
        if (!owner || !isAutoHideTab(owner, tab)) dialog.reject();
    });
    applyTheme(&dialog, manager->theme()); width->setFocus(); width->selectAll();
    const auto accepted = dialog.exec() == QDialog::Accepted;
    if (!owner || !window || !isAutoHideTab(owner, tab)) return false;
    if (!accepted) return true;
    const auto optionalDimension = [](int value) { return value == 0 ? std::optional<int>{} : std::optional<int>{value}; };
    return owner->setPeekSizePolicy(tab, edge, {optionalDimension(width->value()), optionalDimension(height->value())});
}

bool setSize(WindowManager* manager, const CommandContext& context)
{
    const auto tab = targetTab(context);
    const bool explicitWidth = context.args.contains("width"), explicitHeight = context.args.contains("height");
    if (!explicitWidth && !explicitHeight) return chooseSize(manager, context, tab);
    if (!explicitWidth || !explicitHeight) return false;
    int width = 0, height = 0;
    if (!dimension(context.args.value("width"), 140, width)
        || !dimension(context.args.value("height"), 100, height)) return false;
    return manager->setPeekSizePolicy(tab, targetEdge(manager, tab),
        {width == 0 ? std::optional<int>{} : std::optional<int>{width},
         height == 0 ? std::optional<int>{} : std::optional<int>{height}});
}

} // namespace

void registerPeekSizeCommands(WindowManager* manager)
{
    const auto enabled = [manager](const CommandContext& context) { return isAutoHideTab(manager, targetTab(context)); };
    manager->commands().add({"tab.peek.setSize", QCoreApplication::translate("WindowSystem", "Set auto-hide panel size…"), "布局", {}, enabled,
        [manager](const CommandContext& context) { return setSize(manager, context); }});
    manager->commands().add({"tab.peek.resetSize", QCoreApplication::translate("WindowSystem", "Reset auto-hide panel size"), "布局", {}, enabled,
        [manager](const CommandContext& context) {
            const auto tab = targetTab(context);
            return manager->resetPeekSize(tab, targetEdge(manager, tab));
        }});
}

} // namespace window_system
