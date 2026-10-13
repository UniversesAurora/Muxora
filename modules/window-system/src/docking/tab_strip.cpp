#include "tab_strip.hpp"
#include "commands/popup_menu.hpp"
#include "style/compact_tool_button.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QVariantMap>
#include <algorithm>
#include <tuple>

namespace window_system {
namespace {
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
    TabListRow(QString text, QIcon icon, QWidget* parent) : QWidget(parent) {
        setObjectName("TabListRow");
        setMouseTracking(true);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 5, 2); layout->setSpacing(8);
        if (!icon.isNull()) {
            auto* image = new QLabel(this); image->setPixmap(icon.pixmap(16, 16)); layout->addWidget(image);
        }
        title_ = new TabListTitle(text, this); title_->setToolTip(text);
        title_->setObjectName("TabListTitle");
        title_->setAttribute(Qt::WA_TransparentForMouseEvents); layout->addWidget(title_, 1);
        closeButton = new QToolButton(this);
        auto policy = closeButton->sizePolicy(); policy.setRetainSizeWhenHidden(true);
        closeButton->setSizePolicy(policy); closeButton->hide(); layout->addWidget(closeButton);
        setMinimumWidth(240); setMaximumWidth(520);
        if (auto* menu = qobject_cast<QMenu*>(parent))
            QObject::connect(menu, &QMenu::hovered, this, [this] { updateEmphasis(); });
    }
    QToolButton* closeButton;
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
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) { const auto callback = activate; if (callback) callback(); }
        else QWidget::mouseReleaseEvent(event);
    }
private:
    void updateEmphasis() {
        auto* menu = qobject_cast<QMenu*>(parentWidget());
        auto* action = menu ? qobject_cast<QWidgetAction*>(menu->activeAction()) : nullptr;
        const bool emphasized = action && action->defaultWidget() == this;
        if (property("emphasized").toBool() == emphasized) return;
        setProperty("emphasized", emphasized);
        update(); title_->update();
    }
    QLabel* title_ = nullptr;
};
} // namespace

DragTabBar::DragTabBar(QWidget* parent) : QTabBar(parent)
{
    setObjectName("TabBar"); setExpanding(false); setElideMode(Qt::ElideNone);
    setUsesScrollButtons(false); setMouseTracking(true); setDocumentMode(true);
}
void DragTabBar::setCloseIcon(QIcon icon) { closeIcon_ = std::move(icon); update(); }
void DragTabBar::setDraggedTab(TabId tab)
{
    draggedIndex_ = -1;
    for (int i = 0; i < count(); ++i) if (!tab.isNull() && tabData(i).toUuid() == tab) draggedIndex_ = i;
    update();
}
int DragTabBar::draggedExtent() const
{
    return draggedIndex_ < 0 ? 0 : tabRect(draggedIndex_).width();
}
void DragTabBar::setInsertionPreview(int index, int extent, QColor color)
{
    insertionIndex_ = index < 0 ? -1 : qBound(0, index, count() - (draggedIndex_ >= 0 ? 1 : 0));
    insertionExtent_ = insertionIndex_ < 0 ? 0 : qMax(1, extent);
    insertionColor_ = color;
    setProperty("insertionIndex", insertionIndex_);
    update();
}
QRect DragTabBar::insertionPreviewRect() const
{
    if (insertionIndex_ < 0) return {};
    int x = 0, visible = 0;
    for (int i = 0; i < count() && visible < insertionIndex_; ++i)
        if (i != draggedIndex_) { x += tabRect(i).width(); ++visible; }
    return {x, 0, insertionExtent_, height()};
}
QRect DragTabBar::displayTabRect(int index) const
{
    auto result = tabRect(index);
    if (index == draggedIndex_) return {};
    const int visibleIndex = index - (draggedIndex_ >= 0 && index > draggedIndex_ ? 1 : 0);
    if (draggedIndex_ >= 0 && index > draggedIndex_) result.translate(-draggedExtent(), 0);
    if (insertionIndex_ >= 0 && visibleIndex >= insertionIndex_) result.translate(insertionExtent_, 0);
    return result;
}
int DragTabBar::insertionIndexAt(QPoint local) const
{
    const auto gap = insertionPreviewRect();
    if (gap.contains(local)) return insertionIndex_;
    int x = local.x();
    if (!gap.isEmpty() && x > gap.right()) x -= gap.width();
    int visible = 0, left = 0;
    for (int index = 0; index < count(); ++index) if (index != draggedIndex_) {
        const int width = tabRect(index).width();
        if (x < left + width / 2) return visible;
        left += width; ++visible;
    }
    return visible;
}
QSize DragTabBar::tabSizeHint(int index) const {
    auto size = QTabBar::tabSizeHint(index);
    const int iconWidth = tabIcon(index).isNull() ? 0 : iconSize().width() + 4;
    size.setWidth(fontMetrics().size(Qt::TextShowMnemonic, tabText(index)).width() + iconWidth + 35);
    return size;
}
QRect DragTabBar::closeRect(int index) const {
    const auto area = displayTabRect(index);
    return QRect(layoutDirection() == Qt::RightToLeft ? area.left() + 6 : area.right() - 22,
                 area.center().y() - 8, 16, 16);
}
void DragTabBar::mousePressEvent(QMouseEvent* event) {
    start_ = event->position().toPoint(); pressed_ = tabAt(start_);
    if (event->button() == Qt::LeftButton && pressed_ >= 0 && closeRect(pressed_).contains(start_)) {
        closePressed_ = pressed_; pressed_ = -1; event->accept(); return;
    }
    QTabBar::mousePressEvent(event);
}
void DragTabBar::mouseMoveEvent(QMouseEvent* event) {
    const auto hover = tabAt(event->position().toPoint());
    if (hover != hovered_) { hovered_ = hover; update(); }
    if (event->buttons().testFlag(Qt::LeftButton) && pressed_ >= 0 &&
        (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance()) {
        const auto index = pressed_; pressed_ = -1;
        const auto callback = drag; // Detachment may rebuild and destroy this view.
        if (callback) callback(index, event->globalPosition().toPoint());
        return;
    }
    QTabBar::mouseMoveEvent(event);
}
void DragTabBar::mouseReleaseEvent(QMouseEvent* event) {
    pressed_ = -1;
    if (closePressed_ >= 0) {
        const auto index = closePressed_; closePressed_ = -1;
        if (event->button() == Qt::LeftButton && closeRect(index).contains(event->position().toPoint())) {
            const auto callback = close; if (callback) callback(index);
        }
        return;
    }
    QTabBar::mouseReleaseEvent(event);
}
void DragTabBar::leaveEvent(QEvent* event) { hovered_ = -1; update(); QTabBar::leaveEvent(event); }
void DragTabBar::paintEvent(QPaintEvent* event) {
    QStylePainter painter(this);
    const auto draw = [&](int index) {
        if (index == draggedIndex_) return;
        QStyleOptionTab option; initStyleOption(&option, index);
        option.rect = displayTabRect(index);
        if (!option.rect.intersects(event->rect())) return;
        painter.drawControl(QStyle::CE_TabBarTabShape, option);
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        auto textArea = option.rect.adjusted(rtl ? 27 : 7, 0, rtl ? -7 : -27, 0);
        if (!option.icon.isNull()) {
            const int side = iconSize().width();
            const QRect iconArea(rtl ? textArea.right() - side + 1 : textArea.left(),
                                 textArea.center().y() - iconSize().height() / 2,
                                 side, iconSize().height());
            option.icon.paint(&painter, iconArea, Qt::AlignCenter,
                              isTabEnabled(index) ? QIcon::Normal : QIcon::Disabled,
                              index == currentIndex() ? QIcon::On : QIcon::Off);
            textArea.adjust(rtl ? 0 : side + 4, 0, rtl ? -side - 4 : 0, 0);
        }
        painter.drawItemText(textArea, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextShowMnemonic,
                             palette(), isTabEnabled(index), option.text, QPalette::WindowText);
    };
    for (int index = 0; index < count(); ++index) if (index != currentIndex()) draw(index);
    if (currentIndex() >= 0) draw(currentIndex());
    const auto gap = insertionPreviewRect();
    if (!gap.isEmpty()) {
        painter.fillRect(gap.adjusted(1, 1, -1, -1), insertionColor_);
        painter.setPen(QPen(insertionColor_.darker(125), 1, Qt::DashLine));
        painter.drawRect(gap.adjusted(1, 1, -2, -2));
    }
    if (hovered_ >= 0 && hovered_ != draggedIndex_) closeIcon_.paint(&painter, closeRect(hovered_));
}
void DragTabBar::contextMenuEvent(QContextMenuEvent* event) {
    const auto callback = menu; if (callback) callback(tabAt(event->pos()), event->globalPos());
}

GroupTabs::GroupTabs(const Theme& theme, QWidget* parent) : QFrame(parent), theme_(theme)
{
    setObjectName("TabGroup"); setMinimumSize(120, 90);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* header = new QWidget(this); header_ = header; header->setObjectName("TabGroupHeader");
    header->installEventFilter(this); header->setMouseTracking(true);
    auto* row = new QHBoxLayout(header); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
    auto* strip = new QWidget(header);
    auto* stripLayout = new QVBoxLayout(strip); stripLayout->setContentsMargins(0, 0, 0, 0); stripLayout->setSpacing(0);
    scroll_ = new QScrollArea(strip); scroll_->setObjectName("TabScrollViewport");
    scroll_->setFrameShape(QFrame::NoFrame); scroll_->setWidgetResizable(false);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->installEventFilter(this); scroll_->viewport()->setMouseTracking(true);
    bar_ = new DragTabBar(scroll_); bar_->setCloseIcon(iconFor("close", theme));
    bar_->setIconSize(QSize(theme.metrics.iconSize, theme.metrics.iconSize));
    bar_->installEventFilter(this); scroll_->setWidget(bar_);
    // The thin scrollbar overlays the tabs' bottom pixels. Showing it must
    // not reserve a second row or move the content below the tab header.
    scrollbar_ = new QScrollBar(Qt::Horizontal, scroll_->viewport()); scrollbar_->setObjectName("TabScrollBar");
    scrollbar_->setFixedHeight(4); scrollbar_->setSingleStep(28);
    scrollbar_->installEventFilter(this);
    scrollbar_->setStyleSheet("QScrollBar:horizontal{background:transparent;height:4px;border:0;}"
        "QScrollBar::handle:horizontal{background:#9C8FB0;min-width:18px;border-radius:2px;margin:0;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
        "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}");
    scrollbar_->hide();
    stripLayout->addWidget(scroll_, 1);
    list_ = new CompactToolButton(header); list_->setObjectName("TabListButton");
    list_->installEventFilter(this); list_->setMouseTracking(true);
    list_->setIcon(iconFor("chevron-down", theme)); list_->setToolTip(QStringLiteral("所有标签"));
    list_->setAccessibleName(list_->toolTip());
    row->addWidget(strip, 1); row->addWidget(list_, 0, Qt::AlignVCenter);
    layout->addWidget(header);
    stack_ = new QStackedWidget(this); layout->addWidget(stack_, 1);
    autoScroll_ = new QTimer(this); autoScroll_->setInterval(200);
    QObject::connect(autoScroll_, &QTimer::timeout, this, [this] {
        auto* scroll = scroll_->horizontalScrollBar(); const int previous = scroll->value();
        scroll->setValue(previous + scrollDirection_ * 12);
        if (scroll->value() == previous) { autoScroll_->stop(); return; }
        if (autoScroll_->interval() != 35) autoScroll_->setInterval(35);
    });
    QObject::connect(bar_, &QTabBar::currentChanged, this, [this](int index) {
        stack_->setCurrentIndex(index);
        const auto callback = currentChanged; if (callback) callback(index);
    });
    bar_->close = [this](int index) { const auto callback = closeRequested; if (callback) callback(index); };
    QObject::connect(list_, &QToolButton::clicked, this, [this] { showTabList(); });
    QObject::connect(scroll_->horizontalScrollBar(), &QScrollBar::rangeChanged, scrollbar_, [this](int low, int high) {
        scrollbar_->setRange(low, high); scrollbar_->setPageStep(scroll_->viewport()->width());
        updateScrollBarVisibility();
    });
    QObject::connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, scrollbar_, &QScrollBar::setValue);
    QObject::connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        const auto callback = hoverGeometryChanged;
        if (callback) callback();
    });
    QObject::connect(scrollbar_, &QScrollBar::valueChanged, scroll_->horizontalScrollBar(), &QScrollBar::setValue);
}
DragTabBar* GroupTabs::tabBar() const { return bar_; }
GroupTabs::~GroupTabs()
{
    autoScroll_->stop();
    for (auto* child : findChildren<QObject*>()) {
        child->removeEventFilter(this);
        disconnect(child, nullptr, this, nullptr);
    }
}
void GroupTabs::setTabBarVisible(bool visible)
{
    if (header_->isHidden() == !visible) return;
    header_->setVisible(visible);
    if (!visible) {
        hoveredSurfaces_.clear(); autoScroll_->stop(); scrollDirection_ = 0;
        if (tabListMenu_) tabListMenu_->close();
        scrollbar_->hide();
    } else updateScrollBarVisibility();
    layout()->activate();
}
void GroupTabs::updateScrollBarVisibility()
{
    scrollbar_->setVisible(!header_->isHidden() && scrollbar_->maximum() > 0
                           && (dragHovered_ || !hoveredSurfaces_.isEmpty() || header_->underMouse()));
    if (scrollbar_->isVisible()) scrollbar_->raise();
}
void GroupTabs::updateScrollBarGeometry()
{
    if (!scrollbar_) return;
    const auto area = scroll_->viewport()->rect();
    scrollbar_->setGeometry(0, std::max(0, area.height() - 4), area.width(), 4);
    scrollbar_->raise();
}
void GroupTabs::addTab(QWidget* widget, QIcon icon, QString title, TabId id) {
    stack_->addWidget(widget); const auto index = bar_->addTab(icon, title);
    bar_->setTabData(index, id); bar_->setTabToolTip(index, title); updateBarGeometry();
}
void GroupTabs::setCurrentIndex(int index) {
    bar_->setCurrentIndex(index); stack_->setCurrentIndex(index);
    if (index >= 0) {
        const auto area = bar_->tabRect(index); auto* scroll = scroll_->horizontalScrollBar();
        if (area.left() < scroll->value()) scroll->setValue(area.left());
        else if (area.right() >= scroll->value() + scroll_->viewport()->width())
            scroll->setValue(area.right() - scroll_->viewport()->width() + 1);
    }
}
int GroupTabs::currentIndex() const { return bar_->currentIndex(); }
int GroupTabs::count() const { return bar_->count(); }
void GroupTabs::updateBarGeometry() {
    const auto hint = bar_->sizeHint();
    bar_->setFixedSize(qMax(1, hint.width() - bar_->draggedExtent() + bar_->insertionExtent()), qMax(24, hint.height()));
    scroll_->setFixedHeight(bar_->height());
    updateScrollBarGeometry();
}
QRect GroupTabs::viewportRectGlobal() const {
    if (header_->isHidden()) return {};
    return QRect(scroll_->viewport()->mapToGlobal(QPoint()), scroll_->viewport()->size());
}
int GroupTabs::insertionIndex(QPoint global) const {
    return bar_->insertionIndexAt(bar_->mapFromGlobal(global));
}
void GroupTabs::setInsertionPreview(int index, int extent)
{
    if (bar_->property("insertionIndex").toInt() == index && bar_->insertionExtent() == (index < 0 ? 0 : extent)) return;
    bar_->setInsertionPreview(index, extent, theme_.accentSoft);
    updateBarGeometry();
}
void GroupTabs::setDraggedTab(TabId tab)
{
    bar_->setDraggedTab(tab); updateBarGeometry();
}
QRect GroupTabs::insertionPreviewRectGlobal() const
{
    const auto area = bar_->insertionPreviewRect();
    return area.isEmpty() ? QRect{} : QRect(bar_->mapToGlobal(area.topLeft()), area.size()).intersected(viewportRectGlobal());
}
void GroupTabs::setDragPresentation(bool compact, bool edge)
{
    list_->setVisible(!compact);
    for (const auto name : {"UnpinCurrentButton", "UnpinAllButton"})
        if (auto* button = header_->findChild<QToolButton*>(name)) button->setVisible(!compact);
    if (compact) setTabBarVisible(!edge);
}
void GroupTabs::setDragHover(QPoint global) {
    const auto area = viewportRectGlobal();
    dragHovered_ = area.contains(global);
    const int direction = !area.contains(global) ? 0 : global.x() < area.left() + 24 ? -1
        : global.x() > area.right() - 24 ? 1 : 0;
    if (direction != scrollDirection_) autoScroll_->stop();
    scrollDirection_ = direction;
    const auto* scroll = scroll_->horizontalScrollBar();
    const bool canScroll = direction < 0 ? scroll->value() > scroll->minimum() : direction > 0 && scroll->value() < scroll->maximum();
    if (canScroll && !autoScroll_->isActive()) { autoScroll_->setInterval(200); autoScroll_->start(); }
    else if (!canScroll) autoScroll_->stop();
    updateScrollBarVisibility();
}
void GroupTabs::setPinnedActions(std::function<void()> current, std::function<void()> all) {
    auto* row = qobject_cast<QHBoxLayout*>(list_->parentWidget()->layout());
    for (auto item : {std::tuple{std::move(current), QStringLiteral("取消固定当前标签"), QStringLiteral("unpin")},
                      std::tuple{std::move(all), QStringLiteral("取消固定这一侧全部标签"), QStringLiteral("unpin-all")}}) {
        auto* button = new CompactToolButton(list_->parentWidget()); button->setIcon(iconFor(std::get<2>(item), theme_));
        button->setObjectName(std::get<2>(item) == "unpin-all" ? "UnpinAllButton" : "UnpinCurrentButton");
        button->setToolTip(std::get<1>(item)); button->setAccessibleName(std::get<1>(item));
        row->insertWidget(row->count() - 1, button, 0, Qt::AlignVCenter);
        QObject::connect(button, &QToolButton::clicked, this, [callback = std::move(std::get<0>(item))] { if (callback) callback(); });
    }
}
bool GroupTabs::eventFilter(QObject* watched, QEvent* event) {
    if (!scroll_ || !bar_ || !scrollbar_ || !list_) return QFrame::eventFilter(watched, event);
    if (watched == scroll_->viewport() && event->type() == QEvent::Resize) updateScrollBarGeometry();
    if (watched == header_ || watched == list_ || watched == bar_ || watched == scroll_->viewport() || watched == scrollbar_) {
        if (event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            const auto delta = wheel->pixelDelta().isNull() ? wheel->angleDelta() / 3 : wheel->pixelDelta();
            auto* scroll = scroll_->horizontalScrollBar();
            scroll->setValue(scroll->value() - (delta.x() ? delta.x() : delta.y()));
            wheel->accept(); return true;
        }
        if (event->type() == QEvent::Enter) {
            hoveredSurfaces_.insert(watched); updateScrollBarVisibility();
        }
        if (event->type() == QEvent::Leave) {
            hoveredSurfaces_.remove(watched);
            // Leave is delivered before Enter when moving from the tabs onto
            // their scrollbar. Keep the handle available through that handoff.
            QTimer::singleShot(0, this, [this] { updateScrollBarVisibility(); });
        }
    }
    return QFrame::eventFilter(watched, event);
}
void GroupTabs::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); updateBarGeometry(); }
void GroupTabs::showTabList() {
    if (tabListMenu_ && tabListMenu_->isVisible()) { tabListMenu_->close(); return; }
    auto* menu = new DismissOnlyMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose);
    tabListMenu_ = menu;
    list_->setDown(true);
    QObject::connect(menu, &QMenu::aboutToHide, this, [this] { list_->setDown(false); list_->update(); });
    menu->setStyleSheet("QMenu{menu-scrollable:1;}");
    for (int index = 0; index < bar_->count(); ++index) {
        auto* action = new QWidgetAction(menu);
        auto* row = new TabListRow(bar_->tabText(index), bar_->tabIcon(index), menu);
        row->setProperty("tabId", bar_->tabData(index));
        row->closeButton->setIcon(iconFor("close", theme_)); row->closeButton->setToolTip(QStringLiteral("关闭标签"));
        const QPointer<GroupTabs> self(this); const QPointer<QMenu> popup(menu);
        row->activate = [self, popup, index] { if (popup) popup->close(); if (self) self->setCurrentIndex(index); };
        QObject::connect(row->closeButton, &QToolButton::clicked, menu, [self, popup, index] {
            if (popup) popup->close();
            if (self) { const auto callback = self->closeRequested; if (callback) callback(index); }
        });
        action->setDefaultWidget(row); menu->addAction(action);
    }
    menu->popup(list_->mapToGlobal(QPoint(0, list_->height())));
}

} // namespace window_system
