// SPDX-License-Identifier: MIT
#include "popup_menu.hpp"
#include "qml_scene_component.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include "platform/popup_frame.hpp"
#include <window_system/ui_scale.hpp>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPlatformSurfaceEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <algorithm>

namespace window_system::quick {
namespace {
QObject* create(SceneComponent kind, const QVariantMap& properties, QObject* parent)
{
    auto& source = sceneComponent(kind);
    auto* object = source.createWithInitialProperties(properties);
    if (!object) qFatal("Cannot create Quick menu: %s", qPrintable(source.errorString()));
    object->setParent(parent);
    QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
    return object;
}
} // namespace

// Reuse retained native text and icon nodes. Controls only moves the selection
// background and routes input; entering a row does not rebuild the menu tree.
class MenuLabelItem final : public QQuickItem {
public:
    MenuLabelItem(const MenuItemDescription& item, const Theme& theme, const QFont& font)
        : item_(item), theme_(theme), font_(font), title_(new TabLabelTextItem(this)),
          shortcut_(new TabLabelTextItem(this)), check_(new TabLabelTextItem(this)),
          icon_(new TabIconItem(this)), arrow_(new TabIconItem(this))
    {
        const QFontMetrics metrics(font);
        titleWidth_ = metrics.size(Qt::TextShowMnemonic, item.action.title).width();
        shortcutWidth_ = metrics.horizontalAdvance(item.action.shortcut);
        checkWidth_ = metrics.horizontalAdvance(QChar(0x2713));
        iconSource_ = iconFor(item.iconName, theme);
        arrowSource_ = iconFor(QStringLiteral("chevron-right"), theme);
        setImplicitHeight(metrics.height() + uiPixels(10));
        setImplicitWidth(uiPixels(22 + 24) + titleWidth_ + (shortcutWidth_ ? uiPixels(20) + shortcutWidth_ : 0));
    }
    void setHighlighted(bool selected)
    {
        if (selected_ == selected) return;
        selected_ = selected; refresh();
    }
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override
    {
        QQuickItem::geometryChange(now, previous);
        if (now.size() != previous.size()) refresh();
    }
private:
    void text(TabLabelTextItem* target, QString value, int naturalWidth, QRect area, QColor color, TabTextRole role)
    {
        if (value.isEmpty() || area.isEmpty()) { target->clearLabel(); return; }
        TabLabelPresentation label;
        label.parts.tab = area; label.parts.title = area;
        label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = std::move(value);
        label.font = font_; label.naturalWidth = naturalWidth;
        TabStripLabelStyle style; style.palette.text = color;
        target->setLabel(label, style, role); target->setPosition(area.topLeft());
    }
    void refresh()
    {
        const int h = qRound(height()), end = qRound(width()) - uiPixels(24), start = uiPixels(22);
        const auto ink = !item_.action.enabled ? theme_.mutedText : selected_ ? theme_.selectionText : theme_.text;
        const int shortcutStart = std::max(start, end - shortcutWidth_);
        const int titleEnd = shortcutWidth_ ? shortcutStart - uiPixels(20) : end;
        text(title_, item_.action.title, titleWidth_, {start, 0, std::max(0, titleEnd - start), h}, ink, TabTextRole::Title);
        text(shortcut_, item_.action.shortcut, shortcutWidth_, {shortcutStart, 0, shortcutWidth_, h},
            selected_ ? ink : theme_.mutedText, TabTextRole::Literal);
        const int extent = uiPixels(14), y = (h - extent) / 2;
        const bool checked = item_.action.checkable && item_.action.checked.value_or(false);
        text(check_, checked ? QString(QChar(0x2713)) : QString{}, checkWidth_, {uiPixels(3), 0, extent, h}, ink, TabTextRole::Literal);
        icon_->setVisible(!checked && !item_.iconName.isEmpty());
        if (icon_->isVisible()) {
            icon_->setPosition({qreal(uiPixels(3)), qreal(y)});
            icon_->setIcon(iconSource_, {extent, extent}, {}, item_.action.enabled, selected_);
        }
        arrow_->setVisible(item_.kind == MenuItemDescription::Kind::Submenu);
        if (arrow_->isVisible()) {
            arrow_->setPosition({qreal(qRound(width()) - uiPixels(18)), qreal(y)});
            arrow_->setIcon(arrowSource_, {extent, extent}, {}, item_.action.enabled, selected_);
        }
    }
    const MenuItemDescription& item_;
    Theme theme_;
    QFont font_;
    TabLabelTextItem *title_, *shortcut_, *check_;
    TabIconItem *icon_, *arrow_;
    QIcon iconSource_, arrowSource_;
    int titleWidth_ = 0, shortcutWidth_ = 0, checkWidth_ = 0;
    bool selected_ = false;
};

MenuEntry::MenuEntry(MenuItemDescription description, PopupMenu& owner)
    : QObject(&owner), description_(std::move(description)), owner_(owner),
      label_(new MenuLabelItem(description_, owner.theme(), owner.menuFont()))
{
    label_->setParent(this);
}
MenuEntry::~MenuEntry() = default;
QQuickItem* MenuEntry::content() const { return label_; }
void MenuEntry::setHighlighted(bool highlighted) { label_->setHighlighted(highlighted); }
void MenuEntry::activate() { owner_.activate(description_); }

PopupMenu::PopupMenu(const Theme& theme, QObject* parent)
    : QObject(parent), theme_(theme), font_(QGuiApplication::font()) {}
PopupMenu::~PopupMenu()
{
    dismiss();
    // Window destruction callbacks still remove their entries from the map.
    // Release the QML tree before any session member is destroyed.
    menu_.reset();
}
qreal PopupMenu::scale() const { return uiPixelsF(1); }
QPalette PopupMenu::menuPalette() const
{
    QPalette result;
    result.setColor(QPalette::Window, theme_.surface);
    result.setColor(QPalette::WindowText, theme_.text);
    result.setColor(QPalette::Mid, theme_.border);
    result.setColor(QPalette::Highlight, theme_.selection);
    result.setColor(QPalette::HighlightedText, theme_.selectionText);
    return result;
}

QObject* PopupMenu::buildMenu(const QVector<MenuItemDescription>& items, MenuEntry* entry, QObject* parent)
{
    auto* menu = create(SceneComponent::Menu,
        {{"session", QVariant::fromValue(this)}, {"entry", QVariant::fromValue(entry)}, {"maximumSize", maximumSize_}}, parent);
    watchWindow(menu, menu->property("contentItem").value<QQuickItem*>());
    qreal width = 0;
    for (const auto& description : items) {
        if (!description.action.visible) continue;
        if (description.kind == MenuItemDescription::Kind::Separator) {
            auto* separator = qobject_cast<QQuickItem*>(create(SceneComponent::MenuSeparator, {{"session", QVariant::fromValue(this)}}, menu));
            QMetaObject::invokeMethod(menu, "addItem", Q_ARG(QQuickItem*, separator));
            continue;
        }
        auto* value = new MenuEntry(description, *this);
        width = std::max(width, value->content()->implicitWidth());
        if (description.kind == MenuItemDescription::Kind::Submenu) {
            auto* child = buildMenu(description.children, value, menu);
            QMetaObject::invokeMethod(menu, "appendMenu", Q_ARG(QObject*, child));
        } else {
            auto* row = qobject_cast<QQuickItem*>(create(SceneComponent::MenuRow,
                {{"session", QVariant::fromValue(this)}, {"entry", QVariant::fromValue(value)}}, menu));
            QMetaObject::invokeMethod(menu, "addItem", Q_ARG(QQuickItem*, row));
        }
    }
    menu->setProperty("contentWidthHint", width);
    return menu;
}

void PopupMenu::watchWindow(QObject* menu, QQuickItem* content)
{
    connect(content, &QQuickItem::windowChanged, this, [this, menu](QQuickWindow* window) {
        if (!window || window->type() != Qt::Popup) return;
        // Popup.Window defaults to transparent. Our menu has an opaque surface
        // just like the old menu, including native text rendering.
        window->setColor(theme_.surface);
        menuWindows_.insert(window, menu);
        connect(window, &QObject::destroyed, this, [this, window] { menuWindows_.remove(window); });
        const auto updateLimit = [menu, window] { menu->setProperty("maximumSize", window->screen()->availableGeometry().size()); };
        connect(window, &QWindow::screenChanged, menu, updateLimit);
        updateLimit();
        window->installEventFilter(this);
        preparePopupFrame(window);
    });
}

bool PopupMenu::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Home || key == Qt::Key_End) {
            auto* menu = menuWindows_.value(object).data();
            Q_ASSERT(menu);
            QMetaObject::invokeMethod(menu, "selectBoundary", Q_ARG(bool, key == Qt::Key_End));
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::Show || (event->type() == QEvent::PlatformSurface
        && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated))
        preparePopupFrame(qobject_cast<QWindow*>(object));
    return QObject::eventFilter(object, event);
}

bool PopupMenu::open(QQuickItem* anchor, QPoint globalPosition, const QVector<MenuItemDescription>& items)
{
    Q_ASSERT(!menu_);
    if (!anchor || !anchor->window() || !std::any_of(items.cbegin(), items.cend(), [](const auto& item) {
        return item.action.visible && item.kind != MenuItemDescription::Kind::Separator;
    })) return false;
    const auto* screen = QGuiApplication::screenAt(globalPosition);
    if (!screen) screen = anchor->window()->screen();
    maximumSize_ = screen->availableGeometry().size();
    menu_.reset(buildMenu(items, nullptr, this));
    const auto position = anchor->mapFromGlobal(globalPosition);
    return QMetaObject::invokeMethod(menu_.get(), "showAt", Q_ARG(QQuickItem*, anchor), Q_ARG(QPointF, position));
}

void PopupMenu::dismiss()
{
    if (menu_) QMetaObject::invokeMethod(menu_.get(), "dismiss");
}

void PopupMenu::activate(const MenuItemDescription& item)
{
    if (!item.action.enabled || item.kind == MenuItemDescription::Kind::Submenu) return;
    // Controls dismisses the menu in its trigger handler. Execute only after
    // that handler returns: a command may close the window and this session.
    QTimer::singleShot(0, this, [this, item] { emit triggered(item); });
}

void PopupMenu::finished()
{
    QTimer::singleShot(0, this, [this] { emit closed(); });
}

} // namespace window_system::quick
