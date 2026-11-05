// SPDX-License-Identifier: MIT
#pragma once

#include "commands/menu_description.hpp"
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class QShortcut;
Q_MOC_INCLUDE("popup_menu.hpp")

namespace window_system::quick {
class MenuEntry;
class PopupMenu;

// The existing menu bar extension: folded roots use the same popup renderer,
// and keep their window-scoped mnemonics while their bar buttons are hidden.
class MenuBarOverflow final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the menu bar")
    Q_PROPERTY(MenuEntry* entry READ entry CONSTANT)
    Q_PROPERTY(qreal extent READ extent NOTIFY presentationChanged)
    Q_PROPERTY(bool opened READ opened NOTIFY openedChanged)
public:
    explicit MenuBarOverflow(PopupMenu& owner);
    ~MenuBarOverflow() override;
    MenuEntry* entry() const { return entry_; }
    qreal extent() const;
    bool opened() const;
    void attach(QQuickItem* bar);
    void sync(const QVector<MenuItemDescription>& items, const QVector<QQuickItem*>& buttons);
    void updatePresentation(bool remeasure);
    void dismiss();
    Q_INVOKABLE void open();
    Q_INVOKABLE void updateShortcuts();
signals:
    void openedChanged();
    void presentationChanged();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    struct Root {
        MenuItemDescription description;
        QPointer<QQuickItem> button;
        QPointer<QShortcut> shortcut;
    };
    void requestOpen(QQuickItem* root);
    void navigate(QQuickItem* from, bool forward);
    QVector<MenuItemDescription> popupItems(QQuickItem* root) const;
    PopupMenu& owner_;
    MenuEntry* entry_;
    QPointer<QQuickItem> bar_;
    QPointer<PopupMenu> popup_;
    QPointer<QQuickItem> requested_;
    QVector<Root> roots_;
};
} // namespace window_system::quick
