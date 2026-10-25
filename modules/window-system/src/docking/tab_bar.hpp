// SPDX-License-Identifier: MIT
#pragma once

#include "tab_layout.hpp"
#include "style/tab_painter.hpp"
#include "style/tab_title_layout.hpp"
#include <window_system/layout_model.hpp>
#include <window_system/tab_appearance.hpp>
#include <window_system/theme.hpp>

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <optional>

class QToolButton;

namespace window_system {

// A tab being carried onto a strip, measured by the strip it would join; or
// the header of a carried tab folder (`title` its name).
struct CarriedLabel {
    QString title;
    bool icon = false;
    bool folderHeader = false;
    bool collapsed = false;
    int count = 0;
};

// A tab folder as a strip shows it: a run of `count` tabs from `first`
// (tab strip design 12).
struct TabStripFolder {
    QUuid id;
    QString name;
    QString colorId;
    bool collapsed = false;
    int first = 0;
    int count = 0;
    bool operator==(const TabStripFolder&) const = default;
};

// One tab strip: its tabs, their arrangement and input. It reads the pure
// layout (tab_layout.hpp) and paints through TabPainter; it never computes a
// position of its own (docs/design/tab-strip-design.md, ADR 0011).
class TabBar final : public QWidget {
    Q_OBJECT
public:
    explicit TabBar(const Theme& theme, QWidget* parent);
    ~TabBar() override;

    // Tabs. A tab's data is its TabId; hosts read it with toUuid().
    int addTab(const QIcon& icon, const QString& title);
    void removeTab(int index);
    [[nodiscard]] int count() const { return int(tabs_.size()); }
    [[nodiscard]] QVariant tabData(int index) const;
    void setTabData(int index, const QVariant& data);
    [[nodiscard]] QString tabText(int index) const;
    void setTabText(int index, const QString& text);
    [[nodiscard]] QIcon tabIcon(int index) const;
    void setTabIcon(int index, const QIcon& icon);
    [[nodiscard]] QString tabToolTip(int index) const;
    void setTabToolTip(int index, const QString& tip);
    void setTabMetadata(int index, const TabRecord& tab);
    [[nodiscard]] const TabColorPreset* tabPreset(int index) const;
    [[nodiscard]] int currentIndex() const { return current_; }
    void setCurrentIndex(int index);
    [[nodiscard]] QSize iconSize() const { return {metrics_.iconSize, metrics_.iconSize}; }
    [[nodiscard]] int indexOf(TabId tab) const;

    void setTheme(const Theme& theme);
    void setAppearance(const TabAppearance& appearance, TabBarPlacement placement);
    [[nodiscard]] TabBarPlacement placement() const { return placement_; }
    [[nodiscard]] bool vertical() const { return placement_ == TabBarPlacement::Left || placement_ == TabBarPlacement::Right; }
    [[nodiscard]] const TabAppearance& appearance() const { return appearance_; }
    [[nodiscard]] const TabMetrics& metrics() const { return metrics_; }
    void setStickyCount(int count);
    [[nodiscard]] int stickyCount() const { return stickyCount_; }
    // Tab folders, in strip order.
    void setFolders(QVector<TabStripFolder> folders);
    [[nodiscard]] const QVector<TabStripFolder>& folders() const { return folders_; }
    // The folder holding a tab, or -1.
    [[nodiscard]] int folderOf(int index) const;
    [[nodiscard]] QRect folderHeaderRect(int folder) const;
    [[nodiscard]] int folderAt(QPoint local) const;
    // The folder's color preset: its own, or one picked from its identity.
    [[nodiscard]] const TabColorPreset* folderPreset(int folder) const;
    void setGroupFocused(bool focused);
    // Vertical: only icons in a narrow column; hovering peeks at full width.
    void setIconMode(bool iconMode);
    [[nodiscard]] bool iconMode() const { return iconMode_; }
    // Horizontal: the controls after the last tab (new tab, list, sidebar
    // actions). The bar places it; hidden widgets take no space.
    void setTrailingWidget(QWidget* widget);
    // Horizontal: controls aligned to the end of the first row (the list and
    // sidebar actions); the row count joins them.
    void setEndWidget(QWidget* widget);
    void refreshTrailing();

    // Geometry. The group asks how much it must keep for the strip; an
    // expanded overlay or a peeking icon column is larger than that.
    // `available` is the group's size for a horizontal strip and (column
    // width, group height) for a vertical one.
    [[nodiscard]] QSize reservedSize(QSize available) const;
    // The size to show: larger than reserved while an overlay is open.
    [[nodiscard]] QSize displaySize(QSize available) const;
    void setAvailableSize(QSize available);
    [[nodiscard]] QSize availableSize() const { return available_; }
    [[nodiscard]] const TabLayout& tabLayout() const { return layout_; }
    // Where tab coordinates of the layout start inside the widget.
    [[nodiscard]] QPoint tabOrigin() const;
    // Displayed and visible: what a click on it hits.
    [[nodiscard]] QRect tabRect(int index) const;
    [[nodiscard]] QRect displayTabRect(int index) const { return tabRect(index); }
    [[nodiscard]] int tabAt(QPoint local) const;
    // The title slot, for an inline editor (8.5) and tests.
    [[nodiscard]] QRect titleRect(int index) const;
    [[nodiscard]] QRect actionRect(int index) const;
    // The lane of the regular tabs: its offset and viewport, for the rules
    // that keep shown tabs in place when the strip changes size.
    [[nodiscard]] int scrollOffset() const;
    void setScrollOffset(int offset);
    [[nodiscard]] int maximumScrollOffset() const;
    [[nodiscard]] QRect scrollViewport() const;
    // The start of the main lane covered by frozen sticky tabs.
    [[nodiscard]] int frozenExtent() const;
    // A tab's rectangle in its lane's content, independent of scrolling.
    [[nodiscard]] QRect contentTabRect(int index) const;
    [[nodiscard]] bool scrollsHorizontally() const;
    // Scrolls the lane under `local`; returns whether anything moved.
    bool scrollAt(QPoint local, QPoint delta);
    void ensureVisible(int index);
    [[nodiscard]] bool isOverflowing() const;

    // Collapsed rows (tab strip design 3.4).
    [[nodiscard]] bool collapsible() const;
    [[nodiscard]] bool expanded() const { return expanded_; }
    void setExpanded(bool expanded, bool byWheel = false);
    [[nodiscard]] int hiddenTabs() const { return layout_.hidden; }
    // Dragging over a collapsed strip shows all rows as an overlay.
    void setDragExpanded(bool expanded);
    [[nodiscard]] bool overlayActive() const;

    // Drag (tab strip design 6).
    void setDraggedTab(TabId tab);
    [[nodiscard]] int draggedIndex() const { return dragged_; }
    // The dragged tab drawn lifted under the pointer, `grab` being where in
    // the tab it was taken (tab strip design 6.1). A null point ends it.
    void setLifted(QPoint local, QPoint grab = {});
    // Where the lifted tab is drawn, in this widget.
    [[nodiscard]] QRect liftedRect() const;
    [[nodiscard]] TabInsertion insertionAt(QPoint local, int stickyReach = 0,
                                           TabLayoutInput::Carried carried = TabLayoutInput::Carried::Tab) const;
    // `folder` is the folder the block joins, -1 for none.
    void setDropPreview(int index, bool sticky, QVector<int> block, int folder = -1);
    [[nodiscard]] int dropIndex() const { return dropIndex_; }
    [[nodiscard]] bool dropSticky() const { return dropSticky_; }
    [[nodiscard]] int dropFolder() const { return dropFolder_; }
    // A whole folder being carried: its header and tabs leave the strip.
    void setDraggedFolder(QUuid folder);
    [[nodiscard]] int draggedFolder() const { return draggedFolder_; }
    // A folder header's width in this strip (tab strip design 12).
    [[nodiscard]] int folderHeaderExtent(const TabStripFolder& folder) const;
    [[nodiscard]] QRect dropPreviewRect() const;
    // A tab's width in the sticky or regular section of this strip.
    [[nodiscard]] int tabExtent(int index, bool sticky) const;
    // Its width ignoring sticky sizing.
    [[nodiscard]] int naturalTabWidth(int index) const;
    // The width a carried tab will have in this strip's sticky or regular
    // section (tab strip design 6.3).
    [[nodiscard]] int carriedExtent(const CarriedLabel& label, bool sticky) const;

    std::function<void(int, QPoint)> drag;
    std::function<void(int, QPoint)> menu;
    std::function<void(int)> close;
    std::function<void(int)> unstick;
    // Folder headers: a click toggles the folder, the context menu, a drag
    // carries the folder, and a vertical header's add button.
    std::function<void(int)> toggleFolder;
    std::function<void(int, QPoint)> folderMenu;
    std::function<void(int, QPoint)> folderDrag;
    std::function<void(int)> newTabInFolder;
    // The space the group keeps for the strip changed.
    std::function<void()> reservedSizeChanged;

    // Tests switch animations off; Windows' animation setting is honored.
    static void setAnimationsEnabled(bool enabled);
    [[nodiscard]] static bool animationsAllowed() { return animationsEnabled(); }

signals:
    void currentChanged(int index);
    // Offsets, expansion or layout changed: hover geometry is stale.
    void viewChanged();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Tab {
        QVariant data;
        QString title;
        QIcon icon;
        QString tooltip;
        QString colorId;
        TabTitleLayout titleLayout;
        int titleWidth = -1;
    };
    [[nodiscard]] TabLayoutInput layoutInput(QSize area, bool collapsed) const;
    [[nodiscard]] QSize tabArea(QSize size) const;
    [[nodiscard]] bool collapsedNow() const;
    void relayout(bool animate);
    void relayoutTrailing();
    [[nodiscard]] TabLabelParts labelParts(int index, bool sticky) const;
    [[nodiscard]] bool isCompactSticky(int index) const;
    [[nodiscard]] bool rowShape() const;
    [[nodiscard]] TabShape shapeOf(int index) const;
    [[nodiscard]] TabParts partsOf(int index, const QRect& rect) const;
    [[nodiscard]] QRect animatedRect(int index) const;
    void paintTabAt(QPainter& painter, int index);
    void paintLaneDecorations(QPainter& painter);
    // A lane's folder headers, under its tabs; the folder lines and the
    // folder outline of its current tab, over them.
    void paintFolders(QPainter& painter, int lane);
    void paintFolderLines(QPainter& painter, int lane);
    void paintFolderOutline(QPainter& painter, int lane);
    [[nodiscard]] TabFolderPaintState folderPaintState(int folder) const;
    [[nodiscard]] int laneAt(QPoint local) const;
    [[nodiscard]] QRect scrollThumb(int lane) const;
    [[nodiscard]] QRect scrollTrack(int lane) const;
    void setLaneOffset(int lane, int offset);
    [[nodiscard]] int laneOffsetInput(int lane) const;
    void startCollapseTimer();
    void updateHover(QPoint local);
    [[nodiscard]] qreal hoverProgress() const;
    [[nodiscard]] qreal liftProgress() const;
    void updateTitleWidths();
    static bool animationsEnabled();

    Theme theme_;
    TabMetrics metrics_;
    TabPainter painter_;
    TabAppearance appearance_;
    TabBarPlacement placement_ = TabBarPlacement::Top;
    QVector<Tab> tabs_;
    int current_ = -1;
    int stickyCount_ = 0;
    QVector<TabStripFolder> folders_;
    bool groupFocused_ = false;
    bool iconMode_ = false;
    bool peeking_ = false;
    QSize available_;
    TabLayout layout_;
    // Offsets of the sticky and regular lanes (horizontal single rows or
    // vertical sections) and of wrapped rows.
    int stickyOffset_ = 0;
    int regularOffset_ = 0;
    int rowsOffset_ = 0;
    int collapsedStickyRow_ = 0;
    int collapsedRegularRow_ = 0;
    bool expanded_ = false;
    bool expandedByWheel_ = false;
    bool dragExpanded_ = false;
    QTimer collapseTimer_;
    QTimer peekTimer_;
    // Drag.
    int dragged_ = -1;
    int dropIndex_ = -1;
    std::optional<QPoint> liftedPointer_;
    QPoint liftedGrab_;
    bool dropSticky_ = false;
    QVector<int> dropBlock_;
    int dropFolder_ = -1;
    int draggedFolder_ = -1;
    // Input.
    QPoint pressPoint_;
    int pressed_ = -1;
    int actionPressed_ = -1;
    int middlePressed_ = -1;
    int hovered_ = -1;
    bool actionHovered_ = false;
    int hoveredHeader_ = -1;
    bool headerAddHovered_ = false;
    int pressedHeader_ = -1;
    int hoveredLane_ = -1;
    int scrollDragLane_ = -1;
    int scrollDragOrigin_ = 0;
    QPoint scrollDragStart_;
    QPointer<QWidget> trailing_;
    QPointer<QWidget> end_;
    QToolButton* countButton_ = nullptr;
    // Slides: where each tab was drawn when its position last changed.
    struct Slide { QRect from; };
    QHash<QUuid, Slide> slides_;
    QHash<QUuid, QRect> shown_;
    QElapsedTimer slideClock_;
    QElapsedTimer hoverClock_;
    // Since the dragged tab was lifted: its shadow fades in (tab strip design 7).
    QElapsedTimer liftClock_;
    QTimer slideTimer_;
    int slideDuration_ = 150;
};

} // namespace window_system
