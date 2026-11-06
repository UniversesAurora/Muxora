// SPDX-License-Identifier: MIT
#pragma once

#include "commands/menu_description.hpp"
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class QShortcut;
class QKeyEvent;
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
    Q_PROPERTY(QQuickItem* keyboardItem READ keyboardItem NOTIFY keyboardModeChanged)
public:
    explicit MenuBarOverflow(PopupMenu& owner);
    ~MenuBarOverflow() override;
    MenuEntry* entry() const { return entry_; }
    qreal extent() const;
    bool opened() const;
    QQuickItem* keyboardItem() const { return keyboardItem_; }
    bool handleWindowEvent(QEvent* event);
    bool handlePopupEvent(QEvent* event);
    void attach(QQuickItem* bar);
    void sync(const QVector<MenuItemDescription>& items, const QVector<QQuickItem*>& buttons);
    void updatePresentation(bool remeasure);
    void dismiss();
    Q_INVOKABLE void open();
    Q_INVOKABLE void updateShortcuts();
signals:
    void openedChanged();
    void presentationChanged();
    void keyboardModeChanged();
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
    QQuickItem* nextRoot(QQuickItem* from, bool forward) const;
    QVector<MenuItemDescription> popupItems(QQuickItem* root) const;
    QQuickItem* nextKeyboardItem(bool forward) const;
    void selectKeyboardItem(QQuickItem* item);
    void leaveKeyboardMode();
    bool navigateKeyboard(QKeyEvent* event);
    void openKeyboardItem();
    void cancelAlt();
    PopupMenu& owner_;
    MenuEntry* entry_;
    QPointer<QQuickItem> bar_;
    QPointer<PopupMenu> popup_;
    QPointer<QQuickItem> requested_;
    QVector<Root> roots_;
    bool forwardingKey_ = false;
    bool altCandidate_ = false;
    bool keyboardMode_ = false;
    QPointer<QQuickItem> keyboardItem_;
    QPointer<QQuickItem> previousFocus_;
};
} // namespace window_system::quick
