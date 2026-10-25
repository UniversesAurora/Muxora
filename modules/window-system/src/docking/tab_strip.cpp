#include <QCoreApplication>
#include "tab_strip.hpp"
#include "style/tab_painter.hpp"
#include "commands/popup_menu.hpp"
#include "style/compact_tool_button.hpp"
#include "style/tab_appearance_p.hpp"
#include "style/themed_controls.hpp"

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

// A group's content is a card with rounded corners. Drawn above the pages,
// this layer fills the corners outside the card with the background and
// draws the outline and the focus glow, so any content (a white page, a
// dark terminal) gets smooth corners. The outline opens under the active
// tab, which flares into the card.
class CardOverlay final : public QWidget {
public:
    CardOverlay(const Theme& theme, QWidget* parent) : QWidget(parent), theme_(theme)
    {
        setObjectName("TabGroupCard");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        focus_.setDuration(150); focus_.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&focus_, &QVariantAnimation::valueChanged, this, [this] { update(); });
    }
    void setTheme(const Theme& theme) { theme_ = theme; update(); }
    // The card inside this layer, the active tab's span along the open edge
    // (empty when none joins it) and which edge that is.
    void setCard(QRect card, QPair<int, int> join, Qt::Edge edge)
    {
        card_ = card; join_ = join; edge_ = edge; update();
    }
    void setFocused(bool focused)
    {
        if (focused_ == focused) return;
        focused_ = focused;
        const qreal now = focus_.state() == QAbstractAnimation::Running ? focus_.currentValue().toReal() : (focused ? 0.0 : 1.0);
        focus_.stop(); focus_.setStartValue(now); focus_.setEndValue(focused ? 1.0 : 0.0);
        if (TabBar::animationsAllowed()) focus_.start(); else update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        if (card_.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal radius = 8;
        const QRectF card(card_);
        QPainterPath rounded; rounded.addRoundedRect(card, radius, radius);
        // Background in the corners outside the rounded card.
        QPainterPath corners; corners.addRect(card);
        painter.fillPath(corners.subtracted(rounded), theme_.headerBackground);
        const qreal progress = focus_.state() == QAbstractAnimation::Running ? focus_.currentValue().toReal() : (focused_ ? 1.0 : 0.0);
        // The focused card glows softly outside its outline.
        if (progress > 0) for (int ring = 3; ring >= 1; --ring) {
            auto glow = theme_.accent; glow.setAlphaF(0.07 * progress * (4 - ring) / 3.0);
            painter.setPen(QPen(glow, ring * 1.5)); painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(card.adjusted(-ring * 0.5, -ring * 0.5, ring * 0.5, ring * 0.5), radius + ring * 0.5, radius + ring * 0.5);
        }
        auto line = theme_.border; line.setAlphaF(0.6);
        if (progress > 0) {
            auto accent = theme_.accent; accent.setAlphaF(0.6);
            line = QColor::fromRgbF(line.redF() + (accent.redF() - line.redF()) * progress, line.greenF() + (accent.greenF() - line.greenF()) * progress,
                                    line.blueF() + (accent.blueF() - line.blueF()) * progress, line.alphaF() + (accent.alphaF() - line.alphaF()) * progress);
        }
        painter.setPen(QPen(line, 1)); painter.setBrush(Qt::NoBrush);
        painter.save();
        if (join_.second > join_.first) {
            // Leave the outline open where the active tab joins the card.
            QPainterPath visible; visible.addRect(QRectF(rect()));
            QRectF gap;
            if (edge_ == Qt::TopEdge) gap = QRectF(join_.first, card.top() - 2, join_.second - join_.first, 4);
            else if (edge_ == Qt::BottomEdge) gap = QRectF(join_.first, card.bottom() - 2, join_.second - join_.first, 4);
            QPainterPath hole; hole.addRect(gap);
            painter.setClipPath(visible.subtracted(hole));
        }
        painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter.restore();
    }
private:
    Theme theme_;
    QRect card_;
    QPair<int, int> join_;
    Qt::Edge edge_ = Qt::TopEdge;
    bool focused_ = false;
    QVariantAnimation focus_;
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

class TabCreateButtons final : public QWidget {
public:
    TabCreateButtons(const Theme& theme, QWidget* parent) : QWidget(parent) {
        setObjectName("TabCreateButtons");
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(createButtonGap);
        add = new CompactToolButton(this, QSize(newTabButtonWidth, theme.metrics.tabHeight));
        add->setObjectName("NewTabButton"); setThemedIcon(add, "add", theme);
        add->setToolTip(QCoreApplication::translate("WindowSystem", "New tab")); add->setAccessibleName(add->toolTip());
        types = new CompactToolButton(this, QSize(listButtonWidth, theme.metrics.tabHeight));
        types->setObjectName("NewTabTypeButton"); setThemedIcon(types, "chevron-down", theme);
        types->setToolTip(QCoreApplication::translate("WindowSystem", "Choose new tab type")); types->setAccessibleName(types->toolTip());
        row->addWidget(add); row->addWidget(types);
        add->installEventFilter(this); types->installEventFilter(this);
    }
    CompactToolButton* add = nullptr;
    CompactToolButton* types = nullptr;
    std::function<void()> menuRequested;
    void setHeight(int height) {
        add->setTargetSize(QSize(newTabButtonWidth, height));
        types->setTargetSize(QSize(listButtonWidth, height));
        setFixedSize(newTabButtonWidth + createButtonGap + listButtonWidth, height);
    }
    void setPopupOpen(bool open) {
        setProperty("popupOpen", open);
        types->setDown(open);
        if (!open) {
            // A popup may consume leave events. Recompute from the pointer
            // instead of retaining the coupled highlight from before opening.
            emphasized_ = rect().contains(mapFromGlobal(QCursor::pos()));
        }
        refreshStyle();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ContextMenu) {
            const auto callback = menuRequested;
            if (callback) callback();
            return true;
        }
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            emphasized_ = event->type() == QEvent::Enter;
            refreshStyle();
        }
        return QWidget::eventFilter(watched, event);
    }
private:
    void refreshStyle() {
        setProperty("emphasized", emphasized_);
        for (auto* button : {add, types}) {
            button->style()->unpolish(button); button->style()->polish(button); button->update();
        }
    }
    bool emphasized_ = false;
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
    viewportWindow_ = window(); viewportWindow_->installEventFilter(this);
    stack_ = new QStackedWidget(this);
    bar_ = new TabBar(theme, this);
    bar_->installEventFilter(this);
    const int row = bar_->metrics().rowHeight;
    // Right after the last tab: new tab. At the first row's end, from the
    // right: the complete list, then sidebar actions (tab strip design 2.1).
    trailing_ = new QWidget(this); trailing_->setObjectName("TabCreateActions");
    auto* actions = new QHBoxLayout(trailing_); actions->setContentsMargins(0, 0, 0, 0); actions->setSpacing(0);
    auto* create = new TabCreateButtons(theme, trailing_); createButtons_ = create;
    create->setHeight(row);
    actions->addWidget(create);
    trailing_->installEventFilter(this);
    bar_->setTrailingWidget(trailing_);
    end_ = new QWidget(this); end_->setObjectName("TabHeaderActions");
    auto* ending = new QHBoxLayout(end_); ending->setContentsMargins(0, 0, 4, 0); ending->setSpacing(bar_->metrics().controlGap);
    list_ = new CompactToolButton(end_, QSize(bar_->metrics().listWidth, row)); list_->setObjectName("TabListButton");
    list_->setMouseTracking(true);
    setThemedIcon(list_, "tab-list", theme); list_->setToolTip(QCoreApplication::translate("WindowSystem", "All tabs"));
    list_->setAccessibleName(list_->toolTip());
    // A named group shows its name before the end controls, quietly.
    name_ = new QLabel(end_); name_->setObjectName("TabGroupName"); name_->hide();
    auto nameFont = name_->font(); nameFont.setPointSizeF(std::max(7.0, nameFont.pointSizeF() - 1)); nameFont.setWeight(QFont::DemiBold);
    name_->setFont(nameFont); name_->setContentsMargins(6, 0, 6, 0);
    ending->addWidget(name_);
    ending->addWidget(list_);
    title_ = new QLabel(this); title_->setObjectName("TabGroupTitle"); title_->setFont(nameFont); title_->hide();
    end_->installEventFilter(this);
    bar_->setEndWidget(end_);
    // A vertical strip's footer: new tab, sidebar actions, icon mode.
    footer_ = new QWidget(this); footer_->setObjectName("TabStripFooter"); footer_->hide();
    auto* footer = new QBoxLayout(QBoxLayout::LeftToRight, footer_);
    footer->setContentsMargins(bar_->metrics().verticalInset, 2, bar_->metrics().verticalInset, 2); footer->setSpacing(2);
    iconModeButton_ = new CompactToolButton(footer_, QSize(26, row)); iconModeButton_->setObjectName("TabStripIconModeButton");
    footer_->installEventFilter(this);
    QObject::connect(iconModeButton_, &QToolButton::clicked, this, [this] {
        iconMode_ = !iconMode_;
        bar_->setIconMode(iconMode_);
        layoutChildren();
        if (const auto callback = iconModeChanged) callback(iconMode_);
    });
    resizeHandle_ = new QWidget(this); resizeHandle_->setObjectName("TabStripResizeHandle");
    resizeHandle_->setCursor(Qt::SizeHorCursor); resizeHandle_->hide(); resizeHandle_->installEventFilter(this);
    autoScroll_ = new QTimer(this); autoScroll_->setInterval(200);
    QObject::connect(autoScroll_, &QTimer::timeout, this, [this] {
        cancelViewportReflow();
        if (scrollDirection_.isNull() || !bar_->scrollAt(dragProbe_, -scrollDirection_ * 12)) { autoScroll_->stop(); return; }
        if (autoScroll_->interval() != 35) autoScroll_->setInterval(35);
    });
    QObject::connect(create->add, &QToolButton::clicked, this, [this] {
        const auto callback = newTabRequested; if (callback) callback();
    });
    QObject::connect(create->types, &QToolButton::clicked, this, [this] { showNewTabMenu(); });
    create->menuRequested = [this] { showNewTabMenu(); };
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
    QObject::connect(list_, &QToolButton::clicked, this, [this] { showTabList(); });
    QObject::connect(qApp, &QApplication::focusChanged, this, [this] { refreshFocus(); });
}
TabBar* GroupTabs::tabBar() const { return bar_; }
void GroupTabs::updateCardJoin()
{
    if (!card_ || vertical() || !barVisible_) return;
    // The outline opens under the active tab, flares included.
    const int current = bar_->currentIndex();
    const auto active = current >= 0 ? bar_->tabRect(current) : QRect{};
    const int flare = bar_->metrics().controlRadius;
    const auto content = stack_->geometry();
    const int margin = compactPresentation_ ? 0 : bar_->metrics().cardMargin;
    const QPair<int, int> join = active.isEmpty() ? QPair<int, int>{}
        : QPair<int, int>{bar_->x() + active.left() - flare - content.left() + margin, bar_->x() + active.right() + 1 + flare - content.left() + margin};
    static_cast<CardOverlay*>(card_)->setCard(QRect(QPoint(margin, margin), content.size()), join,
                                              bar_->placement() == TabBarPlacement::Bottom ? Qt::BottomEdge : Qt::TopEdge);
}
void GroupTabs::refreshFocus()
{
    const auto callback = isActiveGroup;
    const bool focused = window()->isActiveWindow() && callback && callback();
    bar_->setGroupFocused(focused);
    if (card_) static_cast<CardOverlay*>(card_)->setFocused(focused);
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
    return std::clamp(verticalWidth_ > 0 ? verticalWidth_ : appearance_.verticalTabWidth, minimumVerticalTabWidth, maximumVerticalTabWidth);
}
void GroupTabs::placeControls()
{
    auto* create = static_cast<TabCreateButtons*>(createButtons_);
    auto* footer = static_cast<QBoxLayout*>(footer_->layout());
    auto* trailing = static_cast<QHBoxLayout*>(trailing_->layout());
    auto* ending = static_cast<QHBoxLayout*>(end_->layout());
    QList<QToolButton*> sidebar;
    for (const auto name : {"UnpinCurrentButton", "UnpinAllButton"})
        if (auto* button = findChild<QToolButton*>(QString::fromLatin1(name))) sidebar.append(button);
    const int row = bar_->metrics().rowHeight;
    if (vertical()) {
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
        footer->setDirection(iconMode_ ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        // A labeled new-tab button, or only its icon in a narrow column.
        create->add->setText(QCoreApplication::translate("WindowSystem", "New tab"));
        create->add->setToolButtonStyle(iconMode_ ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        const int label = iconMode_ ? bar_->metrics().newTabWidth
            : bar_->metrics().newTabWidth + fontMetrics().horizontalAdvance(create->add->text()) + 8;
        create->add->setTargetSize(QSize(label, row));
        create->types->setVisible(!iconMode_);
        create->setFixedSize((iconMode_ ? 0 : bar_->metrics().typeWidth + bar_->metrics().controlGap) + label, row);
        for (auto* button : sidebar) button->setVisible(!iconMode_ && !compactPresentation_);
        const bool left = bar_->placement() == TabBarPlacement::Left;
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
    placeControls();
    // The group keeps a margin around its card; a drag carrier shows only
    // its tabs and needs none.
    const int margin = compactPresentation_ ? 0 : bar_->metrics().cardMargin;
    const auto area = rect().adjusted(margin, margin, -margin, -margin);
    if (!card_) card_ = new CardOverlay(theme_, this);
    auto* card = static_cast<CardOverlay*>(card_);
    const auto placeCard = [&](QRect content, QPair<int, int> join, Qt::Edge edge) {
        stack_->setGeometry(content);
        card->setGeometry(content.adjusted(-margin, -margin, margin, margin));
        card->setCard(QRect(QPoint(margin, margin), content.size()), {join.first - content.left() + margin, join.second - content.left() + margin}, edge);
        card->setVisible(!compactPresentation_);
        card->raise();
    };
    if (!barVisible_) {
        bar_->hide(); footer_->hide(); resizeHandle_->hide(); title_->hide();
        placeCard(area, {}, Qt::TopEdge);
        return;
    }
    bar_->show();
    if (vertical()) {
        const int footerHeight = iconMode_ ? 2 * bar_->metrics().rowHeight + 6 : bar_->metrics().rowHeight + 4;
        const bool titled = !groupName_.isEmpty() && appearance_.showGroupNames && !iconMode_ && !compactPresentation_;
        const int titleHeight = titled ? bar_->metrics().rowHeight : 0;
        const QSize available(columnWidth(), std::max(0, area.height() - footerHeight - titleHeight));
        bar_->setAvailableSize(available);
        const auto reserved = bar_->reservedSize(available), shown = bar_->displaySize(available);
        const bool left = bar_->placement() == TabBarPlacement::Left;
        const int x = area.left() + (left ? 0 : area.width() - shown.width());
        if (titled) {
            const int inset = bar_->metrics().verticalInset + 4;
            title_->setGeometry(x + inset, area.top(), std::max(0, shown.width() - 2 * inset), titleHeight);
            title_->setText(title_->fontMetrics().elidedText(groupName_, Qt::ElideRight, title_->width()));
        }
        title_->setVisible(titled);
        bar_->setGeometry(x, area.top() + titleHeight, shown.width(), available.height());
        footer_->setGeometry(x, area.top() + titleHeight + available.height(), shown.width(), footerHeight); footer_->setVisible(!compactPresentation_);
        // The column sits on the background beside the card, with a gap.
        const int gap = margin * 2;
        placeCard(left ? QRect(area.left() + reserved.width() + gap, area.top(), area.width() - reserved.width() - gap, area.height())
                       : QRect(area.left(), area.top(), area.width() - reserved.width() - gap, area.height()), {}, Qt::TopEdge);
        const int edge = area.left() + (left ? reserved.width() + margin : area.width() - reserved.width() - margin);
        resizeHandle_->setGeometry(edge - bar_->metrics().resizeBand / 2, area.top(), bar_->metrics().resizeBand, area.height());
        resizeHandle_->setVisible(!iconMode_ && !compactPresentation_);
        resizeHandle_->raise();
        if (shown.width() > reserved.width()) { bar_->raise(); footer_->raise(); }
    } else {
        footer_->hide(); resizeHandle_->hide(); title_->hide();
        // The first tab keeps clear of the card's rounded corner.
        const int inset = compactPresentation_ ? 0 : bar_->metrics().stripInset;
        const QSize strip(std::max(1, area.width() - 2 * inset), area.height());
        bar_->setAvailableSize(strip);
        const auto reserved = bar_->reservedSize(strip), shown = bar_->displaySize(strip);
        const bool top = bar_->placement() != TabBarPlacement::Bottom;
        bar_->setGeometry(area.left() + inset, top ? area.top() : area.bottom() + 1 - shown.height(), strip.width(), shown.height());
        const QRect content(area.left(), top ? area.top() + reserved.height() : area.top(), area.width(), std::max(0, area.height() - reserved.height()));
        placeCard(content, {}, top ? Qt::TopEdge : Qt::BottomEdge);
        bar_->raise();
        updateCardJoin();
    }
    if (contentPreview_ && contentPreview_->isVisible()) { contentPreview_->setGeometry(stack_->geometry()); contentPreview_->raise(); card->raise(); bar_->raise(); }
    footer_->raise();
    updateGeometry();
    const auto callback = hoverGeometryChanged; if (callback) callback();
}
QSize GroupTabs::sizeHint() const
{
    // The card keeps a margin all round; a vertical column a gap beside it.
    const int margin = compactPresentation_ ? 0 : bar_->metrics().cardMargin;
    auto size = stack_->sizeHint() + QSize(2 * margin, 2 * margin);
    if (!barVisible_) return size;
    if (vertical()) size.rwidth() += columnWidth() + 2 * margin;
    else size.rheight() += bar_->metrics().rowHeight + bar_->metrics().outerMargin;
    return size;
}
QSize GroupTabs::minimumSizeHint() const
{
    const int margin = compactPresentation_ ? 0 : bar_->metrics().cardMargin;
    auto size = stack_->minimumSizeHint().expandedTo(stack_->minimumSize()) + QSize(2 * margin, 2 * margin);
    if (!barVisible_) return size;
    if (vertical()) size.rwidth() += (iconMode_ ? bar_->metrics().verticalIconWidth : columnWidth()) + 2 * margin;
    else size.rheight() += bar_->reservedSize(QSize(std::max(width(), 1), height())).height();
    return size;
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
    state.rowsExpanded = bar_->expanded();
    state.windowClientBounds = window()->geometry();
    if (isVisible() && barVisible_ && bar_->scrollsHorizontally()) {
        const auto viewport = bar_->scrollViewport();
        state.bounds = {bar_->mapTo(window(), viewport.topLeft()), viewport.size()};
    }
    const auto current = bar_->currentIndex();
    if (current >= 0) {
        state.activeTab = bar_->tabData(current).toUuid();
        const auto area = bar_->contentTabRect(current);
        state.activeFullyVisible = !area.isEmpty() && !state.bounds.isEmpty()
            && area.left() >= state.offset + stickyInset(current) && area.right() < state.offset + state.bounds.width();
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
    if (bar_->collapsible() && state.rowsExpanded) bar_->setExpanded(*state.rowsExpanded);
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
    // Only a single horizontal row keeps shown tabs in place this way.
    if (!bar_->scrollsHorizontally()) return true;
    QScopedValueRollback applying(applyingViewportState_, true);
    const auto old = *pendingViewportState_;
    const auto current = currentViewportState();
    if (current.bounds.isEmpty()) return false;
    if (old.bounds.isEmpty()) {
        // A newly constructed hidden strip has no displayed anchor. Its
        // provisional focus scroll must clamp to the first real viewport,
        // rather than manufacture trailing space before the window is shown.
        bar_->setScrollOffset(old.offset);
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
    bar_->setScrollOffset(offset);
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
bool GroupTabs::scrollStripAt(QPoint global, QPoint delta)
{
    if (!barVisible_ || !QRect(bar_->mapToGlobal(QPoint()), bar_->size()).contains(global)) return false;
    cancelViewportReflow();
    // The scroll may destroy this group; touch nothing after it.
    bar_->scrollAt(bar_->mapFromGlobal(global), delta);
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
    if (card_) static_cast<CardOverlay*>(card_)->setTheme(theme);
    for (auto* button : findChildren<QAbstractButton*>()) {
        const auto name = button->property("themeIcon").toString();
        if (!name.isEmpty()) button->setIcon(iconFor(name, theme));
    }
    updateGeometry();
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
void GroupTabs::setGroupFocused(bool focused) { bar_->setGroupFocused(focused); }
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
int GroupTabs::insertionIndex(QPoint global, TabLayoutInput::Carried carried) const {
    dragProbe_ = bar_->mapFromGlobal(global);
    if (lifting_) if (const auto lifted = bar_->liftedRect(); !lifted.isEmpty()) {
        // A lifted folder decides by its header, which leads it.
        dragProbe_ = lifted.center();
        if (bar_->draggedFolder() >= 0) dragProbe_ = bar_->vertical() ? QPoint(lifted.center().x(), lifted.top() + bar_->metrics().rowHeight / 2)
                                                                      : QPoint(lifted.left() + bar_->metrics().rowHeight / 2, lifted.center().y());
    }
    return bar_->insertionAt(dragProbe_, 0, carried).index;
}
void GroupTabs::setLiftedTab(QPoint global, QPoint grab)
{
    lifting_ = true;
    bar_->setLifted(bar_->mapFromGlobal(global), grab);
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
    compactPresentation_ = compact;
    if (compact) setTabBarVisible(!edge);
    bar_->refreshTrailing();
    layoutChildren();
}
void GroupTabs::setDragHover(QPoint global) {
    const auto area = viewportRectGlobal();
    dragHovered_ = area.contains(global);
    dragProbe_ = bar_->mapFromGlobal(global);
    // Dragging over collapsed rows shows them all (tab strip design 3.4).
    bar_->setDragExpanded(dragHovered_);
    QPoint direction;
    if (dragHovered_) {
        const auto lane = bar_->scrollViewport();
        if (bar_->scrollsHorizontally()) {
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
void GroupTabs::setPinnedActions(std::function<void()> current, std::function<void()> all) {
    QWidget* owner = vertical() ? footer_ : end_;
    for (auto item : {std::tuple{std::move(current), QCoreApplication::translate("WindowSystem", "Unpin current tab"), QStringLiteral("unpin")},
                      std::tuple{std::move(all), QCoreApplication::translate("WindowSystem", "Unpin all sidebar tabs"), QStringLiteral("unpin-all")}}) {
        auto* button = new CompactToolButton(owner, QSize(20, bar_->metrics().rowHeight)); setThemedIcon(button, std::get<2>(item), theme_);
        button->setObjectName(std::get<2>(item) == "unpin-all" ? "UnpinAllButton" : "UnpinCurrentButton");
        button->setToolTip(std::get<1>(item)); button->setAccessibleName(std::get<1>(item));
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
    return QFrame::event(event);
}
bool GroupTabs::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ContextMenu && (watched == footer_ || watched == trailing_ || watched == end_)) {
        const auto callback = groupMenuRequested;
        if (callback) callback(static_cast<QContextMenuEvent*>(event)->globalPos());
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
            resizeOrigin_ = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x();
            resizeStartWidth_ = columnWidth();
            return true;
        }
        if (event->type() == QEvent::MouseMove && resizeOrigin_) {
            const int moved = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x() - *resizeOrigin_;
            verticalWidth_ = std::clamp(resizeStartWidth_ + (left ? moved : -moved), minimumVerticalTabWidth, maximumVerticalTabWidth);
            layoutChildren();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && resizeOrigin_) {
            resizeOrigin_.reset();
            if (const auto callback = verticalWidthChanged) callback(verticalWidth_);
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            resizeOrigin_.reset(); verticalWidth_ = 0; layoutChildren();
            if (const auto callback = verticalWidthChanged) callback(0);
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
void GroupTabs::moveEvent(QMoveEvent* event) { QFrame::moveEvent(event); queueViewportReflow(); }
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
