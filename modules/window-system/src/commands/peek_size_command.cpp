// SPDX-License-Identifier: MIT
#include "peek_size_command.hpp"
#include <window_system/window_manager.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMetaType>
#include <QPointer>
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
    if (!valid || !std::isfinite(number) || number < minimum || number > QWIDGETSIZE_MAX
        || std::floor(number) != number) return false;
    result = int(number);
    return true;
}

bool chooseSize(WindowManager* manager, const CommandContext& context, TabId tab)
{
    const QPointer<WindowManager> owner(manager);
    const QPointer<QWidget> window(manager->windowWidget(context.window));
    if (!window) return false;
    // Keep the stack dialog out of Qt parent ownership. Closing its window or
    // manager rejects it without deleting a stack object during the modal loop.
    QDialog dialog;
    dialog.setObjectName("PeekSizeDialog");
    dialog.setWindowTitle(QStringLiteral("临时展开面板大小"));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.winId();
    if (window->windowHandle()) dialog.windowHandle()->setTransientParent(window->windowHandle());
    auto* form = new QFormLayout(&dialog);
    auto* width = new QSpinBox(&dialog); width->setObjectName("PeekWidthInput");
    auto* height = new QSpinBox(&dialog); height->setObjectName("PeekHeightInput");
    width->setRange(140, QWIDGETSIZE_MAX); height->setRange(100, QWIDGETSIZE_MAX);
    const auto current = manager->peekSize(tab);
    width->setValue(current.width()); height->setValue(current.height());
    width->setSuffix(QStringLiteral(" 像素")); height->setSuffix(QStringLiteral(" 像素"));
    form->addRow(QStringLiteral("宽度(&W)"), width); form->addRow(QStringLiteral("高度(&H)"), height);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("PeekSizeButtons"); form->addRow(buttons);
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
    return owner->setPeekSize(tab, QSize(width->value(), height->value()));
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
    return manager->setPeekSize(tab, QSize(width, height));
}

} // namespace

void registerPeekSizeCommands(WindowManager* manager)
{
    const auto enabled = [manager](const CommandContext& context) { return isAutoHideTab(manager, targetTab(context)); };
    manager->commands().add({"tab.peek.setSize", QStringLiteral("设置临时展开大小…"), "布局", {}, enabled,
        [manager](const CommandContext& context) { return setSize(manager, context); }});
    manager->commands().add({"tab.peek.resetSize", QStringLiteral("重置临时展开大小"), "布局", {}, enabled,
        [manager](const CommandContext& context) { return manager->resetPeekSize(targetTab(context)); }});
}

} // namespace window_system
