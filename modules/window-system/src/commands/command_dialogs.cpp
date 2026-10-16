// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "command_dialogs.hpp"
#include "../settings/settings_page.hpp"

#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace window_system {
namespace {

QString displayTitle(const Command& command)
{
    return command.title.isEmpty() ? command.id : command.title;
}

QString shortcutText(const Command& command)
{
    QStringList sequences;
    for (const auto& binding : command.bindings)
        sequences.append(binding.sequence.toString(QKeySequence::NativeText));
    return sequences.join(QStringLiteral(" / "));
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

FloatingPanel* createDialog(WindowManager* manager, QWidget* parent,
                             const QString& name, const QString& title, QSize size,
                             const FloatingPanelOptions& options)
{
    auto* dialog = new FloatingPanel(parent, options);
    dialog->setObjectName(name);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
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
            if (tree_) tree_->scrollToItem(tree_->currentItem());
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

void openCommandPalette(WindowManager* manager, const CommandContext& context, QWidget* parent,
                        const FloatingPanelOptions& options)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    const CommandContext target = context;
    auto* dialog = createDialog(manager, parent, QStringLiteral("CommandPalette"),
                                QCoreApplication::translate("WindowSystem", "命令面板"), QSize(780, 450), options);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("CommandPaletteSearch"));
    search->setPlaceholderText(QCoreApplication::translate("WindowSystem", "输入命令名称、分类或命令 ID…"));
    search->setAccessibleName(QCoreApplication::translate("WindowSystem", "搜索命令"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QCoreApplication::translate("WindowSystem", "命令"), QCoreApplication::translate("WindowSystem", "分类"), QCoreApplication::translate("WindowSystem", "快捷键")});
    tree->setObjectName(QStringLiteral("CommandPaletteCommands"));
    layout->addWidget(tree, 1);
    auto* details = hintLabel(QCoreApplication::translate("WindowSystem", "↑ ↓ 选择  ·  Enter 执行  ·  Esc 关闭"), dialog);
    layout->addWidget(details);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("WindowSystem", "执行"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("WindowSystem", "关闭"));
    layout->addWidget(buttons);
    new SearchListNavigation(search, tree);

    const QPointer<QTreeWidget> retainedTree(tree);
    const QPointer<QDialogButtonBox> retainedButtons(buttons);
    const QPointer<QLabel> retainedDetails(details);
    const QPointer<QLineEdit> retainedSearch(search);
    const QPointer<FloatingPanel> retainedDialog(dialog);
    const auto updateSelected = [alive, tree = retainedTree, buttons = retainedButtons,
                                 details = retainedDetails, target] {
        if (!tree || !buttons || !details) return;
        const auto* item = tree->currentItem();
        const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
        const auto* found = alive ? alive->commands().find(id) : nullptr;
        const Command command = found ? *found : Command{};
        const bool enabled = found && canExecute(command, target);
        if (!alive || !tree || !buttons || !details) return;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(enabled);
        details->setText(found ? command.id : QCoreApplication::translate("WindowSystem", "没有匹配的可执行命令。"));
    };
    const auto rebuild = [alive, tree = retainedTree, search = retainedSearch, target, updateSelected] {
        if (!tree || !search) return;
        tree->clear();
        if (!alive) return;
        const QString query = search->text();
        const auto commands = alive->commands().commands();
        for (const auto& command : commands) {
            const QString category = QCoreApplication::translate("WindowSystem", command.category.toUtf8().constData());
            const QString searchable = displayTitle(command) + QLatin1Char(' ') + category + QLatin1Char(' ') + command.id;
            if (!matchesSearch(searchable, query)) continue;
            const bool enabled = canExecute(command, target);
            if (!alive || !tree || !search) return;
            auto* item = new QTreeWidgetItem(tree, {displayTitle(command), category, shortcutText(command)});
            item->setData(0, Qt::UserRole, command.id);
            item->setToolTip(0, command.id);
            if (!enabled) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setToolTip(0, command.id + QCoreApplication::translate("WindowSystem", "\n当前窗口或内容不满足执行条件。"));
            }
        }
        selectFirstEnabled(tree);
        updateSelected();
    };
    const auto accept = [alive, tree = retainedTree, target, dialog = retainedDialog] {
        if (!tree || !dialog) return;
        const auto* item = tree->currentItem();
        if (!alive || !item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const auto* found = alive->commands().find(id);
        if (!found) return;
        const Command command = *found;
        const bool enabled = canExecute(command, target);
        if (enabled && alive && tree && dialog) {
            dialog->setProperty("selectedCommand", id);
            dialog->accept();
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, rebuild);
    QObject::connect(search, &QLineEdit::returnPressed, dialog, accept);
    QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog, updateSelected);
    QObject::connect(tree, &QTreeWidget::itemActivated, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(dialog, &QDialog::finished, dialog, [alive, target, dialog](int result) {
        const auto id = dialog->property("selectedCommand").toString();
        if (result != QDialog::Accepted || !alive || id.isEmpty()) return;
        // Resolve after hiding the panel, while retaining the IDs captured
        // before its search field obtained focus. Commands may close the owner.
        QTimer::singleShot(0, alive, [alive, target, id] {
            if (!alive) return;
            const bool executed = alive->executeInContext(id, target);
            if (!executed && alive && alive->model().window(target.window))
                alive->setStatus(target.window, QCoreApplication::translate("WindowSystem", "命令未执行：%1").arg(id));
        });
    });
    rebuild();
    if (!retainedDialog || !alive) return;
    positionDialog(dialog, parent);
    dialog->show();
    search->setFocus();
}

void openWindowSelector(WindowManager* manager, WindowId source, QWidget* parent,
                        const FloatingPanelOptions& options)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    auto* dialog = createDialog(manager, parent, QStringLiteral("WindowSelector"),
                                QCoreApplication::translate("WindowSystem", "切换窗口"), QSize(780, 410), options);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("WindowSelectorSearch"));
    search->setPlaceholderText(QCoreApplication::translate("WindowSystem", "搜索窗口、所属窗口或形态…"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QCoreApplication::translate("WindowSystem", "窗口"), QCoreApplication::translate("WindowSystem", "形态"), QCoreApplication::translate("WindowSystem", "所属完整窗口"), QCoreApplication::translate("WindowSystem", "标签")});
    tree->setObjectName(QStringLiteral("WindowSelectorWindows"));
    layout->addWidget(tree, 1);
    layout->addWidget(hintLabel(QCoreApplication::translate("WindowSystem", "包含所有完整窗口和轻量浮窗。Enter 切换；Esc 取消。"), dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("WindowSystem", "切换"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("WindowSystem", "取消"));
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
            const QString title = widget ? widget->windowTitle() : QCoreApplication::translate("WindowSystem", "工作区窗口");
            const QString kind = record->kind == WindowKind::Workbench ? QCoreApplication::translate("WindowSystem", "完整窗口") : QCoreApplication::translate("WindowSystem", "轻量浮窗");
            const WindowId rootId = model.familyRoot(record->familyId);
            const auto* ownerWidget = alive->windowWidget(rootId);
            const QString owner = record->kind == WindowKind::Workbench ? QStringLiteral("—") :
                (ownerWidget ? ownerWidget->windowTitle() : QCoreApplication::translate("WindowSystem", "所属完整窗口"));
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
    const auto accept = [alive, tree, dialog] {
        if (!alive || !tree->currentItem()) return;
        const WindowId id = tree->currentItem()->data(0, Qt::UserRole).toUuid();
        if (alive->model().window(id)) {
            dialog->setProperty("selectedWindow", id);
            dialog->accept();
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, rebuild);
    QObject::connect(manager, &WindowManager::layoutChanged, dialog, rebuild);
    QObject::connect(tree, &QTreeWidget::itemActivated, dialog, accept);
    QObject::connect(search, &QLineEdit::returnPressed, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(dialog, &QDialog::finished, dialog, [alive, dialog](int result) {
        const auto target = dialog->property("selectedWindow").toUuid();
        if (result != QDialog::Accepted || !alive || target.isNull()) return;
        QTimer::singleShot(0, alive, [alive, target] { if (alive) alive->activateWindow(target); });
    });
    rebuild();
    positionDialog(dialog, parent);
    dialog->show();
    search->setFocus();
}

namespace {

void openSettingsTab(WindowManager* manager, QWidget* parent, const QString& type,
                     const QString& title)
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    // Built-in preferences are ordinary workspace content. Search stable pane
    // IDs instead of keeping a weak tab cache that could become stale on moves.
    for (const auto& record : manager->model().tabs()) {
        for (const auto pane : manager->model().panes(record.id)) {
            auto* widget = manager->paneWidget(pane);
            if (!widget || widget->property("windowSystemSettingsKind").toString() != type) continue;
            const auto* group = manager->model().group(manager->model().groupForTab(record.id));
            if (!group) continue;
            const WindowId targetWindow = group->windowId;
            const TabId targetTab = record.id;
            const PaneId targetPane = pane;
            manager->showWindow(targetWindow);
            if (!alive) return;
            alive->activateTab(targetTab);
            if (!alive) return;
            alive->focusPane(targetTab, targetPane);
            return;
        }
    }
    const auto window = parent ? parent->window()->property("windowId").toUuid() : WindowId{};
    const auto context = manager->context(window);
    if (!manager->model().window(context.window)) return;
    const TabOptions options{title, type, {}};
    auto* page = createInternalSettingsContent(manager, options);
    if (!page) return;
    if (manager->addTab(context.window, page, options, context.group).isNull()) delete page;
}

} // namespace

bool isInternalSettingsContentType(const QString& type)
{
    return type == QStringLiteral("window_system.settings") ||
           type == QStringLiteral("window_system.shortcuts");
}

QWidget* createInternalSettingsContent(WindowManager* manager, const TabOptions& options)
{
    if (!manager) return nullptr;
    if (options.contentType == QStringLiteral("window_system.settings")) return new SettingsPage(manager);
    if (options.contentType == QStringLiteral("window_system.shortcuts")) return new ShortcutsPage(manager);
    return nullptr;
}

void openBehaviorSettings(WindowManager* manager, QWidget* parent)
{
    openSettingsTab(manager, parent, QStringLiteral("window_system.settings"), QCoreApplication::translate("WindowSystem", "设置"));
}

void openShortcutEditor(WindowManager* manager, QWidget* parent)
{
    openSettingsTab(manager, parent, QStringLiteral("window_system.shortcuts"), QCoreApplication::translate("WindowSystem", "键盘快捷键"));
}

} // namespace window_system
