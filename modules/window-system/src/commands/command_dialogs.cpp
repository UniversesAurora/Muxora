#include "style/ui_metrics.hpp"
// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "command_dialogs.hpp"
#include "panel_rows.hpp"
#include "../settings/settings_page.hpp"
#include "localization/display_names.hpp"

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

FloatingPanel* createDialog(WindowManager* manager, QWidget* parent,
                             const QString& name, const QString& title, QSize size,
                             const FloatingPanelOptions& options)
{
    auto* dialog = new FloatingPanel(parent, options);
    dialog->setObjectName(name);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(size);
    uiMinimumSize(dialog, 420, 240);
    applyTheme(dialog, manager->interfaceTheme());
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
    tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
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
                                QCoreApplication::translate("WindowSystem", "Command palette"), QSize(780, 450), options);
    auto* layout = new QVBoxLayout(dialog);
    uiMargins(layout, 12, 12, 12, 12);
    uiSpacing(layout, 8);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("CommandPaletteSearch"));
    search->setPlaceholderText(QCoreApplication::translate("WindowSystem", "Type a command name, category, or ID…"));
    search->setAccessibleName(QCoreApplication::translate("WindowSystem", "Search commands"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QCoreApplication::translate("WindowSystem", "Command"), QCoreApplication::translate("WindowSystem", "Category"), QCoreApplication::translate("WindowSystem", "Shortcut")});
    tree->setObjectName(QStringLiteral("CommandPaletteCommands"));
    layout->addWidget(tree, 1);
    auto* details = hintLabel(QCoreApplication::translate("WindowSystem", "↑ ↓ Select  ·  Enter Run  ·  Esc Close"), dialog);
    layout->addWidget(details);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("WindowSystem", "Run"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("WindowSystem", "Close"));
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
        const bool found = alive && alive->commands().find(id);
        const bool enabled = found && canRunFromPalette(alive, id, target);
        if (!alive || !tree || !buttons || !details) return;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(enabled);
        details->setText(found ? id : QCoreApplication::translate("WindowSystem", "No matching command is available."));
    };
    const auto rebuild = [alive, tree = retainedTree, search = retainedSearch, target, updateSelected] {
        if (!tree || !search) return;
        tree->clear();
        if (!alive) return;
        const auto rows = commandPaletteRows(alive, target, search->text());
        if (!alive || !tree || !search) return;
        for (const auto& row : rows) {
            auto* item = new QTreeWidgetItem(tree, row.cells);
            item->setData(0, Qt::UserRole, row.id);
            item->setToolTip(0, row.tooltip);
            if (!row.enabled) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        selectFirstEnabled(tree);
        updateSelected();
    };
    const auto accept = [alive, tree = retainedTree, target, dialog = retainedDialog] {
        if (!tree || !dialog) return;
        const auto* item = tree->currentItem();
        if (!alive || !item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const bool enabled = canRunFromPalette(alive, id, target);
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
                alive->setStatus(target.window, QCoreApplication::translate("WindowSystem", "Command was not executed: %1").arg(id));
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
                                QCoreApplication::translate("WindowSystem", "Switch window"), QSize(780, 410), options);
    auto* layout = new QVBoxLayout(dialog);
    uiMargins(layout, 12, 12, 12, 12);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("WindowSelectorSearch"));
    search->setPlaceholderText(QCoreApplication::translate("WindowSystem", "Search windows, owners, or window types…"));
    layout->addWidget(search);
    auto* tree = createList(dialog, {QCoreApplication::translate("WindowSystem", "Window"), QCoreApplication::translate("WindowSystem", "Type"), QCoreApplication::translate("WindowSystem", "Owner workbench"), QCoreApplication::translate("WindowSystem", "Tab")});
    tree->setObjectName(QStringLiteral("WindowSelectorWindows"));
    layout->addWidget(tree, 1);
    layout->addWidget(hintLabel(QCoreApplication::translate("WindowSystem", "Includes all workbench and floating windows. Enter to switch; Esc to cancel."), dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("WindowSystem", "Switch"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("WindowSystem", "Cancel"));
    layout->addWidget(buttons);
    new SearchListNavigation(search, tree);
    const auto rebuild = [alive, tree, search, buttons, source] {
        WindowId previous = source;
        if (tree->currentItem()) previous = tree->currentItem()->data(0, Qt::UserRole).toUuid();
        tree->clear();
        if (!alive) return;
        for (const auto& row : windowSelectorRows(alive, search->text())) {
            auto* item = new QTreeWidgetItem(tree, row.cells);
            const WindowId id(row.id);
            item->setData(0, Qt::UserRole, id);
            item->setToolTip(0, row.tooltip);
            item->setIcon(0, iconFor(row.iconName, alive->interfaceTheme()));
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
                     const QString& title, WindowId window = {})
{
    if (!manager) return;
    const QPointer<WindowManager> alive(manager);
    if (manager->presentation() == WindowManager::Presentation::External) {
        // The presentation creates the content from its type; an open page
        // comes forward instead of a second one.
        for (const auto& record : manager->model().tabs())
            for (const auto pane : manager->model().panes(record.id)) {
                if (manager->paneOptions(pane).contentType != type) continue;
                const auto* group = manager->model().group(manager->model().groupForTab(record.id));
                if (!group) continue;
                const auto tab = record.id;
                manager->showWindow(group->windowId);
                if (alive) alive->activateTab(tab);
                if (alive) alive->focusPane(tab, pane);
                return;
            }
        const auto context = manager->context(window);
        if (manager->model().window(context.window)) manager->addTab(context.window, TabOptions{title, type, {}}, context.group);
        return;
    }
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
    if (parent) window = parent->window()->property("windowId").toUuid();
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

void openBehaviorSettings(WindowManager* manager, QWidget* parent, WindowId window)
{
    openSettingsTab(manager, parent, QStringLiteral("window_system.settings"), QCoreApplication::translate("WindowSystem", "Settings"), window);
}

void openShortcutEditor(WindowManager* manager, QWidget* parent, WindowId window)
{
    openSettingsTab(manager, parent, QStringLiteral("window_system.shortcuts"), QCoreApplication::translate("WindowSystem", "Keyboard shortcuts"), window);
}

} // namespace window_system
