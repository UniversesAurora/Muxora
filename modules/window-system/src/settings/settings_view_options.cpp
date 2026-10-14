// SPDX-License-Identifier: MIT
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
    {WindowBar::Toolbars, "toolbars", "工具栏"}, {WindowBar::StatusBar, "statusBar", "状态栏"},
    {WindowBar::TabBars, "tabBars", "标签栏"}, {WindowBar::SideBars, "sideBars", "侧边收纳条"}};
struct ModeDescription { ViewMode mode; const char* key; const char* name; };
constexpr ModeDescription modes[] = {
    {ViewMode::WindowFullscreen, "windowFullscreen", "窗口全屏"},
    {ViewMode::GroupFullscreen, "groupFullscreen", "标签组全屏"},
    {ViewMode::PaneFullscreen, "paneFullscreen", "窗格全屏"}};
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
    addCategory(parent, rows, "window/display", QStringLiteral("显示与全屏"));
    viewMode_ = new ThemedComboBox(parent); viewMode_->setObjectName("WindowViewMode");
    viewMode_->addItem(QStringLiteral("普通窗口"), int(ViewMode::Normal));
    for (const auto& mode : modes) viewMode_->addItem(QString::fromUtf8(mode.name), int(mode.mode));
    rows->addWidget(addRow(parent, "window/display", QStringLiteral("当前窗口显示范围"), "window.viewMode",
        QStringLiteral("三种全屏均铺满当前显示器；标签组和窗格全屏保留原来的内容与布局。"), viewMode_));
    connect(viewMode_, &QComboBox::currentIndexChanged, this, [this] {
        if (manager_) manager_->setViewMode(settingsWindow(), ViewMode(viewMode_->currentData().toInt()));
    });
    hideSingleWorkbench_ = new ThemedCheckBox(QStringLiteral("完整窗口只有一个标签时隐藏标签栏"), parent);
    hideSingleWorkbench_->setObjectName("HideSingleTabWorkbench");
    rows->addWidget(addRow(parent, "window/display", QStringLiteral("完整窗口的单标签栏"), "window.hideSingleTabWorkbench",
        QStringLiteral("按整个窗口的标签数量判断；固定面板和侧边收纳条保持显示。"), hideSingleWorkbench_));
    hideSingleFloating_ = new ThemedCheckBox(QStringLiteral("轻量浮窗只有一个标签时隐藏标签栏"), parent);
    hideSingleFloating_->setObjectName("HideSingleTabFloating");
    rows->addWidget(addRow(parent, "window/display", QStringLiteral("轻量浮窗的单标签栏"), "window.hideSingleTabFloating",
        QStringLiteral("默认开启；出现第二个标签时恢复标签栏。"), hideSingleFloating_));
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
    zen_ = new ThemedCheckBox(QStringLiteral("隐藏所有栏，专注当前内容"), parent);
    zen_->setObjectName("WindowZenMode");
    rows->addWidget(addRow(parent, "window/display", QStringLiteral("禅模式"), "window.zenMode",
        QStringLiteral("在全屏中临时隐藏各类栏。显示任意栏时退出禅模式，恢复此前设置。"), zen_));
    connect(zen_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (manager_) manager_->setZenMode(settingsWindow(), enabled);
    });
    for (const auto& bar : bars) {
        auto* control = new ThemedCheckBox(QStringLiteral("显示") + QString::fromUtf8(bar.name), parent);
        const auto key = QStringLiteral("window.visibility.") + QString::fromLatin1(bar.key);
        control->setObjectName(key); visibilityControls_.append({bar.bar, control});
        rows->addWidget(addRow(parent, "window/display", QString::fromUtf8(bar.name), key,
            QStringLiteral("各栏显隐与全屏无关，在当前窗口生效并随布局保存。禅模式临时隐藏全部栏。"), control));
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
    const QSignalBlocker workbenchBlock(hideSingleWorkbench_), floatingBlock(hideSingleFloating_);
    const auto behavior = manager_->behavior();
    hideSingleWorkbench_->setChecked(behavior.hideSingleTabWorkbench);
    hideSingleFloating_->setChecked(behavior.hideSingleTabFloating);
    viewMode_->setCurrentIndex(int(manager_->viewMode(window)));
    zen_->setChecked(manager_->zenMode(window));
    zen_->setEnabled(manager_->viewMode(window) != ViewMode::Normal);
    const auto effective = manager_->barVisibility(window);
    for (const auto& item : visibilityControls_) {
        const QSignalBlocker block(item.control); item.control->setChecked(shown(effective, item.bar));
    }
}
} // namespace window_system
