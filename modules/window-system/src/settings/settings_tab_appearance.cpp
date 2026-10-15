// SPDX-License-Identifier: MIT
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
#include <QVBoxLayout>

namespace window_system {

void SettingsPage::addTabAppearanceOptions(QWidget* parent, QVBoxLayout* rows)
{
    addCategory(parent, rows, "window/tabs", QStringLiteral("标签外观"));
    const auto placementControl = [this, parent, rows](const char* name, const char* key,
        const QString& title, bool pinned) {
        auto* control = new ThemedComboBox(parent);
        control->setObjectName(QString::fromLatin1(name));
        control->addItem(QStringLiteral("顶部"), int(TabBarPlacement::Top));
        control->addItem(QStringLiteral("底部"), int(TabBarPlacement::Bottom));
        rows->addWidget(addRow(parent, "window/tabs", title, QString::fromLatin1(key),
            pinned ? QStringLiteral("单独设置固定侧栏面板的标签位置。") : QStringLiteral("普通标签栏可放在内容顶部或底部。"), control));
        connect(control, &QComboBox::currentIndexChanged, this, [this, control, pinned] {
            if (!manager_) return;
            auto next = manager_->tabAppearance();
            (pinned ? next.pinnedPlacement : next.centralPlacement) = TabBarPlacement(control->currentData().toInt());
            manager_->setTabAppearance(next);
        });
        return control;
    };
    centralPlacement_ = placementControl("CentralTabPlacement", "tabs.centralPlacement", QStringLiteral("普通标签栏位置"), false);
    pinnedPlacement_ = placementControl("PinnedTabPlacement", "tabs.pinnedPlacement", QStringLiteral("固定侧栏标签位置"), true);
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
    multiline_ = flagControl("MultilineTabs", "tabs.multiline", QStringLiteral("多行显示标签"),
        QStringLiteral("标签排满一行后换行，不使用横向滚动条。"), &TabAppearance::multiline);
    wheelExpandRows_ = flagControl("WheelExpandTabRows", "tabs.wheelExpandRows", QStringLiteral("滚轮展开标签行"),
        QStringLiteral("多行模式下向下滚动展开全部行，向上滚动只显示当前标签所在行。"), &TabAppearance::wheelExpandRows);
    showIcons_ = flagControl("ShowTabIcons", "tabs.showIcons", QStringLiteral("显示标签图标"),
        QStringLiteral("宿主为单个标签隐藏图标时，仍遵循该标签的设置。"), &TabAppearance::showIcons);
    showNewTabButton_ = flagControl("ShowNewTabButton", "tabs.showNewTabButton", QStringLiteral("显示新建标签按钮"),
        QStringLiteral("显示标签尾部的加号和类型下拉。隐藏后仍可使用菜单、命令或快捷键新建。"), &TabAppearance::showNewTabButton);
    showNumbers_ = flagControl("ShowTabNumbers", "tabs.showNumbers", QStringLiteral("显示标签编号"),
        QStringLiteral("编号显示在图标左侧，按当前标签栏内的顺序重新编号。"), &TabAppearance::showNumbers);

    auto* editor = new QWidget(parent);
    auto* layout = new QVBoxLayout(editor); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    tabColors_ = new QPlainTextEdit(editor); tabColors_->setObjectName("TabColorPresetsJson");
    tabColors_->setMinimumHeight(150); tabColors_->setMaximumHeight(240);
    tabColors_->setAccessibleName(QStringLiteral("标签颜色定义 JSON"));
    layout->addWidget(tabColors_);
    tabColorDiagnostic_ = new QLabel(editor); tabColorDiagnostic_->setObjectName("TabColorPresetsDiagnostic");
    tabColorDiagnostic_->setWordWrap(true); layout->addWidget(tabColorDiagnostic_);
    auto* apply = new QPushButton(QStringLiteral("应用颜色列表"), editor); apply->setObjectName("ApplyTabColorPresets");
    layout->addWidget(apply, 0, Qt::AlignLeft);
    rows->addWidget(addRow(parent, "window/tabs", QStringLiteral("标签颜色"), "tabs.colors",
        QStringLiteral("编辑严格 JSON 数组，可覆盖默认颜色列表。每项包含稳定 ID、名称，以及普通栏和收纳条各自的选中／未选中状态。每种状态包括背景、文字、下划线和图标颜色，写为 #RRGGBB 或 #AARRGGBB。"), editor));
    connect(tabColors_, &QPlainTextEdit::textChanged, this, [this] { tabColorsDirty_ = true; });
    connect(apply, &QPushButton::clicked, this, [this] {
        if (!manager_) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(tabColors_->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            tabColorDiagnostic_->setText(QStringLiteral("颜色列表未修改：请输入有效 JSON 数组。")); return;
        }
        auto definition = tabAppearanceToJson(manager_->tabAppearance()); definition.insert("colors", document.array());
        TabAppearance next;
        const auto parsed = parseTabAppearance(definition, next);
        if (!parsed) { tabColorDiagnostic_->setText(parsed.error); return; }
        const QPointer<SettingsPage> self(this);
        tabColorsDirty_ = false;
        if (manager_->setTabAppearance(next) && self) {
            tabColorDiagnostic_->setText(QStringLiteral("颜色列表已应用。")); syncTabAppearanceOptions();
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
        std::pair{showNumbers_, appearance.showNumbers}}) {
        const QSignalBlocker block(value.first); value.first->setChecked(value.second);
    }
    wheelExpandRows_->setEnabled(appearance.multiline);
    if (!tabColorsDirty_) {
        const QSignalBlocker block(tabColors_);
        tabColors_->setPlainText(QString::fromUtf8(QJsonDocument(tabAppearanceToJson(appearance).value("colors").toArray()).toJson(QJsonDocument::Indented)));
    }
}

} // namespace window_system
