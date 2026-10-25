// SPDX-License-Identifier: MIT
#include "tab_bar.hpp"
#include "style/compact_tool_button.hpp"
#include "style/tab_appearance_p.hpp"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QEasingCurve>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace window_system {
namespace {

bool animationsSwitch = true;

// Rows of a column or an icon column are shaped as rows; the rest are tabs.
TabShape horizontalShape(TabBarPlacement placement) { return placement == TabBarPlacement::Bottom ? TabShape::Bottom : TabShape::Top; }

QRect lerp(const QRect& from, const QRect& to, qreal t)
{
    const auto mix = [t](int a, int b) { return int(std::lround(a + (b - a) * t)); };
    return QRect(QPoint(mix(from.left(), to.left()), mix(from.top(), to.top())),
                 QSize(mix(from.width(), to.width()), mix(from.height(), to.height())));
}

// Screen readers see the strip as a page tab list of its tabs.
class TabAccessible final : public QAccessibleInterface {
public:
    TabAccessible(TabBar* bar, QVariant id) : bar_(bar), id_(std::move(id)) {}
    [[nodiscard]] bool isValid() const override { return bar_ && index() >= 0; }
    [[nodiscard]] QObject* object() const override { return nullptr; }
    [[nodiscard]] QWindow* window() const override { return bar_ ? bar_->window()->windowHandle() : nullptr; }
    [[nodiscard]] QAccessibleInterface* parent() const override { return bar_ ? QAccessible::queryAccessibleInterface(bar_) : nullptr; }
    [[nodiscard]] QAccessibleInterface* child(int) const override { return nullptr; }
    [[nodiscard]] int childCount() const override { return 0; }
    [[nodiscard]] int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    [[nodiscard]] QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    [[nodiscard]] QString text(QAccessible::Text type) const override
    {
        const int at = index();
        if (at < 0) return {};
        if (type == QAccessible::Name) return bar_->tabText(at);
        if (type == QAccessible::Description) return bar_->tabToolTip(at);
        return {};
    }
    void setText(QAccessible::Text, const QString&) override {}
    [[nodiscard]] QRect rect() const override
    {
        const int at = index();
        if (at < 0) return {};
        const auto area = bar_->tabRect(at);
        return QRect(bar_->mapToGlobal(area.topLeft()), area.size());
    }
    [[nodiscard]] QAccessible::Role role() const override { return QAccessible::PageTab; }
    [[nodiscard]] QAccessible::State state() const override
    {
        QAccessible::State result;
        const int at = index();
        if (at < 0) { result.invalid = true; return result; }
        result.selectable = true; result.selected = at == bar_->currentIndex();
        result.focusable = true; result.focused = result.selected && bar_->hasFocus();
        result.invisible = bar_->tabRect(at).isEmpty();
        return result;
    }
private:
    [[nodiscard]] int index() const
    {
        if (!bar_) return -1;
        for (int at = 0; at < bar_->count(); ++at) if (bar_->tabData(at) == id_) return at;
        return -1;
    }
    QPointer<TabBar> bar_;
    QVariant id_;
};

// A tab folder's header: a button that expands or collapses the folder.
class TabFolderAccessible final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    TabFolderAccessible(TabBar* bar, QUuid id) : bar_(bar), id_(id) {}
    [[nodiscard]] bool isValid() const override { return bar_ && index() >= 0; }
    [[nodiscard]] QObject* object() const override { return nullptr; }
    [[nodiscard]] QWindow* window() const override { return bar_ ? bar_->window()->windowHandle() : nullptr; }
    [[nodiscard]] QAccessibleInterface* parent() const override { return bar_ ? QAccessible::queryAccessibleInterface(bar_) : nullptr; }
    [[nodiscard]] QAccessibleInterface* child(int) const override { return nullptr; }
    [[nodiscard]] int childCount() const override { return 0; }
    [[nodiscard]] int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    [[nodiscard]] QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    [[nodiscard]] QString text(QAccessible::Text type) const override
    {
        const int at = index();
        if (at < 0 || type != QAccessible::Name) return {};
        const auto& folder = bar_->folders().at(at);
        const auto count = QCoreApplication::translate("WindowSystem", "%n tab(s)", nullptr, folder.count);
        return folder.name.isEmpty() ? QCoreApplication::translate("WindowSystem", "Tab folder, %1").arg(count)
                                     : QCoreApplication::translate("WindowSystem", "Tab folder %1, %2").arg(folder.name, count);
    }
    void setText(QAccessible::Text, const QString&) override {}
    [[nodiscard]] QRect rect() const override
    {
        const int at = index();
        if (at < 0) return {};
        const auto area = bar_->folderHeaderRect(at);
        return QRect(bar_->mapToGlobal(area.topLeft()), area.size());
    }
    [[nodiscard]] QAccessible::Role role() const override { return QAccessible::Button; }
    [[nodiscard]] QAccessible::State state() const override
    {
        QAccessible::State result;
        const int at = index();
        if (at < 0) { result.invalid = true; return result; }
        result.expandable = true; result.expanded = !bar_->folders().at(at).collapsed;
        result.collapsed = !result.expanded; result.invisible = bar_->folderHeaderRect(at).isEmpty();
        return result;
    }
    void* interface_cast(QAccessible::InterfaceType type) override
    {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr;
    }
    [[nodiscard]] QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString& name) override
    {
        const int at = index();
        if (at >= 0 && name == pressAction()) if (const auto callback = bar_->toggleFolder) callback(at);
    }
    [[nodiscard]] QStringList keyBindingsForAction(const QString&) const override { return {}; }
private:
    [[nodiscard]] int index() const
    {
        if (!bar_) return -1;
        for (int at = 0; at < bar_->folders().size(); ++at) if (bar_->folders().at(at).id == id_) return at;
        return -1;
    }
    QPointer<TabBar> bar_;
    QUuid id_;
};

class TabBarAccessible final : public QAccessibleWidget {
public:
    explicit TabBarAccessible(TabBar* bar) : QAccessibleWidget(bar, QAccessible::PageTabList) {}
    // The tabs, then the folder headers.
    [[nodiscard]] int childCount() const override { return bar()->count() + int(bar()->folders().size()); }
    [[nodiscard]] QAccessibleInterface* child(int index) const override
    {
        if (index < 0 || index >= childCount()) return nullptr;
        const bool folder = index >= bar()->count();
        const auto key = folder ? bar()->folders().at(index - bar()->count()).id : bar()->tabData(index).toUuid();
        auto& cached = children_[key];
        if (!cached || !QAccessible::accessibleInterface(cached)) {
            QAccessibleInterface* created = folder ? static_cast<QAccessibleInterface*>(new TabFolderAccessible(bar(), key))
                                                   : new TabAccessible(bar(), key);
            cached = QAccessible::registerAccessibleInterface(created);
        }
        return QAccessible::accessibleInterface(cached);
    }
    [[nodiscard]] int indexOfChild(const QAccessibleInterface* child) const override
    {
        for (int index = 0; index < childCount(); ++index) if (this->child(index) == child) return index;
        return -1;
    }
    [[nodiscard]] QAccessibleInterface* childAt(int x, int y) const override
    {
        const auto local = bar()->mapFromGlobal(QPoint(x, y));
        const int index = bar()->tabAt(local);
        if (index >= 0) return child(index);
        const int folder = bar()->folderAt(local);
        return folder >= 0 ? child(bar()->count() + folder) : nullptr;
    }
    ~TabBarAccessible() override
    {
        for (const auto id : std::as_const(children_)) if (QAccessible::accessibleInterface(id)) QAccessible::deleteAccessibleInterface(id);
    }
private:
    [[nodiscard]] TabBar* bar() const { return static_cast<TabBar*>(widget()); }
    mutable QHash<QUuid, QAccessible::Id> children_;
};

QAccessibleInterface* accessibleFactory(const QString& className, QObject* object)
{
    if (className == QLatin1StringView("window_system::TabBar") && object && object->isWidgetType())
        return new TabBarAccessible(static_cast<TabBar*>(object));
    return nullptr;
}

} // namespace

void TabBar::setAnimationsEnabled(bool enabled) { animationsSwitch = enabled; }

bool TabBar::animationsEnabled()
{
    if (!animationsSwitch || qEnvironmentVariable("WINDOW_SYSTEM_ANIMATIONS") == QLatin1StringView("0")) return false;
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) return false;
#endif
    return true;
}

TabBar::TabBar(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme), metrics_(TabMetrics::forTheme(theme, font())),
      painter_(metrics_, TabPalette::forTheme(theme))
{
    static const bool factory = [] { QAccessible::installFactory(accessibleFactory); return true; }();
    Q_UNUSED(factory);
    setObjectName("TabBar");
    setMouseTracking(true);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
    countButton_ = new CompactToolButton(this, QSize(metrics_.countMinimumWidth, metrics_.rowHeight));
    countButton_->setObjectName("TabRowCountButton");
    countButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    countButton_->hide();
    connect(countButton_, &QToolButton::clicked, this, [this] { setExpanded(!expanded_); });
    collapseTimer_.setSingleShot(true); collapseTimer_.setInterval(400);
    connect(&collapseTimer_, &QTimer::timeout, this, [this] { if (expandedByWheel_) setExpanded(false); });
    peekTimer_.setSingleShot(true); peekTimer_.setInterval(400);
    connect(&peekTimer_, &QTimer::timeout, this, [this] {
        if (!iconMode_ || peeking_) return;
        peeking_ = true;
        if (const auto callback = reservedSizeChanged) callback();
    });
    slideTimer_.setInterval(15);
    connect(&slideTimer_, &QTimer::timeout, this, [this] {
        if (slideClock_.elapsed() >= slideDuration_) { slides_.clear(); slideTimer_.stop(); }
        update();
    });
}

TabBar::~TabBar() { qApp->removeEventFilter(this); }

int TabBar::addTab(const QIcon& icon, const QString& title)
{
    Tab tab; tab.icon = icon; tab.title = title;
    tabs_.append(std::move(tab));
    if (current_ < 0) current_ = 0;
    relayout(true);
    return int(tabs_.size()) - 1;
}

void TabBar::removeTab(int index)
{
    if (index < 0 || index >= tabs_.size()) return;
    const bool wasCurrent = index == current_;
    tabs_.removeAt(index);
    if (index < stickyCount_) --stickyCount_;
    if (dragged_ == index) dragged_ = -1; else if (dragged_ > index) --dragged_;
    if (hovered_ >= index) hovered_ = -1;
    pressed_ = actionPressed_ = middlePressed_ = -1;
    const int previous = current_;
    if (tabs_.isEmpty()) current_ = -1;
    else if (index < current_) --current_;
    else if (wasCurrent) current_ = std::min(index, int(tabs_.size()) - 1);
    relayout(true);
    if (wasCurrent || previous != current_) emit currentChanged(current_);
}

QVariant TabBar::tabData(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).data : QVariant{}; }
void TabBar::setTabData(int index, const QVariant& data) { if (index >= 0 && index < tabs_.size()) tabs_[index].data = data; }
QString TabBar::tabText(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).title : QString{}; }
void TabBar::setTabText(int index, const QString& text)
{
    if (index < 0 || index >= tabs_.size() || tabs_.at(index).title == text) return;
    tabs_[index].title = text; tabs_[index].titleWidth = -1;
    relayout(true);
}
QIcon TabBar::tabIcon(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).icon : QIcon{}; }
void TabBar::setTabIcon(int index, const QIcon& icon)
{
    if (index < 0 || index >= tabs_.size()) return;
    tabs_[index].icon = icon; relayout(true);
}
QString TabBar::tabToolTip(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).tooltip : QString{}; }
void TabBar::setTabToolTip(int index, const QString& tip) { if (index >= 0 && index < tabs_.size()) tabs_[index].tooltip = tip; }
void TabBar::setTabMetadata(int index, const TabRecord& tab)
{
    if (index < 0 || index >= tabs_.size()) return;
    tabs_[index].colorId = tab.colorId; update();
}
const TabColorPreset* TabBar::tabPreset(int index) const
{
    if (index < 0 || index >= tabs_.size() || tabs_.at(index).colorId.isEmpty()) return nullptr;
    for (const auto& preset : appearance_.colors) if (preset.id == tabs_.at(index).colorId) return &preset;
    return nullptr;
}
int TabBar::indexOf(TabId tab) const
{
    for (int index = 0; index < tabs_.size(); ++index) if (tabs_.at(index).data.toUuid() == tab) return index;
    return -1;
}

void TabBar::setCurrentIndex(int index)
{
    if (index < -1 || index >= tabs_.size() || index == current_) return;
    current_ = index;
    // The current row may change while collapsed.
    relayout(false);
    emit currentChanged(index);
}

void TabBar::setTheme(const Theme& theme)
{
    theme_ = theme;
    metrics_ = TabMetrics::forTheme(theme, font());
    painter_ = TabPainter(metrics_, TabPalette::forTheme(theme));
    updateTitleWidths();
    relayout(false);
}

void TabBar::setAppearance(const TabAppearance& appearance, TabBarPlacement placement)
{
    const bool arrangement = appearance_.multiline != appearance.multiline || appearance_.collapseRows != appearance.collapseRows
        || placement_ != placement;
    appearance_ = appearance; placement_ = placement;
    if (arrangement) {
        // A new arrangement starts collapsed (when it collapses) and at its start.
        expanded_ = false; expandedByWheel_ = false; rowsOffset_ = 0;
    }
    if (!vertical()) { iconMode_ = false; peeking_ = false; }
    updateTitleWidths();
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setStickyCount(int count)
{
    count = std::clamp(count, 0, int(tabs_.size()));
    if (stickyCount_ == count) return;
    stickyCount_ = count;
    relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setFolders(QVector<TabStripFolder> folders)
{
    if (folders_ == folders) return;
    const auto dragged = draggedFolder_ >= 0 && draggedFolder_ < folders_.size() ? folders_.at(draggedFolder_).id : QUuid{};
    folders_ = std::move(folders);
    draggedFolder_ = -1;
    for (int folder = 0; folder < folders_.size(); ++folder) if (folders_.at(folder).id == dragged) draggedFolder_ = folder;
    hoveredHeader_ = pressedHeader_ = -1;
    relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}
int TabBar::folderOf(int index) const
{
    for (int folder = 0; folder < folders_.size(); ++folder)
        if (index >= folders_.at(folder).first && index < folders_.at(folder).first + folders_.at(folder).count) return folder;
    return -1;
}
QRect TabBar::folderHeaderRect(int folder) const
{
    const auto visible = layout_.visibleHeaderRect(folder);
    return visible.isEmpty() ? QRect{} : visible.translated(tabOrigin());
}
int TabBar::folderAt(QPoint local) const { return layout_.headerAt(local - tabOrigin()); }
const TabColorPreset* TabBar::folderPreset(int folder) const
{
    if (folder < 0 || folder >= folders_.size() || appearance_.colors.isEmpty()) return nullptr;
    const auto& colors = appearance_.colors;
    const auto explicitIndex = [&](int at) {
        for (int index = 0; index < colors.size(); ++index) if (colors.at(index).id == folders_.at(at).colorId) return index;
        return -1;
    };
    if (const int index = explicitIndex(folder); index >= 0) return &colors.at(index);
    // Automatic: from the folder's identity, so it stays the same, past the
    // colors that other folders of this strip already show.
    QVector<bool> taken(colors.size(), false);
    for (int other = 0; other < folders_.size(); ++other) if (other != folder && explicitIndex(other) >= 0) taken[explicitIndex(other)] = true;
    int chosen = -1;
    for (int at = 0; at <= folder; ++at) {
        if (explicitIndex(at) >= 0) continue;
        int index = int(qHash(folders_.at(at).id) % uint(colors.size()));
        for (int step = 0; step < colors.size() && taken.at(index); ++step) index = (index + 1) % int(colors.size());
        taken[index] = true; chosen = index;
    }
    return &colors.at(std::max(0, chosen));
}
int TabBar::folderHeaderExtent(const TabStripFolder& folder) const
{
    if (vertical()) return 0;
    const auto& m = metrics_;
    const auto count = QString::number(folder.count);
    if (folder.name.isEmpty() && !folder.collapsed) return m.folderDot + 2 * m.folderChipMargin + 4;
    const auto label = folder.collapsed ? (folder.name.isEmpty() ? count : folder.name + QStringLiteral("  ") + count) : folder.name;
    // A pixel of slack: elision compares fractional advances.
    const int text = std::min(fontMetrics().horizontalAdvance(label) + 2, appearance_.maximumTitleWidth);
    return text + 2 * m.folderChipPadding + 2 * m.folderChipMargin;
}
void TabBar::setGroupFocused(bool focused)
{
    if (groupFocused_ == focused) return;
    groupFocused_ = focused; update();
}

void TabBar::setIconMode(bool iconMode)
{
    if (!vertical()) iconMode = false;
    if (iconMode_ == iconMode) return;
    iconMode_ = iconMode; peeking_ = false;
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setTrailingWidget(QWidget* widget)
{
    if (trailing_ && trailing_ != widget) trailing_->removeEventFilter(this);
    trailing_ = widget;
    if (widget) { widget->setParent(this); widget->installEventFilter(this); widget->show(); }
    relayout(false);
}

void TabBar::setEndWidget(QWidget* widget)
{
    if (end_ && end_ != widget) end_->removeEventFilter(this);
    end_ = widget;
    if (widget) { widget->setParent(this); widget->installEventFilter(this); widget->show(); }
    relayout(false);
}

void TabBar::refreshTrailing()
{
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

bool TabBar::isCompactSticky(int index) const
{
    return index >= 0 && index < stickyCount_ && appearance_.stickySizing == StickyTabSizing::Compact;
}

bool TabBar::rowShape() const { return vertical(); }

TabLabelParts TabBar::labelParts(int index, bool sticky) const
{
    const auto& tab = tabs_.at(index);
    TabLabelParts parts;
    parts.number = appearance_.showNumbers ? fontMetrics().horizontalAdvance(QString::number(index + 1)) : 0;
    parts.icon = appearance_.showIcons && !tab.icon.isNull() ? metrics_.iconSize : 0;
    const int natural = std::max(0, tab.titleWidth);
    if (vertical()) parts.title = natural;
    else if (sticky && appearance_.stickySizing == StickyTabSizing::Shrink) parts.title = std::min(natural, appearance_.shrinkStickyTitleWidth);
    else parts.title = appearance_.clipTitles ? std::min(natural, appearance_.maximumTitleWidth) : natural;
    return parts;
}

int TabBar::tabExtent(int index, bool sticky) const
{
    if (index < 0 || index >= tabs_.size()) return 0;
    if (sticky && appearance_.stickySizing == StickyTabSizing::Compact) return compactStickyTabWidth;
    // A sticky tab's action always shows and keeps its room.
    return tabBarTabWidth(labelParts(index, sticky), sticky);
}

int TabBar::carriedExtent(const CarriedLabel& label, bool sticky) const
{
    if (label.folderHeader) return folderHeaderExtent({{}, label.title, {}, label.collapsed, 0, label.count});
    if (sticky && appearance_.stickySizing == StickyTabSizing::Compact) return compactStickyTabWidth;
    TabTitleLayout title;
    title.prepare(label.title, font(), layoutDirection(), true, this);
    TabLabelParts parts;
    parts.icon = appearance_.showIcons && label.icon ? metrics_.iconSize : 0;
    const int natural = title.naturalWidth();
    if (sticky && appearance_.stickySizing == StickyTabSizing::Shrink) parts.title = std::min(natural, appearance_.shrinkStickyTitleWidth);
    else parts.title = appearance_.clipTitles ? std::min(natural, appearance_.maximumTitleWidth) : natural;
    // The number it gets is unknown until it lands; one digit approximates it.
    parts.number = appearance_.showNumbers ? fontMetrics().horizontalAdvance(QStringLiteral("0")) : 0;
    return tabBarTabWidth(parts, sticky);
}

int TabBar::naturalTabWidth(int index) const { return index >= 0 && index < tabs_.size() ? tabBarTabWidth(labelParts(index, false)) : 0; }

void TabBar::updateTitleWidths()
{
    for (auto& tab : tabs_) {
        tab.titleLayout.prepare(tab.title, font(), layoutDirection(), true, this);
        tab.titleWidth = tab.titleLayout.naturalWidth();
    }
}

bool TabBar::collapsible() const
{
    return !vertical() && appearance_.multiline && appearance_.collapseRows != TabRowCollapse::Off;
}

bool TabBar::collapsedNow() const { return collapsible() && !expanded_ && !dragExpanded_; }

bool TabBar::overlayActive() const
{
    if (vertical()) return iconMode_ && peeking_;
    return collapsible() && (dragExpanded_ || (expanded_ && appearance_.collapseRows == TabRowCollapse::Overlay));
}

TabLayoutInput TabBar::layoutInput(QSize area, bool collapsed) const
{
    TabLayoutInput input;
    input.orientation = vertical() ? TabLayoutInput::Orientation::Vertical : TabLayoutInput::Orientation::Horizontal;
    input.width = area.width();
    // Horizontal rows touch, so colored tabs join across rows.
    input.rowHeight = metrics_.rowHeight; input.rowGap = vertical() ? metrics_.rowGap : 0;
    input.inset = metrics_.verticalInset; input.tile = metrics_.tile; input.minimumTileWidth = metrics_.tile.width();
    input.stickyCount = stickyCount_;
    input.extents.reserve(tabs_.size());
    for (int index = 0; index < tabs_.size(); ++index)
        input.extents.append(vertical() ? 0 : tabExtent(index, index < stickyCount_));
    if (vertical()) {
        input.height = area.height();
        input.stickyTiles = !iconMode_ && !peeking_ && appearance_.stickySizing == StickyTabSizing::Compact;
        input.stickyOffset = stickyOffset_; input.regularOffset = regularOffset_;
    } else {
        input.multiline = appearance_.multiline;
        input.separateStickyRow = appearance_.stickyTabsOnSeparateRow;
        input.freezeSticky = !input.multiline && !input.separateStickyRow && appearance_.stickySizing != StickyTabSizing::Normal;
        input.collapsed = collapsed;
        // Rows take at most 40 % of the group, an overlay half of it.
        const int group = available_.height();
        input.height = !input.multiline ? 0 : group <= 0 ? 0 : int(group * (overlayActive() ? 0.5 : 0.4));
        input.trailingExtent = trailing_ && !trailing_->isHidden() ? trailing_->sizeHint().width() : 0;
        int end = end_ && !end_->isHidden() ? end_->sizeHint().width() : 0;
        if (countButton_ && !countButton_->isHidden()) end += countButton_->width();
        input.endExtent = end;
        input.stickyOffset = stickyOffset_;
        input.regularOffset = input.multiline ? rowsOffset_ : regularOffset_;
        input.collapsedStickyRow = collapsedStickyRow_; input.collapsedRegularRow = collapsedRegularRow_;
    }
    input.current = current_;
    input.removed = dragged_;
    input.insertion = dropIndex_; input.insertionSticky = dropSticky_; input.block = dropBlock_;
    // Vertical headers are rows; their tabs sit indented under them.
    input.folderIndent = metrics_.folderIndent;
    for (const auto& folder : folders_)
        input.folders.append({folder.first, folder.count, folderHeaderExtent(folder), folder.collapsed});
    input.removedFolder = draggedFolder_;
    input.insertionFolder = dropFolder_;
    return input;
}

QPoint TabBar::tabOrigin() const
{
    if (vertical()) return {0, metrics_.outerMargin};
    // Below the content an overlay grows upward, its shadow on top.
    if (placement_ == TabBarPlacement::Bottom) return {0, overlayActive() ? metrics_.shadowBlur : 0};
    return {0, metrics_.outerMargin};
}

QSize TabBar::tabArea(QSize size) const
{
    // An overlay keeps room for its shadow below the rows.
    const int shadow = overlayActive() && !vertical() ? metrics_.shadowBlur : 0;
    return {size.width(), std::max(0, size.height() - metrics_.outerMargin - shadow)};
}

QSize TabBar::reservedSize(QSize available) const
{
    if (vertical()) {
        const int width = iconMode_ ? metrics_.verticalIconWidth : std::clamp(available.width(), minimumVerticalTabWidth, maximumVerticalTabWidth);
        // `available` is the column's width and the group's height.
        return {width, available.height()};
    }
    // The strip keeps the collapsed height while an overlay shows more.
    const bool collapsed = collapsible() && !(expanded_ && appearance_.collapseRows == TabRowCollapse::Push);
    auto input = layoutInput(QSize(available.width(), 0), collapsed);
    input.block.clear(); input.insertion = -1;
    const auto layout = layoutTabs(input);
    return {available.width(), std::max(metrics_.rowHeight, layout.size.height()) + metrics_.outerMargin};
}

QSize TabBar::displaySize(QSize available) const
{
    if (!overlayActive()) return reservedSize(available);
    if (vertical()) return {std::clamp(available.width(), minimumVerticalTabWidth, maximumVerticalTabWidth), available.height()};
    auto input = layoutInput(QSize(available.width(), 0), false);
    input.height = std::max(0, int(available.height() * 0.5));
    const auto layout = layoutTabs(input);
    return {available.width(), std::min(available.height(), layout.size.height() + metrics_.outerMargin + metrics_.shadowBlur)};
}

void TabBar::setAvailableSize(QSize available)
{
    if (available_ == available) return;
    available_ = available;
    relayout(false);
}

void TabBar::relayout(bool animate)
{
    // Where each tab is drawn now, to slide from.
    QHash<QUuid, QRect> before;
    const bool sliding = animate && animationsEnabled() && isVisible();
    if (sliding) for (int index = 0; index < tabs_.size() && index < layout_.tabs.size(); ++index)
        if (!layout_.tabs.at(index).isNull()) before.insert(tabs_.at(index).data.toUuid(), animatedRect(index));
    for (auto& tab : tabs_) if (tab.titleWidth < 0) {
        tab.titleLayout.prepare(tab.title, font(), layoutDirection(), true, this);
        tab.titleWidth = tab.titleLayout.naturalWidth();
    }
    const auto area = tabArea(size());
    // The count shows only while rows are collapsed away; measure without
    // it first, then make room for it.
    bool countShown = false;
    if (collapsible()) {
        auto probe = layoutInput(area, true); probe.block.clear(); probe.insertion = -1;
        if (countButton_) countButton_->hide();
        probe.endExtent = end_ && !end_->isHidden() ? end_->sizeHint().width() : 0;
        countShown = layoutTabs(probe).hidden > 0;
    }
    if (countButton_) countButton_->setVisible(countShown && !dragExpanded_);
    layout_ = layoutTabs(layoutInput(area, collapsedNow()));
    if (countButton_ && countButton_->isVisible()) {
        const bool collapsed = collapsedNow();
        countButton_->setText(collapsed ? QStringLiteral("+%1").arg(layout_.hidden) : QStringLiteral("−"));
        static_cast<CompactToolButton*>(countButton_)->setTargetSize(QSize(
            std::max(metrics_.countMinimumWidth, fontMetrics().horizontalAdvance(countButton_->text()) + 14), metrics_.rowHeight));
        countButton_->setToolTip(collapsed
            ? QCoreApplication::translate("WindowSystem", "%n more tab(s) in other rows; click to expand", nullptr, layout_.hidden)
            : QCoreApplication::translate("WindowSystem", "Collapse other rows"));
        countButton_->setAccessibleName(countButton_->toolTip());
    }
    // Clamped offsets are the offsets.
    for (const auto& lane : std::as_const(layout_.laneList)) {
        if (!vertical() && appearance_.multiline) { rowsOffset_ = lane.offset; continue; }
        (lane.sticky ? stickyOffset_ : regularOffset_) = lane.offset;
    }
    if (collapsedNow()) { collapsedStickyRow_ = layout_.stickyRow; collapsedRegularRow_ = layout_.regularRow; }
    relayoutTrailing();
    if (sliding) {
        // Restart from wherever each tab is drawn now.
        slides_.clear();
        for (int index = 0; index < tabs_.size(); ++index) {
            const auto id = tabs_.at(index).data.toUuid();
            const auto from = before.value(id);
            const auto to = layout_.tabs.at(index).translated(tabOrigin());
            if (!from.isNull() && !layout_.tabs.at(index).isNull() && from != to) slides_.insert(id, {from});
        }
        if (!slides_.isEmpty()) { slideClock_.start(); slideTimer_.start(); }
    }
    update();
    emit viewChanged();
}

void TabBar::relayoutTrailing()
{
    // A vertical strip has no trailing controls; its group shows a footer.
    if (vertical()) { if (countButton_) countButton_->hide(); return; }
    if (!layout_.trailing.isNull() && trailing_ && !trailing_->isHidden()) {
        const auto area = layout_.trailing.translated(tabOrigin());
        trailing_->setGeometry(QRect(area.topLeft(), QSize(trailing_->sizeHint().width(), metrics_.rowHeight)));
        trailing_->raise();
    }
    if (!layout_.end.isNull()) {
        // From the row's end leftward: the end widget, then the row count.
        auto area = layout_.end.translated(tabOrigin());
        if (end_ && !end_->isHidden()) {
            const int width = end_->sizeHint().width();
            end_->setGeometry(area.right() - width + 1, area.top(), width, metrics_.rowHeight);
            end_->raise();
            area.setRight(area.right() - width);
        }
        if (countButton_ && countButton_->isVisible()) {
            countButton_->move(area.right() - countButton_->width() + 1, area.top());
            countButton_->raise();
        }
    }
}

QRect TabBar::tabRect(int index) const
{
    if (index < 0 || index >= layout_.tabs.size()) return {};
    const auto visible = layout_.visibleRect(index);
    return visible.isEmpty() ? QRect{} : visible.translated(tabOrigin());
}

int TabBar::tabAt(QPoint local) const { return layout_.tabAt(local - tabOrigin()); }

TabShape TabBar::shapeOf(int index) const
{
    if (!vertical()) return horizontalShape(placement_);
    const bool tiles = !iconMode_ && !peeking_ && appearance_.stickySizing == StickyTabSizing::Compact;
    return index < stickyCount_ && tiles ? TabShape::Tile : TabShape::Row;
}

TabParts TabBar::partsOf(int index, const QRect& rect) const
{
    const bool sticky = index < stickyCount_;
    const bool iconOnly = (vertical() && iconMode_ && !peeking_) || (!vertical() && isCompactSticky(index));
    const bool action = !iconOnly && shapeOf(index) != TabShape::Tile;
    return painter_.parts(rect, shapeOf(index), labelParts(index, sticky), iconOnly, action, sticky, layoutDirection());
}

QRect TabBar::titleRect(int index) const
{
    const auto rect = tabRect(index);
    return rect.isEmpty() ? QRect{} : partsOf(index, layout_.tabs.at(index).translated(tabOrigin())).title.intersected(rect);
}

QRect TabBar::actionRect(int index) const
{
    const auto rect = tabRect(index);
    return rect.isEmpty() ? QRect{} : partsOf(index, layout_.tabs.at(index).translated(tabOrigin())).action.intersected(rect);
}

QRect TabBar::animatedRect(int index) const
{
    const auto target = layout_.tabs.at(index).translated(tabOrigin());
    const auto slide = slides_.constFind(tabs_.at(index).data.toUuid());
    if (slide == slides_.cend() || !slideClock_.isValid()) return target;
    const qreal progress = std::clamp(qreal(slideClock_.elapsed()) / slideDuration_, 0.0, 1.0);
    return lerp(slide->from, target, QEasingCurve(QEasingCurve::OutCubic).valueForProgress(progress));
}

int TabBar::scrollOffset() const
{
    if (!vertical() && appearance_.multiline) return 0;
    return regularOffset_;
}

void TabBar::setScrollOffset(int offset)
{
    if (!vertical() && appearance_.multiline) return;
    for (int lane = 0; lane < layout_.laneList.size(); ++lane)
        if (!layout_.laneList.at(lane).sticky) { setLaneOffset(lane, offset); return; }
}

int TabBar::maximumScrollOffset() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.maximumOffset();
    return 0;
}

QRect TabBar::scrollViewport() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.viewport.translated(tabOrigin());
    return {};
}

int TabBar::frozenExtent() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.frozen;
    return 0;
}

QRect TabBar::contentTabRect(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return {};
    const auto& lane = layout_.laneList.at(layout_.lanes.at(index));
    auto rect = layout_.tabs.at(index).translated(tabOrigin());
    // Frozen sticky tabs do not scroll; the others are shifted back.
    if (lane.frozen && index < stickyCount_) return rect.translated(lane.offset, 0);
    return lane.vertical ? rect.translated(0, lane.offset) : rect.translated(lane.offset, 0);
}

bool TabBar::scrollsHorizontally() const { return !vertical() && !appearance_.multiline; }

bool TabBar::isOverflowing() const
{
    for (const auto& lane : layout_.laneList) if (lane.maximumOffset() > 0) return true;
    return false;
}

int TabBar::laneAt(QPoint local) const
{
    const auto point = local - tabOrigin();
    int nearest = -1, distance = std::numeric_limits<int>::max();
    for (int lane = 0; lane < layout_.laneList.size(); ++lane) {
        const auto viewport = layout_.laneList.at(lane).viewport;
        if (viewport.contains(point)) return lane;
        const int gap = point.y() < viewport.top() ? viewport.top() - point.y() : point.y() > viewport.bottom() ? point.y() - viewport.bottom() : 0;
        if (gap < distance) { distance = gap; nearest = lane; }
    }
    return nearest;
}

void TabBar::setLaneOffset(int lane, int offset)
{
    if (lane < 0 || lane >= layout_.laneList.size()) return;
    const auto& info = layout_.laneList.at(lane);
    offset = std::clamp(offset, 0, info.maximumOffset());
    if (offset == info.offset) return;
    if (!vertical() && appearance_.multiline) rowsOffset_ = offset;
    else (info.sticky ? stickyOffset_ : regularOffset_) = offset;
    relayout(false);
}

bool TabBar::scrollAt(QPoint local, QPoint delta)
{
    if (collapsible() && appearance_.wheelExpandRows && (collapsedNow() || expanded_)) {
        const int amount = delta.y() ? delta.y() : delta.x();
        if (amount < 0 && collapsedNow()) { setExpanded(true, true); return true; }
        if (amount > 0 && expanded_ && layout_.laneList.value(0).offset == 0) { setExpanded(false); return true; }
    }
    const int lane = laneAt(local);
    if (lane < 0) return false;
    const auto& info = layout_.laneList.at(lane);
    const int amount = info.vertical ? (delta.y() ? delta.y() : delta.x()) : (delta.x() ? delta.x() : delta.y());
    const int before = info.offset;
    // A listener of the change may destroy this strip.
    const QPointer<TabBar> self(this);
    setLaneOffset(lane, before - amount);
    return !self || layout_.laneList.value(lane).offset != before;
}

void TabBar::ensureVisible(int index)
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return;
    const int laneIndex = layout_.lanes.at(index);
    const auto& lane = layout_.laneList.at(laneIndex);
    if (lane.frozen && index < stickyCount_) return;
    const auto rect = layout_.tabs.at(index);
    if (lane.vertical) {
        if (rect.top() < lane.viewport.top()) setLaneOffset(laneIndex, lane.offset - (lane.viewport.top() - rect.top()));
        else if (rect.bottom() > lane.viewport.bottom()) setLaneOffset(laneIndex, lane.offset + rect.bottom() - lane.viewport.bottom());
        return;
    }
    const int start = lane.viewport.left() + lane.frozen;
    if (rect.left() < start) setLaneOffset(laneIndex, lane.offset - (start - rect.left()));
    else if (rect.right() > lane.viewport.right())
        setLaneOffset(laneIndex, lane.offset + std::min(rect.right() - lane.viewport.right(), rect.left() - start));
}

void TabBar::setExpanded(bool expanded, bool byWheel)
{
    if (!collapsible()) expanded = false;
    expandedByWheel_ = expanded && byWheel;
    collapseTimer_.stop();
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    rowsOffset_ = 0;
    if (expanded && appearance_.collapseRows == TabRowCollapse::Overlay) qApp->installEventFilter(this);
    else qApp->removeEventFilter(this);
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setDragExpanded(bool expanded)
{
    if (!collapsible()) expanded = false;
    if (dragExpanded_ == expanded) return;
    dragExpanded_ = expanded;
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setDraggedTab(TabId tab)
{
    const int index = tab.isNull() ? -1 : indexOf(tab);
    if (index == dragged_) return;
    dragged_ = index;
    if (index < 0) liftedPointer_.reset();
    relayout(true);
}

void TabBar::setDraggedFolder(QUuid folder)
{
    int index = -1;
    for (int at = 0; at < folders_.size(); ++at) if (folders_.at(at).id == folder) index = at;
    if (index == draggedFolder_) return;
    draggedFolder_ = index;
    if (index < 0) liftedPointer_.reset();
    relayout(true);
}
void TabBar::setLifted(QPoint local, QPoint grab)
{
    if (local.isNull()) { if (!liftedPointer_) return; liftedPointer_.reset(); update(); return; }
    if (!liftedPointer_) {
        liftClock_.start();
        if (animationsEnabled()) for (int step = 1; step <= 5; ++step)
            QTimer::singleShot(step * 18, this, [self = QPointer<TabBar>(this)] { if (self) self->update(); });
    }
    liftedPointer_ = local; liftedGrab_ = grab;
    update();
}

QRect TabBar::liftedRect() const
{
    if (liftedPointer_ && draggedFolder_ >= 0 && draggedFolder_ < folders_.size()) {
        // A carried folder: its header and, open, its tabs, as one block.
        const auto& folder = folders_.at(draggedFolder_);
        const auto origin = tabOrigin();
        const int tabs = folder.collapsed ? 0 : folder.count;
        if (vertical()) {
            const int rows = 1 + tabs, height = rows * metrics_.rowHeight + (rows - 1) * metrics_.rowGap;
            const int y = std::clamp(liftedPointer_->y() - liftedGrab_.y(), origin.y(), std::max(origin.y(), this->height() - height));
            return {metrics_.verticalInset, y, std::max(0, width() - 2 * metrics_.verticalInset), height};
        }
        int extent = folderHeaderExtent(folder);
        for (int index = folder.first; index < folder.first + tabs && index < tabs_.size(); ++index) extent += tabExtent(index, false);
        int y = origin.y();
        if (!layout_.gap.isEmpty() && layout_.gapLane >= 0) y = layout_.laneList.at(layout_.gapLane).viewport.top() + origin.y();
        return {liftedPointer_->x() - liftedGrab_.x(), y, extent, metrics_.rowHeight};
    }
    if (!liftedPointer_ || dragged_ < 0 || dragged_ >= tabs_.size()) return {};
    const bool sticky = dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_;
    const auto origin = tabOrigin();
    if (vertical()) {
        const int rowWidth = std::max(0, width() - 2 * metrics_.verticalInset);
        const int y = std::clamp(liftedPointer_->y() - liftedGrab_.y(), origin.y(), std::max(origin.y(), height() - metrics_.rowHeight));
        return {metrics_.verticalInset, y, rowWidth, metrics_.rowHeight};
    }
    const int extent = sticky && appearance_.stickySizing == StickyTabSizing::Compact ? compactStickyTabWidth : tabExtent(dragged_, sticky);
    // A single row keeps the tab on its row; several rows follow the pointer
    // to the row it is over.
    int y = origin.y();
    if (appearance_.multiline) {
        const int pitch = metrics_.rowHeight + metrics_.rowGap;
        const int row = std::max(0, (liftedPointer_->y() - origin.y()) / pitch);
        y = origin.y() + std::min(row, std::max(0, (height() - origin.y()) / pitch - 1)) * pitch;
    } else if (layout_.laneList.size() > 1) {
        // A separate sticky row: the row under the pointer.
        y = layout_.laneList.at(std::max(0, laneAt(*liftedPointer_))).viewport.top() + origin.y();
    } else if (!layout_.gap.isEmpty() && layout_.gapLane >= 0) y = layout_.laneList.at(layout_.gapLane).viewport.top() + origin.y();
    return {liftedPointer_->x() - liftedGrab_.x(), y, extent, metrics_.rowHeight};
}

TabInsertion TabBar::insertionAt(QPoint local, int stickyReach, TabLayoutInput::Carried carried) const
{
    auto input = layoutInput(tabArea(size()), collapsedNow());
    input.carried = carried;
    return tabInsertionAt(input, local - tabOrigin(), stickyReach);
}

void TabBar::setDropPreview(int index, bool sticky, QVector<int> block, int folder)
{
    if (index < 0) { block.clear(); sticky = false; index = -1; folder = -1; }
    if (dropIndex_ == index && dropSticky_ == sticky && dropBlock_ == block && dropFolder_ == folder) return;
    dropIndex_ = index; dropSticky_ = sticky; dropBlock_ = std::move(block); dropFolder_ = folder;
    relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}

QRect TabBar::dropPreviewRect() const
{
    QRect result;
    for (const auto& cell : layout_.gap) if (!cell.isNull()) result |= cell.translated(tabOrigin());
    if (layout_.gapLane >= 0) result &= layout_.laneList.at(layout_.gapLane).viewport.translated(tabOrigin());
    return result;
}

QRect TabBar::scrollTrack(int laneIndex) const
{
    const auto& lane = layout_.laneList.at(laneIndex);
    const auto viewport = lane.viewport.translated(tabOrigin());
    const int thickness = metrics_.scrollbar;
    if (!lane.vertical)
        return QRect(viewport.left() + lane.frozen, viewport.bottom() - thickness + 1, viewport.width() - lane.frozen, thickness);
    // Vertical lanes put the bar on the content side.
    const bool left = placement_ == TabBarPlacement::Right;
    return QRect(left ? viewport.left() : viewport.right() - thickness + 1, viewport.top(), thickness, viewport.height());
}

QRect TabBar::scrollThumb(int laneIndex) const
{
    const auto& lane = layout_.laneList.at(laneIndex);
    if (lane.maximumOffset() <= 0 || lane.extent <= 0) return {};
    const auto track = scrollTrack(laneIndex);
    const int length = lane.vertical ? track.height() : track.width();
    const int visible = lane.vertical ? lane.viewport.height() : lane.viewport.width() - lane.frozen;
    const int thumb = std::max(16, int(qint64(length) * visible / std::max(1, lane.extent - (lane.vertical ? 0 : lane.frozen))));
    const int position = int(qint64(length - thumb) * lane.offset / std::max(1, lane.maximumOffset()));
    return lane.vertical ? QRect(track.left(), track.top() + position, track.width(), thumb)
                         : QRect(track.left() + position, track.top(), thumb, track.height());
}

void TabBar::paintTabAt(QPainter& painter, int index)
{
    const auto rect = animatedRect(index);
    if (rect.isEmpty()) return;
    TabPaintState state;
    state.shape = shapeOf(index);
    state.selected = index == current_;
    state.hovered = index == hovered_;
    state.pressed = index == pressed_;
    state.groupFocused = groupFocused_;
    state.sticky = index < stickyCount_;
    state.iconOnly = (vertical() && iconMode_ && !peeking_) || (!vertical() && isCompactSticky(index));
    state.outerRight = placement_ == TabBarPlacement::Right;
    state.preset = tabPreset(index);
    state.number = appearance_.showNumbers ? QString::number(index + 1) : QString{};
    state.icon = appearance_.showIcons ? tabs_.at(index).icon : QIcon{};
    state.title = tabs_.at(index).title;
    state.titleLayout = &tabs_[index].titleLayout;
    // A sticky tab always shows its action; the others on hover.
    state.actionShown = state.sticky || index == hovered_ || index == actionPressed_;
    state.actionHovered = index == hovered_ && actionHovered_;
    state.actionPressed = index == actionPressed_;
    state.actionReserved = state.sticky;
    state.actionOpacity = state.sticky || index != hovered_ ? 1.0 : hoverProgress();
    state.actionIcon = iconFor(!state.sticky ? QStringLiteral("close") : vertical() ? QStringLiteral("sticky-vertical") : QStringLiteral("sticky"), theme_);
    painter_.paintTab(painter, partsOf(index, rect), state);
}

TabFolderPaintState TabBar::folderPaintState(int folder) const
{
    const auto& record = folders_.at(folder);
    TabFolderPaintState state;
    state.name = record.name; state.count = record.count; state.collapsed = record.collapsed;
    state.hovered = folder == hoveredHeader_; state.pressed = folder == pressedHeader_;
    state.addHovered = state.hovered && headerAddHovered_;
    if (const auto* preset = folderPreset(folder)) {
        state.color = preset->groupActive.line;
        state.wash = preset->groupInactive.background; state.washText = preset->groupInactive.text;
    } else { state.color = painter_.palette().accent; state.wash = painter_.palette().hover; state.washText = painter_.palette().text; }
    state.addIcon = iconFor(QStringLiteral("add"), theme_);
    return state;
}
void TabBar::paintFolders(QPainter& painter, int lane)
{
    const auto origin = tabOrigin();
    for (int folder = 0; folder < folders_.size(); ++folder) {
        if (folder == draggedFolder_ || layout_.headerLanes.value(folder, -1) != lane) continue;
        painter_.paintFolderHeader(painter, layout_.headers.at(folder).translated(origin), vertical(), folderPaintState(folder));
    }
}
void TabBar::paintFolderLines(QPainter& painter, int lane)
{
    const auto origin = tabOrigin();
    const bool bottom = placement_ == TabBarPlacement::Bottom;
    for (int folder = 0; folder < folders_.size(); ++folder) {
        if (folder == draggedFolder_) continue;
        const auto state = folderPaintState(folder);
        const auto& record = folders_.at(folder);
        const bool headerHere = layout_.headerLanes.value(folder, -1) == lane;
        // The folder's shown tabs in this lane, row by row.
        QVector<QRect> rows;
        if (headerHere) rows.append(layout_.headers.at(folder));
        for (int index = record.first; index < record.first + record.count && index < tabs_.size(); ++index) {
            if (index == dragged_ || layout_.lanes.value(index, -1) != lane) continue;
            const auto rect = layout_.tabs.at(index);
            auto row = std::find_if(rows.begin(), rows.end(), [&](const QRect& r) { return vertical() || r.top() == rect.top(); });
            if (row == rows.end()) rows.append(rect); else *row |= rect;
        }
        // A collapsed folder showing only its header has no line.
        if (record.collapsed && rows.size() <= 1 && (rows.isEmpty() || (headerHere && rows.first() == layout_.headers.at(folder)))) continue;
        for (auto row : rows) {
            row.translate(origin);
            if (vertical()) {
                // A side line in the indent, along the folder's rows.
                auto span = row;
                if (headerHere) span.setTop(layout_.headers.at(folder).translated(origin).bottom() + 1 + metrics_.rowGap);
                if (span.height() <= 0 || record.collapsed) continue;
                const int x = layout_.headers.value(folder).isNull() ? span.left() : layout_.headers.at(folder).translated(origin).left() + metrics_.folderIndent / 2 - metrics_.folderLine / 2;
                painter_.paintFolderLine(painter, QRect(x, span.top(), metrics_.folderLine, span.height()), state.color);
            } else {
                // A line along the content edge, starting under the chip.
                const int start = headerHere && row.left() == layout_.headers.at(folder).translated(origin).left()
                    ? painter_.folderChipRect(layout_.headers.at(folder).translated(origin)).left() : row.left();
                const int y = bottom ? row.top() : row.bottom() + 1 - metrics_.folderLine;
                QRect line(start, y, row.right() + 1 - start, metrics_.folderLine);
                // The current tab opens into the content; its outline meets the line.
                const auto current = folderOf(current_) == folder && layout_.lanes.value(current_, -1) == lane
                    ? animatedRect(current_) : QRect{};
                if (!current.isEmpty() && current.intersects(line.adjusted(0, -1, 0, 1))) {
                    painter_.paintFolderLine(painter, QRect(line.left(), y, current.left() - line.left(), metrics_.folderLine), state.color);
                    painter_.paintFolderLine(painter, QRect(current.right() + 1, y, line.right() - current.right(), metrics_.folderLine), state.color);
                } else painter_.paintFolderLine(painter, line, state.color);
            }
        }
    }
}
void TabBar::paintFolderOutline(QPainter& painter, int lane)
{
    const int folder = folderOf(current_);
    if (folder < 0 || folder == draggedFolder_ || current_ == dragged_ || layout_.lanes.value(current_, -1) != lane) return;
    const auto state = folderPaintState(folder);
    painter_.paintFolderOutline(painter, animatedRect(current_), shapeOf(current_), state.color);
}
void TabBar::paintLaneDecorations(QPainter& painter)
{
    const auto origin = tabOrigin();
    for (int laneIndex = 0; laneIndex < layout_.laneList.size(); ++laneIndex) {
        const auto& lane = layout_.laneList.at(laneIndex);
        const auto viewport = lane.viewport.translated(origin);
        const int fade = metrics_.edgeFade;
        if (lane.offset > 0) {
            painter_.paintFade(painter, lane.vertical ? QRect(viewport.left(), viewport.top(), viewport.width(), fade)
                                                      : QRect(viewport.left() + lane.frozen, viewport.top(), fade, viewport.height()),
                               lane.vertical ? Qt::TopEdge : Qt::LeftEdge);
        }
        if (lane.offset < lane.maximumOffset()) {
            painter_.paintFade(painter, lane.vertical ? QRect(viewport.left(), viewport.bottom() - fade + 1, viewport.width(), fade)
                                                      : QRect(viewport.right() - fade + 1, viewport.top(), fade, viewport.height()),
                               lane.vertical ? Qt::BottomEdge : Qt::RightEdge);
        }
        const bool showBar = lane.maximumOffset() > 0 && (laneIndex == hoveredLane_ || laneIndex == scrollDragLane_);
        if (showBar) painter_.paintScrollbar(painter, scrollThumb(laneIndex), laneIndex == scrollDragLane_);
    }
    if (!layout_.separator.isNull()) painter_.paintSeparator(painter, layout_.separator.translated(origin));
}

void TabBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setFont(font());
    // An overlay's shadow band stays unfilled, so the content shows through.
    auto background = rect();
    if (overlayActive() && !vertical()) {
        if (placement_ == TabBarPlacement::Bottom) background.setTop(metrics_.shadowBlur);
        else background.setBottom(height() - metrics_.shadowBlur - 1);
    }
    painter.fillRect(background, painter_.palette().strip);
    const auto origin = tabOrigin();
    // Short lines between resting tabs of one row.
    for (int index = 0; index + 1 < tabs_.size(); ++index) {
        const int next = index + 1;
        if (vertical() || layout_.lanes.at(index) < 0 || layout_.lanes.at(index) != layout_.lanes.at(next)) continue;
        if (index == current_ || next == current_ || index == hovered_ || next == hovered_ || tabPreset(index) || tabPreset(next)) continue;
        if (slides_.contains(tabs_.at(index).data.toUuid()) || slides_.contains(tabs_.at(next).data.toUuid())) continue;
        const auto a = layout_.visibleRect(index), b = layout_.visibleRect(next);
        if (a.isEmpty() || b.isEmpty() || a.top() != b.top() || a.right() + 1 != b.left()) continue;
        const int height = metrics_.rowHeight / 2;
        painter_.paintSeparator(painter, QRect(b.left() + origin.x(), a.top() + origin.y() + (a.height() - height) / 2, metrics_.separator, height));
    }
    for (int laneIndex = 0; laneIndex < layout_.laneList.size(); ++laneIndex) {
        const auto& lane = layout_.laneList.at(laneIndex);
        painter.save();
        painter.setClipRect(lane.viewport.translated(origin));
        // Tabs scrolling under a frozen run first, then the run; the current
        // tab of each part over its neighbors.
        const auto paintRange = [&](int first, int last, bool clipFrozen) {
            painter.save();
            if (clipFrozen && lane.frozen)
                painter.setClipRect(lane.viewport.translated(origin).adjusted(lane.frozen, 0, 0, 0), Qt::IntersectClip);
            for (int index = first; index < last; ++index)
                if (index != current_ && layout_.lanes.at(index) == laneIndex) paintTabAt(painter, index);
            if (current_ >= first && current_ < last && layout_.lanes.value(current_) == laneIndex) paintTabAt(painter, current_);
            painter.restore();
        };
        // Folders live among the regular tabs, under a frozen run.
        painter.save();
        if (lane.frozen) painter.setClipRect(lane.viewport.translated(origin).adjusted(lane.frozen, 0, 0, 0), Qt::IntersectClip);
        paintFolders(painter, laneIndex);
        painter.restore();
        if (lane.frozen) { paintRange(stickyCount_, count(), true); paintRange(0, stickyCount_, false); }
        else paintRange(0, count(), false);
        painter.save();
        if (lane.frozen) painter.setClipRect(lane.viewport.translated(origin).adjusted(lane.frozen, 0, 0, 0), Qt::IntersectClip);
        paintFolderLines(painter, laneIndex);
        paintFolderOutline(painter, laneIndex);
        painter.restore();
        painter.restore();
    }
    paintLaneDecorations(painter);
    if (const auto lifted = liftedRect(); !lifted.isEmpty() && draggedFolder_ >= 0) {
        // The carried folder rides above everything: header, tabs, its line.
        const auto& folder = folders_.at(draggedFolder_);
        const auto state = folderPaintState(draggedFolder_);
        painter.save(); painter.setOpacity(liftProgress());
        painter_.paintShadow(painter, QRectF(lifted), metrics_.radius);
        painter.restore();
        painter.fillRect(lifted, painter_.palette().strip);
        const int pitch = metrics_.rowHeight + metrics_.rowGap;
        QRect header = vertical() ? QRect(lifted.left(), lifted.top(), lifted.width(), metrics_.rowHeight)
                                  : QRect(lifted.left(), lifted.top(), folderHeaderExtent(folder), metrics_.rowHeight);
        painter_.paintFolderHeader(painter, header, vertical(), state);
        int x = header.right() + 1, y = header.top() + pitch;
        const int tabs = folder.collapsed ? 0 : folder.count;
        for (int index = folder.first; index < folder.first + tabs && index < tabs_.size(); ++index) {
            const QRect rect = vertical() ? QRect(lifted.left() + metrics_.folderIndent, y, lifted.width() - metrics_.folderIndent, metrics_.rowHeight)
                                          : QRect(x, lifted.top(), tabExtent(index, false), metrics_.rowHeight);
            TabPaintState tab;
            tab.shape = shapeOf(index); tab.selected = index == current_; tab.groupFocused = true;
            tab.outerRight = placement_ == TabBarPlacement::Right; tab.preset = tabPreset(index);
            tab.icon = appearance_.showIcons ? tabs_.at(index).icon : QIcon{};
            tab.title = tabs_.at(index).title; tab.titleLayout = &tabs_[index].titleLayout;
            painter_.paintTab(painter, painter_.parts(rect, tab.shape, labelParts(index, false), false, false, false, layoutDirection()), tab);
            x = rect.right() + 1; y += pitch;
        }
        if (tabs && !vertical()) {
            const int line = placement_ == TabBarPlacement::Bottom ? lifted.top() : lifted.bottom() + 1 - metrics_.folderLine;
            const int start = painter_.folderChipRect(header).left();
            painter_.paintFolderLine(painter, QRect(start, line, lifted.right() + 1 - start, metrics_.folderLine), state.color);
        } else if (tabs) {
            const int x0 = lifted.left() + metrics_.folderIndent / 2 - metrics_.folderLine / 2;
            painter_.paintFolderLine(painter, QRect(x0, header.bottom() + 1 + metrics_.rowGap, metrics_.folderLine, lifted.bottom() - header.bottom() - metrics_.rowGap), state.color);
        }
    } else if (const auto lifted = liftedRect(); !lifted.isEmpty()) {
        // The dragged tab rides above everything, with its shadow.
        TabPaintState state;
        state.shape = shapeOf(dragged_);
        state.lifted = true; state.selected = true; state.groupFocused = true;
        state.shadowOpacity = liftProgress();
        state.sticky = dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_;
        state.outerRight = placement_ == TabBarPlacement::Right;
        state.preset = tabPreset(dragged_);
        state.number = appearance_.showNumbers ? QString::number(dragged_ + 1) : QString{};
        state.icon = appearance_.showIcons ? tabs_.at(dragged_).icon : QIcon{};
        state.title = tabs_.at(dragged_).title;
        state.titleLayout = &tabs_[dragged_].titleLayout;
        state.iconOnly = !vertical() && state.sticky && appearance_.stickySizing == StickyTabSizing::Compact;
        state.actionShown = state.sticky && !state.iconOnly; state.actionReserved = true;
        state.actionIcon = iconFor(vertical() ? QStringLiteral("sticky-vertical") : QStringLiteral("sticky"), theme_);
        const auto label = labelParts(dragged_, state.sticky);
        painter_.paintTab(painter, painter_.parts(lifted, state.shape, label, state.iconOnly, state.actionShown, state.sticky,
                                                  layoutDirection()), state);
    }
    if (overlayActive() && !vertical()) {
        // The overlay reads as the strip grown over the content.
        const bool above = placement_ == TabBarPlacement::Bottom;
        const QRect band(0, above ? 0 : height() - metrics_.shadowBlur, width(), metrics_.shadowBlur);
        QLinearGradient shade(above ? band.bottomLeft() : band.topLeft(), above ? band.topLeft() : band.bottomLeft());
        shade.setColorAt(0, QColor(0, 0, 0, 50)); shade.setColorAt(1, QColor(0, 0, 0, 0));
        painter.fillRect(band, shade);
    }
}

qreal TabBar::liftProgress() const
{
    if (!liftClock_.isValid() || !animationsEnabled()) return 1.0;
    return std::clamp(qreal(liftClock_.elapsed()) / 80.0, 0.0, 1.0);
}
qreal TabBar::hoverProgress() const
{
    if (!hoverClock_.isValid() || !animationsEnabled()) return 1.0;
    return std::clamp(qreal(hoverClock_.elapsed()) / 100.0, 0.0, 1.0);
}

void TabBar::updateHover(QPoint local)
{
    const int hovered = tabAt(local);
    if (hovered != hovered_ && hovered >= 0) {
        // A covering close button fades in over the title.
        hoverClock_.start();
        if (animationsEnabled()) for (int step = 1; step <= 6; ++step)
            QTimer::singleShot(step * 18, this, [self = QPointer<TabBar>(this)] { if (self) self->update(); });
    }
    const bool overAction = hovered >= 0 && actionRect(hovered).contains(local)
        && (hovered < stickyCount_ ? !isCompactSticky(hovered) : true);
    const int lane = layout_.laneList.isEmpty() || !rect().contains(local) ? -1 : laneAt(local);
    const int header = rect().contains(local) ? folderAt(local) : -1;
    const bool overAdd = header >= 0 && vertical() && painter_.folderAddRect(folderHeaderRect(header)).contains(local);
    if (hovered != hovered_ || overAction != actionHovered_ || lane != hoveredLane_ || header != hoveredHeader_ || overAdd != headerAddHovered_) {
        hovered_ = hovered; actionHovered_ = overAction; hoveredLane_ = lane;
        hoveredHeader_ = header; headerAddHovered_ = overAdd;
        update();
    }
}

void TabBar::mousePressEvent(QMouseEvent* event)
{
    const auto point = event->position().toPoint();
    if (event->button() == Qt::MiddleButton) { middlePressed_ = tabAt(point); event->accept(); return; }
    if (event->button() != Qt::LeftButton) { event->accept(); return; }
    // The scrollbar of a hovered lane.
    if (hoveredLane_ >= 0 && hoveredLane_ < layout_.laneList.size() && layout_.laneList.at(hoveredLane_).maximumOffset() > 0) {
        const auto track = scrollTrack(hoveredLane_);
        if (track.adjusted(0, -2, 0, 2).contains(point)) {
            const auto thumb = scrollThumb(hoveredLane_);
            const auto& lane = layout_.laneList.at(hoveredLane_);
            if (!thumb.contains(point)) {
                // A press on the track jumps there.
                const int length = lane.vertical ? track.height() - thumb.height() : track.width() - thumb.width();
                const int along = lane.vertical ? point.y() - track.top() - thumb.height() / 2 : point.x() - track.left() - thumb.width() / 2;
                setLaneOffset(hoveredLane_, int(qint64(std::clamp(along, 0, std::max(1, length))) * lane.maximumOffset() / std::max(1, length)));
            }
            scrollDragLane_ = hoveredLane_; scrollDragOrigin_ = layout_.laneList.at(hoveredLane_).offset; scrollDragStart_ = point;
            event->accept(); return;
        }
    }
    const int index = tabAt(point);
    pressPoint_ = point;
    if (const int header = index < 0 ? folderAt(point) : -1; header >= 0) {
        pressedHeader_ = header; update(); event->accept(); return;
    }
    if (index >= 0 && actionRect(index).contains(point) && !(isCompactSticky(index) && !vertical())) {
        actionPressed_ = index; update(); event->accept(); return;
    }
    pressed_ = index;
    if (index >= 0) {
        setCurrentIndex(index);
        if (appearance_.collapseRows == TabRowCollapse::Overlay && expanded_) setExpanded(false);
    }
    event->accept();
}

void TabBar::mouseMoveEvent(QMouseEvent* event)
{
    const auto point = event->position().toPoint();
    if (scrollDragLane_ >= 0 && scrollDragLane_ < layout_.laneList.size()) {
        const auto& lane = layout_.laneList.at(scrollDragLane_);
        const auto track = scrollTrack(scrollDragLane_);
        const auto thumb = scrollThumb(scrollDragLane_);
        const int length = std::max(1, lane.vertical ? track.height() - thumb.height() : track.width() - thumb.width());
        const int moved = lane.vertical ? point.y() - scrollDragStart_.y() : point.x() - scrollDragStart_.x();
        setLaneOffset(scrollDragLane_, scrollDragOrigin_ + int(qint64(moved) * lane.maximumOffset() / length));
        return;
    }
    updateHover(point);
    if (event->buttons().testFlag(Qt::LeftButton) && pressedHeader_ >= 0
        && (point - pressPoint_).manhattanLength() >= QApplication::startDragDistance()) {
        // Dragging a header carries the whole folder.
        const int folder = pressedHeader_; pressedHeader_ = -1; update();
        const auto callback = folderDrag;
        if (callback) callback(folder, event->globalPosition().toPoint());
        return;
    }
    if (event->buttons().testFlag(Qt::LeftButton) && pressed_ >= 0
        && (point - pressPoint_).manhattanLength() >= QApplication::startDragDistance()) {
        const int index = pressed_; pressed_ = -1;
        const auto callback = drag; // Detachment may rebuild and destroy this view.
        if (callback) callback(index, event->globalPosition().toPoint());
    }
}

void TabBar::mouseReleaseEvent(QMouseEvent* event)
{
    const auto point = event->position().toPoint();
    if (event->button() == Qt::MiddleButton) {
        const int index = middlePressed_; middlePressed_ = -1; event->accept();
        const bool kept = index < stickyCount_ && guardsMiddleClick(appearance_.preventStickyClose);
        if (index >= 0 && !kept && tabAt(point) == index) { const auto callback = close; if (callback) callback(index); }
        return;
    }
    if (scrollDragLane_ >= 0) { scrollDragLane_ = -1; update(); return; }
    pressed_ = -1;
    if (pressedHeader_ >= 0) {
        const int folder = pressedHeader_; pressedHeader_ = -1; update();
        if (event->button() == Qt::LeftButton && folderAt(point) == folder) {
            const bool add = vertical() && painter_.folderAddRect(folderHeaderRect(folder)).contains(point);
            const auto callback = add ? newTabInFolder : toggleFolder;
            if (callback) callback(folder);
        }
        event->accept(); return;
    }
    if (actionPressed_ >= 0) {
        const int index = actionPressed_; actionPressed_ = -1; update();
        if (event->button() == Qt::LeftButton && actionRect(index).contains(point)) {
            const auto callback = index < stickyCount_ ? unstick : close;
            if (callback) callback(index);
        }
    }
    event->accept();
}

void TabBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A double click on a tab is a second press; nothing else happens.
    mousePressEvent(event);
}

void TabBar::wheelEvent(QWheelEvent* event)
{
    const auto delta = event->pixelDelta().isNull() ? event->angleDelta() / 3 : event->pixelDelta();
    scrollAt(event->position().toPoint(), delta);
    event->accept();
}

void TabBar::enterEvent(QEnterEvent* event)
{
    collapseTimer_.stop();
    if (iconMode_ && !peeking_) peekTimer_.start();
    QWidget::enterEvent(event);
}

void TabBar::leaveEvent(QEvent* event)
{
    hovered_ = -1; actionHovered_ = false; hoveredLane_ = -1; hoveredHeader_ = -1; headerAddHovered_ = false;
    peekTimer_.stop();
    if (peeking_) { peeking_ = false; relayout(false); if (const auto callback = reservedSizeChanged) callback(); }
    if (expandedByWheel_) collapseTimer_.start();
    update();
    QWidget::leaveEvent(event);
}

void TabBar::contextMenuEvent(QContextMenuEvent* event)
{
    if (const int folder = tabAt(event->pos()) < 0 ? folderAt(event->pos()) : -1; folder >= 0) {
        const auto callback = folderMenu;
        if (callback) callback(folder, event->globalPos());
        return;
    }
    const auto callback = menu;
    if (callback) callback(tabAt(event->pos()), event->globalPos());
}

void TabBar::keyPressEvent(QKeyEvent* event)
{
    const bool backward = event->key() == (vertical() ? Qt::Key_Up : Qt::Key_Left);
    const bool forward = event->key() == (vertical() ? Qt::Key_Down : Qt::Key_Right);
    if ((backward || forward) && count()) {
        const int next = std::clamp(current_ + (forward ? 1 : -1), 0, count() - 1);
        setCurrentIndex(next); ensureVisible(next); event->accept(); return;
    }
    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        if (count()) { const int next = event->key() == Qt::Key_Home ? 0 : count() - 1; setCurrentIndex(next); ensureVisible(next); }
        event->accept(); return;
    }
    if (event->key() == Qt::Key_Escape && expanded_) { setExpanded(false); event->accept(); return; }
    QWidget::keyPressEvent(event);
}

void TabBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout(false);
}

void TabBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange) {
        metrics_ = TabMetrics::forTheme(theme_, font());
        painter_ = TabPainter(metrics_, TabPalette::forTheme(theme_));
        updateTitleWidths();
        relayout(false);
        if (const auto callback = reservedSizeChanged) callback();
    }
    QWidget::changeEvent(event);
}

bool TabBar::eventFilter(QObject* watched, QEvent* event)
{
    // The trailing controls change size when their contents change.
    if ((watched == trailing_ || watched == end_) && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::ShowToParent
                                 || event->type() == QEvent::HideToParent)) {
        QTimer::singleShot(0, this, [self = QPointer<TabBar>(this)] { if (self) self->refreshTrailing(); });
        return false;
    }
    // An expanded overlay closes on a press elsewhere or Escape.
    if (expanded_ && appearance_.collapseRows == TabRowCollapse::Overlay) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const QRect area(mapToGlobal(QPoint()), size());
            if (!area.contains(mouse->globalPosition().toPoint())) setExpanded(false);
        } else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setExpanded(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool TabBar::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = tabAt(help->pos());
        QString text; QRect area;
        if (index >= 0) {
            area = tabRect(index);
            const auto action = actionRect(index);
            if (!action.isEmpty() && action.contains(help->pos())) {
                area = action;
                text = index < stickyCount_ ? QCoreApplication::translate("WindowSystem", "Unstick tab")
                                            : QCoreApplication::translate("WindowSystem", "Close tab");
            } else {
                text = tabToolTip(index);
                // Leaving the title for its action dismisses the title's tip.
                if (!action.isEmpty()) {
                    if (help->pos().x() < action.left()) area.setRight(action.left() - 1);
                    else if (help->pos().x() > action.right()) area.setLeft(action.right() + 1);
                }
            }
        }
        if (const int folder = index < 0 ? folderAt(help->pos()) : -1; folder >= 0) {
            area = folderHeaderRect(folder);
            const auto& record = folders_.at(folder);
            if (vertical() && painter_.folderAddRect(area).contains(help->pos()))
                text = QCoreApplication::translate("WindowSystem", "New tab in this tab folder");
            else {
                const auto count = QCoreApplication::translate("WindowSystem", "%n tab(s)", nullptr, record.count);
                text = record.name.isEmpty() ? count : record.name + QStringLiteral(" · ") + count;
            }
        }
        if (!text.isEmpty()) QToolTip::showText(help->globalPos(), text, this, area);
        else { QToolTip::hideText(); event->ignore(); }
        return true;
    }
    return QWidget::event(event);
}

} // namespace window_system
