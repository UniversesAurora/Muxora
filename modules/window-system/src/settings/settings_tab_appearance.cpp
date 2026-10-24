// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "settings_page.hpp"
#include "style/themed_controls.hpp"
#include "style/integer_slider.hpp"
#include "style/tab_appearance_p.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace window_system {

void SettingsPage::addTabAppearanceOptions(QWidget* parent, QVBoxLayout* rows)
{
    addCategory(parent, rows, "window/tabs", QCoreApplication::translate("WindowSystem", "Tab appearance"));
    const auto placementControl = [this, parent, rows](const char* name, const char* key,
        const QString& title, bool pinned) {
        auto* control = new ThemedComboBox(parent);
        control->setObjectName(QString::fromLatin1(name));
        control->addItem(QCoreApplication::translate("WindowSystem", "Top"), int(TabBarPlacement::Top));
        control->addItem(QCoreApplication::translate("WindowSystem", "Bottom"), int(TabBarPlacement::Bottom));
        rows->addWidget(addRow(parent, "window/tabs", title, QString::fromLatin1(key),
            pinned ? QCoreApplication::translate("WindowSystem", "Choose the tab position for pinned sidebar panels independently.") : QCoreApplication::translate("WindowSystem", "Place central tab bars above or below the content."), control));
        connect(control, &QComboBox::currentIndexChanged, this, [this, control, pinned] {
            if (!manager_) return;
            auto next = manager_->tabAppearance();
            (pinned ? next.pinnedPlacement : next.centralPlacement) = TabBarPlacement(control->currentData().toInt());
            manager_->setTabAppearance(next);
        });
        return control;
    };
    centralPlacement_ = placementControl("CentralTabPlacement", "tabs.centralPlacement", QCoreApplication::translate("WindowSystem", "Central tab bar position"), false);
    pinnedPlacement_ = placementControl("PinnedTabPlacement", "tabs.pinnedPlacement", QCoreApplication::translate("WindowSystem", "Pinned sidebar tab position"), true);
    const auto flagControl = [this, parent, rows](const char* name, const char* key, const QString& title,
        const QString& explanation, bool TabAppearance::*field) {
        auto* control = new ThemedCheckBox(title, parent); control->setObjectName(QString::fromLatin1(name));
        rows->addWidget(addRow(parent, "window/tabs", title, QString::fromLatin1(key), explanation, control));
        connect(control, &QCheckBox::toggled, this, [this, field](bool enabled) {
            if (!manager_) return;
            auto next = manager_->tabAppearance(); next.*field = enabled;
            if (!next.multiline) next.wheelExpandRows = false;
            manager_->setTabAppearance(next);
        });
        return control;
    };
    multiline_ = flagControl("MultilineTabs", "tabs.multiline", QCoreApplication::translate("WindowSystem", "Show tabs in multiple rows"),
        QCoreApplication::translate("WindowSystem", "Wrap tabs to additional rows instead of using a horizontal scrollbar."), &TabAppearance::multiline);
    wheelExpandRows_ = flagControl("WheelExpandTabRows", "tabs.wheelExpandRows", QCoreApplication::translate("WindowSystem", "Expand tab rows with mouse wheel"),
        QCoreApplication::translate("WindowSystem", "In multiline mode, scroll down to show all rows and up to show only the active tab's row."), &TabAppearance::wheelExpandRows);
    showIcons_ = flagControl("ShowTabIcons", "tabs.showIcons", QCoreApplication::translate("WindowSystem", "Show tab icons"),
        QCoreApplication::translate("WindowSystem", "Applies to central tab bars independently of sidebars. Per-tab icon visibility set by the host still applies."), &TabAppearance::showIcons);
    showNewTabButton_ = flagControl("ShowNewTabButton", "tabs.showNewTabButton", QCoreApplication::translate("WindowSystem", "Show new tab button"),
        QCoreApplication::translate("WindowSystem", "Show the plus button and type menu after the last tab. When hidden, menus, commands, and shortcuts can still create tabs."), &TabAppearance::showNewTabButton);
    showNumbers_ = flagControl("ShowTabNumbers", "tabs.showNumbers", QCoreApplication::translate("WindowSystem", "Show tab numbers"),
        QCoreApplication::translate("WindowSystem", "Show central tab numbers before the icon, starting at 1 in each tab bar."), &TabAppearance::showNumbers);
    showSidebarIcons_ = flagControl("ShowSidebarTabIcons", "tabs.showSidebarIcons", QCoreApplication::translate("WindowSystem", "Show sidebar tab icons"),
        QCoreApplication::translate("WindowSystem", "Show icons in pinned and auto-hide sidebar bars independently. Hidden by default; per-tab icon visibility still applies."), &TabAppearance::showSidebarIcons);
    showSidebarNumbers_ = flagControl("ShowSidebarTabNumbers", "tabs.showSidebarNumbers", QCoreApplication::translate("WindowSystem", "Show sidebar tab numbers"),
        QCoreApplication::translate("WindowSystem", "Show numbers in pinned and auto-hide sidebar bars independently. Hidden by default. Number pinned bars separately and each auto-hide side continuously, starting at 1."), &TabAppearance::showSidebarNumbers);
    clipTitles_ = flagControl("ClipTabTitles", "tabs.clipTitles", QCoreApplication::translate("WindowSystem", "Clip long regular and pinned tab titles"),
        QCoreApplication::translate("WindowSystem", "Clip titles that exceed the display limit without adding an ellipsis. Applies to regular tabs and pinned sidebar tab bars."), &TabAppearance::clipTitles);
    const auto widthControl = [parent](const char* name, const QString& title) {
        auto* editor = new UnitSpinBox(parent);
        editor->setObjectName(QString::fromLatin1(name));
        editor->setRange(minimumTabTitleWidth, maximumTabTitleWidth);
        editor->setSuffix(QStringLiteral(" px"));
        editor->setAccessibleName(title);
        auto* control = new IntegerSlider(editor, minimumTabTitleWidth, 1000, parent);
        control->setObjectName(QString::fromLatin1(name) + QStringLiteral("Control"));
        control->setMaximumWidth(440);
        control->slider()->setObjectName(QString::fromLatin1(name) + QStringLiteral("Slider"));
        control->slider()->setAccessibleName(title);
        return control;
    };
    maximumTitleWidth_ = widthControl("MaximumTabTitleWidth", QCoreApplication::translate("WindowSystem", "Maximum regular and pinned tab title width"));
    rows->addWidget(addRow(parent, "window/tabs", QCoreApplication::translate("WindowSystem", "Maximum regular and pinned tab title width"),
        "tabs.maximumTitleWidth", QCoreApplication::translate("WindowSystem", "Clip title text that exceeds this length."), maximumTitleWidth_));
    connect(maximumTitleWidth_, &IntegerSlider::valueChanged, this, [this](int width) {
        if (!manager_) return;
        auto next = manager_->tabAppearance(); next.maximumTitleWidth = width;
        manager_->setTabAppearance(next);
    });
    clipSidebarTitles_ = flagControl("ClipSidebarTabTitles", "tabs.clipSidebarTitles", QCoreApplication::translate("WindowSystem", "Clip long auto-hide sidebar tab titles"),
        QCoreApplication::translate("WindowSystem", "Clip titles in auto-hide sidebar rails without adding an ellipsis. Regular and pinned tabs use their own settings."), &TabAppearance::clipSidebarTitles);
    maximumSidebarTitleWidth_ = widthControl("MaximumSidebarTabTitleWidth", QCoreApplication::translate("WindowSystem", "Maximum auto-hide sidebar tab title length"));
    rows->addWidget(addRow(parent, "window/tabs", QCoreApplication::translate("WindowSystem", "Maximum auto-hide sidebar tab title length"),
        "tabs.maximumSidebarTitleWidth", QCoreApplication::translate("WindowSystem", "Clip title text that exceeds this length."), maximumSidebarTitleWidth_));
    connect(maximumSidebarTitleWidth_, &IntegerSlider::valueChanged, this, [this](int width) {
        if (!manager_) return;
        auto next = manager_->tabAppearance(); next.maximumSidebarTitleWidth = width;
        manager_->setTabAppearance(next);
    });

    auto* editor = new QWidget(parent);
    auto* layout = new QVBoxLayout(editor); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    tabColors_ = new QPlainTextEdit(editor); tabColors_->setObjectName("TabColorPresetsJson");
    tabColors_->setMinimumHeight(150); tabColors_->setMaximumHeight(240);
    tabColors_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Tab color definitions JSON"));
    layout->addWidget(tabColors_);
    tabColorDiagnostic_ = new QLabel(editor); tabColorDiagnostic_->setObjectName("TabColorPresetsDiagnostic");
    tabColorDiagnostic_->setWordWrap(true); layout->addWidget(tabColorDiagnostic_);
    auto* apply = new QPushButton(QCoreApplication::translate("WindowSystem", "Apply color list"), editor); apply->setObjectName("ApplyTabColorPresets");
    layout->addWidget(apply, 0, Qt::AlignLeft);
    rows->addWidget(addRow(parent, "window/tabs", QCoreApplication::translate("WindowSystem", "Tab colors"), "tabs.colors",
        QCoreApplication::translate("WindowSystem", "Edit a strict JSON array to override the default palette. Each entry has a stable ID, name, and active/inactive states for central and auto-hide bars. Each state contains background, text, underline, and icon colors in #RRGGBB or #AARRGGBB format."), editor));
    connect(tabColors_, &QPlainTextEdit::textChanged, this, [this] { tabColorsDirty_ = true; });
    connect(apply, &QPushButton::clicked, this, [this] {
        if (!manager_) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(tabColors_->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            tabColorDiagnostic_->setText(QCoreApplication::translate("WindowSystem", "Color list unchanged: enter a valid JSON array.")); return;
        }
        auto definition = tabAppearanceToJson(manager_->tabAppearance()); definition.insert("colors", document.array());
        TabAppearance next;
        const auto parsed = parseTabAppearance(definition, next);
        if (!parsed) { tabColorDiagnostic_->setText(parsed.error); return; }
        const QPointer<SettingsPage> self(this);
        tabColorsDirty_ = false;
        if (manager_->setTabAppearance(next) && self) {
            tabColorDiagnostic_->setText(QCoreApplication::translate("WindowSystem", "Color list applied.")); syncTabAppearanceOptions();
        }
    });

    // The sticky tab options follow VS Code's pinned editor settings.
    addCategory(parent, rows, "window/tabs/sticky", QCoreApplication::translate("WindowSystem", "Sticky tabs"));
    stickySizing_ = new ThemedComboBox(parent);
    stickySizing_->setObjectName("StickyTabSizing");
    stickySizing_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Sticky tab size"));
    for (const auto& entry : {std::pair{QCoreApplication::translate("WindowSystem", "Normal (default)"), StickyTabSizing::Normal},
                             std::pair{QCoreApplication::translate("WindowSystem", "Compact: icon or first letter only"), StickyTabSizing::Compact},
                             std::pair{QCoreApplication::translate("WindowSystem", "Shrink: fixed width with the start of the title"), StickyTabSizing::Shrink}})
        stickySizing_->addItem(entry.first, int(entry.second));
    rows->addWidget(addRow(parent, "window/tabs/sticky", QCoreApplication::translate("WindowSystem", "Sticky tab size"), "tabs.stickySizing",
        QCoreApplication::translate("WindowSystem", "Compact and shrunk sticky tabs stay at the start of a single-row tab bar while the other tabs scroll."), stickySizing_));
    connect(stickySizing_, &QComboBox::currentIndexChanged, this, [this] {
        if (!manager_) return;
        auto next = manager_->tabAppearance(); next.stickySizing = StickyTabSizing(stickySizing_->currentData().toInt());
        manager_->setTabAppearance(next);
    });
    const auto separateTitle = QCoreApplication::translate("WindowSystem", "Show sticky tabs on a separate row");
    stickyTabsOnSeparateRow_ = new ThemedCheckBox(separateTitle, parent);
    stickyTabsOnSeparateRow_->setObjectName("StickyTabsOnSeparateRow");
    rows->addWidget(addRow(parent, "window/tabs/sticky", separateTitle, "tabs.stickyTabsOnSeparateRow",
        QCoreApplication::translate("WindowSystem", "Sticky tabs take their own row above the other tabs and stay in place while those scroll. Drag a tab between the rows to stick or unstick it."),
        stickyTabsOnSeparateRow_));
    connect(stickyTabsOnSeparateRow_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!manager_) return;
        auto next = manager_->tabAppearance(); next.stickyTabsOnSeparateRow = enabled;
        manager_->setTabAppearance(next);
    });
}

void SettingsPage::syncTabAppearanceOptions()
{
    if (!manager_ || !centralPlacement_) return;
    const auto appearance = manager_->tabAppearance();
    const QSignalBlocker central(centralPlacement_), pinned(pinnedPlacement_);
    centralPlacement_->setCurrentIndex(centralPlacement_->findData(int(appearance.centralPlacement)));
    pinnedPlacement_->setCurrentIndex(pinnedPlacement_->findData(int(appearance.pinnedPlacement)));
    for (const auto& value : {std::pair{multiline_, appearance.multiline}, std::pair{wheelExpandRows_, appearance.wheelExpandRows},
        std::pair{showIcons_, appearance.showIcons}, std::pair{showNewTabButton_, appearance.showNewTabButton},
        std::pair{showNumbers_, appearance.showNumbers}, std::pair{showSidebarIcons_, appearance.showSidebarIcons},
        std::pair{showSidebarNumbers_, appearance.showSidebarNumbers}, std::pair{clipTitles_, appearance.clipTitles},
        std::pair{clipSidebarTitles_, appearance.clipSidebarTitles}}) {
        const QSignalBlocker block(value.first); value.first->setChecked(value.second);
    }
    wheelExpandRows_->setEnabled(appearance.multiline);
    const QSignalBlocker titleWidth(maximumTitleWidth_);
    maximumTitleWidth_->setValue(appearance.maximumTitleWidth);
    maximumTitleWidth_->setEnabled(appearance.clipTitles);
    const QSignalBlocker sidebarTitleWidth(maximumSidebarTitleWidth_);
    maximumSidebarTitleWidth_->setValue(appearance.maximumSidebarTitleWidth);
    maximumSidebarTitleWidth_->setEnabled(appearance.clipSidebarTitles);
    {
        const QSignalBlocker block(stickySizing_);
        stickySizing_->setCurrentIndex(stickySizing_->findData(int(appearance.stickySizing)));
        const QSignalBlocker separate(stickyTabsOnSeparateRow_);
        stickyTabsOnSeparateRow_->setChecked(appearance.stickyTabsOnSeparateRow);
    }
    if (!tabColorsDirty_) {
        const QSignalBlocker block(tabColors_);
        tabColors_->setPlainText(QString::fromUtf8(QJsonDocument(tabAppearanceToJson(appearance).value("colors").toArray()).toJson(QJsonDocument::Indented)));
    }
}

} // namespace window_system
