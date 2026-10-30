#include <window_system/interface_appearance.hpp>
#include <QCoreApplication>
#include "tab_strip.hpp"
#include "tab_group_controller.hpp"
#include "dock_surface.hpp"
#include "style/tab_painter.hpp"
#include "commands/popup_menu.hpp"
#include "style/compact_tool_button.hpp"
#include "style/split_tool_button.hpp"
#include "style/tab_appearance_p.hpp"
#include "style/themed_controls.hpp"
#include "style/ui_metrics.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QCursor>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QVariantMap>
#include <QPixmap>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace window_system {
namespace {
constexpr int listButtonWidth = 12;
constexpr int newTabButtonWidth = 26;
constexpr int createButtonGap = 1;

// Sets a button's theme icon and remembers its name for theme changes.
void setThemedIcon(QAbstractButton* button, const QString& name, const Theme& theme)
{
    if (button->property("themeIcon").toString() == name && !button->icon().isNull()) return;
    button->setProperty("themeIcon", name); button->setIcon(iconFor(name, theme));
}

class TabPagePlaceholder final : public QWidget {
public:
    explicit TabPagePlaceholder(TabId tab, QWidget* parent)
        : QWidget(parent)
    {
        setObjectName("TabPagePlaceholder");
        setProperty("tabId", tab);
    }
};

// One surface for the whole column, including its footer. Its tiny cached
// shadow patches are shared by every animation frame, never a full snapshot.
class VerticalTabPanel final : public QWidget {
public:
    VerticalTabPanel(const Theme& theme, QWidget* parent) : QWidget(parent), theme_(theme), palette_(TabPalette::forTheme(theme))
    {
        setObjectName("VerticalTabPanel"); setMouseTracking(true);
    }
    void setTheme(const Theme& theme) { theme_ = theme; palette_ = TabPalette::forTheme(theme); update(); }
    void setSurface(QRect body, qreal overlay, int verticalOutset)
    {
        const bool changed = body_ != body || (overlay_ > 0) != (overlay > 0);
        body_ = body; content_ = body.adjusted(0, verticalOutset, 0, -verticalOutset); overlay_ = overlay;
        // The resting panel must not intercept its neighbor's content or
        // resize handle through the padding reserved for an overlay shadow.
        if (changed) { if (overlay > 0) clearMask(); else setMask(QRegion(body.adjusted(-uiPixels(TabMetrics{}.columnGap), 0, uiPixels(TabMetrics{}.columnGap), 0))); }
        update();
    }
    [[nodiscard]] QRect contentRect() const { return content_; }
    [[nodiscard]] TabColumnSurface surface(QPoint offset = {}) const
    {
        auto line = palette_.tileBorder.resolve(window()->isActiveWindow());
        line.setAlphaF(line.alphaF() * overlay_);
        return {rect().translated(offset), QRectF(body_).translated(offset), uiPixelsF(8), overlay_,
                theme_.headerBackground, line, palette_.elevation};
    }

protected:
    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::ActivationChange) update();
        QWidget::changeEvent(event);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter paint(this); paintTabColumnSurface(paint, renderer_, surface());
    }
private:
    Theme theme_;
    TabPalette palette_;
    ElevationRenderer renderer_;
    QRect body_;
    QRect content_;
    qreal overlay_ = 0;
};

// Only the corners need masking above arbitrary page contents. Replay the
// shared backplate there, preserving neighboring shadows instead of filling
// the cut-outs with a flat color.
class CardOverlay final : public QWidget {
public:
    CardOverlay(const Theme& theme, QWidget* parent) : QWidget(parent), theme_(theme)
    {
        setObjectName("TabGroupCard");
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setTheme(const Theme& theme) { theme_ = theme; update(corners()); }
    void setCard(QRect card)
    {
        if (card_ == card) return;
        const auto old = corners(); card_ = card; update(old | corners());
    }
    void invalidate(const QRect& area) { update(corners() & QRegion(area)); }
protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (card_.isEmpty()) return;
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const qreal radius = uiPixels(TabMetrics{}.cardRadius);
        QPainterPath rounded; rounded.addRoundedRect(QRectF(card_), radius, radius);
        QPainterPath square; square.addRect(QRectF(card_));
        painter.setClipPath(square.subtracted(rounded), Qt::IntersectClip);
        DockSurface* surface = nullptr;
        for (auto* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            if ((surface = dynamic_cast<DockSurface*>(ancestor))) break;
        if (!surface) { painter.fillRect(card_, theme_.headerBackground); return; }
        const auto origin = mapTo(surface, QPoint());
        painter.translate(-origin);
        surface->paintBackplate(painter, (event->region() & corners()).translated(origin));
    }
private:
    [[nodiscard]] QRegion corners() const
    {
        const int radius = uiPixels(TabMetrics{}.cardRadius) + 1;
        QRegion result;
        for (const auto point : {card_.topLeft(), card_.topRight() - QPoint(radius - 1, 0),
                                card_.bottomLeft() - QPoint(0, radius - 1), card_.bottomRight() - QPoint(radius - 1, radius - 1)})
            result |= QRect(point, QSize(radius, radius));
        return result;
    }
    Theme theme_;
    QRect card_;
};

// The picture of dragged content over a group's pages: top left aligned at
// its own size, the rest in the content's background.
class ContentPreview final : public QWidget {
public:
    explicit ContentPreview(QWidget* parent) : QWidget(parent)
    {
        setObjectName("TabContentPreview");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }
    void setPicture(const QPixmap& picture)
    {
        picture_ = picture;
        const auto image = picture.toImage();
        fill_ = image.isNull() ? palette().color(QPalette::Base) : image.pixelColor(image.width() - 1, image.height() - 1);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), fill_);
        painter.drawPixmap(0, 0, picture_);
    }
private:
    QPixmap picture_;
    QColor fill_;
};

class TabCreateButtons final : public SplitToolButton {
public:
    TabCreateButtons(const Theme& theme, QWidget* parent)
        : SplitToolButton(parent, QSize(uiPixels(newTabButtonWidth), theme.metrics.tabHeight),
                          QSize(uiPixels(listButtonWidth), theme.metrics.tabHeight), uiPixels(createButtonGap))
    {
        setObjectName("TabCreateButtons"); add = primary; types = menu;
        add->setObjectName("NewTabButton"); setThemedIcon(add, "add", theme);
        add->setToolTip(QCoreApplication::translate("WindowSystem", "New tab")); add->setAccessibleName(add->toolTip());
        types->setObjectName("NewTabTypeButton"); setThemedIcon(types, "chevron-down", theme);
        types->setToolTip(QCoreApplication::translate("WindowSystem", "Choose new tab type")); types->setAccessibleName(types->toolTip());
    }
    CompactToolButton* add = nullptr;
    CompactToolButton* types = nullptr;
    void setHeight(int height) {
        setButtonSizes(QSize(uiPixels(newTabButtonWidth), std::max(1, height - uiPixels(4))), QSize(uiPixels(listButtonWidth), std::max(1, height - uiPixels(4))));
        setFixedHeight(height);
    }
};

QColor tabListColor(const QWidget* widget, const char* token, QPalette::ColorRole fallback)
{
    for (auto* root = widget; root; root = root->parentWidget()) {
        const auto tokens = root->property("windowSystemThemeColors");
        if (!tokens.isValid()) continue;
        const auto value = tokens.toMap().value(QString::fromLatin1(token));
        if (value.canConvert<QColor>()) return value.value<QColor>();
    }
    return widget->palette().color(fallback);
}

class TabListTitle final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent*) override {
        const bool emphasized = parentWidget()->property("emphasized").toBool();
        QPainter painter(this);
        painter.setPen(tabListColor(this, emphasized ? "selectionText" : "text",
            emphasized ? QPalette::HighlightedText : QPalette::WindowText));
        painter.drawText(contentsRect(), alignment().toInt() | Qt::TextSingleLine, text());
    }
};

class TabListRow final : public QWidget {
public:
    TabListRow(QString text, QIcon icon, QColor marker, bool active, QIcon sticky, QWidget* parent) : QWidget(parent), marker_(marker) {
        setObjectName("TabListRow");
        setProperty("tabColorMarker", marker_);
        setProperty("activeTab", active);
        setProperty("emphasized", active);
        setMouseTracking(true);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 4, 2); layout->setSpacing(7);
        if (!icon.isNull()) {
            auto* image = new QLabel(this); image->setPixmap(icon.pixmap(16, 16)); layout->addWidget(image);
        }
        title_ = new TabListTitle(text, this); title_->setToolTip(text);
        title_->setObjectName("TabListTitle");
        title_->setAttribute(Qt::WA_TransparentForMouseEvents); layout->addWidget(title_, 1);
        // The row's actions sit together at its end, apart from the title.
        auto* actions = new QHBoxLayout; actions->setContentsMargins({}); actions->setSpacing(2);
        layout->addLayout(actions);
        const auto actionButton = [this, actions](const char* name) {
            auto* button = new CompactToolButton(this, 18);
            button->setObjectName(name);
            button->setStyleSheet("QToolButton{padding:0;min-height:0;min-width:0;}");
            actions->addWidget(button);
            return button;
        };
        // Keep the unstick action visible even before the row is hovered.
        if (!sticky.isNull()) {
            unstickButton = actionButton("TabListUnstickButton");
            unstickButton->setIcon(sticky);
            unstickButton->setToolTip(QCoreApplication::translate("WindowSystem", "Unstick tab"));
            unstickButton->setAccessibleName(unstickButton->toolTip());
        }
        closeButton = actionButton("TabListCloseButton");
        auto policy = closeButton->sizePolicy(); policy.setRetainSizeWhenHidden(true);
        closeButton->setSizePolicy(policy); closeButton->hide();
        setMinimumWidth(240); setMaximumWidth(520);
        if (auto* menu = qobject_cast<QMenu*>(parent))
            QObject::connect(menu, &QMenu::hovered, this, [this] { updateEmphasis(); });
    }
    QToolButton* closeButton;
    QToolButton* unstickButton = nullptr;
    std::function<void()> activate;
protected:
    void enterEvent(QEnterEvent* event) override {
        // QWidgetAction children receive their own mouse events. Selecting
        // their action explicitly gives the menu one highlight owner, shared
        // with keyboard navigation rather than an independent hover state.
        if (auto* menu = qobject_cast<QMenu*>(parentWidget()))
            for (auto* action : menu->actions())
                if (auto* item = qobject_cast<QWidgetAction*>(action); item && item->defaultWidget() == this) {
                    menu->setActiveAction(action);
                    break;
                }
        closeButton->show(); updateEmphasis(); QWidget::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override {
        if (auto* menu = qobject_cast<QMenu*>(parentWidget()))
            if (auto* action = qobject_cast<QWidgetAction*>(menu->activeAction()); action && action->defaultWidget() == this)
                menu->setActiveAction(nullptr);
        closeButton->hide(); updateEmphasis(); QWidget::leaveEvent(event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const bool emphasized = property("emphasized").toBool();
        painter.fillRect(rect(), tabListColor(this, emphasized ? "selection" : "surface",
            emphasized ? QPalette::Highlight : QPalette::Base));
        if (marker_.isValid()) painter.fillRect(QRect(0, 0, 4, height()), marker_);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) { const auto callback = activate; if (callback) callback(); }
        else QWidget::mouseReleaseEvent(event);
    }
private:
    void updateEmphasis() {
        auto* menu = qobject_cast<QMenu*>(parentWidget());
        auto* action = menu ? qobject_cast<QWidgetAction*>(menu->activeAction()) : nullptr;
        const bool emphasized = property("activeTab").toBool()
            || (action && action->defaultWidget() == this);
        if (property("emphasized").toBool() == emphasized) return;
        setProperty("emphasized", emphasized);
        update(); title_->update();
    }
    QLabel* title_ = nullptr;
    QColor marker_;
};
} // namespace

GroupTabs::GroupTabs(const Theme& theme, QWidget* parent) : QFrame(parent), theme_(theme)
{
    setObjectName("TabGroup");
    setAutoFillBackground(false);
    viewportWindow_ = window(); viewportWindow_->installEventFilter(this);
    stack_ = new QStackedWidget(this);
    bar_ = new TabBar(theme, this);
    bar_->setColumnManaged(true);
    group_ = std::make_unique<TabGroupController>(theme, bar_->controller());
    columnPanel_ = new VerticalTabPanel(theme, this); columnPanel_->hide(); columnPanel_->installEventFilter(this);
    motionClock_.start();
    motionDriver_.advance = [this](qint64 timestampNs) {
        edit([&] { group_->advance(timestampNs); });
    };
    bar_->surfaceChanged = [this] {
        const auto callback = surfaceInvalidated; if (callback) callback();
    };
    bar_->installEventFilter(this);
    const int row = bar_->metrics().rowHeight;
    // Right after the last tab: new tab. At the first row's end, from the
    // right: the complete list, then sidebar actions (tab strip design 2.1).
    trailing_ = new QWidget(this); trailing_->setObjectName("TabCreateActions");
    auto* actions = new QHBoxLayout(trailing_); actions->setContentsMargins(4, 0, 0, 0); actions->setSpacing(0);
    auto* create = new TabCreateButtons(theme, trailing_); createButtons_ = create;
    create->setHeight(row);
    actions->addWidget(create);
    trailing_->installEventFilter(this);
    bar_->setTrailingWidget(trailing_);
    end_ = new QWidget(this); end_->setObjectName("TabHeaderActions");
    auto* ending = new QHBoxLayout(end_); ending->setContentsMargins(0, 0, 0, 0); ending->setSpacing(bar_->metrics().controlGap);
    list_ = new CompactToolButton(end_, QSize(bar_->metrics().listWidth, row)); list_->setObjectName("TabListButton");
    list_->setMouseTracking(true);
    setThemedIcon(list_, "tab-list", theme); list_->setToolTip(QCoreApplication::translate("WindowSystem", "All tabs"));
    list_->setAccessibleName(list_->toolTip());
    // A named group shows its name before the end controls, quietly.
    name_ = new QLabel(end_); name_->setObjectName("TabGroupName"); name_->hide();
    auto nameFont = name_->font(); nameFont.setPointSizeF(std::max(7.0, nameFont.pointSizeF() - 1)); nameFont.setWeight(QFont::DemiBold);
    name_->setFont(nameFont); name_->setContentsMargins(6, 0, 6, 0);
    uiFontDelta(name_, -96.0 / 72.0);
    ending->addWidget(name_);
    ending->addWidget(list_);
    title_ = new QLabel(this); title_->setObjectName("TabGroupTitle"); title_->setFont(nameFont); title_->hide();
    uiFontDelta(title_, -96.0 / 72.0);
    end_->installEventFilter(this);
    bar_->setEndWidget(end_);
    // A vertical strip's footer: new tab, sidebar actions, icon mode.
    footer_ = new QWidget(this); footer_->setObjectName("TabStripFooter"); footer_->hide();
    auto* footer = new QBoxLayout(QBoxLayout::LeftToRight, footer_);
    footer->setContentsMargins(bar_->metrics().verticalInset, 0, bar_->metrics().verticalInset, 0); footer->setSpacing(2);
    iconModeButton_ = new CompactToolButton(footer_, QSize(uiPixels(26), row)); iconModeButton_->setObjectName("TabStripIconModeButton");
    footer_->installEventFilter(this);
    QObject::connect(iconModeButton_, &QToolButton::clicked, this, [this] { edit([&] { group_->toggleIconMode(); }); });
    resizeHandle_ = new QWidget(this); resizeHandle_->setObjectName("TabStripResizeHandle");
    resizeHandle_->setCursor(Qt::SizeHorCursor); resizeHandle_->hide(); resizeHandle_->installEventFilter(this);
    autoScroll_ = new QTimer(this); autoScroll_->setInterval(200);
    QObject::connect(autoScroll_, &QTimer::timeout, this, [this] {
        bool moved = false;
        bar_->edit([&] { moved = group_->autoScrollStep(scrollDirection_); });
        if (!moved) { autoScroll_->stop(); return; }
        if (autoScroll_->interval() != 35) autoScroll_->setInterval(35);
    });
    QObject::connect(create->add, &QToolButton::clicked, this, [this] {
        const auto callback = newTabRequested; if (callback) callback();
    });
    QObject::connect(create->types, &QToolButton::clicked, this, [this] { bar_->showColumnPopup([this] { showNewTabMenu(); }); });
    create->menuRequested = [this] { bar_->showColumnPopup([this] { showNewTabMenu(); }); };
    QObject::connect(bar_, &TabBar::currentChanged, this, [this](int index) {
        const QPointer<GroupTabs> self(this);
        stack_->setCurrentIndex(index);
        if (!self) return;
        if (index >= 0) resolvePage(bar_->tabData(index).toUuid());
        if (!self) return;
        layoutChildren();
        const auto callback = currentChanged; if (callback) callback(index);
    });
    QObject::connect(bar_, &TabBar::viewChanged, this, [this] {
        edit([&] { group_->viewChanged(); });
        const auto callback = hoverGeometryChanged; if (callback) callback();
    });
    QObject::connect(bar_, &TabBar::scrollInputStarted, this, [this] { edit([&] { group_->cancelViewportReflow(); }); });
    bar_->close = [this](int index) { const auto callback = closeRequested; if (callback) callback(index); };
    bar_->unstick = [this](int index) { const auto callback = unstickRequested; if (callback) callback(index); };
    bar_->reservedSizeChanged = [this] { layoutChildren(); };
    QObject::connect(list_, &QToolButton::clicked, this, [this] { bar_->showColumnPopup([this] { showTabList(); }); });
    QObject::connect(qApp, &QApplication::focusChanged, this, [this] { refreshFocus(); });
}

template <class Apply> void GroupTabs::edit(Apply&& apply)
{
    const QPointer<GroupTabs> self(this);
    bar_->edit([&] { prepareGroup(); apply(); });
    if (self) applyGroupChanges();
}

void GroupTabs::prepareGroup()
{
    TabGroupEnvironment environment;
    environment.size = size(); environment.originInWindow = mapTo(window(), QPoint());
    environment.windowClientBounds = window()->geometry();
    environment.contentMinimumWidth = stack_->minimumSizeHint().expandedTo(stack_->minimumSize()).width();
    environment.visible = isVisible(); environment.windowActive = window()->isActiveWindow();
    environment.animations = TabBar::animationsAllowed();
    group_->setEnvironment(environment);
    group_->setTimestampNs(motionDriver_.now(motionClock_.nsecsElapsed()));
}

void GroupTabs::applyGroupChanges()
{
    const QPointer<GroupTabs> self(this);
    const auto changes = group_->takeChanges();
    if (changes.layout) layoutChildren();
    if (!self) return;
    if (!group_->needsFrame()) motionDriver_.stop();
    else if (changes.motion) {
        const auto timestamp = motionDriver_.now(motionClock_.nsecsElapsed());
        motionDriver_.restart(timestamp, group_->motionDurationMs());
    }
    if (changes.reflow && !viewportReflowQueued_) {
        viewportReflowQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            viewportReflowQueued_ = false;
            edit([&] { group_->applyViewportReflow(); });
        });
    }
    if (changes.scrollRestore) QTimer::singleShot(0, this, [this] { edit([&] { group_->finishScrollOffsetRestore(); }); });
    if (changes.surface) if (const auto callback = surfaceInvalidated) callback();
    if (!self) return;
    if (changes.columnState) if (const auto callback = verticalStateChanged) callback(changes.columnState->first, changes.columnState->second);
}

TabBar* GroupTabs::tabBar() const { return bar_; }
TabGroupSurfaceFrame GroupTabs::surfaceFrame() const
{
    if (stack_->isHidden()) return {};
    return group_->surfaceFrame();
}
QRect GroupTabs::surfaceBounds() const { return surfaceFrame().bounds; }
QRegion GroupTabs::surfacePaintRegion() const { return surfaceFrame().paintRegion; }
void GroupTabs::paintSurface(QPainter& painter, ElevationRenderer& renderer, QPoint offset, const QRegion& exposed) const
{
    if (stack_->isHidden()) return;
    const auto frame = group_->surfaceFrame(true);
    if (!exposed.intersects(frame.bounds.translated(offset))) return;
    paintTabGroupSurface(painter, renderer, frame, offset, exposed);
}

void GroupTabs::invalidateSurfaceCorners(const QRect& area)
{
    if (card_ && card_->isVisible())
        static_cast<CardOverlay*>(card_)->invalidate(area.translated(-card_->pos()));
}
void GroupTabs::refreshFocus()
{
    const auto callback = isActiveGroup;
    setGroupFocused(window()->isActiveWindow() && callback && callback());
}
GroupTabs::~GroupTabs()
{
    autoScroll_->stop();
    // Focus changes while the window goes away must not reach the destroyed strip.
    disconnect(qApp, nullptr, this, nullptr);
    if (viewportWindow_) viewportWindow_->removeEventFilter(this);
    for (auto* child : findChildren<QObject*>()) {
        child->removeEventFilter(this);
        disconnect(child, nullptr, this, nullptr);
    }
}
bool GroupTabs::vertical() const { return bar_->vertical(); }
void GroupTabs::placeControls()
{
    auto* create = static_cast<TabCreateButtons*>(createButtons_);
    auto* footer = static_cast<QBoxLayout*>(footer_->layout());
    auto* trailing = static_cast<QHBoxLayout*>(trailing_->layout());
    auto* ending = static_cast<QHBoxLayout*>(end_->layout());
    const auto& sidebar = sidebarButtons_;
    const int row = bar_->metrics().rowHeight;
    const bool compact = group_->compactPresentation();
    create->types->setTargetSize(QSize(uiPixels(listButtonWidth), std::max(1, row - 4)));
    if (vertical()) {
        const bool iconMode = group_->iconMode();
        const bool icons = iconMode && !bar_->peeking();
        const bool left = bar_->placement() == TabBarPlacement::Left;
        if (create->parentWidget() != footer_) {
            // The trailing controls move into the footer.
            bar_->setTrailingWidget(nullptr); trailing_->setParent(this); trailing_->hide();
            bar_->setEndWidget(nullptr); end_->setParent(this); end_->hide();
            while (auto* item = footer->takeAt(0)) delete item;
            create->setParent(footer_); footer->addWidget(create);
            footer->addStretch(1);
            for (auto* button : sidebar) { button->setParent(footer_); footer->addWidget(button); }
            footer->addWidget(iconModeButton_);
        }
        footer->setDirection(left ? QBoxLayout::LeftToRight : QBoxLayout::RightToLeft);
        create->setReversed(!left);
        // A labeled new-tab button, or only its icon in a narrow column.
        create->add->setText(QCoreApplication::translate("WindowSystem", "New tab"));
        const auto& metrics = bar_->metrics();
        const auto controls = layoutTabColumnControls({group_->shownColumnWidth(), metrics.verticalInset, row, metrics.newTabWidth,
            iconModeButton_->sizeHint().width(), metrics.typeWidth, metrics.controlGap, uiPixels(tabActionSize),
            int(sidebar.size()), fontMetrics().horizontalAdvance(create->add->text()), icons, compact});
        iconModeButton_->setVisible(controls.toggle);
        create->add->setToolButtonStyle(controls.text ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        create->add->setTargetSize(controls.primary);
        create->add->setIconRail(metrics.verticalIconWidth / 2 - metrics.verticalInset, left ? Qt::LeftEdge : Qt::RightEdge);
        create->types->setVisible(controls.types);
        create->setFixedSize(controls.combined);
        for (auto* button : sidebar) button->setVisible(controls.actions);
        setThemedIcon(iconModeButton_, iconMode == left ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"), theme_);
        iconModeButton_->setToolTip(iconMode ? QCoreApplication::translate("WindowSystem", "Show tab titles")
                                             : QCoreApplication::translate("WindowSystem", "Show only tab icons"));
        iconModeButton_->setAccessibleName(iconModeButton_->toolTip());
    } else if (create->parentWidget() != trailing_) {
        while (auto* item = footer->takeAt(0)) delete item;
        create->setParent(trailing_); trailing->insertWidget(0, create);
        for (int index = 0; index < sidebar.size(); ++index) {
            sidebar.at(index)->setParent(end_); ending->insertWidget(index, sidebar.at(index));
        }
        end_->show();
        bar_->setEndWidget(end_);
        iconModeButton_->setParent(footer_);
        create->add->setText({}); create->add->setToolButtonStyle(Qt::ToolButtonIconOnly);
        create->add->setIconRail();
        create->setReversed(false);
        create->types->show();
        create->setHeight(row);
        trailing_->show();
        bar_->setTrailingWidget(trailing_);
    }
    if (!vertical()) for (auto* button : sidebar) button->setVisible(!compact);
    create->setVisible(!compact && group_->appearance().showNewTabButton);
    // Without its buttons the trailing area takes no room after the last tab.
    if (!vertical()) trailing_->setVisible(!create->isHidden());
    list_->setVisible(!compact);
}

void GroupTabs::layoutChildren()
{
    if (layingOut_) return;
    QScopedValueRollback guard(layingOut_, true);
    const QPointer<GroupTabs> self(this);
    if (vertical()) {
        if (bar_->parentWidget() != columnPanel_) {
            bar_->setParent(columnPanel_); footer_->setParent(columnPanel_); title_->setParent(columnPanel_);
        }
    } else if (bar_->parentWidget() == columnPanel_) {
        bar_->setParent(this); footer_->setParent(this); title_->setParent(this);
    }
    TabGroupPlacement placement;
    bar_->edit([&] { prepareGroup(); placement = group_->place(); });
    if (!self) return;
    placeControls();
    const auto& geometry = placement.geometry;
    if (!card_) card_ = new CardOverlay(theme_, this);
    auto* card = static_cast<CardOverlay*>(card_);
    const auto placeCard = [&] {
        stack_->setGeometry(geometry.content); card->setGeometry(geometry.card); card->setCard(geometry.cardBody);
        card->setVisible(geometry.cardVisible); card->raise();
    };
    if (!group_->tabBarVisible()) {
        bar_->hide(); footer_->hide(); resizeHandle_->hide(); title_->hide(); columnPanel_->hide();
        placeCard();
    } else if (vertical()) {
        bar_->show();
        columnPanel_->setGeometry(geometry.panel);
        static_cast<VerticalTabPanel*>(columnPanel_)->setSurface(geometry.panelBody, group_->columnPeek(), geometry.panelOutset);
        columnPanel_->setVisible(geometry.panelVisible);
        if (!group_->groupName().isEmpty()) {
            title_->setGeometry(geometry.title);
            title_->setText(title_->fontMetrics().elidedText(group_->groupName(), Qt::ElideRight, title_->width()));
        }
        title_->setVisible(geometry.titleVisible);
        bar_->setGeometry(geometry.bar); footer_->setGeometry(geometry.footer); footer_->setVisible(geometry.footerVisible);
        placeCard();
        resizeHandle_->setGeometry(geometry.resizeHandle); resizeHandle_->setVisible(geometry.resizeVisible);
        resizeHandle_->raise(); columnPanel_->raise(); resizeHandle_->raise();
    } else {
        bar_->show();
        columnPanel_->hide(); footer_->hide(); resizeHandle_->hide(); title_->hide();
        bar_->setGeometry(geometry.bar); placeCard();
        bar_->raise();
    }
    bar_->edit([&] { prepareGroup(); group_->placed(); });
    if (!self) return;
    if (contentPreview_ && contentPreview_->isVisible()) { contentPreview_->setGeometry(stack_->geometry()); contentPreview_->raise(); card->raise(); bar_->raise(); if (vertical()) columnPanel_->raise(); }
    footer_->raise();
    bar_->refreshPresentationLayers();
    if (!group_->layoutRunning() && !bar_->rowTransitionRunning())
        updateGeometry();
    applyGroupChanges();
    if (!self) return;
    const auto callback = hoverGeometryChanged; if (callback) callback();
}
QSize GroupTabs::sizeHint() const
{
    return group_->preferredSize(stack_->sizeHint());
}
QSize GroupTabs::minimumSizeHint() const
{
    return group_->minimumSize(stack_->minimumSizeHint().expandedTo(stack_->minimumSize()));
}
void GroupTabs::setContentEdgeToEdge(bool enabled)
{
    if (group_->contentEdgeToEdge() == enabled) return;
    const QPointer<GroupTabs> self(this);
    edit([&] { group_->setContentEdgeToEdge(enabled); });
    if (self) updateGeometry();
}

void GroupTabs::setTabBarVisible(bool visible)
{
    if (group_->tabBarVisible() == visible) return;
    if (!visible) {
        autoScroll_->stop(); scrollDirection_ = {};
        if (tabListMenu_) tabListMenu_->close();
        if (newTabMenu_) newTabMenu_->close();
    }
    edit([&] { group_->setTabBarVisible(visible); });
}
void GroupTabs::addTab(QWidget* widget, QIcon icon, QString title, TabId id) {
    const bool first = bar_->count() == 0;
    QWidget* page = widget;
    if (!page) {
        page = new TabPagePlaceholder(id, stack_);
        placeholders_.insert(id);
    }
    int index = -1;
    {
        const QSignalBlocker quietBar(bar_);
        const QSignalBlocker quietStack(stack_);
        stack_->addWidget(page);
        index = bar_->addTab(icon, title);
        bar_->setTabData(index, id); bar_->setTabToolTip(index, title);
    }
    pages_.insert(id, page);
    if (first) stack_->setCurrentIndex(0);
    layoutChildren();
}
QWidget* GroupTabs::takePage(TabId id)
{
    const QPointer<QWidget> page = pages_.take(id);
    const bool placeholder = placeholders_.remove(id);
    if (const int index = bar_->indexOf(id); index >= 0) {
        const QSignalBlocker quiet(bar_);
        bar_->removeTab(index);
    }
    const bool stacked = page && stack_->indexOf(page) >= 0;
    if (stacked) stack_->removeWidget(page);
    if (stacked && placeholder) page->deleteLater();
    if (bar_->currentIndex() >= 0) stack_->setCurrentIndex(bar_->currentIndex());
    layoutChildren();
    return stacked && !placeholder ? page.data() : nullptr;
}
void GroupTabs::setCurrentIndex(int index, bool reveal) {
    edit([&] { group_->setCurrentIndex(index, reveal); });
    if (index >= 0 && index < bar_->count()) resolvePage(bar_->tabData(index).toUuid());
    stack_->setCurrentIndex(index);
}
int GroupTabs::scrollOffset() const { return group_->scrollOffset(); }
TabViewportState GroupTabs::captureViewportState() const { return group_->captureViewportState(); }
void GroupTabs::restoreViewportState(TabViewportState state)
{
    edit([&] { group_->restoreViewportState(std::move(state)); });
}
void GroupTabs::restoreScrollOffset(int offset)
{
    edit([&] { group_->restoreScrollOffset(offset); });
}
void GroupTabs::scrollByWheel(QPoint delta)
{
    const auto viewport = bar_->scrollViewport();
    bar_->scrollAt(viewport.isEmpty() ? bar_->rect().center() : viewport.center(), delta);
}
bool GroupTabs::scrollStripAt(QPoint global, QPoint delta, bool smooth)
{
    if (!group_->tabBarVisible() || !QRect(bar_->mapToGlobal(QPoint()), bar_->size()).contains(global)) return false;
    // The scroll may destroy this group; touch nothing after it.
    bar_->scrollAt(bar_->mapFromGlobal(global), delta, smooth);
    return true;
}
int GroupTabs::currentIndex() const { return bar_->currentIndex(); }
int GroupTabs::count() const { return bar_->count(); }
QWidget* GroupTabs::page(int index) const { return stack_->widget(index); }
bool GroupTabs::pageIsPlaceholder(TabId tab) const { return placeholders_.contains(tab); }
void GroupTabs::resolvePage(TabId tab)
{
    if (!placeholders_.contains(tab) || !pageRequested) return;
    const QPointer<GroupTabs> retained(this);
    const QPointer<QWidget> placeholder = pages_.value(tab);
    const int index = bar_->indexOf(tab);
    if (index < 0 || !placeholder || stack_->widget(index) != placeholder) return;
    const QPointer<QWidget> page(pageRequested(tab));
    if (!retained || !page || pages_.value(tab) != placeholder
        || stack_->widget(index) != placeholder) return;
    placeholder->hide();
    stack_->insertWidget(index, page);
    pages_.insert(tab, page);
    placeholders_.remove(tab);
    stack_->removeWidget(placeholder);
    placeholder->deleteLater();
    stack_->setCurrentIndex(bar_->currentIndex());
}
void GroupTabs::setTheme(const Theme& theme)
{
    theme_ = theme;
    bar_->setTheme(theme);
    static_cast<VerticalTabPanel*>(columnPanel_)->setTheme(theme);
    if (card_) static_cast<CardOverlay*>(card_)->setTheme(theme);
    for (auto* button : findChildren<QAbstractButton*>()) {
        const auto name = button->property("themeIcon").toString();
        if (!name.isEmpty()) button->setIcon(iconFor(name, theme));
    }
    updateGeometry();
    edit([&] { group_->setTheme(theme); });
}

void GroupTabs::setAppearance(const TabAppearance& appearance, bool pinned)
{
    edit([&] { group_->setAppearance(appearance, pinned); });
}
void GroupTabs::setStickyCount(int count)
{
    if (bar_->stickyCount() == count) return;
    bar_->setStickyCount(count); layoutChildren();
}
void GroupTabs::setTabMetadata(int index, const TabRecord& tab) { bar_->setTabMetadata(index, tab); }
GroupTabs::SurfaceState GroupTabs::captureSurfaceState() const { return group_->captureSurfaceState(); }

void GroupTabs::inheritSurface(const SurfaceState& previous, bool animate)
{
    edit([&] { group_->inheritSurface(previous, animate); });
}

void GroupTabs::setGroupFocused(bool focused)
{
    edit([&] { group_->setGroupFocused(focused); });
}
void GroupTabs::setGroupName(const QString& name)
{
    edit([&] { group_->setGroupName(name); });
    const bool shown = group_->placement().nameShown;
    if (name_->text() == name && name_->isVisible() == shown) return;
    name_->setText(name); name_->setToolTip(name); name_->setVisible(shown);
    title_->setToolTip(name);
    bar_->refreshTrailing();
    layoutChildren();
}
void GroupTabs::setVerticalState(int width, bool iconMode)
{
    edit([&] { group_->setVerticalState(width, iconMode); });
}
QRect GroupTabs::viewportRectGlobal() const {
    if (!group_->tabBarVisible() || bar_->isHidden()) return {};
    return QRect(bar_->mapToGlobal(QPoint()), bar_->size());
}
QRect GroupTabs::contentRectGlobal() const
{
    return QRect(stack_->mapToGlobal(QPoint()), stack_->size());
}
QRect GroupTabs::stripPanelRectGlobal() const
{
    if (!group_->tabBarVisible() || bar_->isHidden()) return {};
    if (!vertical()) return viewportRectGlobal();
    const auto body = static_cast<VerticalTabPanel*>(columnPanel_)->contentRect();
    return QRect(columnPanel_->mapToGlobal(body.topLeft()), body.size());
}
void GroupTabs::restoreColumnPeek()
{
    edit([&] { group_->restoreColumnPeek(); });
}
int GroupTabs::insertionIndex(QPoint global, TabLayoutInput::Carried carried) const {
    int index = -1;
    bar_->edit([&] { index = group_->insertionIndex(global, carried); });
    return index;
}
void GroupTabs::setLiftedTab(QPoint global, QPoint grab)
{
    setLiftedTabWithAnchor(global, {grab, {}});
}

void GroupTabs::setLiftedTabWithAnchor(QPoint global, const DragAnchor& grab)
{
    edit([&] { group_->setLiftedTab(global, grab); });
}
void GroupTabs::clearLiftedTab()
{
    edit([&] { group_->clearLiftedTab(); });
}
void GroupTabs::setContentPreview(const QPixmap& picture)
{
    if (picture.isNull()) { if (contentPreview_) contentPreview_->hide(); return; }
    if (!contentPreview_) contentPreview_ = new ContentPreview(this);
    static_cast<ContentPreview*>(contentPreview_)->setPicture(picture);
    contentPreview_->setGeometry(stack_->geometry());
    contentPreview_->show(); contentPreview_->raise();
    bar_->raise();
}
void GroupTabs::setInsertionPreview(int index, const QVector<CarriedLabel>& carried, bool carriedSticky, TabLayoutInput::Carried kind)
{
    edit([&] { group_->setInsertionPreview(index, carried, carriedSticky, kind); });
}
bool GroupTabs::insertionSticky() const { return bar_->dropSticky(); }
QUuid GroupTabs::insertionFolder() const
{
    const int folder = bar_->dropFolder();
    return folder >= 0 && folder < bar_->folders().size() ? bar_->folders().at(folder).id : QUuid{};
}
void GroupTabs::setDraggedFolder(QUuid folder) { bar_->setDraggedFolder(folder); }
DragAnchor GroupTabs::folderGrabAnchor(QUuid folder, QPoint global) const
{
    DragAnchor anchor;
    bar_->edit([&] { anchor = group_->folderGrabAnchor(folder, global); });
    return anchor;
}
std::optional<bool> GroupTabs::insertionRow(QPoint) const { return std::nullopt; }
void GroupTabs::setDraggedTab(TabId tab) { bar_->setDraggedTab(tab); }
QRect GroupTabs::insertionPreviewRectGlobal() const
{
    const auto area = bar_->dropPreviewRect();
    return area.isEmpty() ? QRect{} : QRect(bar_->mapToGlobal(area.topLeft()), area.size()).intersected(viewportRectGlobal());
}
void GroupTabs::setDragPresentation(bool compact, bool edge)
{
    if (compact && edge && group_->tabBarVisible()) {
        autoScroll_->stop(); scrollDirection_ = {};
        if (tabListMenu_) tabListMenu_->close();
        if (newTabMenu_) newTabMenu_->close();
    }
    edit([&] { group_->setDragPresentation(compact, edge); });
    bar_->refreshTrailing();
    layoutChildren();
}
bool GroupTabs::setDragTabStyle(const TabBar& target, bool sticky)
{
    bool changed = false;
    edit([&] { changed = group_->setDragTabStyle(target.controller(), sticky); });
    return changed;
}
void GroupTabs::setIncomingVisual(TabDragVisual visual, QPoint global)
{
    bar_->setIncomingVisual(std::move(visual), bar_->mapFromGlobal(global));
}
void GroupTabs::setDragHover(QPoint global) {
    QPoint direction;
    bar_->edit([&] { direction = group_->setDragHover(global); });
    if (direction != scrollDirection_) autoScroll_->stop();
    scrollDirection_ = direction;
    if (!direction.isNull() && bar_->isOverflowing() && !autoScroll_->isActive()) { autoScroll_->setInterval(200); autoScroll_->start(); }
    else if (direction.isNull()) autoScroll_->stop();
}
void GroupTabs::clearDragHover(QPoint global)
{
    const bool hovered = vertical() && stripPanelRectGlobal().contains(global);
    scrollDirection_ = {}; autoScroll_->stop();
    bar_->edit([&] { group_->clearDragHover(hovered); });
}
void GroupTabs::setPinnedActions(std::function<void()> current, std::function<void()> all) {
    QWidget* owner = vertical() ? footer_ : end_;
    for (auto item : {std::tuple{std::move(current), QCoreApplication::translate("WindowSystem", "Unpin current tab"), QStringLiteral("unpin")},
                      std::tuple{std::move(all), QCoreApplication::translate("WindowSystem", "Unpin all sidebar tabs"), QStringLiteral("unpin-all")}}) {
        auto* button = new CompactToolButton(owner, QSize(uiPixels(20), bar_->metrics().rowHeight)); setThemedIcon(button, std::get<2>(item), theme_);
        button->setObjectName(std::get<2>(item) == "unpin-all" ? "UnpinAllButton" : "UnpinCurrentButton");
        button->setToolTip(std::get<1>(item)); button->setAccessibleName(std::get<1>(item));
        sidebarButtons_.append(button);
        if (vertical()) static_cast<QBoxLayout*>(footer_->layout())->insertWidget(footer_->layout()->count() - 1, button);
        else { auto* row = static_cast<QHBoxLayout*>(end_->layout()); row->insertWidget(row->count() - 1, button); }
        QObject::connect(button, &QToolButton::clicked, this, [callback = std::move(std::get<0>(item))] { if (callback) callback(); });
    }
    bar_->refreshTrailing();
    layoutChildren();
}
bool GroupTabs::event(QEvent* event) {
    // The children are placed by hand, not by a layout: pass a page's new
    // size limits on to the parent layout.
    if (event->type() == QEvent::LayoutRequest) { updateGeometry(); layoutChildren(); }
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        const auto callback = surfaceInvalidated; if (callback) callback();
    }
    return QFrame::event(event);
}
bool GroupTabs::eventFilter(QObject* watched, QEvent* event) {
    if (watched == columnPanel_) {
        if (event->type() == QEvent::Enter) bar_->setColumnHovered(true);
        if (event->type() == QEvent::Leave && !stripPanelRectGlobal().contains(QCursor::pos()))
            bar_->setColumnHovered(false);
    }
    if (event->type() == QEvent::ContextMenu && (watched == footer_ || watched == trailing_ || watched == end_)) {
        const auto callback = groupMenuRequested;
        if (callback) bar_->showColumnPopup([callback, global = static_cast<QContextMenuEvent*>(event)->globalPos()] { callback(global); });
        return true;
    }
    if ((event->type() == QEvent::Show && (watched == viewportWindow_ || watched == bar_))
        || (watched == viewportWindow_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize)))
        edit([&] { group_->queueViewportReflow(); });
    if (watched == viewportWindow_ && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate))
        refreshFocus();
    if (watched == resizeHandle_) {
        // Dragging the vertical strip's inner edge resizes it; a double
        // click returns it to the default width.
        if (event->type() == QEvent::MouseButtonPress) {
            if (static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton) return false;
            const int x = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x();
            edit([&] { group_->beginColumnResize(x); });
            return true;
        }
        if (event->type() == QEvent::MouseMove && group_->columnResizing()) {
            const int x = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x();
            edit([&] { group_->moveColumnResize(x); });
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && group_->columnResizing()) {
            edit([&] { group_->endColumnResize(); });
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            edit([&] { group_->resetColumnWidth(); });
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}
void GroupTabs::resizeEvent(QResizeEvent* event) {
    // Capture the settled viewport before the strip takes the new size, which
    // would otherwise replace it with the resized one.
    QFrame::resizeEvent(event); edit([] {}); layoutChildren();
}
void GroupTabs::moveEvent(QMoveEvent* event) { QFrame::moveEvent(event); edit([&] { group_->updateCardJoin(); }); }
void GroupTabs::showTabList() {
    if (tabListMenu_ && tabListMenu_->isVisible()) { tabListMenu_->close(); return; }
    auto* menu = new DismissOnlyMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose);
    tabListMenu_ = menu;
    list_->setDown(true);
    QObject::connect(menu, &QMenu::aboutToHide, this, [this] { list_->setDown(false); list_->update(); });
    menu->setStyleSheet("QMenu{menu-scrollable:1;}");
    for (int index = 0; index < bar_->count(); ++index) {
        auto* action = new QWidgetAction(menu);
        action->setCheckable(true); action->setChecked(index == bar_->currentIndex());
        const auto* preset = bar_->tabPreset(index);
        auto* row = new TabListRow(bar_->tabText(index), group_->appearance().showIcons ? bar_->tabIcon(index) : QIcon{},
            preset ? preset->groupInactive.line : QColor{}, index == bar_->currentIndex(),
            index < bar_->stickyCount() ? iconFor("sticky", theme_) : QIcon{}, menu);
        row->setProperty("tabId", bar_->tabData(index));
        row->closeButton->setIcon(iconFor("close", theme_)); row->closeButton->setToolTip(QCoreApplication::translate("WindowSystem", "Close tab"));
        row->closeButton->setAccessibleName(row->closeButton->toolTip());
        const QPointer<GroupTabs> self(this); const QPointer<QMenu> popup(menu);
        // Closing a popup can run host callbacks that reorder the tabs. Each
        // action resolves its stable tab ID afterwards, not the listed index.
        const auto act = [self, popup, id = bar_->tabData(index).toUuid()](auto action) {
            return [self, popup, id, action] {
                if (popup) popup->close();
                if (!self) return;
                if (const int at = self->bar_->indexOf(id); at >= 0) action(self.data(), at);
            };
        };
        row->activate = act([](GroupTabs* tabs, int i) { tabs->setCurrentIndex(i); });
        QObject::connect(row->closeButton, &QToolButton::clicked, menu,
            act([](GroupTabs* tabs, int i) { const auto callback = tabs->closeRequested; if (callback) callback(i); }));
        if (row->unstickButton)
            QObject::connect(row->unstickButton, &QToolButton::clicked, menu,
                act([](GroupTabs* tabs, int i) { const auto callback = tabs->unstickRequested; if (callback) callback(i); }));
        action->setDefaultWidget(row); menu->addAction(action);
    }
    auto* anchor = list_->isVisible() ? static_cast<QWidget*>(list_) : static_cast<QWidget*>(bar_);
    setPopupAnchor(menu, QRect(anchor->mapToGlobal(QPoint()), anchor->size()));
    menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

void GroupTabs::showNewTabMenu()
{
    if (newTabMenu_ && newTabMenu_->isVisible()) { newTabMenu_->close(); return; }
    const auto callback = newTabMenuRequested;
    if (!callback) return;
    QPointer<GroupTabs> self(this);
    auto* menu = callback(this);
    if (!self || !menu) return;
    newTabMenu_ = menu; menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* create = static_cast<TabCreateButtons*>(createButtons_);
    create->setPopupOpen(true);
    QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
        static_cast<TabCreateButtons*>(createButtons_)->setPopupOpen(false);
    });
    setPopupAnchor(menu,QRect(create->types->mapToGlobal(QPoint()),create->types->size()));
    menu->popup(create->types->mapToGlobal(QPoint(0, create->types->height())));
}

} // namespace window_system
