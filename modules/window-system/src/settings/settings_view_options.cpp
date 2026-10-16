// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "settings_page.hpp"
#include "style/themed_controls.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace window_system {
namespace {
struct BarDescription { WindowBar bar; const char* key; const char* name; };
constexpr BarDescription bars[] = {
    {WindowBar::Toolbars, "toolbars", QT_TRANSLATE_NOOP("WindowSystem", "工具栏")}, {WindowBar::StatusBar, "statusBar", QT_TRANSLATE_NOOP("WindowSystem", "状态栏")},
    {WindowBar::TabBars, "tabBars", QT_TRANSLATE_NOOP("WindowSystem", "标签栏")}, {WindowBar::SideBars, "sideBars", QT_TRANSLATE_NOOP("WindowSystem", "侧边收纳条")}};
struct ModeDescription { ViewMode mode; const char* key; const char* name; };
constexpr ModeDescription modes[] = {
    {ViewMode::WindowFullscreen, "windowFullscreen", QT_TRANSLATE_NOOP("WindowSystem", "窗口全屏")},
    {ViewMode::GroupFullscreen, "groupFullscreen", QT_TRANSLATE_NOOP("WindowSystem", "标签组全屏")},
    {ViewMode::PaneFullscreen, "paneFullscreen", QT_TRANSLATE_NOOP("WindowSystem", "窗格全屏")}};
bool shown(const BarVisibility& visibility, WindowBar bar)
{
    switch (bar) {
    case WindowBar::Toolbars: return visibility.toolbars;
    case WindowBar::StatusBar: return visibility.statusBar;
    case WindowBar::TabBars: return visibility.tabBars;
    case WindowBar::SideBars: return visibility.sideBars;
    }
    return false;
}
}
WindowId SettingsPage::settingsWindow() const
{
    if (!manager_) return {};
    const auto id = window()->property("windowId").toUuid();
    return manager_->model().window(id) ? id : initialWindow_;
}
void SettingsPage::addViewOptions(QWidget* parent, QVBoxLayout* rows)
{
    addCategory(parent, rows, "window/display", QCoreApplication::translate("WindowSystem", "显示与全屏"));
    viewMode_ = new ThemedComboBox(parent); viewMode_->setObjectName("WindowViewMode");
    viewMode_->addItem(QCoreApplication::translate("WindowSystem", "普通窗口"), int(ViewMode::Normal));
    for (const auto& mode : modes) viewMode_->addItem(QCoreApplication::translate("WindowSystem", mode.name), int(mode.mode));
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "当前窗口显示范围"), "window.viewMode",
        QCoreApplication::translate("WindowSystem", "三种全屏均铺满当前显示器；标签组和窗格全屏保留原来的内容与布局。"), viewMode_));
    connect(viewMode_, &QComboBox::currentIndexChanged, this, [this] {
        if (manager_) manager_->setViewMode(settingsWindow(), ViewMode(viewMode_->currentData().toInt()));
    });
    hideSingleWorkbench_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "完整窗口只有一个标签时隐藏标签栏"), parent);
    hideSingleWorkbench_->setObjectName("HideSingleTabWorkbench");
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "完整窗口的单标签栏"), "window.hideSingleTabWorkbench",
        QCoreApplication::translate("WindowSystem", "按整个窗口的标签数量判断；固定面板和侧边收纳条保持显示。"), hideSingleWorkbench_));
    hideSingleFloating_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "轻量浮窗只有一个标签时隐藏标签栏"), parent);
    hideSingleFloating_->setObjectName("HideSingleTabFloating");
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "轻量浮窗的单标签栏"), "window.hideSingleTabFloating",
        QCoreApplication::translate("WindowSystem", "默认开启；出现第二个标签时恢复标签栏。"), hideSingleFloating_));
    const auto changeSingleTabPolicy = [this](bool floating, bool enabled) {
        if (!manager_) return;
        auto next = manager_->behavior();
        (floating ? next.hideSingleTabFloating : next.hideSingleTabWorkbench) = enabled;
        manager_->setBehavior(next);
    };
    connect(hideSingleWorkbench_, &QCheckBox::toggled, this,
        [changeSingleTabPolicy](bool enabled) { changeSingleTabPolicy(false, enabled); });
    connect(hideSingleFloating_, &QCheckBox::toggled, this,
        [changeSingleTabPolicy](bool enabled) { changeSingleTabPolicy(true, enabled); });
    zen_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "隐藏所有栏，专注当前内容"), parent);
    zen_->setObjectName("WindowZenMode");
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "禅模式"), "window.zenMode",
        QCoreApplication::translate("WindowSystem", "临时隐藏各类栏；普通窗口会按下方设置先进入全屏。显示任意栏时退出禅模式，恢复此前设置。"), zen_));
    connect(zen_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (manager_ && !manager_->setZenMode(settingsWindow(), enabled)) syncViewOptions();
    });
    zenFullscreenMode_ = new ThemedComboBox(parent); zenFullscreenMode_->setObjectName("ZenFullscreenMode");
    for (const auto& mode : modes) zenFullscreenMode_->addItem(QCoreApplication::translate("WindowSystem", mode.name), int(mode.mode));
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "禅模式默认全屏范围"), "window.zenFullscreenMode",
        QCoreApplication::translate("WindowSystem", "在普通窗口启用禅模式时使用。已经全屏时保留当前范围；退出禅模式恢复各栏，但保留全屏。"), zenFullscreenMode_));
    connect(zenFullscreenMode_, &QComboBox::currentIndexChanged, this, [this] {
        if (!manager_) return;
        auto behavior = manager_->behavior(); behavior.zenFullscreenMode = ViewMode(zenFullscreenMode_->currentData().toInt());
        manager_->setBehavior(behavior);
    });
    for (const auto& bar : bars) {
        auto* control = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "显示%1").arg(QCoreApplication::translate("WindowSystem", bar.name)), parent);
        const auto key = QStringLiteral("window.visibility.") + QString::fromLatin1(bar.key);
        control->setObjectName(key); visibilityControls_.append({bar.bar, control});
        rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", bar.name), key,
            QCoreApplication::translate("WindowSystem", "各栏显隐与全屏无关，在当前窗口生效并随布局保存。禅模式临时隐藏全部栏。"), control));
        connect(control, &QCheckBox::toggled, this, [this, bar](bool visible) {
            if (!manager_) return;
            const auto window = settingsWindow();
            switch (bar.bar) {
            case WindowBar::Toolbars: manager_->setToolbarsVisible(window, visible); break;
            case WindowBar::StatusBar: manager_->setStatusBarVisible(window, visible); break;
            case WindowBar::TabBars: manager_->setTabBarsVisible(window, visible); break;
            case WindowBar::SideBars: manager_->setSideBarsVisible(window, visible); break;
            }
        });
    }
}
void SettingsPage::syncViewOptions()
{
    if (!manager_ || !viewMode_) return;
    const auto window = settingsWindow();
    const QSignalBlocker modeBlock(viewMode_), zenBlock(zen_);
    const QSignalBlocker zenModeBlock(zenFullscreenMode_);
    const QSignalBlocker workbenchBlock(hideSingleWorkbench_), floatingBlock(hideSingleFloating_);
    const auto behavior = manager_->behavior();
    hideSingleWorkbench_->setChecked(behavior.hideSingleTabWorkbench);
    hideSingleFloating_->setChecked(behavior.hideSingleTabFloating);
    zenFullscreenMode_->setCurrentIndex(zenFullscreenMode_->findData(int(behavior.zenFullscreenMode)));
    viewMode_->setCurrentIndex(int(manager_->viewMode(window)));
    zen_->setChecked(manager_->zenMode(window));
    const auto context = manager_->context(window);
    zen_->setEnabled(manager_->model().window(window) && (manager_->viewMode(window) != ViewMode::Normal
        || behavior.zenFullscreenMode == ViewMode::WindowFullscreen
        || (!context.group.isNull() && !context.tab.isNull() && !context.pane.isNull())));
    const auto effective = manager_->barVisibility(window);
    for (const auto& item : visibilityControls_) {
        const QSignalBlocker block(item.control); item.control->setChecked(shown(effective, item.bar));
    }
}
} // namespace window_system
