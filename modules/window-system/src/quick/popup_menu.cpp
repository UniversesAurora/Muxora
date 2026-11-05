// SPDX-License-Identifier: MIT
#include "popup_menu.hpp"
#include "button_item.hpp"
#include "path_item.hpp"
#include "qml_scene_component.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include "platform/popup_frame.hpp"
#include <window_system/ui_scale.hpp>
#include <window_system/interface_appearance.hpp>
#include <QFontMetrics>
#include <QContextMenuEvent>
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
#include <tuple>
#include <utility>

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

QStringList rowKey(const MenuItemDescription& item, int ordinal)
{
    auto key = QStringList{QString::number(int(item.kind)) + QLatin1Char(':')
        + (item.id.isEmpty() ? QString::number(ordinal) : item.id)};
    // Contributed submenus share a type ID; their paths distinguish siblings.
    key.append(item.path);
    return key;
}

bool sameMenuColors(const Theme& a, const Theme& b)
{
    return std::tie(a.surface, a.raisedSurface, a.headerBackground, a.headerText, a.text, a.mutedText, a.border, a.accent,
                    a.accentSoft, a.selection, a.selectionText, a.buttonHover, a.danger)
        == std::tie(b.surface, b.raisedSurface, b.headerBackground, b.headerText, b.text, b.mutedText, b.border, b.accent,
                    b.accentSoft, b.selection, b.selectionText, b.buttonHover, b.danger);
}

} // namespace

// Reuse retained native text and icon nodes. Controls only moves the selection
// background and routes input; entering a row does not rebuild the menu tree.
class MenuLabelItem final : public QQuickItem {
public:
    MenuLabelItem(const MenuItemDescription& item, const Theme& theme, const QFont& font, MenuEntry& entry, bool menuBar)
        : item_(item), theme_(theme), font_(font), title_(new TabLabelTextItem(this)),
          shortcut_(new TabLabelTextItem(this)), check_(new TabLabelTextItem(this)),
          icon_(new TabIconItem(this)), arrow_(new TabIconItem(this)), menuBar_(menuBar)
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
    void setPresentation(const Theme& theme, const QFont& font, bool remeasure)
    {
        theme_ = theme; font_ = font;
        iconSource_ = iconFor(item_.iconName, theme);
        arrowSource_ = iconFor(QStringLiteral("chevron-right"), theme);
        for (int index = 0; index < actions_.size(); ++index) {
            actions_.at(index)->setTheme(theme);
            actions_.at(index)->setIcon(iconFor(item_.rowActions.at(index).iconName, theme));
        }
        refreshDescription(remeasure);
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
            if (menuBar_) setImplicitWidth(item_.iconName == QStringLiteral("menu-overflow") ? uiPixels(22) : uiPixels(16) + titleWidth_);
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
        if (menuBar_) {
            shortcut_->clearLabel(); check_->clearLabel(); arrow_->setVisible(false);
            if (item_.iconName == QStringLiteral("menu-overflow")) {
                title_->clearLabel();
                const int extent = uiPixels(14);
                icon_->setVisible(true);
                icon_->setPosition({(width() - extent) / 2, (height() - extent) / 2});
                icon_->setIcon(iconSource_, {extent, extent}, {}, true, false);
                return;
            }
            text(title_, item_.action.title, titleWidth_, {uiPixels(8), 0, std::max(0, qRound(width()) - uiPixels(16)), qRound(height())},
                theme_.headerText, TabTextRole::Title);
            icon_->setVisible(false);
            return;
        }
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
    bool menuBar_ = false;
};

MenuEntry::MenuEntry(MenuItemDescription description, PopupMenu& owner, bool menuBar)
    : QObject(&owner), description_(std::move(description)), owner_(owner),
      label_(description_.kind == MenuItemDescription::Kind::Separator ? nullptr
          : new MenuLabelItem(description_, owner.theme(), owner.menuFont(), *this, menuBar))
{
    if (label_) label_->setParent(this);
}
MenuEntry::~MenuEntry() = default;
QQuickItem* MenuEntry::content() const { return label_; }
void MenuEntry::setHighlighted(bool highlighted) { if (label_) label_->setHighlighted(highlighted); }
void MenuEntry::setHovered(bool hovered) { if (label_) label_->setHovered(hovered); }
void MenuEntry::activate() { owner_.activate(description_); }
void MenuEntry::activateAction(const QString& action) { owner_.activateAction(description_, action); }
void MenuEntry::updatePresentation(const Theme& theme, const QFont& font, bool remeasure)
{
    if (label_) label_->setPresentation(theme, font, remeasure);
}

void MenuEntry::updateDescription(MenuItemDescription next)
{
    const bool remeasure = description_.action.title != next.action.title
        || description_.action.shortcut != next.action.shortcut || description_.iconName != next.iconName;
    const bool updated = description_.action != next.action || description_.tooltip != next.tooltip || remeasure;
    description_ = std::move(next);
    if (!updated) return;
    if (label_) label_->refreshDescription(remeasure);
    emit changed();
}

PopupMenu::PopupMenu(const Theme& theme, QObject* parent)
    : QObject(parent), theme_(theme), font_(QGuiApplication::font()), scale_(uiPixelsF(1))
{
    if (auto* appearance = InterfaceAppearanceController::applicationInstance())
        connect(appearance, &InterfaceAppearanceController::appearanceChanged, this, [this] { setPresentation(theme_); });
}
PopupMenu::~PopupMenu()
{
    dismiss();
    // Window destruction callbacks still remove their entries from the map.
    // Release the QML tree before any session member is destroyed.
    menu_.reset();
    delete std::exchange(overflow_, nullptr);
}
qreal PopupMenu::scale() const { return scale_; }
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

void PopupMenu::setPresentation(const Theme& theme)
{
    const auto font = QGuiApplication::font();
    const bool remeasure = font != font_ || scale_ != uiPixelsF(1);
    if (!remeasure && sameMenuColors(theme_, theme)) return;
    theme_ = theme; font_ = font; scale_ = uiPixelsF(1);
    const QPointer<PopupMenu> self(this);
    const auto update = [&](auto&& visit, const QVector<Row>& rows) -> bool {
        for (const auto& row : rows) {
            row.entry->updatePresentation(theme_, font_, remeasure);
            if (!self || !visit(visit, row.children)) return false;
        }
        return true;
    };
    if (!update(update, rows_)) return;
    if (overflow_) { overflow_->updatePresentation(remeasure); if (!self) return; }
    for (auto* object : menuWindows_.keys()) {
        static_cast<QQuickWindow*>(object)->setColor(theme_.surface);
        if (!self) return;
    }
    emit presentationChanged();
}

QObject* PopupMenu::buildMenu(const QVector<MenuItemDescription>& items, MenuEntry* entry, QObject* parent, QVector<Row>& rows)
{
    auto* menu = create(SceneComponent::Menu,
        {{"session", QVariant::fromValue(this)}, {"entry", QVariant::fromValue(entry)}, {"maximumSize", maximumSize_}}, parent);
    watchWindow(menu, menu->property("contentItem").value<QQuickItem*>());
    syncMenu(menu, rows, items);
    return menu;
}

PopupMenu::Row PopupMenu::createRow(QObject* menu, const MenuItemDescription& item, int index, bool menuBar)
{
    Row row;
    row.key = rowKey(item, index);
    row.entry = new MenuEntry(item, *this, menuBar);
    if (item.kind == MenuItemDescription::Kind::Submenu) {
        row.submenu = buildMenu(item.children, row.entry, menu, row.children);
        QQuickItem* delegate = nullptr;
        QMetaObject::invokeMethod(menu, "insertBranch", Q_RETURN_ARG(QQuickItem*, delegate),
            Q_ARG(int, index), Q_ARG(QObject*, row.submenu.data()));
        row.item = delegate;
    } else {
        const auto kind = item.kind == MenuItemDescription::Kind::Separator ? SceneComponent::MenuSeparator : SceneComponent::MenuRow;
        row.item = qobject_cast<QQuickItem*>(create(kind,
            {{"session", QVariant::fromValue(this)}, {"entry", QVariant::fromValue(row.entry.data())}}, menu));
        QMetaObject::invokeMethod(menu, "insertItem", Q_ARG(int, index), Q_ARG(QQuickItem*, row.item.data()));
    }
    // Controls releases removed delegates later. Their entries must survive
    // until the delegate has stopped referring to the label and QML state.
    connect(row.item, &QObject::destroyed, row.entry, &QObject::deleteLater);
    return row;
}

void PopupMenu::removeRow(QObject* menu, const Row& row)
{
    if (row.submenu) {
        QMetaObject::invokeMethod(row.submenu, "close");
        QMetaObject::invokeMethod(menu, "removeBranch", Q_ARG(QObject*, row.submenu.data()));
        // MenuBar only detaches the C++-owned menu; Menu also queues deletion.
        // deleteLater is idempotent, so both adapters release the same owner.
        row.submenu->deleteLater();
    } else QMetaObject::invokeMethod(menu, "removeItem", Q_ARG(QQuickItem*, row.item.data()));
}

void PopupMenu::syncMenu(QObject* menu, QVector<Row>& rows, const QVector<MenuItemDescription>& items, bool menuBar)
{
    bool structureChanged = rows.size() != items.size();
    for (int index = 0; !structureChanged && index < items.size(); ++index)
        structureChanged = rows.at(index).key != rowKey(items.at(index), index);
    QPointer<QQuickItem> selected;
    if (structureChanged && !menuBar) {
        const int at = menu->property("currentIndex").toInt();
        if (at >= 0 && at < rows.size()) selected = rows.at(at).item;
        // Qt keeps a current-item pointer as well as its numeric index. Clear
        // both before mutation, then restore the surviving identity once.
        menu->setProperty("currentIndex", -1);
    }
    auto previous = std::move(rows);
    rows = {};
    rows.reserve(items.size());
    QHash<QStringList, int> remaining;
    for (int index = 0; index < previous.size(); ++index) remaining.insert(previous.at(index).key, index);
    for (int index = 0; index < items.size(); ++index) {
        const auto& item = items.at(index);
        const auto found = remaining.find(rowKey(item, index));
        if (found == remaining.end()) rows.append(createRow(menu, item, index, menuBar));
        else {
            auto row = std::move(previous[*found]);
            remaining.erase(found);
            row.entry->updateDescription(item);
            if (row.submenu) syncMenu(row.submenu, row.children, item.children);
            QMetaObject::invokeMethod(menu, "placeItem", Q_ARG(int, index), Q_ARG(QQuickItem*, row.item.data()));
            rows.append(std::move(row));
        }
    }
    for (const auto index : remaining) removeRow(menu, previous.at(index));
    if (menuBar) {
        QVector<QQuickItem*> buttons;
        buttons.reserve(rows.size());
        for (int index = 0; index < rows.size(); ++index) {
            rows.at(index).item->setProperty("barIndex", index);
            buttons.append(rows.at(index).item);
        }
        overflow_->sync(items, buttons);
    }
    if (structureChanged && !menuBar) {
        menu->setProperty("currentIndex", -1);
        for (int index = 0; selected && index < rows.size(); ++index)
            if (rows.at(index).item == selected && rows.at(index).entry->visible() && rows.at(index).entry->enabled()) {
                menu->setProperty("currentIndex", index);
                break;
            }
    }
    if (!menuBar) QMetaObject::invokeMethod(menu, "measureContent");
}

void PopupMenu::watchWindow(QObject* menu, QQuickItem* content)
{
    connect(content, &QQuickItem::windowChanged, this, [this, menu](QQuickWindow* window) {
        if (!window || window->type() != Qt::Popup) return;
        // Closing a child temporarily mounts its content in the parent's
        // popup window. That window belongs to the parent menu, not this one.
        if (menuWindows_.contains(window)) return;
        // Popup.Window defaults to transparent. Our menu has an opaque surface
        // just like the old menu, including native text rendering.
        window->setColor(theme_.surface);
        menuWindows_.insert(window, menu);
        connect(menu, &QObject::destroyed, this, [this, observed = QPointer<QQuickWindow>(window)] {
            if (!observed) return;
            menuWindows_.remove(observed); observed->removeEventFilter(this);
        });
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
    if (menuBar_ && object == menu_.get()) {
        if (event->type() == QEvent::ContextMenu) {
            const auto* request = static_cast<QContextMenuEvent*>(event);
            const auto* bar = static_cast<QQuickItem*>(object);
            const auto global = request->reason() == QContextMenuEvent::Keyboard
                ? bar->mapToGlobal(bar->boundingRect().center()).toPoint() : request->globalPos();
            event->accept(); emit contextMenuRequested(global); return true;
        }
        return QObject::eventFilter(object, event);
    }
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        auto* menu = menuWindows_.value(object).data();
        if (rootNavigation_ && menu == menu_.get() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
            const int selected = menu->property("currentIndex").toInt();
            const bool entersBranch = key == Qt::Key_Right && selected >= 0 && selected < rows_.size() && rows_.at(selected).submenu;
            if (!entersBranch) { rootNavigation_(key == Qt::Key_Right); event->accept(); return true; }
        }
        if (key == Qt::Key_Home || key == Qt::Key_End) {
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
    menu_.reset(buildMenu(items, nullptr, this, rows_));
    const auto position = anchor->mapFromGlobal(globalPosition);
    return QMetaObject::invokeMethod(menu_.get(), "showAt", Q_ARG(QQuickItem*, anchor), Q_ARG(QPointF, position));
}

QQuickItem* PopupMenu::createMenuBar(QQuickItem* parent, const QVector<MenuItemDescription>& items)
{
    Q_ASSERT(!menu_ && parent && parent->window());
    menuBar_ = true;
    overflow_ = new MenuBarOverflow(*this);
    maximumSize_ = parent->window()->screen()->availableGeometry().size();
    auto* bar = qobject_cast<QQuickItem*>(create(SceneComponent::MenuBar, {{"session", QVariant::fromValue(this)}}, this));
    menu_.reset(bar);
    bar->installEventFilter(this);
    bar->setParentItem(parent);
    overflow_->attach(bar);
    barItems_ = items;
    syncMenuBar();
    return bar;
}

void PopupMenu::setExcludedRoots(const QStringList& excluded)
{
    const QSet<QString> next(excluded.cbegin(), excluded.cend());
    if (next == excludedRoots_) return;
    excludedRoots_ = next;
    if (menuBar_) syncMenuBar();
}

void PopupMenu::syncMenuBar()
{
    auto effective = barItems_;
    for (auto& item : effective) item.action.visible = item.action.visible && !excludedRoots_.contains(item.id);
    syncMenu(menu_.get(), rows_, effective, true);
    for (const auto& row : rows_)
        if (row.submenu && (!row.entry->visible() || !row.entry->enabled())) QMetaObject::invokeMethod(row.submenu, "close");
}

void PopupMenu::dismiss()
{
    if (overflow_) overflow_->dismiss();
    if (menu_) QMetaObject::invokeMethod(menu_.get(), "dismiss");
}

bool PopupMenu::isOpen() const { return menu_ && menu_->property(menuBar_ ? "popupOpen" : "visible").toBool(); }

void PopupMenu::setRefreshHandler(std::function<QVector<MenuItemDescription>()> handler)
{
    refreshHandler_ = std::move(handler);
}

void PopupMenu::requestRefresh() { if (isOpen()) queueRefresh(false); }
void PopupMenu::requestCatalogRefresh() { queueRefresh(true); }

void PopupMenu::queueRefresh(bool force)
{
    forceRefresh_ |= force;
    if (refreshQueued_) return;
    refreshQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshQueued_ = false;
        const bool force = std::exchange(forceRefresh_, false);
        if (force || isOpen()) refresh();
    });
}

void PopupMenu::refresh()
{
    if (!menu_ || !refreshHandler_ || refreshing_) return;
    refreshing_ = true;
    const QPointer<PopupMenu> self(this);
    const auto resolve = refreshHandler_;
    const auto items = resolve();
    if (!self) return; // A host query may close the workspace and session.
    if (menuBar_) { barItems_ = items; syncMenuBar(); }
    else syncMenu(menu_.get(), rows_, items);
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
