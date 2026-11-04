// SPDX-License-Identifier: MIT
#pragma once

#include "commands/menu_description.hpp"
#include <window_system/theme.hpp>
#include <QFont>
#include <QHash>
#include <QPalette>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace window_system::quick {

class PopupMenu;
class MenuLabelItem;

// One immutable description for a single opening. Live command state is
// resolved by the caller before opening, not by the rendering/input adapter.
class MenuEntry final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by a menu session")
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    Q_PROPERTY(bool checkable READ checkable CONSTANT)
    Q_PROPERTY(bool checked READ checked CONSTANT)
    Q_PROPERTY(QQuickItem* content READ content CONSTANT)
public:
    MenuEntry(MenuItemDescription description, PopupMenu& owner);
    ~MenuEntry() override;
    QString title() const { return description_.action.title; }
    QString id() const { return description_.id; }
    bool enabled() const { return description_.action.enabled; }
    bool checkable() const { return description_.action.checkable; }
    bool checked() const { return description_.action.checked.value_or(false); }
    QQuickItem* content() const;
    Q_INVOKABLE void setHighlighted(bool highlighted);
    Q_INVOKABLE void activate();
private:
    MenuItemDescription description_;
    PopupMenu& owner_;
    MenuLabelItem* label_;
};

// Qt Quick Controls owns popup capture, cascades, scrolling and key routing.
// This session owns only the description and the existing themed presentation.
class PopupMenu final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the window adapter")
    Q_PROPERTY(QFont menuFont READ menuFont CONSTANT)
    Q_PROPERTY(QPalette menuPalette READ menuPalette CONSTANT)
    Q_PROPERTY(qreal scale READ scale CONSTANT)
public:
    explicit PopupMenu(const Theme& theme, QObject* parent = nullptr);
    ~PopupMenu() override;
    bool open(QQuickItem* anchor, QPoint globalPosition, const QVector<MenuItemDescription>& items);
    void dismiss();
    QFont menuFont() const { return font_; }
    QPalette menuPalette() const;
    qreal scale() const;
    const Theme& theme() const { return theme_; }
    void activate(const MenuItemDescription& item);
    Q_INVOKABLE void finished();
signals:
    void triggered(window_system::MenuItemDescription item);
    void closed();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QObject* buildMenu(const QVector<MenuItemDescription>& items, MenuEntry* entry, QObject* parent);
    void watchWindow(QObject* menu, QQuickItem* content);
    Theme theme_;
    QFont font_;
    QSize maximumSize_;
    std::unique_ptr<QObject> menu_;
    QHash<QObject*, QPointer<QObject>> menuWindows_;
};

} // namespace window_system::quick
