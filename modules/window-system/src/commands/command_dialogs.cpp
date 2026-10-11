// SPDX-License-Identifier: MIT
#include "command_dialogs.hpp"

#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace window_system {
namespace {

QString displayTitle(const Command& command)
{
    return command.title.isEmpty() ? command.id : command.title;
}

QString shortcutText(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::NativeText);
}

bool matchesSearch(const QString& searchable, const QString& query)
{
    const QString folded = searchable.toCaseFolded();
    const auto terms = query.simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(terms.cbegin(), terms.cend(), [&folded](const QString& term) {
        return folded.contains(term);
    });
}

bool canExecute(const Command& command, const CommandContext& context)
{
    return command.execute && (!command.enabled || command.enabled(context));
}

QDialog* createDialog(WindowManager* manager, QWidget* parent,
                      const QString& name, const QString& title, QSize size, bool palette = false)
{
    auto* dialog = new QDialog(parent);
    dialog->setObjectName(name);
    dialog->setWindowTitle(title);
    dialog->setWindowModality(Qt::WindowModal);
    if (palette) dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog->resize(size);
    dialog->setMinimumSize(420, 240);
    applyTheme(dialog, manager->theme());
    QObject::connect(manager, &QObject::destroyed, dialog, &QDialog::reject);
    return dialog;
}

void positionDialog(QDialog* dialog, QWidget* parent)
{
    const QPoint anchor = parent ? parent->mapToGlobal(parent->rect().center()) : QPoint();
    QScreen* screen = parent ? QGuiApplication::screenAt(anchor) : QGuiApplication::primaryScreen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect available = screen->availableGeometry();
    const QSize size(std::min(dialog->width(), available.width()),
                     std::min(dialog->height(), available.height()));
    dialog->resize(size);
    QPoint position = (parent ? anchor : available.center()) - QPoint(size.width() / 2, size.height() / 2);
    position.setX(std::clamp(position.x(), available.left(), available.right() - size.width() + 1));
    position.setY(std::clamp(position.y(), available.top(), available.bottom() - size.height() + 1));
    dialog->move(position);
}

int runDialog(QDialog* dialog, QWidget* parent)
{
    // A root window may be removed while a modal loop is active. Heap ownership
    // plus QPointer handles parent deletion without deleting a stack QObject.
    const QPointer<QDialog> alive(dialog);
    positionDialog(dialog, parent);
    const int result = dialog->exec();
    if (alive) delete alive.data();
    return result;
}

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", QStringLiteral("hint"));
    label->setWordWrap(true);
    return label;
}

QTreeWidget* createList(QWidget* parent, const QStringList& headers)
{
    auto* tree = new QTreeWidget(parent);
    tree->setColumnCount(headers.size());
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setItemsExpandable(false);
    tree->setAlternatingRowColors(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setUniformRowHeights(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int index = 1; index < headers.size(); ++index) {
        tree->header()->setSectionResizeMode(index, QHeaderView::ResizeToContents);
    }
    return tree;
}

class SearchListNavigation final : public QObject {
public:
    SearchListNavigation(QLineEdit* search, QTreeWidget* tree)
        : QObject(search), tree_(tree)
    {
        search->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!tree_ || event->type() != QEvent::KeyPress) return QObject::eventFilter(watched, event);
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() != Qt::NoModifier) return QObject::eventFilter(watched, event);
        int step = 0;
        switch (key->key()) {
        case Qt::Key_Down: step = 1; break;
        case Qt::Key_Up: step = -1; break;
        case Qt::Key_PageDown: step = 8; break;
        case Qt::Key_PageUp: step = -8; break;
        default: return QObject::eventFilter(watched, event);
        }
        const int count = tree_->topLevelItemCount();
        if (!count) return true;
        int row = tree_->indexOfTopLevelItem(tree_->currentItem());
        if (row < 0) row = step > 0 ? -1 : count;
        const int direction = step > 0 ? 1 : -1;
        int remaining = std::abs(step);
        int selected = -1;
        while (row + direction >= 0 && row + direction < count) {
            row += direction;
            if (tree_->topLevelItem(row)->flags().testFlag(Qt::ItemIsEnabled)) {
                selected = row;
                if (--remaining == 0) break;
            }
        }
        if (selected >= 0) {
            tree_->setCurrentItem(tree_->topLevelItem(selected));
            tree_->scrollToItem(tree_->currentItem());
        }
        return true;
    }

private:
    QPointer<QTreeWidget> tree_;
};

void selectFirstEnabled(QTreeWidget* tree)
{
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        auto* item = tree->topLevelItem(index);
        if (item->flags().testFlag(Qt::ItemIsEnabled)) {
            tree->setCurrentItem(item);
            return;
        }
    }
}

} // namespace

void openCommandPalette(WindowManager* manager, const CommandContext& context, QWidget* parent)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    const CommandContext target = context;
    QString selectedCommand;
    auto* dialog = createDialog(manager, parent, QStringLiteral("CommandPalette"),
                                QStringLiteral("命令面板"), QSize(780, 450), true);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("CommandPaletteSearch"));
    search->setPlaceholderText(QStringLiteral("输入命令名称、分类或命令 ID…"));
    search->setAccessibleName(QStringLiteral("搜索命令"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QStringLiteral("命令"), QStringLiteral("分类"), QStringLiteral("快捷键")});
    tree->setObjectName(QStringLiteral("CommandPaletteCommands"));
    layout->addWidget(tree, 1);
    auto* details = hintLabel(QStringLiteral("↑ ↓ 选择  ·  Enter 执行  ·  Esc 关闭"), dialog);
    layout->addWidget(details);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("执行"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("关闭"));
    layout->addWidget(buttons);
    new SearchListNavigation(search, tree);

    const auto updateSelected = [alive, tree, buttons, details, target] {
        const auto* item = tree->currentItem();
        const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
        const auto* command = alive ? alive->commands().find(id) : nullptr;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(command && canExecute(*command, target));
        details->setText(command ? command->id : QStringLiteral("没有匹配的可执行命令。"));
    };
    const auto rebuild = [alive, tree, search, target, updateSelected] {
        tree->clear();
        if (!alive) return;
        const QString query = search->text();
        const auto commands = alive->commands().commands();
        for (const auto& command : commands) {
            const QString searchable = displayTitle(command) + QLatin1Char(' ') + command.category + QLatin1Char(' ') + command.id;
            if (!matchesSearch(searchable, query)) continue;
            auto* item = new QTreeWidgetItem(tree, {displayTitle(command), command.category, shortcutText(command.shortcut)});
            item->setData(0, Qt::UserRole, command.id);
            item->setToolTip(0, command.id);
            if (!canExecute(command, target)) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setToolTip(0, command.id + QStringLiteral("\n当前窗口或内容不满足执行条件。"));
            }
        }
        selectFirstEnabled(tree);
        updateSelected();
    };
    const auto accept = [alive, tree, target, dialog, &selectedCommand] {
        const auto* item = tree->currentItem();
        if (!alive || !item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const auto* command = alive->commands().find(id);
        if (command && canExecute(*command, target)) {
            selectedCommand = id;
            dialog->accept();
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, rebuild);
    QObject::connect(search, &QLineEdit::returnPressed, dialog, accept);
    QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog, updateSelected);
    QObject::connect(tree, &QTreeWidget::itemActivated, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    rebuild();
    search->setFocus();
    if (runDialog(dialog, parent) != QDialog::Accepted || !alive || selectedCommand.isEmpty()) return;
    // The dialog no longer owns focus; execution nevertheless targets the IDs
    // captured before opening it, rather than whichever widget Qt just focused.
    const bool executed = alive->commands().execute(selectedCommand, target);
    if (!executed && alive && alive->model().window(target.window)) {
        alive->setStatus(target.window, QStringLiteral("命令未执行：%1").arg(selectedCommand));
    }
}

void openWindowSelector(WindowManager* manager, WindowId source, QWidget* parent)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    WindowId selected;
    auto* dialog = createDialog(manager, parent, QStringLiteral("WindowSelector"),
                                QStringLiteral("切换窗口"), QSize(780, 410), true);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("WindowSelectorSearch"));
    search->setPlaceholderText(QStringLiteral("搜索窗口、所属窗口或形态…"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QStringLiteral("窗口"), QStringLiteral("形态"), QStringLiteral("所属完整窗口"), QStringLiteral("标签")});
    tree->setObjectName(QStringLiteral("WindowSelectorWindows"));
    layout->addWidget(tree, 1);
    layout->addWidget(hintLabel(QStringLiteral("包含所有完整窗口和轻量浮窗。Enter 切换；Esc 取消。"), dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("切换"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    new SearchListNavigation(search, tree);
    const auto rebuild = [alive, tree, search, buttons, source] {
        WindowId previous = source;
        if (tree->currentItem()) previous = tree->currentItem()->data(0, Qt::UserRole).toUuid();
        tree->clear();
        if (!alive) return;
        const auto& model = alive->model();
        for (const auto id : model.windowOrder()) {
            const auto* record = model.window(id);
            if (!record) continue;
            const auto* widget = alive->windowWidget(id);
            const QString title = widget ? widget->windowTitle() : QStringLiteral("工作区窗口");
            const QString kind = record->kind == WindowKind::Workbench ? QStringLiteral("完整窗口") : QStringLiteral("轻量浮窗");
            const WindowId rootId = model.familyRoot(record->familyId);
            const auto* ownerWidget = alive->windowWidget(rootId);
            const QString owner = record->kind == WindowKind::Workbench ? QStringLiteral("—") :
                (ownerWidget ? ownerWidget->windowTitle() : QStringLiteral("所属完整窗口"));
            if (!matchesSearch(title + QLatin1Char(' ') + kind + QLatin1Char(' ') + owner + QLatin1Char(' ') + id.toString(), search->text())) continue;
            auto* item = new QTreeWidgetItem(tree, {title, kind, owner, QString::number(model.orderedTabs(id).size())});
            item->setData(0, Qt::UserRole, id);
            item->setToolTip(0, id.toString(QUuid::WithoutBraces));
            item->setIcon(0, iconFor(record->kind == WindowKind::Workbench ? QStringLiteral("window") : QStringLiteral("detach"), alive->theme()));
            if (id == previous) tree->setCurrentItem(item);
        }
        if (!tree->currentItem()) selectFirstEnabled(tree);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(tree->currentItem() != nullptr);
    };
    const auto accept = [alive, tree, dialog, &selected] {
        if (!alive || !tree->currentItem()) return;
        const WindowId id = tree->currentItem()->data(0, Qt::UserRole).toUuid();
        if (alive->model().window(id)) {
            selected = id;
            dialog->accept();
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, rebuild);
    QObject::connect(manager, &WindowManager::layoutChanged, dialog, rebuild);
    QObject::connect(tree, &QTreeWidget::itemActivated, dialog, accept);
    QObject::connect(search, &QLineEdit::returnPressed, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    rebuild();
    search->setFocus();
    if (runDialog(dialog, parent) != QDialog::Accepted || !alive) return;
    const auto* record = alive->model().window(selected);
    if (!record) return;
    TabId activeTab;
    if (const auto* group = alive->model().group(record->activeGroup)) activeTab = group->activeTab;
    if (activeTab.isNull()) {
        const auto tabs = alive->model().orderedTabs(selected);
        if (!tabs.isEmpty()) activeTab = tabs.front();
    }
    alive->showWindow(selected);
    const QPointer<QWidget> widget = alive->windowWidget(selected);
    if (!widget) return;
    if (widget->isMinimized()) widget->showNormal();
    widget->raise();
    widget->activateWindow();
    if (!activeTab.isNull()) alive->activateTab(activeTab);
    else widget->setFocus(Qt::ActiveWindowFocusReason);
}

void openBehaviorSettings(WindowManager* manager, QWidget* parent)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    WindowBehavior next = manager->behavior();
    auto* dialog = createDialog(manager, parent, QStringLiteral("BehaviorSettings"),
                                QStringLiteral("窗口预览设置"), QSize(590, 340));
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 12, 16, 12);
    auto* intro = hintLabel(QStringLiteral("设置作用于窗口系统；保存和恢复由宿主应用负责。"), dialog);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* detachKind = new QComboBox(dialog);
    detachKind->setObjectName(QStringLiteral("DetachWindowKind"));
    detachKind->addItem(QStringLiteral("轻量浮窗（保留所属窗口）"), int(WindowKind::Floating));
    detachKind->addItem(QStringLiteral("完整工作窗口"), int(WindowKind::Workbench));
    detachKind->setCurrentIndex(next.dragOutsideKind == WindowKind::Floating ? 0 : 1);
    form->addRow(QStringLiteral("标签拖到窗口外"), detachKind);
    auto* emptyPolicy = new QComboBox(dialog);
    emptyPolicy->setObjectName(QStringLiteral("EmptyWorkbenchPolicy"));
    emptyPolicy->addItem(QStringLiteral("保留空的完整窗口"), false);
    emptyPolicy->addItem(QStringLiteral("自动关闭空的完整窗口"), true);
    emptyPolicy->setCurrentIndex(next.closeEmptyWorkbench ? 1 : 0);
    form->addRow(QStringLiteral("完整窗口移空后"), emptyPolicy);
    auto* reveal = new QCheckBox(QStringLiteral("拖拽停留时抬起被遮挡的目标窗口"), dialog);
    reveal->setObjectName(QStringLiteral("RevealDuringDrag"));
    reveal->setChecked(next.revealDuringDrag);
    form->addRow(QStringLiteral("跨窗口投放"), reveal);
    auto* delay = new QSpinBox(dialog);
    delay->setObjectName(QStringLiteral("RevealDelayMs"));
    delay->setRange(200, 2000);
    delay->setSingleStep(100);
    delay->setSuffix(QStringLiteral(" 毫秒"));
    delay->setValue(std::clamp(next.revealDelayMs, 200, 2000));
    delay->setEnabled(next.revealDuringDrag);
    form->addRow(QStringLiteral("停留时间"), delay);
    QObject::connect(reveal, &QCheckBox::toggled, delay, &QSpinBox::setEnabled);
    layout->addLayout(form);
    layout->addWidget(hintLabel(QStringLiteral("仍有所属轻量浮窗时，完整窗口不会因为移空而自动关闭。"), dialog));
    layout->addStretch();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, detachKind, emptyPolicy, reveal, delay, &next] {
        next.dragOutsideKind = WindowKind(detachKind->currentData().toInt());
        next.closeEmptyWorkbench = emptyPolicy->currentData().toBool();
        next.revealDuringDrag = reveal->isChecked();
        next.revealDelayMs = delay->value();
        dialog->accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    if (runDialog(dialog, parent) == QDialog::Accepted && alive) alive->setBehavior(next);
}

void openShortcutEditor(WindowManager* manager, QWidget* parent)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    auto* dialog = createDialog(manager, parent, QStringLiteral("ShortcutEditor"),
                                QStringLiteral("键盘快捷键"), QSize(820, 570));
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("ShortcutSearch"));
    search->setPlaceholderText(QStringLiteral("搜索全部命令、分类、命令 ID 或快捷键…"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QStringLiteral("命令"), QStringLiteral("快捷键"), QStringLiteral("分类")});
    tree->setObjectName(QStringLiteral("ShortcutCommands"));
    layout->addWidget(tree, 1);
    auto* selectedTitle = new QLabel(QStringLiteral("选择需要修改的命令"), dialog);
    selectedTitle->setProperty("role", QStringLiteral("heading"));
    layout->addWidget(selectedTitle);
    auto* capture = new QKeySequenceEdit(dialog);
    capture->setObjectName(QStringLiteral("ShortcutCapture"));
    capture->setAccessibleName(QStringLiteral("录入命令快捷键"));
    capture->setMaximumSequenceLength(4);
    layout->addWidget(capture);
    auto* diagnostic = hintLabel(QStringLiteral("可录入最多 4 段按键序列；修改立即作用于命令注册表。"), dialog);
    diagnostic->setObjectName(QStringLiteral("ShortcutDiagnostic"));
    layout->addWidget(diagnostic);
    auto* actions = new QHBoxLayout;
    auto* apply = new QPushButton(QStringLiteral("应用快捷键"), dialog);
    apply->setObjectName(QStringLiteral("ApplyShortcut"));
    auto* clear = new QPushButton(QStringLiteral("清除绑定"), dialog);
    actions->addWidget(apply);
    actions->addWidget(clear);
    actions->addStretch();
    auto* close = new QPushButton(QStringLiteral("关闭"), dialog);
    actions->addWidget(close);
    layout->addLayout(actions);
    new SearchListNavigation(search, tree);
    QString selectedId;

    const auto updateDiagnostic = [alive, capture, apply, clear, diagnostic, &selectedId] {
        if (!alive || selectedId.isEmpty()) {
            capture->setEnabled(false);
            apply->setEnabled(false);
            clear->setEnabled(false);
            return;
        }
        capture->setEnabled(true);
        clear->setEnabled(true);
        const QKeySequence sequence = capture->keySequence();
        QStringList duplicates;
        QStringList prefixes;
        for (const auto& command : alive->commands().commands()) {
            if (command.id == selectedId || command.shortcut.isEmpty() || sequence.isEmpty()) continue;
            const auto forward = sequence.matches(command.shortcut);
            const auto backward = command.shortcut.matches(sequence);
            if (forward == QKeySequence::ExactMatch) duplicates.push_back(displayTitle(command));
            else if (forward == QKeySequence::PartialMatch || backward == QKeySequence::PartialMatch) prefixes.push_back(displayTitle(command));
        }
        apply->setEnabled(duplicates.isEmpty());
        if (!duplicates.isEmpty()) {
            diagnostic->setText(QStringLiteral("此快捷键已绑定到：%1。请先清除其他绑定，避免执行目标不明确。").arg(duplicates.join(QStringLiteral("、"))));
        } else if (!prefixes.isEmpty()) {
            diagnostic->setText(QStringLiteral("此按键序列与以下命令有前缀关系：%1。请留意连续按键的触发顺序。").arg(prefixes.join(QStringLiteral("、"))));
        } else {
            diagnostic->setText(sequence.isEmpty() ? QStringLiteral("应用后将清除此命令的快捷键；命令仍可通过菜单和命令面板执行。") :
                                QStringLiteral("可录入最多 4 段按键序列；点击应用后立即生效。"));
        }
    };
    const auto updateSelected = [alive, tree, capture, selectedTitle, updateDiagnostic, &selectedId] {
        selectedId = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString();
        const auto* command = alive ? alive->commands().find(selectedId) : nullptr;
        const QSignalBlocker blocker(capture);
        capture->setKeySequence(command ? command->shortcut : QKeySequence());
        selectedTitle->setText(command ? displayTitle(*command) + QStringLiteral("  ·  ") + command->id : QStringLiteral("没有匹配的命令"));
        updateDiagnostic();
    };
    const auto rebuild = [alive, tree, search, updateSelected, &selectedId] {
        const QString previous = selectedId;
        const QSignalBlocker blocker(tree);
        tree->clear();
        if (alive) {
            for (const auto& command : alive->commands().commands()) {
                const QString searchable = displayTitle(command) + QLatin1Char(' ') + command.category + QLatin1Char(' ') + command.id + QLatin1Char(' ') + shortcutText(command.shortcut);
                if (!matchesSearch(searchable, search->text())) continue;
                auto* item = new QTreeWidgetItem(tree, {displayTitle(command), shortcutText(command.shortcut), command.category});
                item->setData(0, Qt::UserRole, command.id);
                item->setToolTip(0, command.id);
                if (command.id == previous) tree->setCurrentItem(item);
            }
        }
        if (!tree->currentItem()) selectFirstEnabled(tree);
        updateSelected();
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, rebuild);
    QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog, updateSelected);
    QObject::connect(capture, &QKeySequenceEdit::keySequenceChanged, dialog, updateDiagnostic);
    QObject::connect(tree, &QTreeWidget::itemActivated, dialog, [capture] { capture->setFocus(); });
    QObject::connect(apply, &QPushButton::clicked, dialog, [alive, capture, rebuild, updateDiagnostic, &selectedId] {
        updateDiagnostic();
        if (alive && !selectedId.isEmpty() && alive->commands().setShortcut(selectedId, capture->keySequence())) rebuild();
    });
    QObject::connect(clear, &QPushButton::clicked, dialog, [alive, rebuild, &selectedId] {
        if (alive && !selectedId.isEmpty() && alive->commands().setShortcut(selectedId, {})) rebuild();
    });
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    rebuild();
    search->setFocus();
    runDialog(dialog, parent);
}

} // namespace window_system
