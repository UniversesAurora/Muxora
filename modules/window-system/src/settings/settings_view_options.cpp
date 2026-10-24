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
    {WindowBar::Toolbars, "toolbars", QT_TRANSLATE_NOOP("WindowSystem", "Toolbars")}, {WindowBar::StatusBar, "statusBar", QT_TRANSLATE_NOOP("WindowSystem", "Status bar")},
    {WindowBar::TabBars, "tabBars", QT_TRANSLATE_NOOP("WindowSystem", "Tab bar")}, {WindowBar::SideBars, "sideBars", QT_TRANSLATE_NOOP("WindowSystem", "Auto-hide sidebars")}};
struct ModeDescription { ViewMode mode; const char* key; const char* name; };
constexpr ModeDescription modes[] = {
    {ViewMode::WindowFullscreen, "windowFullscreen", QT_TRANSLATE_NOOP("WindowSystem", "Window fullscreen")},
    {ViewMode::GroupFullscreen, "groupFullscreen", QT_TRANSLATE_NOOP("WindowSystem", "Tab group fullscreen")},
    {ViewMode::PaneFullscreen, "paneFullscreen", QT_TRANSLATE_NOOP("WindowSystem", "Pane fullscreen")}};
struct CaptionOrderDescription { CaptionButtonOrder order; const char* name; };
constexpr CaptionOrderDescription captionOrders[] = {
    {CaptionButtonOrder::Windows, QT_TRANSLATE_NOOP("WindowSystem", "Windows: Minimize, Maximize, Close (right)")},
    {CaptionButtonOrder::Reversed, QT_TRANSLATE_NOOP("WindowSystem", "Reversed: Close, Maximize, Minimize (left)")},
    {CaptionButtonOrder::MacOS, QT_TRANSLATE_NOOP("WindowSystem", "macOS: Close, Minimize, Maximize (left)")}};
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
    addCategory(parent, rows, "window/display", QCoreApplication::translate("WindowSystem", "Visibility"));
    hideSingleWorkbench_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "Hide the tab bar when a workbench window has one tab"), parent);
    hideSingleWorkbench_->setObjectName("HideSingleTabWorkbench");
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "Single-tab bar in workbench windows"), "window.hideSingleTabWorkbench",
        QCoreApplication::translate("WindowSystem", "Count tabs across the whole window. Pinned panels and auto-hide sidebars remain visible."), hideSingleWorkbench_));
    hideSingleFloating_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "Hide the tab bar when a floating window has one tab"), parent);
    hideSingleFloating_->setObjectName("HideSingleTabFloating");
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "Single-tab bar in floating windows"), "window.hideSingleTabFloating",
        QCoreApplication::translate("WindowSystem", "Enabled by default. The tab bar returns when a second tab is added."), hideSingleFloating_));
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
    hideToolbarOptionsWhenLocked_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "Hide toolbar options when locked with no overflow"), parent);
    hideToolbarOptionsWhenLocked_->setObjectName("HideToolbarOptionsWhenLocked");
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "Compact locked toolbars"), "window.hideToolbarOptionsWhenLocked",
        QCoreApplication::translate("WindowSystem", "Hide the options button when toolbars are locked and all buttons are visible. Keep it visible when buttons overflow."), hideToolbarOptionsWhenLocked_));
    connect(hideToolbarOptionsWhenLocked_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!manager_) return;
        auto behavior = manager_->behavior(); behavior.hideToolbarOptionsWhenLocked = enabled;
        manager_->setBehavior(behavior);
    });
    captionButtonOrder_ = new ThemedComboBox(parent); captionButtonOrder_->setObjectName("CaptionButtonOrder");
    for (const auto& item : captionOrders)
        captionButtonOrder_->addItem(QCoreApplication::translate("WindowSystem", item.name), int(item.order));
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "Window control button order"), "window.captionButtonOrder",
        QCoreApplication::translate("WindowSystem", "The last two align the buttons to the left with Close outermost, for a window controls toolbar in the top-left corner."), captionButtonOrder_));
    connect(captionButtonOrder_, &QComboBox::currentIndexChanged, this, [this] {
        if (!manager_) return;
        auto behavior = manager_->behavior(); behavior.captionButtonOrder = CaptionButtonOrder(captionButtonOrder_->currentData().toInt());
        manager_->setBehavior(behavior);
    });
    zenFullscreenMode_ = new ThemedComboBox(parent); zenFullscreenMode_->setObjectName("ZenFullscreenMode");
    for (const auto& mode : modes) zenFullscreenMode_->addItem(QCoreApplication::translate("WindowSystem", mode.name), int(mode.mode));
    rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", "Default fullscreen scope for Zen mode"), "window.zenFullscreenMode",
        QCoreApplication::translate("WindowSystem", "Used when entering Zen mode from a normal window. Keep the current scope if already fullscreen. Exiting Zen mode restores the bars and keeps fullscreen."), zenFullscreenMode_));
    connect(zenFullscreenMode_, &QComboBox::currentIndexChanged, this, [this] {
        if (!manager_) return;
        auto behavior = manager_->behavior(); behavior.zenFullscreenMode = ViewMode(zenFullscreenMode_->currentData().toInt());
        manager_->setBehavior(behavior);
    });
    for (const auto& bar : bars) {
        auto* control = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "Show %1").arg(QCoreApplication::translate("WindowSystem", bar.name)), parent);
        const auto key = QStringLiteral("window.visibility.") + QString::fromLatin1(bar.key);
        control->setObjectName(key); visibilityControls_.append({bar.bar, control});
        rows->addWidget(addRow(parent, "window/display", QCoreApplication::translate("WindowSystem", bar.name), key,
            QCoreApplication::translate("WindowSystem", "Bar visibility is independent of fullscreen, applies to the current window, and is saved with the layout. Zen mode temporarily hides every bar."), control));
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
    if (!manager_ || !zenFullscreenMode_) return;
    const auto window = settingsWindow();
    const QSignalBlocker zenModeBlock(zenFullscreenMode_);
    const QSignalBlocker workbenchBlock(hideSingleWorkbench_), floatingBlock(hideSingleFloating_);
    const auto behavior = manager_->behavior();
    const QSignalBlocker hideOptionsBlock(hideToolbarOptionsWhenLocked_);
    hideToolbarOptionsWhenLocked_->setChecked(behavior.hideToolbarOptionsWhenLocked);
    const QSignalBlocker captionOrderBlock(captionButtonOrder_);
    captionButtonOrder_->setCurrentIndex(captionButtonOrder_->findData(int(behavior.captionButtonOrder)));
    hideSingleWorkbench_->setChecked(behavior.hideSingleTabWorkbench);
    hideSingleFloating_->setChecked(behavior.hideSingleTabFloating);
    zenFullscreenMode_->setCurrentIndex(zenFullscreenMode_->findData(int(behavior.zenFullscreenMode)));
    const auto effective = manager_->barVisibility(window);
    for (const auto& item : visibilityControls_) {
        const QSignalBlocker block(item.control); item.control->setChecked(shown(effective, item.bar));
    }
}
} // namespace window_system
