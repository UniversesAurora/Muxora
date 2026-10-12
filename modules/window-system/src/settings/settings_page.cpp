// SPDX-License-Identifier: MIT
#include "settings_page.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace window_system {
namespace {

bool matches(const QString& haystack, const QString& query)
{
    const QString folded = haystack.toCaseFolded();
    const auto terms = query.simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(terms.cbegin(), terms.cend(), [&folded](const QString& term) {
        return folded.contains(term);
    });
}

QString titleFor(const Command& command)
{
    return command.title.isEmpty() ? command.id : command.title;
}

QLabel* descriptionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setProperty("role", QStringLiteral("hint"));
    return label;
}

QVBoxLayout* pageLayout(QWidget* page, const QString& heading, const QString& description)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);
    auto* title = new QLabel(heading, page);
    title->setProperty("role", QStringLiteral("heading"));
    QFont font = title->font();
    font.setPointSizeF(font.pointSizeF() + 4);
    title->setFont(font);
    layout->addWidget(title);
    layout->addWidget(descriptionLabel(description, page));
    return layout;
}

} // namespace

SettingsPage::SettingsPage(WindowManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    setObjectName(QStringLiteral("SettingsPage"));
    setProperty("windowSystemSettingsKind", QStringLiteral("window_system.settings"));
    auto* layout = pageLayout(this, QStringLiteral("设置"),
        QStringLiteral("修改立即生效。窗口行为随工作区布局保存。"));
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("SettingsSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索设置名称、说明或设置 ID…"));
    search_->setAccessibleName(QStringLiteral("搜索设置"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);
    auto* body = new QHBoxLayout;
    body->setSpacing(24);
    categories_ = new QListWidget(this);
    categories_->setObjectName(QStringLiteral("SettingsCategories"));
    categories_->setAccessibleName(QStringLiteral("设置分类"));
    categories_->addItems({QStringLiteral("全部设置"), QStringLiteral("窗口"), QStringLiteral("拖拽")});
    categories_->setMaximumWidth(180);
    categories_->setMinimumWidth(90);
    categories_->setCurrentRow(0);
    body->addWidget(categories_);
    auto* right = new QVBoxLayout;
    results_ = descriptionLabel({}, this);
    results_->setObjectName(QStringLiteral("SettingsResultCount"));
    right->addWidget(results_);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("SettingsScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setObjectName(QStringLiteral("SettingsViewport"));
    scroll->viewport()->setAttribute(Qt::WA_StyledBackground);
    auto* contents = new QWidget(scroll);
    contents->setObjectName(QStringLiteral("SettingsRows"));
    contents->setAttribute(Qt::WA_StyledBackground);
    auto* rows = new QVBoxLayout(contents);
    rows->setContentsMargins(0, 0, 12, 0);
    rows->setSpacing(18);

    detachKind_ = new QComboBox(contents);
    detachKind_->setObjectName(QStringLiteral("DetachWindowKind"));
    detachKind_->setAccessibleName(QStringLiteral("拖出标签的窗口形态"));
    detachKind_->addItem(QStringLiteral("轻量浮窗（保留所属窗口）"), int(WindowKind::Floating));
    detachKind_->addItem(QStringLiteral("完整工作窗口"), int(WindowKind::Workbench));
    rows->addWidget(addRow(contents, QStringLiteral("窗口"), QStringLiteral("窗口：拖出标签的形态"),
        QStringLiteral("window.dragOutsideKind"),
        QStringLiteral("将标签拖出时使用轻量浮窗或完整工作窗口。"), detachKind_));

    emptyPolicy_ = new QComboBox(contents);
    emptyPolicy_->setObjectName(QStringLiteral("EmptyWorkbenchPolicy"));
    emptyPolicy_->setAccessibleName(QStringLiteral("完整窗口移空后的行为"));
    emptyPolicy_->addItem(QStringLiteral("保留空的完整窗口"), false);
    emptyPolicy_->addItem(QStringLiteral("自动关闭空的完整窗口"), true);
    rows->addWidget(addRow(contents, QStringLiteral("窗口"), QStringLiteral("窗口：移空后的行为"),
        QStringLiteral("window.closeEmptyWorkbench"),
        QStringLiteral("如果还有从属轻量浮窗，完整窗口会继续保留。"), emptyPolicy_));

    reveal_ = new QCheckBox(QStringLiteral("悬停时抬起被遮挡的目标窗口"), contents);
    reveal_->setObjectName(QStringLiteral("RevealDuringDrag"));
    rows->addWidget(addRow(contents, QStringLiteral("拖拽"), QStringLiteral("拖拽：显示目标窗口"),
        QStringLiteral("window.revealDuringDrag"),
        QStringLiteral("拖动标签停留在目标窗口位置时，显示该窗口以便投放。"), reveal_));

    delay_ = new QSpinBox(contents);
    delay_->setObjectName(QStringLiteral("RevealDelayMs"));
    delay_->setAccessibleName(QStringLiteral("显示拖拽目标的悬停时间"));
    delay_->setRange(200, 2000);
    delay_->setSingleStep(100);
    delay_->setSuffix(QStringLiteral(" 毫秒"));
    rows->addWidget(addRow(contents, QStringLiteral("拖拽"), QStringLiteral("拖拽：悬停时间"),
        QStringLiteral("window.revealDelayMs"),
        QStringLiteral("显示被遮挡窗口前的等待时间。"), delay_));
    rows->addStretch();
    scroll->setWidget(contents);
    right->addWidget(scroll, 1);
    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    const auto change = [this] {
        if (!manager_) return;
        auto next = manager_->behavior();
        next.dragOutsideKind = WindowKind(detachKind_->currentData().toInt());
        next.closeEmptyWorkbench = emptyPolicy_->currentData().toBool();
        next.revealDuringDrag = reveal_->isChecked();
        next.revealDelayMs = delay_->value();
        manager_->setBehavior(next);
        syncControls();
    };
    QObject::connect(detachKind_, &QComboBox::currentIndexChanged, this, change);
    QObject::connect(emptyPolicy_, &QComboBox::currentIndexChanged, this, change);
    QObject::connect(reveal_, &QCheckBox::toggled, this, change);
    QObject::connect(delay_, &QSpinBox::valueChanged, this, change);
    QObject::connect(search_, &QLineEdit::textChanged, this, [this] { filterRows(); });
    QObject::connect(categories_, &QListWidget::currentRowChanged, this, [this] { filterRows(); });
    QObject::connect(manager, &WindowManager::layoutChanged, this, [this] { syncControls(); });
    syncControls();
    filterRows();
    setFocusProxy(search_);
}

QWidget* SettingsPage::addRow(QWidget* parent, const QString& category,
                              const QString& title, const QString& key,
                              const QString& description, QWidget* control)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("SettingRow"));
    row->setProperty("settingId", key);
    auto* column = new QVBoxLayout(row);
    column->setContentsMargins(8, 4, 8, 8);
    column->setSpacing(6);
    auto* titleLabel = new QLabel(title, row);
    titleLabel->setProperty("role", QStringLiteral("heading"));
    titleLabel->setBuddy(control);
    column->addWidget(titleLabel);
    column->addWidget(descriptionLabel(description, row));
    auto* controlRow = new QHBoxLayout;
    controlRow->addWidget(control);
    controlRow->addStretch();
    column->addLayout(controlRow);
    auto* idLabel = descriptionLabel(key, row);
    idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(idLabel);
    rows_.push_back({category, category + QLatin1Char(' ') + title + QLatin1Char(' ') +
                              key + QLatin1Char(' ') + description, row});
    return row;
}

void SettingsPage::syncControls()
{
    if (!manager_) return;
    const auto behavior = manager_->behavior();
    const QSignalBlocker detachBlock(detachKind_);
    const QSignalBlocker emptyBlock(emptyPolicy_);
    const QSignalBlocker revealBlock(reveal_);
    const QSignalBlocker delayBlock(delay_);
    detachKind_->setCurrentIndex(behavior.dragOutsideKind == WindowKind::Floating ? 0 : 1);
    emptyPolicy_->setCurrentIndex(behavior.closeEmptyWorkbench ? 1 : 0);
    reveal_->setChecked(behavior.revealDuringDrag);
    delay_->setValue(behavior.revealDelayMs);
    delay_->setEnabled(behavior.revealDuringDrag);
}

void SettingsPage::filterRows()
{
    const auto* category = categories_->currentItem();
    const QString selected = categories_->currentRow() <= 0 || !category ? QString() : category->text();
    int count = 0;
    for (const auto& row : rows_) {
        const bool visible = (selected.isEmpty() || row.category == selected) &&
                             matches(row.searchable, search_->text());
        if (row.widget) row.widget->setVisible(visible);
        if (visible) ++count;
    }
    results_->setText(count ? QStringLiteral("%1 项设置").arg(count) : QStringLiteral("没有匹配的设置"));
}

ShortcutsPage::ShortcutsPage(WindowManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    setObjectName(QStringLiteral("ShortcutsPage"));
    setProperty("windowSystemSettingsKind", QStringLiteral("window_system.shortcuts"));
    auto* layout = pageLayout(this, QStringLiteral("键盘快捷键"),
        QStringLiteral("搜索命令并修改按键。当前修改在本次运行中生效。"));
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("ShortcutSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索全部命令、分类、命令 ID 或快捷键…"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);
    commands_ = new QTreeWidget(this);
    commands_->setObjectName(QStringLiteral("ShortcutCommands"));
    commands_->setColumnCount(3);
    commands_->setHeaderLabels({QStringLiteral("命令"), QStringLiteral("快捷键"), QStringLiteral("分类")});
    commands_->setRootIsDecorated(false);
    commands_->setUniformRowHeights(true);
    commands_->setAlternatingRowColors(true);
    commands_->setSelectionMode(QAbstractItemView::SingleSelection);
    commands_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    commands_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    commands_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(commands_, 1);
    selectedTitle_ = new QLabel(this);
    selectedTitle_->setWordWrap(true);
    layout->addWidget(selectedTitle_);
    capture_ = new QKeySequenceEdit(this);
    capture_->setObjectName(QStringLiteral("ShortcutCapture"));
    capture_->setAccessibleName(QStringLiteral("录入命令快捷键"));
    capture_->setMaximumSequenceLength(4);
    layout->addWidget(capture_);
    diagnostic_ = descriptionLabel({}, this);
    diagnostic_->setObjectName(QStringLiteral("ShortcutDiagnostic"));
    layout->addWidget(diagnostic_);
    auto* actions = new QHBoxLayout;
    apply_ = new QPushButton(QStringLiteral("应用快捷键"), this);
    apply_->setObjectName(QStringLiteral("ApplyShortcut"));
    clear_ = new QPushButton(QStringLiteral("清除绑定"), this);
    clear_->setObjectName(QStringLiteral("ClearShortcut"));
    actions->addWidget(apply_);
    actions->addWidget(clear_);
    actions->addStretch();
    layout->addLayout(actions);
    QObject::connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    QObject::connect(commands_, &QTreeWidget::currentItemChanged, this, [this] { selectCommand(); });
    QObject::connect(commands_, &QTreeWidget::itemActivated, this, [this] { capture_->setFocus(); });
    QObject::connect(capture_, &QKeySequenceEdit::keySequenceChanged, this, [this] { updateDiagnostic(); });
    QObject::connect(apply_, &QPushButton::clicked, this, [this] {
        updateDiagnostic();
        if (manager_ && apply_->isEnabled() && !selectedId_.isEmpty() &&
            manager_->commands().setShortcut(selectedId_, capture_->keySequence())) rebuild();
    });
    QObject::connect(clear_, &QPushButton::clicked, this, [this] {
        if (manager_ && !selectedId_.isEmpty() && manager_->commands().setShortcut(selectedId_, {})) rebuild();
    });
    rebuild();
    setFocusProxy(search_);
}

void ShortcutsPage::rebuild()
{
    const QString previous = selectedId_;
    const QString query = search_->text().trimmed();
    // A complete command ID is an unambiguous lookup. Broad text queries still
    // match titles, categories and shortcuts, including partial command IDs.
    const bool exactCommand = manager_ && manager_->commands().find(query);
    {
        const QSignalBlocker blocker(commands_);
        commands_->clear();
        if (manager_) for (const auto& command : manager_->commands().commands()) {
            const QString shortcut = command.shortcut.toString(QKeySequence::NativeText);
            if (exactCommand && command.id != query) continue;
            if (!matches(titleFor(command) + QLatin1Char(' ') + command.category + QLatin1Char(' ') +
                         command.id + QLatin1Char(' ') + shortcut, query)) continue;
            auto* item = new QTreeWidgetItem(commands_, {titleFor(command), shortcut, command.category});
            item->setData(0, Qt::UserRole, command.id);
            item->setToolTip(0, command.id);
            if (command.id == previous) commands_->setCurrentItem(item);
        }
        if (!commands_->currentItem() && commands_->topLevelItemCount())
            commands_->setCurrentItem(commands_->topLevelItem(0));
    }
    selectCommand();
}

void ShortcutsPage::selectCommand()
{
    const auto* current = commands_->currentItem();
    selectedId_ = current ? current->data(0, Qt::UserRole).toString() : QString();
    const auto* command = manager_ ? manager_->commands().find(selectedId_) : nullptr;
    {
        const QSignalBlocker blocker(capture_);
        capture_->setKeySequence(command ? command->shortcut : QKeySequence());
    }
    selectedTitle_->setText(command ? titleFor(*command) + QStringLiteral("  ·  ") + command->id :
                            QStringLiteral("没有匹配的命令"));
    updateDiagnostic();
}

void ShortcutsPage::updateDiagnostic()
{
    const bool selected = manager_ && !selectedId_.isEmpty();
    capture_->setEnabled(selected);
    clear_->setEnabled(selected);
    apply_->setEnabled(selected);
    if (!selected) { diagnostic_->clear(); return; }
    const auto sequence = capture_->keySequence();
    QStringList duplicates, prefixes;
    for (const auto& command : manager_->commands().commands()) {
        if (command.id == selectedId_ || command.shortcut.isEmpty() || sequence.isEmpty()) continue;
        const auto forward = sequence.matches(command.shortcut);
        const auto backward = command.shortcut.matches(sequence);
        if (forward == QKeySequence::ExactMatch) duplicates.push_back(titleFor(command));
        else if (forward == QKeySequence::PartialMatch || backward == QKeySequence::PartialMatch)
            prefixes.push_back(titleFor(command));
    }
    apply_->setEnabled(duplicates.isEmpty());
    if (!duplicates.isEmpty()) {
        diagnostic_->setText(QStringLiteral("此快捷键已绑定到：%1。请先清除其他绑定。").arg(duplicates.join(QStringLiteral("、"))));
    } else if (!prefixes.isEmpty()) {
        diagnostic_->setText(QStringLiteral("此按键序列与以下命令有前缀关系：%1。").arg(prefixes.join(QStringLiteral("、"))));
    } else {
        diagnostic_->setText(sequence.isEmpty() ? QStringLiteral("应用后将清除绑定；命令仍可通过菜单和命令面板执行。") :
                             QStringLiteral("可录入最多 4 段按键序列；点击应用后立即生效。"));
    }
}

} // namespace window_system
