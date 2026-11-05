// SPDX-License-Identifier: MIT
#include "menu_bar_overflow.hpp"
#include "popup_menu.hpp"
#include <window_system/ui_scale.hpp>
#include <QCoreApplication>
#include <QKeySequence>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QShortcut>
#include <QScopedValueRollback>
#include <QTimer>
#include <algorithm>

namespace window_system::quick {

MenuBarOverflow::MenuBarOverflow(PopupMenu& owner) : QObject(&owner), owner_(owner)
{
    MenuItemDescription item;
    item.id = QStringLiteral("MenuOverflow");
    item.iconName = QStringLiteral("menu-overflow");
    item.action.title = QCoreApplication::translate("WindowSystem", "More menus");
    entry_ = new MenuEntry(std::move(item), owner, true);
    entry_->setParent(this);
}

MenuBarOverflow::~MenuBarOverflow()
{
    for (const auto& root : roots_) delete root.shortcut.data();
    delete popup_.data();
}

qreal MenuBarOverflow::extent() const { return uiPixelsF(22); }
bool MenuBarOverflow::opened() const { return popup_ && popup_->isOpen(); }
void MenuBarOverflow::attach(QQuickItem* bar) { bar_ = bar; bar->installEventFilter(this); }
void MenuBarOverflow::dismiss() { if (popup_) popup_->dismiss(); }
void MenuBarOverflow::open() { requestOpen(nullptr); }

void MenuBarOverflow::sync(const QVector<MenuItemDescription>& items, const QVector<QQuickItem*>& buttons)
{
    QHash<QQuickItem*, QPointer<QShortcut>> previous;
    for (const auto& root : roots_) previous.insert(root.button, root.shortcut);
    roots_.clear(); roots_.reserve(items.size());
    for (int index = 0; index < items.size(); ++index) {
        auto* button = buttons.at(index);
        auto shortcut = previous.take(button);
        if (!shortcut) {
            shortcut = new QShortcut(bar_->window());
            connect(shortcut, &QShortcut::activated, this, [this, button = QPointer<QQuickItem>(button)] {
                if (button) requestOpen(button);
            });
        }
        shortcut->setKey(QKeySequence::mnemonic(items.at(index).action.title));
        roots_.append({items.at(index), button, shortcut});
    }
    for (const auto& shortcut : previous) delete shortcut.data();
    updateShortcuts();
}

void MenuBarOverflow::updateShortcuts()
{
    if (!bar_) return;
    const int first = bar_->property("firstHiddenIndex").toInt();
    for (int index = 0; index < roots_.size(); ++index) {
        const auto& root = roots_.at(index);
        root.shortcut->setEnabled(bar_->isVisible() && index >= first
            && root.description.action.visible && root.description.action.enabled);
    }
    if (popup_) popup_->requestCatalogRefresh();
}

QVector<MenuItemDescription> MenuBarOverflow::popupItems(QQuickItem* root) const
{
    QVector<MenuItemDescription> result;
    const int first = bar_->property("firstHiddenIndex").toInt();
    for (int index = 0; index < roots_.size(); ++index) {
        const auto& item = roots_.at(index);
        if (root && item.button == root) return item.description.action.visible && item.description.action.enabled
            ? item.description.children : QVector<MenuItemDescription>{};
        if (!root && index >= first && item.description.action.visible) result.append(item.description);
    }
    return result;
}

bool MenuBarOverflow::eventFilter(QObject* object, QEvent* event)
{
    if (forwardingKey_ || object != bar_ || event->type() != QEvent::KeyPress) return QObject::eventFilter(object, event);
    const bool customNavigation = bar_->property("firstHiddenIndex").toInt() < roots_.size()
        || std::any_of(roots_.cbegin(), roots_.cend(), [](const auto& root) {
            return !root.description.action.visible || !root.description.action.enabled;
        });
    if (customNavigation) {
        // Qt's MenuBar advances through its model without skipping hidden
        // buttons. Route folded roots through the extension's anchor instead.
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            QQuickItem* current = nullptr;
            for (const auto& root : roots_)
                if (root.button && root.button->property("highlighted").toBool()) { current = root.button; break; }
            if (!owner_.isOpen()) {
                // Keep Qt's closed-bar focus state: advance its selection
                // past unavailable roots without turning navigation into a click.
                const QPointer<QQuickItem> target(nextRoot(current, key == Qt::Key_Right));
                if (!target) { event->accept(); return true; }
                const QScopedValueRollback forwarding(forwardingKey_, true);
                for (qsizetype step = 0; step < roots_.size(); ++step) {
                    QCoreApplication::sendEvent(bar_, event);
                    if (target->property("highlighted").toBool()) {
                        if (!target->isVisible()) requestOpen(target);
                        event->accept(); return true;
                    }
                }
                event->accept(); return true;
            }
            navigate(current, key == Qt::Key_Right);
            event->accept(); return true;
        }
    }
    return QObject::eventFilter(object, event);
}

QQuickItem* MenuBarOverflow::nextRoot(QQuickItem* from, bool forward) const
{
    int current = forward ? -1 : 0;
    for (int index = 0; index < roots_.size(); ++index)
        if (roots_.at(index).button == from) { current = index; break; }
    for (int offset = 1; offset <= roots_.size(); ++offset) {
        const int index = (current + (forward ? offset : -offset) + roots_.size()) % roots_.size();
        const auto& root = roots_.at(index);
        if (!root.description.action.visible || !root.description.action.enabled) continue;
        return root.button;
    }
    return nullptr;
}

void MenuBarOverflow::navigate(QQuickItem* from, bool forward)
{
    const QPointer<QQuickItem> target(nextRoot(from, forward));
    if (!target) return;
    if (!target->isVisible()) requestOpen(target);
    else QTimer::singleShot(0, this, [this, target] {
        if (!target) return;
        owner_.dismiss();
        QMetaObject::invokeMethod(target, "click");
    });
}

void MenuBarOverflow::requestOpen(QQuickItem* root)
{
    const QPointer<QQuickItem> requested(root);
    const bool specific = root != nullptr;
    const QPointer<QQuickItem> previousFocus(bar_->window()->activeFocusItem());
    const QPointer<QQuickWindow> window(bar_->window());
    QTimer::singleShot(0, this, [this, requested, specific, previousFocus, window] {
        if (specific && !requested) return;
        if (opened() && requested_ == requested) { dismiss(); return; }
        const QPointer<MenuBarOverflow> self(this);
        owner_.refresh();
        if (!self || !bar_) return;
        owner_.dismiss();
        delete popup_.data();
        requested_ = requested;
        auto* popup = new PopupMenu(owner_.theme(), this);
        popup_ = popup;
        if (specific) popup->rootNavigation_ = [this, requested](bool forward) { if (requested) navigate(requested, forward); };
        popup->setRefreshHandler([this, requested, specific] {
            return specific && !requested ? QVector<MenuItemDescription>{} : popupItems(requested);
        });
        connect(popup, &PopupMenu::triggered, &owner_, &PopupMenu::triggered);
        connect(popup, &PopupMenu::closed, popup, &QObject::deleteLater);
        connect(popup, &PopupMenu::closed, this, [this, previousFocus, window] {
            const QPointer<MenuBarOverflow> self(this);
            emit openedChanged();
            if (!self) return;
            if (!previousFocus || !window || !window->isActive() || previousFocus->window() != window || !previousFocus->isVisible()) return;
            auto* current = window->activeFocusItem();
            if (current && bar_ && (current == bar_ || bar_->isAncestorOf(current))) previousFocus->forceActiveFocus(Qt::PopupFocusReason);
        });
        connect(popup, &QObject::destroyed, this, &MenuBarOverflow::openedChanged);
        auto* button = bar_->property("overflowButton").value<QQuickItem*>();
        if (!popup->open(button, button->mapToGlobal(QPointF(0, button->height())).toPoint(), popupItems(requested))) {
            delete popup; return;
        }
        emit openedChanged();
    });
}

void MenuBarOverflow::updatePresentation(bool remeasure)
{
    entry_->updatePresentation(owner_.theme(), owner_.menuFont(), remeasure);
    if (popup_) popup_->setPresentation(owner_.theme());
    emit presentationChanged();
}

} // namespace window_system::quick
