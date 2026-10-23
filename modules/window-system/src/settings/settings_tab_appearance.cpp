// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "settings_page.hpp"
#include "style/themed_controls.hpp"
#include "style/tab_appearance_p.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace window_system {

void SettingsPage::addTabAppearanceOptions(QWidget* parent, QVBoxLayout* rows)
{
    addCategory(parent, rows, "window/tabs", QCoreApplication::translate("WindowSystem", "标签外观"));
    const auto placementControl = [this, parent, rows](const char* name, const char* key,
        const QString& title, bool pinned) {
        auto* control = new ThemedComboBox(parent);
        control->setObjectName(QString::fromLatin1(name));
        control->addItem(QCoreApplication::translate("WindowSystem", "顶部"), int(TabBarPlacement::Top));
        control->addItem(QCoreApplication::translate("WindowSystem", "底部"), int(TabBarPlacement::Bottom));
        rows->addWidget(addRow(parent, "window/tabs", title, QString::fromLatin1(key),
            pinned ? QCoreApplication::translate("WindowSystem", "单独设置固定侧栏面板的标签位置。") : QCoreApplication::translate("WindowSystem", "普通标签栏可放在内容顶部或底部。"), control));
        connect(control, &QComboBox::currentIndexChanged, this, [this, control, pinned] {
            if (!manager_) return;
            auto next = manager_->tabAppearance();
            (pinned ? next.pinnedPlacement : next.centralPlacement) = TabBarPlacement(control->currentData().toInt());
            manager_->setTabAppearance(next);
        });
        return control;
    };
    centralPlacement_ = placementControl("CentralTabPlacement", "tabs.centralPlacement", QCoreApplication::translate("WindowSystem", "普通标签栏位置"), false);
    pinnedPlacement_ = placementControl("PinnedTabPlacement", "tabs.pinnedPlacement", QCoreApplication::translate("WindowSystem", "固定侧栏标签位置"), true);
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
    multiline_ = flagControl("MultilineTabs", "tabs.multiline", QCoreApplication::translate("WindowSystem", "多行显示标签"),
        QCoreApplication::translate("WindowSystem", "标签排满一行后换行，不使用横向滚动条。"), &TabAppearance::multiline);
    wheelExpandRows_ = flagControl("WheelExpandTabRows", "tabs.wheelExpandRows", QCoreApplication::translate("WindowSystem", "滚轮展开标签行"),
        QCoreApplication::translate("WindowSystem", "多行模式下向下滚动展开全部行，向上滚动只显示当前标签所在行。"), &TabAppearance::wheelExpandRows);
    showIcons_ = flagControl("ShowTabIcons", "tabs.showIcons", QCoreApplication::translate("WindowSystem", "显示标签图标"),
        QCoreApplication::translate("WindowSystem", "控制普通中央标签栏，不影响侧边栏；仍遵循宿主为单个标签隐藏图标的设置。"), &TabAppearance::showIcons);
    showNewTabButton_ = flagControl("ShowNewTabButton", "tabs.showNewTabButton", QCoreApplication::translate("WindowSystem", "显示新建标签按钮"),
        QCoreApplication::translate("WindowSystem", "显示标签尾部的加号和类型下拉。隐藏后仍可使用菜单、命令或快捷键新建。"), &TabAppearance::showNewTabButton);
    showNumbers_ = flagControl("ShowTabNumbers", "tabs.showNumbers", QCoreApplication::translate("WindowSystem", "显示标签编号"),
        QCoreApplication::translate("WindowSystem", "普通中央标签栏的编号显示在图标左侧，按本栏顺序从 1 开始。"), &TabAppearance::showNumbers);
    showSidebarIcons_ = flagControl("ShowSidebarTabIcons", "tabs.showSidebarIcons", QCoreApplication::translate("WindowSystem", "显示侧边栏标签图标"),
        QCoreApplication::translate("WindowSystem", "独立控制固定侧栏标签栏和非固定收纳条的图标，默认隐藏；仍遵循单个标签的图标设置。"), &TabAppearance::showSidebarIcons);
    showSidebarNumbers_ = flagControl("ShowSidebarTabNumbers", "tabs.showSidebarNumbers", QCoreApplication::translate("WindowSystem", "显示侧边栏标签编号"),
        QCoreApplication::translate("WindowSystem", "独立控制固定侧栏标签栏和非固定收纳条的编号，默认隐藏；固定组按本栏、非固定收纳条按同一侧全部标签顺序从 1 开始。"), &TabAppearance::showSidebarNumbers);
    clipTitles_ = flagControl("ClipTabTitles", "tabs.clipTitles", QCoreApplication::translate("WindowSystem", "Clip long regular and pinned tab titles"),
        QCoreApplication::translate("WindowSystem", "Clip titles that exceed the display limit without adding an ellipsis. Applies to regular tabs and pinned sidebar tab bars."), &TabAppearance::clipTitles);
    maximumTitleWidth_ = new ThemedSpinBox(parent);
    maximumTitleWidth_->setObjectName("MaximumTabTitleWidth");
    maximumTitleWidth_->setRange(minimumTabTitleWidth, maximumTabTitleWidth);
    maximumTitleWidth_->setSuffix(QCoreApplication::translate("WindowSystem", " logical pixels"));
    rows->addWidget(addRow(parent, "window/tabs", QCoreApplication::translate("WindowSystem", "Maximum regular and pinned tab title width"),
        "tabs.maximumTitleWidth", QCoreApplication::translate("WindowSystem", "Limits only the text area; icons, numbers, and close buttons use separate space. Logical pixels follow display scaling."), maximumTitleWidth_));
    connect(maximumTitleWidth_, &QSpinBox::valueChanged, this, [this](int width) {
        if (!manager_) return;
        auto next = manager_->tabAppearance(); next.maximumTitleWidth = width;
        manager_->setTabAppearance(next);
    });
    clipSidebarTitles_ = flagControl("ClipSidebarTabTitles", "tabs.clipSidebarTitles", QCoreApplication::translate("WindowSystem", "Clip long auto-hide sidebar tab titles"),
        QCoreApplication::translate("WindowSystem", "Clip titles in auto-hide sidebar rails without adding an ellipsis. Regular and pinned tabs use their own settings."), &TabAppearance::clipSidebarTitles);
    maximumSidebarTitleWidth_ = new ThemedSpinBox(parent);
    maximumSidebarTitleWidth_->setObjectName("MaximumSidebarTabTitleWidth");
    maximumSidebarTitleWidth_->setRange(minimumTabTitleWidth, maximumTabTitleWidth);
    maximumSidebarTitleWidth_->setSuffix(QCoreApplication::translate("WindowSystem", " logical pixels"));
    rows->addWidget(addRow(parent, "window/tabs", QCoreApplication::translate("WindowSystem", "Maximum auto-hide sidebar tab title length"),
        "tabs.maximumSidebarTitleWidth", QCoreApplication::translate("WindowSystem", "Limits only the title text along each rail's text direction; icons and numbers use separate space. Logical pixels follow display scaling."), maximumSidebarTitleWidth_));
    connect(maximumSidebarTitleWidth_, &QSpinBox::valueChanged, this, [this](int width) {
        if (!manager_) return;
        auto next = manager_->tabAppearance(); next.maximumSidebarTitleWidth = width;
        manager_->setTabAppearance(next);
    });

    auto* editor = new QWidget(parent);
    auto* layout = new QVBoxLayout(editor); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    tabColors_ = new QPlainTextEdit(editor); tabColors_->setObjectName("TabColorPresetsJson");
    tabColors_->setMinimumHeight(150); tabColors_->setMaximumHeight(240);
    tabColors_->setAccessibleName(QCoreApplication::translate("WindowSystem", "标签颜色定义 JSON"));
    layout->addWidget(tabColors_);
    tabColorDiagnostic_ = new QLabel(editor); tabColorDiagnostic_->setObjectName("TabColorPresetsDiagnostic");
    tabColorDiagnostic_->setWordWrap(true); layout->addWidget(tabColorDiagnostic_);
    auto* apply = new QPushButton(QCoreApplication::translate("WindowSystem", "应用颜色列表"), editor); apply->setObjectName("ApplyTabColorPresets");
    layout->addWidget(apply, 0, Qt::AlignLeft);
    rows->addWidget(addRow(parent, "window/tabs", QCoreApplication::translate("WindowSystem", "标签颜色"), "tabs.colors",
        QCoreApplication::translate("WindowSystem", "编辑严格 JSON 数组，可覆盖默认颜色列表。每项包含稳定 ID、名称，以及普通栏和收纳条各自的选中／未选中状态。每种状态包括背景、文字、下划线和图标颜色，写为 #RRGGBB 或 #AARRGGBB。"), editor));
    connect(tabColors_, &QPlainTextEdit::textChanged, this, [this] { tabColorsDirty_ = true; });
    connect(apply, &QPushButton::clicked, this, [this] {
        if (!manager_) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(tabColors_->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            tabColorDiagnostic_->setText(QCoreApplication::translate("WindowSystem", "颜色列表未修改：请输入有效 JSON 数组。")); return;
        }
        auto definition = tabAppearanceToJson(manager_->tabAppearance()); definition.insert("colors", document.array());
        TabAppearance next;
        const auto parsed = parseTabAppearance(definition, next);
        if (!parsed) { tabColorDiagnostic_->setText(parsed.error); return; }
        const QPointer<SettingsPage> self(this);
        tabColorsDirty_ = false;
        if (manager_->setTabAppearance(next) && self) {
            tabColorDiagnostic_->setText(QCoreApplication::translate("WindowSystem", "颜色列表已应用。")); syncTabAppearanceOptions();
        }
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
    if (!tabColorsDirty_) {
        const QSignalBlocker block(tabColors_);
        tabColors_->setPlainText(QString::fromUtf8(QJsonDocument(tabAppearanceToJson(appearance).value("colors").toArray()).toJson(QJsonDocument::Indented)));
    }
}

} // namespace window_system
