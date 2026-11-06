// SPDX-License-Identifier: MIT
#pragma once

#include "platform/keyboard_cues.hpp"

#include "commands/menu_description.hpp"
#include "menu_bar_overflow.hpp"
#include <window_system/theme.hpp>
#include <QFont>
#include <QHash>
#include <QSet>
#include <QPalette>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

namespace window_system::quick {

class PopupMenu;
class MenuLabelItem;

// The caller resolves state and structure; the adapter retains unchanged rows
// while adding or retiring only the input objects whose identity changed.
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
    MenuEntry(MenuItemDescription description, PopupMenu& owner, bool menuBar = false);
    QString title() const { return description_.action.title; }
    QString id() const { return description_.id; }
    QString tooltip() const { return description_.tooltip; }
    bool visible() const { return description_.action.visible; }
    bool enabled() const { return description_.action.enabled; }
    bool checkable() const { return description_.action.checkable; }
    bool checked() const { return description_.action.checked.value_or(false); }
    bool persistentHighlight() const { return description_.kind == MenuItemDescription::Kind::Tab && checked(); }
    QQuickItem* content() const;
    const QVector<MenuItemDescription>& childDescriptions() const { return description_.children; }
    void updateDescription(MenuItemDescription description);
    void updatePresentation(const Theme& theme, const QFont& font, bool remeasure);
    Q_INVOKABLE void setHighlighted(bool highlighted);
    Q_INVOKABLE void setHovered(bool hovered);
    void setMnemonicsShown(bool shown);
    Q_INVOKABLE void activate();
    Q_INVOKABLE void activateAction(const QString& action);
signals:
    void changed();
private:
    MenuItemDescription description_;
    PopupMenu& owner_;
    MenuLabelItem* label_;
};

// One retained tree, rooted at a popup or the title's menu bar. Quick Controls
// owns popup capture, cascades, scrolling and key routing.
// This session owns only the description and the existing themed presentation.
class PopupMenu final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the window adapter")
    Q_PROPERTY(QFont menuFont READ menuFont NOTIFY presentationChanged)
    Q_PROPERTY(QPalette menuPalette READ menuPalette NOTIFY presentationChanged)
    Q_PROPERTY(QColor hoverFill READ hoverFill NOTIFY presentationChanged)
    Q_PROPERTY(qreal scale READ scale NOTIFY presentationChanged)
    Q_PROPERTY(MenuBarOverflow* overflow READ overflow CONSTANT)
public:
    explicit PopupMenu(const Theme& theme, QObject* parent = nullptr);
    ~PopupMenu() override;
    bool open(QQuickItem* anchor, QPoint globalPosition, const QVector<MenuItemDescription>& items);
    QQuickItem* createMenuBar(QQuickItem* parent, const QVector<MenuItemDescription>& items);
    void setExcludedRoots(const QStringList& excluded);
    [[nodiscard]] QStringList menuBarItemIds() const;
    void setPresentation(const Theme& theme);
    void dismiss();
    bool isOpen() const;
    // Whether titles underline their access keys: when the system asks for
    // it, or while the keyboard drives the menus.
    void setMnemonicsShown(bool shown);
    [[nodiscard]] bool mnemonicsShown() const { return mnemonicsShown_; }
    QFont menuFont() const { return font_; }
    QPalette menuPalette() const;
    qreal scale() const;
    QColor hoverFill() const { return theme_.accentSoft; }
    const Theme& theme() const { return theme_; }
    MenuBarOverflow* overflow() const { return overflow_; }
    void activate(const MenuItemDescription& item);
    void activateAction(const MenuItemDescription& item, const QString& action);
    void setRefreshHandler(std::function<QVector<MenuItemDescription>()> handler);
    Q_INVOKABLE void finished();
    Q_INVOKABLE void prepareSubmenus(QObject* menu);
public slots:
    void requestRefresh();
    void requestCatalogRefresh();
signals:
    void presentationChanged();
    void contextMenuRequested(QPoint global);
    void triggered(window_system::MenuItemDescription item);
    void rowActionTriggered(window_system::MenuItemDescription item, QString action);
    void closed();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    friend class MenuBarOverflow;
    struct Row {
        QStringList key;
        QPointer<MenuEntry> entry;
        QPointer<QQuickItem> item;
        QPointer<QObject> submenu;
        QVector<Row> children;
        bool childrenPrepared = false;
    };
    QObject* buildMenu(MenuEntry* entry, QObject* parent);
    void prepareChildren(QVector<Row>& rows);
    Row createRow(QObject* menu, const MenuItemDescription& item, int index, bool menuBar);
    void syncMenu(QObject* menu, QVector<Row>& rows, const QVector<MenuItemDescription>& items, bool menuBar = false);
    void removeRow(QObject* menu, const Row& row);
    void watchWindow(QObject* menu, QQuickItem* content);
    void refresh();
    void queueRefresh(bool force);
    void syncMenuBar();
    Theme theme_;
    QFont font_;
    qreal scale_ = 1;
    bool mnemonicsShown_ = keyboardCuesAlwaysShown();
    QSize maximumSize_;
    std::unique_ptr<QObject> menu_;
    QHash<QObject*, QPointer<QObject>> menuWindows_;
    QVector<Row> rows_;
    MenuBarOverflow* overflow_ = nullptr;
    std::function<QVector<MenuItemDescription>()> refreshHandler_;
    std::function<void(bool)> rootNavigation_;
    std::function<bool(QEvent*)> keyboardHandler_;
    QVector<MenuItemDescription> barItems_;
    QSet<QString> excludedRoots_;
    bool menuBar_ = false, refreshing_ = false, refreshQueued_ = false, forceRefresh_ = false;
};

} // namespace window_system::quick
