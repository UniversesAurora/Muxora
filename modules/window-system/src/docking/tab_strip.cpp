#include <window_system/interface_appearance.hpp>
#include <QCoreApplication>
#include "tab_strip.hpp"
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
#include <QVariantAnimation>
#include <QEasingCurve>
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
    VerticalTabPanel(const Theme& theme, QWidget* parent) : QWidget(parent), theme_(theme), painter_(panelMetrics(), TabPalette::forTheme(theme))
    {
        setObjectName("VerticalTabPanel"); setMouseTracking(true);
    }
    void setTheme(const Theme& theme) { theme_ = theme; painter_ = TabPainter(panelMetrics(), TabPalette::forTheme(theme)); update(); }
    void setSurface(QRect body, qreal overlay, int verticalOutset)
    {
        const bool changed = body_ != body || (overlay_ > 0) != (overlay > 0);
        body_ = body; content_ = body.adjusted(0, verticalOutset, 0, -verticalOutset); overlay_ = overlay;
        // The resting panel must not intercept its neighbor's content or
        // resize handle through the padding reserved for an overlay shadow.
        if (changed) { if (overlay > 0) clearMask(); else setMask(QRegion(body.adjusted(-uiPixels(TabMetrics{}.columnGap), 0, uiPixels(TabMetrics{}.columnGap), 0))); }
        update();
    }
    [[nodiscard]] QRect bodyRect() const { return body_; }
    [[nodiscard]] QRect contentRect() const { return content_; }
    void paintShell(QPainter& paint, QPoint offset = {}) const
    {
        if (overlay_ <= 0) return;
        paint.save(); paint.translate(offset); paint.setRenderHint(QPainter::Antialiasing);
        const QRectF body(body_);
        QPainterPath outline; outline.addRoundedRect(body, uiPixelsF(8), uiPixelsF(8));
        QPainterPath outside; outside.addRect(rect());
        paint.save(); paint.setClipPath(outside.subtracted(outline), Qt::IntersectClip);
        paint.setOpacity(overlay_); painter_.paintPanelShadow(paint, body, uiPixelsF(8), true); paint.restore();
        paint.setBrush(theme_.headerBackground);
        auto line = painter_.palette().tileBorder.resolve(window()->isActiveWindow()); line.setAlphaF(line.alphaF() * overlay_);
        paint.setPen(QPen(line, 1)); paint.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), uiPixelsF(8), uiPixelsF(8));
        paint.restore();
    }
protected:
    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::ActivationChange) update();
        QWidget::changeEvent(event);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter paint(this); paintShell(paint);
    }
private:
    TabMetrics panelMetrics() const
    {
        return TabMetrics::forTheme(theme_, font());
    }
    Theme theme_;
    TabPainter painter_;
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
    void paintEvent(QPaintEvent*) override
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
        surface->paintBackplate(painter, card_.translated(origin));
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
    columnPanel_ = new VerticalTabPanel(theme, this); columnPanel_->hide(); columnPanel_->installEventFilter(this);
    for (auto* motion : {&columnExpansion_, &peekExpansion_})
    {
        QObject::connect(motion, &QVariantAnimation::valueChanged, this, [this] { layoutChildren(); });
        QObject::connect(motion, &QVariantAnimation::finished, this, [this] { layoutChildren(); });
    }
    bar_->surfaceChanged = [this] {
        const auto callback = surfaceInvalidated; if (callback) callback();
    };
    QObject::connect(&surfaceFocus_, &QVariantAnimation::valueChanged, this, [this] { updateCardJoin(); });
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
    QObject::connect(iconModeButton_, &QToolButton::clicked, this, [this] {
        iconMode_ = !iconMode_;
        bar_->setIconMode(iconMode_);
        layoutChildren();
        if (const auto callback = verticalStateChanged) callback(verticalWidth_, iconMode_);
    });
    resizeHandle_ = new QWidget(this); resizeHandle_->setObjectName("TabStripResizeHandle");
    resizeHandle_->setCursor(Qt::SizeHorCursor); resizeHandle_->hide(); resizeHandle_->installEventFilter(this);
    autoScroll_ = new QTimer(this); autoScroll_->setInterval(200);
    QObject::connect(autoScroll_, &QTimer::timeout, this, [this] {
        cancelViewportReflow();
        if (scrollDirection_.isNull() || !bar_->scrollAt(dragProbe_, -scrollDirection_ * 12, false)) { autoScroll_->stop(); return; }
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
        if (!applyingViewportState_ && !pendingViewportState_ && !pendingScrollOffset_) settledViewport_ = currentViewportState();
        updateCardJoin();
        const auto callback = hoverGeometryChanged; if (callback) callback();
    });
    bar_->close = [this](int index) { const auto callback = closeRequested; if (callback) callback(index); };
    bar_->unstick = [this](int index) { const auto callback = unstickRequested; if (callback) callback(index); };
    bar_->reservedSizeChanged = [this] { layoutChildren(); };
    QObject::connect(list_, &QToolButton::clicked, this, [this] { bar_->showColumnPopup([this] { showTabList(); }); });
    QObject::connect(qApp, &QApplication::focusChanged, this, [this] { refreshFocus(); });
}
TabBar* GroupTabs::tabBar() const { return bar_; }
void GroupTabs::updateCardJoin()
{
    if (!stack_) return;
    const auto barOrigin = bar_->mapTo(this, QPoint());
    const QRectF body = compactPresentation_ ? QRectF{} : QRectF(stack_->geometry()).translated(-barOrigin);
    bar_->setSurfaceContext(body, surfaceFocus_.value(), 1 - peekExpansion_.value());
    const auto callback = surfaceInvalidated; if (callback) callback();
}
QRect GroupTabs::surfaceBounds() const
{
    if (compactPresentation_ || contentEdgeToEdge_ || !stack_ || stack_->isHidden()) return {};
    const auto profile = elevationProfile(ElevationLevel::BodyRaised);
    const auto margins = elevationMargins(profile);
    QRectF bounds = QRectF(stack_->geometry()).marginsAdded(margins);
    if (barVisible_) for (const auto& anchor : bar_->surfaceAnchors(bar_->mapTo(this, QPoint())))
        bounds |= anchor.geometry.anchorPath.boundingRect().marginsAdded(elevationMargins(anchor.elevation));
    if (vertical() && peekExpansion_.value() > 0) {
        const auto panel = static_cast<VerticalTabPanel*>(columnPanel_);
        bounds |= QRectF(panel->bodyRect().translated(panel->pos())).marginsAdded(
            elevationMargins(elevationProfile(ElevationLevel::ColumnOverlay)));
    }
    return bounds.toAlignedRect();
}
QRegion GroupTabs::surfacePaintRegion() const
{
    if (compactPresentation_ || contentEdgeToEdge_ || !stack_ || stack_->isHidden()) return {};
    // Elevation changes affect the perimeter, never the hosted content.
    // Keep the corners and every anchor, including a lifted outer row.
    const int inset = qCeil(bar_->metrics().cardRadius) + 1;
    QRegion region(surfaceBounds());
    region -= stack_->geometry().adjusted(inset, inset, -inset, -inset);
    return region;
}
void GroupTabs::paintSurface(QPainter& painter, ElevationRenderer& renderer, QPoint offset, const QRect& exposed) const
{
    if (compactPresentation_ || !stack_ || stack_->isHidden() || !surfaceBounds().translated(offset).intersects(exposed)) return;
    const QRectF body(stack_->geometry());
    const auto profile = interpolateElevation(elevationProfile(ElevationLevel::BodyRest),
        elevationProfile(ElevationLevel::BodyRaised), surfaceFocus_.value());
    auto style = elevationStyleForTheme(theme_);
    style.focused = surfaceFocused_;
    const auto anchors = barVisible_ ? bar_->surfaceAnchors(bar_->mapTo(this, QPoint())) : QVector<TabSurfaceAnchor>{};
    painter.save(); painter.translate(offset);
    painter.save();
    QPainterPath bodyRegion; bodyRegion.addRect(surfaceBounds());
    const auto bodyMargins = elevationMargins(profile);
    for (const auto& anchor : anchors) if (!anchor.lifted && !anchor.foreground && !anchor.geometry.compact) {
        const auto margins = elevationMargins(anchor.elevation);
        const qreal extent = std::ceil(std::max({margins.left(), margins.top(), margins.bottom(),
            bodyMargins.left(), bodyMargins.top(), bodyMargins.bottom()})) + 1;
        QPainterPath local; local.addRect(surfaceShadowClip(anchor.geometry, extent));
        bodyRegion = bodyRegion.subtracted(local);
    }
    painter.setClipPath(bodyRegion, Qt::IntersectClip);
    renderer.paintBody(painter, body, bar_->metrics().cardRadius, profile, style);
    painter.restore();
    QVector<ElevatedAnchor> shadows;
    for (const auto& anchor : anchors) if (!anchor.lifted && !anchor.foreground && !anchor.geometry.compact)
        shadows.append({anchor.geometry, anchor.elevation});
    renderer.paintAnchors(painter, shadows, profile, style);
    bar_->paintSurfaceExtension(painter, bar_->mapTo(this, QPoint()));
    if (vertical() && peekExpansion_.value() > 0) {
        // The shell can enter shared splitter margins. Paint only its part
        // outside this group here; the live panel paints the rest once.
        QPainterPath outside, group;
        outside.addRect(surfaceBounds()); group.addRect(rect());
        painter.save(); painter.setClipPath(outside.subtracted(group), Qt::IntersectClip);
        static_cast<VerticalTabPanel*>(columnPanel_)->paintShell(painter, columnPanel_->pos());
        painter.restore();
    }
    painter.restore();
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
int GroupTabs::columnWidth() const
{
    return std::clamp(uiPixels(verticalWidth_ > 0 ? verticalWidth_ : appearance_.verticalTabWidth),
        bar_->minimumColumnWidth(), compactPresentation_ ? uiPixels(maximumVerticalTabWidth) : maximumColumnWidth());
}
int GroupTabs::maximumColumnWidth() const
{
    const int margin = contentEdgeToEdge_ ? 0 : bar_->metrics().cardMargin;
    const int content = std::max(0, stack_->minimumSizeHint().expandedTo(stack_->minimumSize()).width());
    // The strip borrows space only from its own content, never from the
    // enclosing splitter. Keep the content's declared minimum intact.
    return std::clamp(width() - content - 2 * margin - bar_->metrics().columnGap, bar_->minimumColumnWidth(), maximumVerticalTabWidth);
}
void GroupTabs::placeControls()
{
    auto* create = static_cast<TabCreateButtons*>(createButtons_);
    auto* footer = static_cast<QBoxLayout*>(footer_->layout());
    auto* trailing = static_cast<QHBoxLayout*>(trailing_->layout());
    auto* ending = static_cast<QHBoxLayout*>(end_->layout());
    const auto& sidebar = sidebarButtons_;
    const int row = bar_->metrics().rowHeight;
    create->types->setTargetSize(QSize(uiPixels(listButtonWidth), std::max(1, row - 4)));
    if (vertical()) {
        const bool icons = iconMode_ && !bar_->peeking();
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
        const int shownWidth = qRound(metrics.verticalIconWidth + (columnWidth() - metrics.verticalIconWidth)
            * std::max(columnExpansion_.value(), peekExpansion_.value()));
        const int room = shownWidth - 2 * metrics.verticalInset;
        const bool showToggle = !icons && room >= metrics.newTabWidth + iconModeButton_->sizeHint().width() + 2;
        iconModeButton_->setVisible(showToggle);
        const int toggle = showToggle ? iconModeButton_->sizeHint().width() : 0;
        const int types = metrics.typeWidth + metrics.controlGap;
        const int actions = int(sidebar.size()) * (uiPixels(tabActionSize) + 2);
        const bool showActions = !icons && !compactPresentation_
            && room >= metrics.newTabWidth + types + toggle + actions + 2;
        const int remaining = room - toggle - (showActions ? actions : 0) - 2;
        const bool showTypes = !icons && remaining >= metrics.newTabWidth + types;
        const int fullLabel = metrics.newTabWidth + fontMetrics().horizontalAdvance(create->add->text()) + 8;
        const bool showText = !icons && remaining >= fullLabel + (showTypes ? types : 0);
        create->add->setToolButtonStyle(showText ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        const int label = showText ? fullLabel : metrics.newTabWidth;
        create->add->setTargetSize(QSize(label, std::max(1, row - 4)));
        create->add->setIconRail(metrics.verticalIconWidth / 2 - metrics.verticalInset, left ? Qt::LeftEdge : Qt::RightEdge);
        create->types->setVisible(showTypes);
        create->setFixedSize((showTypes ? types : 0) + label, row);
        for (auto* button : sidebar) button->setVisible(showActions);
        setThemedIcon(iconModeButton_, iconMode_ == left ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"), theme_);
        iconModeButton_->setToolTip(iconMode_ ? QCoreApplication::translate("WindowSystem", "Show tab titles")
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
    if (!vertical()) for (auto* button : sidebar) button->setVisible(!compactPresentation_);
    create->setVisible(!compactPresentation_ && appearance_.showNewTabButton);
    // Without its buttons the trailing area takes no room after the last tab.
    if (!vertical()) trailing_->setVisible(!create->isHidden());
    list_->setVisible(!compactPresentation_);
}
void GroupTabs::layoutChildren()
{
    if (layingOut_) return;
    QScopedValueRollback guard(layingOut_, true);
    if (vertical()) {
        const bool animate = isVisible() && !compactPresentation_ && !resizeOrigin_;
        const auto advance = [&](ProgressAnimation& motion, qreal target) {
            if (!animate) motion.jump(target);
            else if (motion.target() != target) motion.animateTo(target, Motion::Reveal, TabBar::animationsAllowed());
        };
        advance(columnExpansion_, iconMode_ ? 0.0 : 1.0);
        advance(peekExpansion_, iconMode_ && bar_->peeking() ? 1.0 : 0.0);
        if (bar_->parentWidget() != columnPanel_) {
            bar_->setParent(columnPanel_); footer_->setParent(columnPanel_); title_->setParent(columnPanel_);
        }
    } else if (bar_->parentWidget() == columnPanel_) {
        bar_->setParent(this); footer_->setParent(this); title_->setParent(this);
    }
    placeControls();
    // The group keeps a margin around its card; a drag carrier shows only
    // its tabs and needs none.
    const int margin = compactPresentation_ || contentEdgeToEdge_ ? 0 : bar_->metrics().cardMargin;
    const auto area = rect().adjusted(margin, margin, -margin, -margin);
    if (!card_) card_ = new CardOverlay(theme_, this);
    auto* card = static_cast<CardOverlay*>(card_);
    const auto placeCard = [&](QRect content) {
        stack_->setGeometry(content);
        card->setGeometry(content.adjusted(-margin, -margin, margin, margin));
        card->setCard(QRect(QPoint(margin, margin), content.size()));
        updateCardJoin();
        card->setVisible(!compactPresentation_ && !contentEdgeToEdge_);
        card->raise();
    };
    if (!barVisible_) {
        bar_->hide(); footer_->hide(); resizeHandle_->hide(); title_->hide(); columnPanel_->hide();
        placeCard(area);
        return;
    }
    bar_->show();
    if (vertical()) {
        const auto& metrics = bar_->metrics();
        const qreal reveal = std::max(columnExpansion_.value(), peekExpansion_.value());
        bar_->setColumnReveal(reveal, columnExpansion_.state() == QAbstractAnimation::Running);
        const int footerHeight = metrics.rowHeight;
        const bool titled = !groupName_.isEmpty() && appearance_.showGroupNames && !compactPresentation_;
        const int titleHeight = titled ? bar_->metrics().rowHeight : 0;
        const QSize available(columnWidth(), std::max(0, area.height() - footerHeight - titleHeight));
        bar_->setAvailableSize(available);
        const int reservedWidth = qRound(metrics.verticalIconWidth + (columnWidth() - metrics.verticalIconWidth) * columnExpansion_.value());
        const int shownWidth = qRound(metrics.verticalIconWidth + (columnWidth() - metrics.verticalIconWidth) * reveal);
        const bool left = bar_->placement() == TabBarPlacement::Left;
        const int x = area.left() + (left ? 0 : area.width() - shownWidth);
        const int pad = 3;
        const int outset = qRound(metrics.columnOverlayOutset * peekExpansion_.value());
        columnPanel_->setGeometry(x - pad, area.top() - pad - outset, shownWidth + 2 * pad, area.height() + 2 * (pad + outset));
        static_cast<VerticalTabPanel*>(columnPanel_)->setSurface(
            QRect(pad, pad, shownWidth, area.height() + 2 * outset), peekExpansion_.value(), outset);
        columnPanel_->show();
        if (titled) {
            const int inset = bar_->metrics().verticalInset + 4;
            title_->setGeometry(pad + inset, pad + outset, std::max(0, shownWidth - 2 * inset), titleHeight);
            title_->setText(title_->fontMetrics().elidedText(groupName_, Qt::ElideRight, title_->width()));
        }
        title_->setVisible(titled && reveal > 0);
        bar_->setGeometry(pad, pad + outset + titleHeight, shownWidth, available.height());
        footer_->setGeometry(pad, pad + outset + titleHeight + available.height(), shownWidth, footerHeight); footer_->setVisible(!compactPresentation_);
        // The column sits on the background beside the card, with a gap.
        const int gap = compactPresentation_ ? 0 : metrics.columnGap;
        placeCard(left ? QRect(area.left() + reservedWidth + gap, area.top(), area.width() - reservedWidth - gap, area.height())
                       : QRect(area.left(), area.top(), area.width() - reservedWidth - gap, area.height()));
        const int edge = area.left() + (left ? reservedWidth + gap - margin : area.width() - reservedWidth - gap + margin);
        resizeHandle_->setGeometry(edge - bar_->metrics().resizeBand / 2, area.top(), bar_->metrics().resizeBand, area.height());
        resizeHandle_->setVisible((!iconMode_ || resizeOrigin_) && !compactPresentation_);
        resizeHandle_->raise();
        columnPanel_->raise();
        resizeHandle_->raise();
    } else {
        columnPanel_->hide();
        footer_->hide(); resizeHandle_->hide(); title_->hide();
        const int availableWidth = compactPresentation_ && normalDragStyle_
            ? std::min(area.width(), dragTabArea_.width()) : area.width();
        const QSize strip(std::max(1, availableWidth), area.height());
        bar_->setAvailableSize(strip);
        const auto reserved = bar_->reservedSize(strip), shown = bar_->displaySize(strip);
        const bool top = bar_->placement() != TabBarPlacement::Bottom;
        bar_->setGeometry(area.left(), top ? area.top() : area.bottom() + 1 - shown.height(), strip.width(), shown.height());
        const QRect content(area.left(), top ? area.top() + reserved.height() : area.top(), area.width(), std::max(0, area.height() - reserved.height()));
        placeCard(content);
        bar_->raise();
        updateCardJoin();
    }
    if (lifting_) bar_->setLifted(bar_->mapFromGlobal(liftedGlobal_), liftedGrab_);
    if (contentPreview_ && contentPreview_->isVisible()) { contentPreview_->setGeometry(stack_->geometry()); contentPreview_->raise(); card->raise(); bar_->raise(); if (vertical()) columnPanel_->raise(); }
    footer_->raise();
    bar_->refreshPresentationLayers();
    if (columnExpansion_.state() != QAbstractAnimation::Running && peekExpansion_.state() != QAbstractAnimation::Running && !bar_->rowTransitionRunning())
        updateGeometry();
    const auto callback = hoverGeometryChanged; if (callback) callback();
}
QSize GroupTabs::sizeHint() const
{
    // The card keeps a margin all round; a vertical column a gap beside it.
    const int margin = compactPresentation_ || contentEdgeToEdge_ ? 0 : bar_->metrics().cardMargin;
    auto size = stack_->sizeHint() + QSize(2 * margin, 2 * margin);
    if (!barVisible_) return size;
    if (vertical()) size.rwidth() += columnWidth() + 2 * margin;
    else size.rheight() += bar_->metrics().rowHeight + bar_->metrics().outerMargin;
    return size;
}
QSize GroupTabs::minimumSizeHint() const
{
    const int margin = compactPresentation_ || contentEdgeToEdge_ ? 0 : bar_->metrics().cardMargin;
    auto size = stack_->minimumSizeHint().expandedTo(stack_->minimumSize()) + QSize(2 * margin, 2 * margin);
    if (!barVisible_) return size;
    // A requested column width must not become a splitter minimum: resizing
    // the column would otherwise resize this group and squeeze its neighbor.
    if (vertical()) size.rwidth() += (iconMode_ ? bar_->metrics().verticalIconWidth : bar_->minimumColumnWidth())
        + bar_->metrics().columnGap;
    else size.rheight() += bar_->reservedSize(QSize(std::max(width(), 1), height())).height();
    return size;
}
void GroupTabs::setContentEdgeToEdge(bool enabled)
{
    if (contentEdgeToEdge_ == enabled) return;
    const QPointer<GroupTabs> self(this);
    contentEdgeToEdge_ = enabled;
    layoutChildren();
    if (self) updateGeometry();
}

void GroupTabs::setTabBarVisible(bool visible)
{
    if (barVisible_ == visible) return;
    barVisible_ = visible;
    if (!visible) {
        autoScroll_->stop(); scrollDirection_ = {};
        if (tabListMenu_) tabListMenu_->close();
        if (newTabMenu_) newTabMenu_->close();
    }
    layoutChildren();
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
    bar_->setCurrentIndex(index);
    if (index >= 0 && index < bar_->count()) resolvePage(bar_->tabData(index).toUuid());
    stack_->setCurrentIndex(index);
    if (reveal && index >= 0) { cancelViewportReflow(); bar_->ensureVisible(index); }
}
int GroupTabs::scrollOffset() const { return pendingScrollOffset_.value_or(bar_->scrollOffset()); }
TabViewportState GroupTabs::currentViewportState() const
{
    TabViewportState state;
    state.offset = scrollOffset();
    state.stickyOffset = bar_->stickyScrollOffset();
    state.rowsOffset = bar_->rowScrollOffset();
    state.rowsExpanded = bar_->expanded();
    state.windowClientBounds = window()->geometry();
    if (isVisible() && barVisible_) {
        const auto viewport = bar_->scrollViewport();
        state.bounds = {bar_->mapTo(window(), viewport.topLeft()), viewport.size()};
    }
    const auto current = bar_->currentIndex();
    if (current >= 0) {
        state.activeTab = bar_->tabData(current).toUuid();
        state.activeFullyVisible = !state.bounds.isEmpty() && bar_->fullyVisible(current);
    }
    return state;
}
int GroupTabs::stickyInset(int index) const
{
    return index >= bar_->stickyCount() ? bar_->frozenExtent() : 0;
}
TabViewportState GroupTabs::captureViewportState() const
{
    // Consecutive API mutations before the next layout turn retain the same
    // origin instead of capturing an intermediate, incompletely sized strip.
    auto state = pendingViewportState_.value_or(currentViewportState());
    state.rowsExpanded = bar_->expanded();
    return state;
}
void GroupTabs::restoreViewportState(TabViewportState state)
{
    if (bar_->collapsible() && state.rowsExpanded) bar_->setExpanded(*state.rowsExpanded, false, false);
    pendingScrollOffset_.reset();
    pendingViewportState_ = std::move(state);
    applyPendingViewportState();
    queueViewportReflow();
}
void GroupTabs::cancelViewportReflow()
{
    pendingViewportState_.reset(); pendingScrollOffset_.reset();
    settledViewport_ = currentViewportState();
}
void GroupTabs::queueViewportReflow()
{
    if (!pendingViewportState_ && settledViewport_ && !pendingScrollOffset_)
        pendingViewportState_ = settledViewport_;
    if (viewportReflowQueued_) return;
    viewportReflowQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        viewportReflowQueued_ = false;
        if (applyPendingViewportState()) {
            pendingViewportState_.reset(); settledViewport_ = currentViewportState();
        }
    });
}
bool GroupTabs::applyPendingViewportState()
{
    if (!pendingViewportState_) return true;
    QScopedValueRollback applying(applyingViewportState_, true);
    const auto old = *pendingViewportState_;
    const auto current = currentViewportState();
    if (current.bounds.isEmpty()) return !barVisible_;
    const bool newActive = !old.activeTab.isNull() && current.activeTab != old.activeTab;
    if (!bar_->scrollsHorizontally()) {
        // Vertical sections and expanded multiline rows each own their
        // scroll position. A replacement strip must restore all of them.
        bar_->restoreScrollOffsets(old.offset, old.stickyOffset, old.rowsOffset);
        if (newActive || old.activeFullyVisible) bar_->ensureVisible(bar_->currentIndex());
        return true;
    }
    if (old.bounds.isEmpty()) {
        // A newly constructed hidden strip has no displayed anchor. Its
        // provisional focus scroll must clamp to the first real viewport,
        // rather than manufacture trailing space before the window is shown.
        bar_->restoreScrollOffsets(old.offset, old.stickyOffset, old.rowsOffset);
        if (newActive) bar_->ensureVisible(bar_->currentIndex());
        return true;
    }
    int offset = old.offset;
    // Preserve the displayed content through either edge moving, including
    // expansion. A plain desktop translation is unrelated to strip scrolling.
    const int windowLeftShift = current.windowClientBounds.width() != old.windowClientBounds.width()
        ? current.windowClientBounds.left() - old.windowClientBounds.left() : 0;
    offset += current.bounds.left() - old.bounds.left() + windowLeftShift;
    if (old.activeFullyVisible && old.activeTab == current.activeTab) {
        const auto area = bar_->contentTabRect(bar_->currentIndex());
        if (!area.isEmpty()) {
            const int inset = stickyInset(bar_->currentIndex());
            const int rightLimit = area.right() - current.bounds.width() + 1;
            // Only move as far as needed to keep the active tab readable.
            // An oversized label exposes its beginning; no extra blank extent
            // is manufactured for a last tab or an underfilled strip.
            offset = current.bounds.width() - inset >= area.width()
                ? qBound(rightLimit, offset, area.left() - inset) : area.left() - inset;
        }
    }
    bar_->restoreScrollOffsets(offset, old.stickyOffset, old.rowsOffset);
    if (newActive || old.activeFullyVisible) bar_->ensureVisible(bar_->currentIndex());
    return true;
}
void GroupTabs::applyPendingScrollOffset()
{
    if (pendingScrollOffset_) bar_->setScrollOffset(*pendingScrollOffset_);
}
void GroupTabs::restoreScrollOffset(int offset)
{
    cancelViewportReflow();
    pendingScrollOffset_ = qMax(0, offset);
    applyPendingScrollOffset();
    // The replacement viewport gets its real width from the owning layout.
    // Restore again after that pass; explicit navigation or scrolling cancels
    // the pending restore so it cannot overwrite newer input.
    QTimer::singleShot(0, this, [this] {
        applyPendingScrollOffset(); pendingScrollOffset_.reset(); settledViewport_ = currentViewportState();
    });
}
void GroupTabs::scrollByWheel(QPoint delta)
{
    cancelViewportReflow();
    const auto viewport = bar_->scrollViewport();
    bar_->scrollAt(viewport.isEmpty() ? bar_->rect().center() : viewport.center(), delta);
}
bool GroupTabs::scrollStripAt(QPoint global, QPoint delta, bool smooth)
{
    if (!barVisible_ || !QRect(bar_->mapToGlobal(QPoint()), bar_->size()).contains(global)) return false;
    cancelViewportReflow();
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
    updateCardJoin();
    layoutChildren();
}

void GroupTabs::setAppearance(const TabAppearance& appearance, bool pinned)
{
    // Label widths may change, which moves every tab after the first. Keep a
    // fully shown active tab where it is on screen and let the others resize
    // around it; once it reaches a viewport edge it is pushed only as far as
    // needed to stay shown. Without a shown active tab, keep the tab at the
    // leading edge in place. The raw offset alone would clamp and then land
    // on unrelated tabs.
    struct Anchor { int tab; int shift; bool active; };
    std::optional<Anchor> anchor;
    // Settle a queued reflow first, so the anchor is taken from what is shown.
    if (pendingViewportState_ && !pendingScrollOffset_ && applyPendingViewportState()) {
        pendingViewportState_.reset(); settledViewport_ = currentViewportState();
    }
    auto next = appearance;
    if (pinned) { next.showIcons = appearance.showSidebarIcons; next.showNumbers = appearance.showSidebarNumbers; }
    const auto placement = pinned ? next.pinnedPlacement : next.centralPlacement;
    const bool sameRow = bar_->scrollsHorizontally() && !next.multiline
        && (placement == TabBarPlacement::Top || placement == TabBarPlacement::Bottom);
    if (isVisible() && barVisible_ && sameRow && !pendingViewportState_ && !pendingScrollOffset_ && bar_->count()) {
        const int offset = scrollOffset();
        const int width = bar_->scrollViewport().width();
        const int current = bar_->currentIndex();
        const auto active = current >= 0 ? bar_->contentTabRect(current) : QRect{};
        if (!active.isEmpty() && active.left() >= offset + stickyInset(current) && active.right() < offset + width) {
            anchor = Anchor{current, active.left() - offset, true};
        } else {
            // Sticky tabs kept in view cannot anchor the scrolled ones.
            const int inset = bar_->frozenExtent();
            int leading = inset ? std::min(bar_->stickyCount(), bar_->count() - 1) : 0;
            while (leading + 1 < bar_->count() && bar_->contentTabRect(leading).right() < offset + inset) ++leading;
            anchor = Anchor{leading, bar_->contentTabRect(leading).left() - offset - inset, false};
        }
    }
    appearance_ = next; pinned_ = pinned;
    bar_->setAppearance(appearance_, placement);
    layoutChildren();
    if (!anchor || anchor->tab >= bar_->count()) return;
    const auto area = bar_->contentTabRect(anchor->tab);
    const int inset = stickyInset(anchor->tab);
    int offset = area.left() - anchor->shift;
    if (anchor->active) {
        const int width = bar_->scrollViewport().width();
        offset = width - inset >= area.width() ? qBound(area.right() - width + 1, offset, area.left() - inset) : area.left() - inset;
    } else {
        // A partly scrolled-off leading tab keeps its hidden part, within its new width.
        offset = area.left() - inset - std::max(anchor->shift, -std::max(0, area.width() - 1));
    }
    // The strip limits the offset to the real content range. A reflow
    // queued by the resize above still holds the old widths; this replaces it.
    bar_->setScrollOffset(offset);
    pendingViewportState_.reset();
    settledViewport_ = currentViewportState();
}
void GroupTabs::setStickyCount(int count)
{
    if (bar_->stickyCount() == count) return;
    bar_->setStickyCount(count); layoutChildren();
}
void GroupTabs::setTabMetadata(int index, const TabRecord& tab) { bar_->setTabMetadata(index, tab); }
GroupTabs::SurfaceState GroupTabs::captureSurfaceState() const
{
    return {surfaceFocus_.value(), surfaceFocused_, bar_->captureSurfaceState()};
}

void GroupTabs::inheritSurface(const SurfaceState& previous, bool animate)
{
    surfaceFocused_ = previous.focused;
    surfaceFocus_.jump(previous.focus);
    surfaceFocus_.animateTo(surfaceFocused_ ? 1 : 0, surfaceFocused_ ? Motion::SurfaceLift : Motion::SurfaceLower, animate);
    bar_->inheritSurface(previous.tabs, animate);
    updateCardJoin();
}

void GroupTabs::setGroupFocused(bool focused)
{
    const QPointer<GroupTabs> retained(this);
    bar_->setGroupFocused(focused);
    if (!retained) return;
    if (surfaceFocused_ == focused) return;
    surfaceFocused_ = focused;
    surfaceFocus_.animateTo(focused ? 1 : 0, focused ? Motion::SurfaceLift : Motion::SurfaceLower,
                            isVisible() && TabBar::animationsAllowed());
    updateCardJoin();
}
void GroupTabs::setGroupName(const QString& name)
{
    // At the end of a horizontal strip; above a vertical one, as its title
    // (tab strip design 13).
    const bool shown = !name.isEmpty() && appearance_.showGroupNames && !vertical();
    if (groupName_ == name && name_->isVisible() == shown) return;
    groupName_ = name;
    name_->setText(name); name_->setToolTip(name); name_->setVisible(shown);
    title_->setToolTip(name);
    bar_->refreshTrailing();
    layoutChildren();
}
void GroupTabs::setVerticalState(int width, bool iconMode)
{
    if (verticalWidth_ == width && iconMode_ == iconMode && bar_->iconMode() == iconMode) return;
    verticalWidth_ = width; iconMode_ = iconMode;
    bar_->setIconMode(iconMode);
    layoutChildren();
}
QRect GroupTabs::viewportRectGlobal() const {
    if (!barVisible_ || bar_->isHidden()) return {};
    return QRect(bar_->mapToGlobal(QPoint()), bar_->size());
}
QRect GroupTabs::contentRectGlobal() const
{
    return QRect(stack_->mapToGlobal(QPoint()), stack_->size());
}
QRect GroupTabs::stripPanelRectGlobal() const
{
    if (!barVisible_ || bar_->isHidden()) return {};
    if (!vertical()) return viewportRectGlobal();
    const auto body = static_cast<VerticalTabPanel*>(columnPanel_)->contentRect();
    return QRect(columnPanel_->mapToGlobal(body.topLeft()), body.size());
}
void GroupTabs::restoreColumnPeek()
{
    if (!vertical() || !iconMode_) return;
    peekExpansion_.jump(1);
    bar_->restoreColumnPeek(true);
    layoutChildren();
}
int GroupTabs::insertionIndex(QPoint global, TabLayoutInput::Carried carried) const {
    updateDragProbe(global);
    return bar_->insertionAt(dragProbe_, 0, carried).index;
}
void GroupTabs::updateDragProbe(QPoint global) const {
    dragProbe_ = bar_->mapFromGlobal(global);
    if (lifting_) if (const auto lifted = bar_->liftedRect(); !lifted.isEmpty()) {
        // The label's center chooses its slot along the strip, but the
        // pointer chooses the row. The painted label may still be clipped
        // at a row boundary while the next gap is being laid out.
        if (bar_->vertical()) dragProbe_.setY(bar_->draggedFolder() >= 0
            ? lifted.top() + bar_->metrics().rowHeight / 2 : lifted.center().y());
        else dragProbe_.setX(bar_->draggedFolder() >= 0
            ? lifted.left() + bar_->metrics().rowHeight / 2 : lifted.center().x());
    }
}
void GroupTabs::setLiftedTab(QPoint global, QPoint grab)
{
    lifting_ = true;
    liftedGlobal_ = global; liftedGrab_ = grab;
    bar_->setLifted(bar_->mapFromGlobal(global), grab);
    // beginTab opens the first gap before asking for an insertion index.
    // Its probe must already belong to the row where the label was taken.
    updateDragProbe(global);
}
void GroupTabs::clearLiftedTab()
{
    lifting_ = false;
    bar_->setLifted({});
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
    if (index < 0 || carried.isEmpty()) { bar_->setDropPreview(-1, false, {}); return; }
    // The drop position decides stickiness (tab strip design 5); a block that
    // was sticky stays so a little past the sticky tabs. It also decides the
    // tab folder a single tab joins (tab strip design 12).
    const int first = bar_->carriedExtent(carried.first(), carriedSticky);
    const auto decision = bar_->insertionAt(dragProbe_, carriedSticky ? first / 2 : 0, kind);
    QVector<int> block;
    for (const auto& label : carried) block.append(bar_->carriedExtent(label, decision.sticky));
    bar_->setDropPreview(decision.index, decision.sticky, block, decision.folder);
}
bool GroupTabs::insertionSticky() const { return bar_->dropSticky(); }
QUuid GroupTabs::insertionFolder() const
{
    const int folder = bar_->dropFolder();
    return folder >= 0 && folder < bar_->folders().size() ? bar_->folders().at(folder).id : QUuid{};
}
void GroupTabs::setDraggedFolder(QUuid folder) { bar_->setDraggedFolder(folder); }
QPoint GroupTabs::folderGrabOffset(QUuid folder, QPoint global) const
{
    for (int index = 0; index < bar_->folders().size(); ++index)
        if (bar_->folders().at(index).id == folder) return bar_->mapFromGlobal(global) - bar_->folderHeaderRect(index).topLeft();
    return {};
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
    if (normalDragStyle_ && (!compact || edge)) {
        const auto saved = *normalDragStyle_;
        normalDragStyle_.reset(); dragTabArea_ = {};
        appearance_ = saved.appearance; verticalWidth_ = saved.width; iconMode_ = saved.iconMode;
        bar_->setAppearance(appearance_, saved.placement);
        bar_->setIconMode(iconMode_); bar_->setStickyCount(saved.stickyCount);
    }
    compactPresentation_ = compact;
    bar_->setCarriedDisplay(compact && !edge);
    if (compact) setTabBarVisible(!edge);
    bar_->refreshTrailing();
    layoutChildren();
}
bool GroupTabs::setDragTabStyle(const TabBar& target, bool sticky)
{
    if (!compactPresentation_) return false;
    const int count = sticky ? bar_->count() : 0;
    const QSize area(target.width(), target.availableSize().height());
    const bool icons = target.iconsOnly();
    const auto presentation = target.verticalPresentation();
    const int columns = target.tabLayout().tileColumns;
    if (normalDragStyle_ && appearance_ == target.appearance() && bar_->placement() == target.placement()
        && dragTabArea_ == area && iconMode_ == icons && bar_->stickyCount() == count
        && bar_->carriedTileColumns() == columns && bar_->verticalPresentation() == presentation) return false;
    if (!normalDragStyle_) normalDragStyle_ = DragStyle{appearance_, bar_->placement(), verticalWidth_, iconMode_, bar_->stickyCount()};
    appearance_ = target.appearance(); dragTabArea_ = area;
    verticalWidth_ = qRound(dragTabArea_.width() / interfaceScale()); iconMode_ = icons;
    bar_->setAppearance(appearance_, target.placement());
    bar_->setIconMode(iconMode_); bar_->setStickyCount(count);
    bar_->setCarriedPresentation(presentation);
    bar_->setCarriedTileColumns(columns);
    layoutChildren();
    return true;
}
void GroupTabs::setIncomingVisual(TabDragVisual visual, QPoint global)
{
    bar_->setIncomingVisual(std::move(visual), bar_->mapFromGlobal(global));
}
void GroupTabs::setDragHover(QPoint global) {
    const auto area = viewportRectGlobal();
    dragHovered_ = area.contains(global);
    if (vertical()) bar_->setColumnHovered(dragHovered_);
    dragProbe_ = bar_->mapFromGlobal(global);
    // Dragging over collapsed rows shows them all (tab strip design 3.4).
    bar_->setDragExpanded(dragHovered_);
    QPoint direction;
    if (dragHovered_) {
        const auto lane = bar_->scrollViewportAt(dragProbe_);
        if (bar_->scrollsHorizontallyAt(dragProbe_)) {
            const int start = lane.left() + bar_->frozenExtent();
            if (dragProbe_.x() >= start && dragProbe_.x() < start + 24) direction = {-1, 0};
            else if (dragProbe_.x() > lane.right() - 24) direction = {1, 0};
        } else if (!lane.isEmpty()) {
            if (dragProbe_.y() < lane.top() + 24) direction = {0, -1};
            else if (dragProbe_.y() > lane.bottom() - 24) direction = {0, 1};
        }
    }
    if (direction != scrollDirection_) autoScroll_->stop();
    scrollDirection_ = direction;
    if (!direction.isNull() && bar_->isOverflowing() && !autoScroll_->isActive()) { autoScroll_->setInterval(200); autoScroll_->start(); }
    else if (direction.isNull()) autoScroll_->stop();
}
void GroupTabs::clearDragHover(QPoint global)
{
    // End drag-only expansion and edge scrolling without inventing a leave
    // event for a pointer that still belongs to the complete column panel.
    const bool hovered = vertical() && stripPanelRectGlobal().contains(global);
    dragHovered_ = false; scrollDirection_ = {}; autoScroll_->stop();
    const QPointer<GroupTabs> self(this);
    bar_->setDragExpanded(false);
    if (self && vertical()) bar_->setColumnHovered(hovered);
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
    if (event->type() == QEvent::Show && (watched == viewportWindow_ || watched == bar_)) queueViewportReflow();
    if (watched == viewportWindow_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
        queueViewportReflow();
    if (watched == viewportWindow_ && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate))
        refreshFocus();
    if (watched == resizeHandle_) {
        // Dragging the vertical strip's inner edge resizes it; a double
        // click returns it to the default width.
        const bool left = bar_->placement() == TabBarPlacement::Left;
        if (event->type() == QEvent::MouseButtonPress) {
            if (static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton) return false;
            resizeOrigin_ = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x();
            resizeStartWidth_ = columnWidth();
            return true;
        }
        if (event->type() == QEvent::MouseMove && resizeOrigin_) {
            const int moved = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x() - *resizeOrigin_;
            const int requested = resizeStartWidth_ + (left ? moved : -moved);
            const int minimum = bar_->minimumColumnWidth();
            // Reaching the title-less row is not yet a collapse. A little
            // extra travel makes the icon-only transition deliberate.
            const bool icons = requested <= minimum - bar_->metrics().iconCollapseTravel;
            // Crossing the collapse threshold is a mode switch, not a new
            // expanded width. Retain this gesture's initial width for peeking
            // and for a later explicit expansion; reversing the drag still
            // resumes direct sizing from its original pointer origin.
            verticalWidth_ = qRound((icons ? resizeStartWidth_ : std::clamp(requested, minimum, maximumColumnWidth())) / interfaceScale());
            if (iconMode_ != icons) { iconMode_ = icons; bar_->setIconMode(icons); }
            layoutChildren();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && resizeOrigin_) {
            resizeOrigin_.reset();
            layoutChildren();
            if (const auto callback = verticalStateChanged) callback(verticalWidth_, iconMode_);
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            resizeOrigin_.reset(); verticalWidth_ = 0; layoutChildren();
            if (const auto callback = verticalStateChanged) callback(0, iconMode_);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}
void GroupTabs::resizeEvent(QResizeEvent* event) {
    // Capture the settled viewport before the strip takes the new size, which
    // would otherwise replace it with the resized one.
    QFrame::resizeEvent(event); queueViewportReflow(); layoutChildren();
}
void GroupTabs::moveEvent(QMoveEvent* event) { QFrame::moveEvent(event); queueViewportReflow(); updateCardJoin(); }
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
        auto* row = new TabListRow(bar_->tabText(index), appearance_.showIcons ? bar_->tabIcon(index) : QIcon{},
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
