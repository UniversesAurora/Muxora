// SPDX-License-Identifier: MIT
#include "popup_menu.hpp"
#include "button_item.hpp"
#include "path_item.hpp"
#include "qml_scene_component.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include "platform/popup_frame.hpp"
#include <window_system/ui_scale.hpp>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainterPath>
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

QStringList entryPath(const QStringList& parent, const MenuItemDescription& item, int ordinal)
{
    auto key = parent + QStringList{QString::number(int(item.kind)) + QLatin1Char(':')
        + (item.id.isEmpty() ? QString::number(ordinal) : item.id)};
    // Contributed submenus share a type ID; their declared path identifies
    // each branch. Prefix path tokens so they cannot alias a child row key.
    for (const auto& part : item.path) key.append(QStringLiteral("path:") + part);
    return key;
}

void indexDescriptions(const QVector<MenuItemDescription>& items, const QStringList& path,
                       QHash<QStringList, const MenuItemDescription*>& result)
{
    for (int index = 0; index < items.size(); ++index) {
        const auto& item = items.at(index);
        const auto key = entryPath(path, item, index);
        result.insert(key, &item);
        indexDescriptions(item.children, key, result);
    }
}
} // namespace

// Reuse retained native text and icon nodes. Controls only moves the selection
// background and routes input; entering a row does not rebuild the menu tree.
class MenuLabelItem final : public QQuickItem {
public:
    MenuLabelItem(const MenuItemDescription& item, const Theme& theme, const QFont& font, MenuEntry& entry)
        : item_(item), theme_(theme), font_(font), title_(new TabLabelTextItem(this)),
          shortcut_(new TabLabelTextItem(this)), check_(new TabLabelTextItem(this)),
          icon_(new TabIconItem(this)), arrow_(new TabIconItem(this))
    {
        const bool tab = item.kind == MenuItemDescription::Kind::Tab;
        arrowSource_ = iconFor(QStringLiteral("chevron-right"), theme);
        if (tab) {
            for (const auto& action : item.rowActions) {
                auto* button = new ButtonItem(theme, this);
                button->setObjectName(action.id);
                button->setIcon(iconFor(action.iconName, theme));
                button->setToolTip(action.tooltip);
                button->setVisible(!action.hoverOnly);
                connect(button, &ButtonItem::clicked, &entry, [&entry, id = action.id] { entry.activateAction(id); });
                connect(button, &ButtonItem::hoveredChanged, this, [this] { refreshActions(); });
                actions_.append(button);
            }
            marker_ = new PathItem(this);
            marker_->setFill(item.marker); marker_->setVisible(item.marker.isValid());
        }
        refreshDescription(true);
    }
    void refreshDescription(bool remeasure)
    {
        if (remeasure) {
            const QFontMetrics metrics(font_);
            const bool tab = item_.kind == MenuItemDescription::Kind::Tab;
            titleWidth_ = tab ? metrics.horizontalAdvance(item_.action.title) : metrics.size(Qt::TextShowMnemonic, item_.action.title).width();
            shortcutWidth_ = metrics.horizontalAdvance(item_.action.shortcut);
            checkWidth_ = metrics.horizontalAdvance(QChar(0x2713));
            iconSource_ = iconFor(item_.iconName, theme_);
            setImplicitHeight(metrics.height() + uiPixels(10));
            setImplicitWidth(uiPixels(22 + 24) + titleWidth_ + (shortcutWidth_ ? uiPixels(20) + shortcutWidth_ : 0));
            if (tab) {
                const int icons = item_.iconName.isEmpty() ? 0 : uiPixels(16 + 7);
                const int count = int(actions_.size());
                const int buttons = uiPixels(18 * count + 2 * std::max(0, count - 1));
                setImplicitHeight(std::max(metrics.height(), uiPixels(18)) + uiPixels(4));
                setImplicitWidth(std::clamp(uiPixels(8 + 7 + 4) + icons + titleWidth_ + buttons, uiPixels(240), uiPixels(520)));
                elideWidth_ = -1;
            }
        }
        refresh();
    }
    void setHighlighted(bool selected)
    {
        if (selected_ == selected) return;
        selected_ = selected; refresh();
    }
    void setHovered(bool hovered)
    {
        hovered_ = hovered;
        refreshActions();
    }
private:
    void refreshActions()
    {
        // A child tool button accepts hover before Controls sees the row.
        // Keep the row actions exposed while either target owns that hover.
        const bool inside = hovered_ || std::any_of(actions_.cbegin(), actions_.cend(), [](const auto* button) { return button->hovered(); });
        for (int index = 0; index < actions_.size(); ++index)
            if (item_.rowActions.at(index).hoverOnly) actions_.at(index)->setVisible(inside);
    }
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override
    {
        QQuickItem::geometryChange(now, previous);
        if (now.size() != previous.size()) {
            if (marker_) {
                QPainterPath path; path.addRect(QRectF(0, 0, uiPixels(4), height()));
                marker_->setSize(size()); marker_->setPath(path);
            }
            refresh();
        }
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
        if (item_.kind == MenuItemDescription::Kind::Tab) { refreshTab(); return; }
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
    void refreshTab()
    {
        const bool selected = selected_ || item_.action.checked.value_or(false);
        const auto ink = selected ? theme_.selectionText : theme_.text;
        const int h = qRound(height()), extent = uiPixels(16), button = uiPixels(18);
        int start = uiPixels(8), end = qRound(width()) - uiPixels(4);
        shortcut_->clearLabel(); check_->clearLabel(); arrow_->setVisible(false);
        icon_->setVisible(!item_.iconName.isEmpty());
        if (icon_->isVisible()) {
            icon_->setPosition({qreal(start), qreal((h - extent) / 2)});
            icon_->setIcon(iconSource_, {extent, extent}, {}, true, selected);
            start += extent + uiPixels(7);
        }
        for (int index = int(actions_.size()) - 1; index >= 0; --index) {
            end -= button;
            actions_.at(index)->setPosition({qreal(end), qreal((h - button) / 2)});
            actions_.at(index)->setSize({qreal(button), qreal(button)});
            end -= uiPixels(2);
        }
        const int available = std::max(0, end - uiPixels(5) - start);
        if (available != elideWidth_) {
            elideWidth_ = available;
            const QFontMetrics metrics(font_);
            elided_ = metrics.elidedText(item_.action.title, Qt::ElideRight, available);
            elidedWidth_ = metrics.horizontalAdvance(elided_);
        }
        text(title_, elided_, elidedWidth_, {start, 0, available, h}, ink, TabTextRole::Literal);
    }
    const MenuItemDescription& item_;
    Theme theme_;
    QFont font_;
    TabLabelTextItem *title_, *shortcut_, *check_;
    TabIconItem *icon_, *arrow_;
    QIcon iconSource_, arrowSource_;
    QVector<ButtonItem*> actions_;
    PathItem* marker_ = nullptr;
    QString elided_;
    int elideWidth_ = -1, elidedWidth_ = 0;
    int titleWidth_ = 0, shortcutWidth_ = 0, checkWidth_ = 0;
    bool selected_ = false, hovered_ = false;
};

MenuEntry::MenuEntry(MenuItemDescription description, PopupMenu& owner)
    : QObject(&owner), description_(std::move(description)), owner_(owner),
      label_(description_.kind == MenuItemDescription::Kind::Separator ? nullptr
          : new MenuLabelItem(description_, owner.theme(), owner.menuFont(), *this))
{
    if (label_) label_->setParent(this);
}
MenuEntry::~MenuEntry() = default;
QQuickItem* MenuEntry::content() const { return label_; }
void MenuEntry::setHighlighted(bool highlighted) { if (label_) label_->setHighlighted(highlighted); }
void MenuEntry::setHovered(bool hovered) { if (label_) label_->setHovered(hovered); }
void MenuEntry::activate() { owner_.activate(description_); }
void MenuEntry::activateAction(const QString& action) { owner_.activateAction(description_, action); }

void MenuEntry::updateDescription(const MenuItemDescription* value)
{
    auto next = value ? *value : description_;
    if (!value) { next.action.visible = false; next.action.enabled = false; }
    const bool remeasure = description_.action.title != next.action.title
        || description_.action.shortcut != next.action.shortcut || description_.iconName != next.iconName;
    const bool updated = description_.action != next.action || description_.tooltip != next.tooltip || remeasure;
    description_ = std::move(next);
    if (!updated) return;
    if (label_) label_->refreshDescription(remeasure);
    emit changed();
}

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

QObject* PopupMenu::buildMenu(const QVector<MenuItemDescription>& items, MenuEntry* entry, QObject* parent, const QStringList& path)
{
    auto* menu = create(SceneComponent::Menu,
        {{"session", QVariant::fromValue(this)}, {"entry", QVariant::fromValue(entry)}, {"maximumSize", maximumSize_}}, parent);
    watchWindow(menu, menu->property("contentItem").value<QQuickItem*>());
    menus_.append(menu);
    qreal width = 0;
    for (int index = 0; index < items.size(); ++index) {
        const auto& description = items.at(index);
        const auto key = entryPath(path, description, index);
        auto* value = new MenuEntry(description, *this);
        entries_.insert(key, value);
        if (description.kind == MenuItemDescription::Kind::Separator) {
            auto* separator = qobject_cast<QQuickItem*>(create(SceneComponent::MenuSeparator,
                {{"session", QVariant::fromValue(this)}, {"entry", QVariant::fromValue(value)}}, menu));
            QMetaObject::invokeMethod(menu, "addItem", Q_ARG(QQuickItem*, separator));
            continue;
        }
        if (description.action.visible) width = std::max(width, value->content()->implicitWidth());
        if (description.kind == MenuItemDescription::Kind::Submenu) {
            auto* child = buildMenu(description.children, value, menu, key);
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

bool PopupMenu::isOpen() const { return menu_ && menu_->property("visible").toBool(); }

void PopupMenu::setRefreshHandler(std::function<QVector<MenuItemDescription>()> handler)
{
    refreshHandler_ = std::move(handler);
}

void PopupMenu::requestRefresh()
{
    if (refreshQueued_) return;
    refreshQueued_ = true;
    QTimer::singleShot(0, this, [this] { refreshQueued_ = false; if (isOpen()) refresh(); });
}

void PopupMenu::refresh()
{
    if (!refreshHandler_ || refreshing_) return;
    refreshing_ = true;
    const QPointer<PopupMenu> self(this);
    const auto resolve = refreshHandler_;
    const auto items = resolve();
    if (!self) return; // A host query may close the workspace and session.
    QHash<QStringList, const MenuItemDescription*> descriptions;
    indexDescriptions(items, {}, descriptions);
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it)
        it.value()->updateDescription(descriptions.value(it.key()));
    for (const auto& menu : std::as_const(menus_)) if (menu) QMetaObject::invokeMethod(menu, "measureContent");
    refreshing_ = false;
    if (!std::any_of(items.cbegin(), items.cend(), [](const auto& item) {
        return item.action.visible && item.kind != MenuItemDescription::Kind::Separator;
    })) dismiss();
}

void PopupMenu::activateAction(const MenuItemDescription& item, const QString& action)
{
    if (!item.action.visible || !item.action.enabled) return;
    QTimer::singleShot(0, this, [this, item, action] { emit rowActionTriggered(item, action); });
    dismiss();
}

void PopupMenu::activate(const MenuItemDescription& item)
{
    if (!item.action.visible || !item.action.enabled || item.kind == MenuItemDescription::Kind::Submenu) return;
    // Controls dismisses the menu in its trigger handler. Execute only after
    // that handler returns: a command may close the window and this session.
    QTimer::singleShot(0, this, [this, item] { emit triggered(item); });
}

void PopupMenu::finished()
{
    QTimer::singleShot(0, this, [this] { emit closed(); });
}

} // namespace window_system::quick
