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
#include <functional>
#include <memory>

namespace window_system::quick {

class PopupMenu;
class MenuLabelItem;

// Stable rows for one opening; their state is resolved by the caller, not by
// the rendering/input adapter. Refresh never replaces the input objects.
class MenuEntry final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by a menu session")
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString tooltip READ tooltip NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool checkable READ checkable NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(bool persistentHighlight READ persistentHighlight NOTIFY changed)
    Q_PROPERTY(QQuickItem* content READ content CONSTANT)
public:
    MenuEntry(MenuItemDescription description, PopupMenu& owner);
    ~MenuEntry() override;
    QString title() const { return description_.action.title; }
    QString id() const { return description_.id; }
    QString tooltip() const { return description_.tooltip; }
    bool visible() const { return description_.action.visible; }
    bool enabled() const { return description_.action.enabled; }
    bool checkable() const { return description_.action.checkable; }
    bool checked() const { return description_.action.checked.value_or(false); }
    bool persistentHighlight() const { return description_.kind == MenuItemDescription::Kind::Tab && checked(); }
    QQuickItem* content() const;
    void updateDescription(const MenuItemDescription* description);
    Q_INVOKABLE void setHighlighted(bool highlighted);
    Q_INVOKABLE void setHovered(bool hovered);
    Q_INVOKABLE void activate();
    Q_INVOKABLE void activateAction(const QString& action);
signals:
    void changed();
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
    bool isOpen() const;
    QFont menuFont() const { return font_; }
    QPalette menuPalette() const;
    qreal scale() const;
    const Theme& theme() const { return theme_; }
    void activate(const MenuItemDescription& item);
    void activateAction(const MenuItemDescription& item, const QString& action);
    void setRefreshHandler(std::function<QVector<MenuItemDescription>()> handler);
    Q_INVOKABLE void finished();
public slots:
    void requestRefresh();
signals:
    void triggered(window_system::MenuItemDescription item);
    void rowActionTriggered(window_system::MenuItemDescription item, QString action);
    void closed();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QObject* buildMenu(const QVector<MenuItemDescription>& items, MenuEntry* entry, QObject* parent, const QStringList& path = {});
    void watchWindow(QObject* menu, QQuickItem* content);
    void refresh();
    Theme theme_;
    QFont font_;
    QSize maximumSize_;
    std::unique_ptr<QObject> menu_;
    QHash<QObject*, QPointer<QObject>> menuWindows_;
    QHash<QStringList, MenuEntry*> entries_;
    QList<QPointer<QObject>> menus_;
    std::function<QVector<MenuItemDescription>()> refreshHandler_;
    bool refreshing_ = false, refreshQueued_ = false;
};

} // namespace window_system::quick
