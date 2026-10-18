#include <QCoreApplication>
#include "tab_strip.hpp"
#include "commands/popup_menu.hpp"
#include "style/compact_tool_button.hpp"
#include "style/themed_controls.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QCursor>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QVariantMap>
#include <QPixmap>
#include <algorithm>
#include <limits>
#include <tuple>

namespace window_system {
namespace {
constexpr int listButtonWidth = 12;
constexpr int newTabButtonWidth = 26;
constexpr int createButtonGap = 1;

class TabCreateButtons final : public QWidget {
public:
    TabCreateButtons(const Theme& theme, QWidget* parent) : QWidget(parent) {
        setObjectName("TabCreateButtons");
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(createButtonGap);
        add = new CompactToolButton(this, QSize(newTabButtonWidth, theme.metrics.tabHeight));
        add->setObjectName("NewTabButton"); add->setIcon(iconFor("add", theme));
        add->setToolTip(QCoreApplication::translate("WindowSystem", "新建标签")); add->setAccessibleName(add->toolTip());
        types = new CompactToolButton(this, QSize(listButtonWidth, theme.metrics.tabHeight));
        types->setObjectName("NewTabTypeButton"); types->setIcon(iconFor("chevron-down", theme));
        types->setToolTip(QCoreApplication::translate("WindowSystem", "选择新建标签类型")); types->setAccessibleName(types->toolTip());
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
    TabListRow(QString text, QIcon icon, QColor marker, bool active, QWidget* parent) : QWidget(parent), marker_(marker) {
        setObjectName("TabListRow");
        setProperty("tabColorMarker", marker_);
        setProperty("activeTab", active);
        setMouseTracking(true);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 5, 2); layout->setSpacing(8);
        auto* activeMark = new QLabel(active ? QStringLiteral("\u2713") : QString(), this);
        activeMark->setObjectName("TabListActiveMark");
        activeMark->setFixedWidth(14); activeMark->setAlignment(Qt::AlignCenter);
        activeMark->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(activeMark);
        if (!icon.isNull()) {
            auto* image = new QLabel(this); image->setPixmap(icon.pixmap(16, 16)); layout->addWidget(image);
        }
        title_ = new TabListTitle(text, this); title_->setToolTip(text);
        auto font = title_->font(); font.setBold(active); title_->setFont(font);
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
        if (marker_.isValid()) painter.fillRect(QRect(2, 2, 3, height() - 4), marker_);
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
    QColor marker_;
};
} // namespace

DragTabBar::DragTabBar(QWidget* parent) : QTabBar(parent)
{
    setObjectName("TabBar"); setExpanding(false); setElideMode(Qt::ElideNone);
    setUsesScrollButtons(false); setMouseTracking(true); setDocumentMode(true);
}
void DragTabBar::setAppearance(const TabAppearance& appearance)
{
    appearance_ = appearance;
    // QTabBar caches native size hints. Appearance changes can remove icon
    // space or add a number even when the text/icon identities are unchanged.
    QEvent styleChange(QEvent::StyleChange); QApplication::sendEvent(this,&styleChange);
    updateGeometry(); update();
}
void DragTabBar::setTabMetadata(int index, const TabRecord& tab)
{
    decorations_[index] = {tab.colorId};
}
const TabColorPreset* DragTabBar::tabPreset(int index) const
{
    const auto id = decorations_.value(index).colorId;
    for (const auto& preset : appearance_.colors) if (preset.id == id) return &preset;
    return nullptr;
}
int DragTabBar::displayedTabAt(QPoint local) const
{
    for (int i = 0; i < count(); ++i) if (displayTabRect(i).contains(local)) return i;
    return -1;
}
QSize DragTabBar::displayedSize() const { return displayedSize_; }
QPoint DragTabBar::creationPosition() const { return creationPosition_; }
int DragTabBar::rowCount() const { return rowCount_; }
void DragTabBar::layoutTabs(int availableWidth, bool expanded, int trailingActionsWidth)
{
    customLayout_ = appearance_.multiline;
    displayedRects_.clear(); previewRect_ = {};
    const int lineHeight = std::max(24, count() ? tabSizeHint(0).height() : 28);
    // Expanded rows use the full bar width. Only the final row reserves the
    // list/pinning controls; a collapsed one-row view reserves them normally.
    const int limit = std::max(1, availableWidth - (appearance_.multiline && !expanded ? trailingActionsWidth : 0));
    if (!expanded) trailingActionsWidth = 0;
    int x = 0, y = 0, ordinal = 0;
    auto append = [&](int width) {
        width = appearance_.multiline ? std::min(width, limit) : width;
        if (appearance_.multiline && x && x + width > limit) { x = 0; y += lineHeight; }
        const QRect area(x, y, width, lineHeight); x += width; return area;
    };
    for (int i = 0; i < count(); ++i) {
        if (isDragged(i)) continue;
        if (insertionIndex_ == ordinal && insertionExtent_) {
            // Keep the insertion on the row of the label it precedes. A short
            // carried label must not fill the previous row's leftover space
            // and change the hit target when only the active row is displayed.
            const int followingWidth = std::min(tabSizeHint(i).width(), limit);
            if (appearance_.multiline && x && x + followingWidth > limit) { x = 0; y += lineHeight; }
            previewRect_ = append(insertionExtent_);
        }
        ++ordinal;
        displayedRects_[i] = append(tabSizeHint(i).width());
    }
    if (insertionIndex_ == ordinal && insertionExtent_) previewRect_ = append(insertionExtent_);
    creationPosition_ = {x,y};
    const int creationWidth = appearance_.showNewTabButton ? newTabButtonWidth + listButtonWidth + createButtonGap : 0;
    if (appearance_.multiline && x && x + creationWidth + trailingActionsWidth > limit) {
        creationPosition_ = {0, y + lineHeight}; y += lineHeight;
    }
    rowCount_ = y / lineHeight + 1;
    if (appearance_.multiline) {
        const auto active = displayedRects_.value(currentIndex());
        // A dragged current label temporarily has no displayed rectangle.
        // Keep its logical row until the insertion gap takes over the anchor.
        // Rebased, active-only rectangles cannot supply this logical index.
        if (!previewRect_.isEmpty()) activeLogicalRow_ = previewRect_.top() / lineHeight;
        else if (!active.isEmpty()) activeLogicalRow_ = active.top() / lineHeight;
        activeLogicalRow_ = std::clamp(activeLogicalRow_, 0, rowCount_ - 1);
    }
    if (appearance_.multiline && !expanded) {
        const int activeY = activeLogicalRow_ * lineHeight;
        for (auto it = displayedRects_.begin(); it != displayedRects_.end(); ++it)
            it.value() = it.value().top() == activeY ? it.value().translated(0,-activeY) : QRect{};
        previewRect_ = previewRect_.top() == activeY ? previewRect_.translated(0,-activeY) : QRect{};
        creationPosition_.ry() -= activeY;
        if (creationPosition_.y()) creationPosition_.setY(-lineHeight * 2);
        y = 0;
    }
    displayedSize_ = {appearance_.multiline ? limit : x, y + lineHeight};
}
void DragTabBar::setCloseIcon(QIcon icon) { closeIcon_ = std::move(icon); update(); }
void DragTabBar::setDraggedTab(TabId tab)
{
    draggedIndex_ = -1;
    for (int i = 0; i < count(); ++i) if (!tab.isNull() && tabData(i).toUuid() == tab) draggedIndex_ = i;
    update();
}
bool DragTabBar::isDragged(int index) const { return index == draggedIndex_; }
int DragTabBar::draggedExtent() const
{
    return draggedIndex_ < 0 ? 0 : tabRect(draggedIndex_).width();
}
void DragTabBar::setInsertionPreview(int index, int extent, QColor color)
{
    int moved = 0; for(int i=0;i<count();++i) if(isDragged(i)) ++moved;
    insertionIndex_ = index < 0 ? -1 : qBound(0, index, count() - moved);
    insertionExtent_ = insertionIndex_ < 0 ? 0 : qMax(1, extent);
    insertionColor_ = color;
    setProperty("insertionIndex", insertionIndex_);
    update();
}
QRect DragTabBar::insertionPreviewRect() const
{
    if (insertionIndex_ < 0) return {};
    if (customLayout_) return previewRect_;
    int x = 0, visible = 0;
    for (int i = 0; i < count() && visible < insertionIndex_; ++i)
        if (i != draggedIndex_) { x += tabRect(i).width(); ++visible; }
    return {x, 0, insertionExtent_, height()};
}
QRect DragTabBar::displayTabRect(int index) const
{
    if (customLayout_) return displayedRects_.value(index);
    auto result = tabRect(index);
    if (index == draggedIndex_) return {};
    const int visibleIndex = index - (draggedIndex_ >= 0 && index > draggedIndex_ ? 1 : 0);
    if (draggedIndex_ >= 0 && index > draggedIndex_) result.translate(-draggedExtent(), 0);
    if (insertionIndex_ >= 0 && visibleIndex >= insertionIndex_) result.translate(insertionExtent_, 0);
    return result;
}
int DragTabBar::insertionIndexAt(QPoint local) const
{
    if (customLayout_) {
        if (previewRect_.contains(local)) return insertionIndex_;
        int nearestRow = -1, distance = std::numeric_limits<int>::max();
        for (const auto& rect : displayedRects_) if (!rect.isEmpty()) {
            const int dy = local.y() < rect.top() ? rect.top() - local.y()
                : local.y() > rect.bottom() ? local.y() - rect.bottom() : 0;
            if(dy < distance) { nearestRow=rect.top(); distance=dy; }
        }
        int ordinal = 0, after = 0;
        for (int i = 0; i < count(); ++i) if (!isDragged(i)) {
            const auto rect = displayedRects_.value(i);
            if (!rect.isEmpty() && rect.top() == nearestRow) {
                if(local.x() < rect.center().x()) return ordinal;
                after=ordinal+1;
            }
            ++ordinal;
        }
        return nearestRow < 0 ? (insertionIndex_ >= 0 ? insertionIndex_ : 0) : after;
    }
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
    const int iconWidth = !appearance_.showIcons || tabIcon(index).isNull() ? 0 : iconSize().width() + 4;
    const int numberWidth = appearance_.showNumbers ? fontMetrics().horizontalAdvance(QString::number(index + 1)) + 5 : 0;
    // Without an icon a one-character label can put its center inside the
    // trailing close hitbox. Keep a small independent drag/click area even for
    // the shortest labels; the close target retains its full 16 pixels.
    size.setWidth(std::max(46, fontMetrics().size(Qt::TextShowMnemonic, tabText(index)).width() + iconWidth + numberWidth + 35));
    return size;
}
QRect DragTabBar::closeRect(int index) const {
    const auto area = displayTabRect(index);
    return QRect(layoutDirection() == Qt::RightToLeft ? area.left() + 6 : area.right() - 22,
                 area.center().y() - 8, 16, 16);
}
void DragTabBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) { pressed_ = -1; closePressed_ = -1; event->accept(); return; }
    start_ = event->position().toPoint(); pressed_ = displayedTabAt(start_);
    if (event->button() == Qt::LeftButton && pressed_ >= 0 && closeRect(pressed_).contains(start_)) {
        closePressed_ = pressed_; pressed_ = -1; event->accept(); return;
    }
    if (customLayout_) { if (event->button() == Qt::LeftButton && pressed_ >= 0) setCurrentIndex(pressed_); event->accept(); }
    else QTabBar::mousePressEvent(event);
}
void DragTabBar::mouseMoveEvent(QMouseEvent* event) {
    const auto hover = displayedTabAt(event->position().toPoint());
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
    if(customLayout_) event->accept(); else QTabBar::mouseReleaseEvent(event);
}
void DragTabBar::leaveEvent(QEvent* event) { hovered_ = -1; update(); QTabBar::leaveEvent(event); }
void DragTabBar::paintEvent(QPaintEvent* event) {
    QStylePainter painter(this);
    const auto draw = [&](int index) {
        if (isDragged(index)) return;
        QStyleOptionTab option; initStyleOption(&option, index);
        option.rect = displayTabRect(index);
        if (!option.rect.intersects(event->rect())) return;
        const auto* preset = tabPreset(index);
        const auto* colors = preset ? (index == currentIndex() ? &preset->groupActive : &preset->groupInactive) : nullptr;
        if (colors) {
            painter.fillRect(option.rect, colors->background);
            painter.fillRect(QRect(option.rect.left(), option.rect.bottom() - 1, option.rect.width(), 2), colors->line);
        } else painter.drawControl(QStyle::CE_TabBarTabShape, option);
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        auto textArea = option.rect.adjusted(rtl ? 27 : 7, 0, rtl ? -7 : -27, 0);
        if (appearance_.showNumbers) {
            const auto number = QString::number(index + 1);
            const int width = fontMetrics().horizontalAdvance(number) + 5;
            painter.setPen(colors ? colors->text : palette().color(QPalette::WindowText));
            painter.drawText(QRect(textArea.left(),textArea.top(),width,textArea.height()), Qt::AlignVCenter,number);
            textArea.adjust(width,0,0,0);
        }
        if (appearance_.showIcons && !option.icon.isNull()) {
            const int side = iconSize().width();
            const QRect iconArea(rtl ? textArea.right() - side + 1 : textArea.left(),
                                 textArea.center().y() - iconSize().height() / 2,
                                 side, iconSize().height());
            if (colors) {
                auto pixmap = option.icon.pixmap(iconArea.size());
                QPainter iconPainter(&pixmap); iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                iconPainter.fillRect(pixmap.rect(), colors->icon); iconPainter.end(); painter.drawPixmap(iconArea,pixmap);
            } else option.icon.paint(&painter, iconArea, Qt::AlignCenter,
                              isTabEnabled(index) ? QIcon::Normal : QIcon::Disabled,
                              index == currentIndex() ? QIcon::On : QIcon::Off);
            textArea.adjust(rtl ? 0 : side + 4, 0, rtl ? -side - 4 : 0, 0);
        }
        auto textPalette = palette(); if (colors) textPalette.setColor(QPalette::WindowText,colors->text);
        painter.drawItemText(textArea, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextShowMnemonic,
                             textPalette, isTabEnabled(index), fontMetrics().elidedText(option.text,Qt::ElideRight,textArea.width()), QPalette::WindowText);
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
    const auto callback = menu; if (callback) callback(displayedTabAt(event->pos()), event->globalPos());
}

GroupTabs::GroupTabs(const Theme& theme, QWidget* parent) : QFrame(parent), theme_(theme)
{
    setObjectName("TabGroup");
    viewportWindow_ = window(); viewportWindow_->installEventFilter(this);
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
    canvas_ = new QWidget; canvas_->setObjectName("TabStripCanvas"); canvas_->installEventFilter(this);
    bar_ = new DragTabBar(canvas_); bar_->setCloseIcon(iconFor("close", theme));
    bar_->setIconSize(QSize(theme.metrics.iconSize, theme.metrics.iconSize));
    bar_->installEventFilter(this); scroll_->setWidget(canvas_);
    auto* create = new TabCreateButtons(theme, canvas_); createButtons_ = create;
    create->show();
    QObject::connect(create->add, &QToolButton::clicked, this, [this] {
        const auto callback = newTabRequested; if (callback) callback();
    });
    QObject::connect(create->types, &QToolButton::clicked, this, [this] { showNewTabMenu(); });
    create->menuRequested = [this] { showNewTabMenu(); };
    // The thin scrollbar overlays the tabs' bottom pixels. Showing it must
    // not reserve a second row or move the content below the tab header.
    scrollbar_ = new QScrollBar(Qt::Horizontal, scroll_->viewport()); scrollbar_->setObjectName("TabScrollBar");
    scrollbar_->setFixedHeight(4); scrollbar_->setSingleStep(28);
    scrollbar_->installEventFilter(this);
    scrollbar_->setStyleSheet(overlayScrollbarStyle(theme));
    scrollbar_->hide();
    stripLayout->addWidget(scroll_, 1);
    headerActions_ = new QWidget(header); headerActions_->setObjectName("TabHeaderActions");
    auto* actions = new QHBoxLayout(headerActions_); actions->setContentsMargins(0, 0, 0, 0); actions->setSpacing(0);
    list_ = new CompactToolButton(headerActions_, QSize(listButtonWidth, theme.metrics.tabHeight)); list_->setObjectName("TabListButton");
    list_->installEventFilter(this); list_->setMouseTracking(true);
    list_->setIcon(iconFor("chevron-down", theme)); list_->setToolTip(QCoreApplication::translate("WindowSystem", "所有标签"));
    list_->setAccessibleName(list_->toolTip());
    actions->addWidget(list_);
    row->addWidget(strip, 1); row->addWidget(headerActions_,0,Qt::AlignBottom);
    layout->addWidget(header);
    stack_ = new QStackedWidget(this); layout->addWidget(stack_, 1);
    autoScroll_ = new QTimer(this); autoScroll_->setInterval(200);
    QObject::connect(autoScroll_, &QTimer::timeout, this, [this] {
        cancelViewportReflow();
        auto* scroll = scroll_->horizontalScrollBar(); const int previous = scroll->value();
        scroll->setValue(previous + scrollDirection_ * 12);
        if (scroll->value() == previous) { autoScroll_->stop(); return; }
        if (autoScroll_->interval() != 35) autoScroll_->setInterval(35);
    });
    QObject::connect(bar_, &QTabBar::currentChanged, this, [this](int index) {
        const QPointer<GroupTabs> self(this);
        stack_->setCurrentIndex(index);
        if(!self) return;
        if(appearance_.multiline) updateBarGeometry();
        const auto callback = currentChanged; if (callback) callback(index);
    });
    bar_->close = [this](int index) { const auto callback = closeRequested; if (callback) callback(index); };
    QObject::connect(list_, &QToolButton::clicked, this, [this] { showTabList(); });
    QObject::connect(scroll_->horizontalScrollBar(), &QScrollBar::rangeChanged, scrollbar_, [this](int low, int high) {
        scrollbar_->setRange(low, high); scrollbar_->setPageStep(scroll_->viewport()->width());
        applyPendingScrollOffset();
        updateScrollBarVisibility();
    });
    QObject::connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, scrollbar_, &QScrollBar::setValue);
    QObject::connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!applyingViewportState_ && !pendingViewportState_ && !pendingScrollOffset_) {
            settledViewport_ = currentViewportState();
        }
        const auto callback = hoverGeometryChanged;
        if (callback) callback();
    });
    QObject::connect(scrollbar_, &QScrollBar::sliderMoved, this, [this] { cancelViewportReflow(); });
    QObject::connect(scrollbar_, &QScrollBar::actionTriggered, this, [this] { cancelViewportReflow(); });
    QObject::connect(scrollbar_, &QScrollBar::valueChanged, scroll_->horizontalScrollBar(), &QScrollBar::setValue);
}
DragTabBar* GroupTabs::tabBar() const { return bar_; }
GroupTabs::~GroupTabs()
{
    autoScroll_->stop();
    if (viewportWindow_) viewportWindow_->removeEventFilter(this);
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
        if (newTabMenu_) newTabMenu_->close();
        scrollbar_->hide();
    } else updateScrollBarVisibility();
    layout()->activate();
}
void GroupTabs::updateScrollBarVisibility()
{
    scrollbar_->setVisible(!appearance_.multiline && !header_->isHidden() && scrollbar_->maximum() > 0
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
void GroupTabs::setCurrentIndex(int index, bool reveal) {
    bar_->setCurrentIndex(index); stack_->setCurrentIndex(index);
    if (appearance_.multiline) { updateBarGeometry(); return; }
    if (reveal && index >= 0) {
        cancelViewportReflow();
        const auto area = bar_->displayTabRect(index); auto* scroll = scroll_->horizontalScrollBar();
        if (area.left() < scroll->value()) scroll->setValue(area.left());
        else if (area.right() >= scroll->value() + scroll_->viewport()->width())
            scroll->setValue(area.right() - scroll_->viewport()->width() + 1);
    }
}
int GroupTabs::scrollOffset() const { return pendingScrollOffset_.value_or(scroll_->horizontalScrollBar()->value()); }
TabViewportState GroupTabs::currentViewportState() const
{
    TabViewportState state;
    state.offset = scrollOffset();
    state.rowsExpanded = rowsExpanded_;
    state.windowClientBounds = window()->geometry();
    if (isVisible() && !header_->isHidden())
        state.bounds = {scroll_->viewport()->mapTo(window(), QPoint()), scroll_->viewport()->size()};
    const auto current = bar_->currentIndex();
    if (current >= 0) {
        state.activeTab = bar_->tabData(current).toUuid();
        const auto area = bar_->displayTabRect(current);
        state.activeFullyVisible = !area.isEmpty() && !state.bounds.isEmpty()
            && area.left() >= state.offset && area.right() < state.offset + state.bounds.width();
    }
    return state;
}
TabViewportState GroupTabs::captureViewportState() const
{
    // Consecutive API mutations before the next layout turn retain the same
    // origin instead of capturing an intermediate, incompletely sized strip.
    auto state=pendingViewportState_.value_or(currentViewportState());
    state.rowsExpanded=rowsExpanded_;
    return state;
}
void GroupTabs::restoreViewportState(TabViewportState state)
{
    if(appearance_.multiline && appearance_.wheelExpandRows && state.rowsExpanded) {
        rowsExpanded_=*state.rowsExpanded; updateBarGeometry();
    }
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
    if (current.bounds.isEmpty()) return false;
    if (old.bounds.isEmpty()) {
        // A newly constructed hidden strip has no displayed anchor. Its
        // provisional focus scroll must clamp to the first real viewport,
        // rather than manufacture trailing space before the window is shown.
        updateCanvasGeometry();
        scroll_->horizontalScrollBar()->setValue(old.offset);
        return true;
    }
    int offset = old.offset;
    // Preserve the displayed content through either edge moving, including
    // expansion. A plain desktop translation is unrelated to strip scrolling.
    const int windowLeftShift = current.windowClientBounds.width() != old.windowClientBounds.width()
        ? current.windowClientBounds.left() - old.windowClientBounds.left() : 0;
    offset += current.bounds.left() - old.bounds.left() + windowLeftShift;
    if (old.activeFullyVisible && old.activeTab == current.activeTab) {
        const auto area = bar_->displayTabRect(bar_->currentIndex());
        if (!area.isEmpty()) {
            const int rightLimit = area.right() - current.bounds.width() + 1;
            // Only move as far as needed to keep the active tab readable.
            // An oversized label exposes its beginning; no extra blank extent
            // is manufactured for a last tab or an underfilled strip.
            offset = current.bounds.width() >= area.width()
                ? qBound(rightLimit, offset, area.left()) : area.left();
        }
    }
    updateCanvasGeometry();
    scroll_->horizontalScrollBar()->setValue(offset);
    return true;
}
void GroupTabs::applyPendingScrollOffset()
{
    if (pendingScrollOffset_) scroll_->horizontalScrollBar()->setValue(*pendingScrollOffset_);
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
    if (appearance_.multiline) {
        if (appearance_.wheelExpandRows && !delta.isNull()) {
            cancelViewportReflow();
            rowsExpanded_ = (delta.y() ? delta.y() : delta.x()) < 0; updateBarGeometry();
            settledViewport_=currentViewportState();
        }
        return;
    }
    cancelViewportReflow();
    auto* scroll = scroll_->horizontalScrollBar();
    scroll->setValue(scroll->value() - (delta.x() ? delta.x() : delta.y()));
}
int GroupTabs::currentIndex() const { return bar_->currentIndex(); }
int GroupTabs::count() const { return bar_->count(); }
void GroupTabs::updateBarGeometry() {
    if (updatingBarGeometry_) return;
    QScopedValueRollback updating(updatingBarGeometry_, true);
    auto* headerLayout = static_cast<QHBoxLayout*>(header_->layout());
    const int actionsWidth = headerActions_->sizeHint().width();
    const bool inLayout = headerLayout->indexOf(headerActions_) >= 0;
    if (appearance_.multiline && inLayout) headerLayout->removeWidget(headerActions_);
    else if (!appearance_.multiline && !inLayout) headerLayout->addWidget(headerActions_, 0, Qt::AlignBottom);
    headerLayout->activate();
    bar_->layoutTabs(scroll_->viewport()->width(),rowsExpanded_,appearance_.multiline ? actionsWidth : 0);
    const auto hint = bar_->displayedSize();
    bar_->setFixedSize(qMax(1, hint.width()), qMax(24, hint.height()));
    const int lineHeight = bar_->height() / (appearance_.multiline && rowsExpanded_ ? std::max(1,bar_->rowCount()) : 1);
    auto* create = static_cast<TabCreateButtons*>(createButtons_);
    create->setHeight(lineHeight); create->move(bar_->creationPosition());
    updateCanvasGeometry();
    static_cast<CompactToolButton*>(list_)->setTargetSize(QSize(listButtonWidth, lineHeight));
    scroll_->setFixedHeight(bar_->height());
    headerActions_->setFixedHeight(lineHeight);
    if (appearance_.multiline) {
        headerActions_->setGeometry(std::max(0, header_->width() - actionsWidth),
            std::max(0, bar_->height() - lineHeight), actionsWidth, lineHeight);
        headerActions_->raise();
    }
    updateScrollBarGeometry();
    // In particular, a bottom header changes its origin when rows expand.
    // Publish that position together with the rectangles used for hit testing,
    // rather than leaving the new height at the previous row's origin.
    for (auto* section = scroll_->parentWidget(); section; section = section->parentWidget()) {
        if (section->layout()) {
            section->layout()->invalidate();
            section->layout()->activate();
        }
        section->updateGeometry();
        if (section == this) break;
    }
    // Changing the row count from a viewport Resize callback can resize the
    // scroll area while QAbstractScrollArea is already laying it out. Qt skips
    // that nested Resize, leaving the viewport at the previous row count.
    // Both native scrollbars are AlwaysOff, so an owned layout request safely
    // recomputes the viewport after our final height, without pumping input.
    QEvent request(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(scroll_, &request);
    updateScrollBarGeometry();
}
void GroupTabs::updateCanvasGeometry()
{
    if (!canvas_ || !createButtons_) return;
    const int naturalWidth = bar_->width() + (appearance_.multiline || createButtons_->isHidden() ? 0 : createButtons_->width());
    canvas_->setFixedSize(naturalWidth, bar_->height());
}
void GroupTabs::setAppearance(const TabAppearance& appearance, bool pinned)
{
    appearance_ = appearance; pinned_ = pinned;
    if (pinned_) {
        appearance_.showIcons = appearance.showSidebarIcons;
        appearance_.showNumbers = appearance.showSidebarNumbers;
    }
    rowsExpanded_ = !appearance_.wheelExpandRows;
    bar_->setAppearance(appearance_);
    createButtons_->setVisible(appearance_.showNewTabButton);
    auto* box = qobject_cast<QVBoxLayout*>(layout());
    const auto placement = pinned ? appearance_.pinnedPlacement : appearance_.centralPlacement;
    box->removeWidget(header_);
    box->insertWidget(placement == TabBarPlacement::Top ? 0 : box->count(),header_);
    updateBarGeometry();
}
void GroupTabs::setTabMetadata(int index,const TabRecord& tab)
{
    bar_->setTabMetadata(index,tab); updateBarGeometry();
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
    createButtons_->setVisible(!compact && appearance_.showNewTabButton);
    for (const auto name : {"UnpinCurrentButton", "UnpinAllButton"})
        if (auto* button = header_->findChild<QToolButton*>(name)) button->setVisible(!compact);
    if (compact) setTabBarVisible(!edge);
    updateBarGeometry();
}
void GroupTabs::setDragHover(QPoint global) {
    const auto area = viewportRectGlobal();
    dragHovered_ = area.contains(global);
    const int direction = appearance_.multiline || !area.contains(global) ? 0 : global.x() < area.left() + 24 ? -1
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
    for (auto item : {std::tuple{std::move(current), QCoreApplication::translate("WindowSystem", "取消固定当前标签"), QStringLiteral("unpin")},
                      std::tuple{std::move(all), QCoreApplication::translate("WindowSystem", "取消固定这一侧全部标签"), QStringLiteral("unpin-all")}}) {
        auto* button = new CompactToolButton(list_->parentWidget()); button->setIcon(iconFor(std::get<2>(item), theme_));
        button->setObjectName(std::get<2>(item) == "unpin-all" ? "UnpinAllButton" : "UnpinCurrentButton");
        button->setToolTip(std::get<1>(item)); button->setAccessibleName(std::get<1>(item));
        row->insertWidget(row->count() - 1, button, 0, Qt::AlignBottom);
        QObject::connect(button, &QToolButton::clicked, this, [callback = std::move(std::get<0>(item))] { if (callback) callback(); });
    }
    updateBarGeometry();
}
bool GroupTabs::eventFilter(QObject* watched, QEvent* event) {
    if(event->type()==QEvent::ContextMenu && (watched==header_ || watched==canvas_ || (scroll_ && watched==scroll_->viewport()))) {
        const auto callback=groupMenuRequested;
        const auto pos=static_cast<QContextMenuEvent*>(event)->globalPos();
        if(callback) callback(pos);
        return true;
    }
    if (event->type() == QEvent::Show && (watched == viewportWindow_ || watched == header_ || watched == scroll_->viewport()))
        queueViewportReflow();
    if (!scroll_ || !bar_ || !scrollbar_ || !list_) return QFrame::eventFilter(watched, event);
    if (watched == viewportWindow_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
        queueViewportReflow();
    if (watched == scroll_->viewport() && event->type() == QEvent::Resize) {
        updateBarGeometry(); updateScrollBarGeometry(); queueViewportReflow();
    }
    if (watched == header_ || watched == list_ || watched == bar_ || watched == scroll_->viewport() || watched == scrollbar_) {
        if (event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            const auto delta = wheel->pixelDelta().isNull() ? wheel->angleDelta() / 3 : wheel->pixelDelta();
            scrollByWheel(delta);
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
void GroupTabs::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event); updateBarGeometry(); queueViewportReflow();
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
            preset ? preset->groupInactive.line : QColor{}, index == bar_->currentIndex(), menu);
        row->setProperty("tabId", bar_->tabData(index));
        row->closeButton->setIcon(iconFor("close", theme_)); row->closeButton->setToolTip(QCoreApplication::translate("WindowSystem", "关闭标签"));
        const QPointer<GroupTabs> self(this); const QPointer<QMenu> popup(menu);
        row->activate = [self, popup, index] { if (popup) popup->close(); if (self) self->setCurrentIndex(index); };
        QObject::connect(row->closeButton, &QToolButton::clicked, menu, [self, popup, index] {
            if (popup) popup->close();
            if (self) { const auto callback = self->closeRequested; if (callback) callback(index); }
        });
        action->setDefaultWidget(row); menu->addAction(action);
    }
    setPopupAnchor(menu,QRect(list_->mapToGlobal(QPoint()),list_->size()));
    menu->popup(list_->mapToGlobal(QPoint(0, list_->height())));
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
