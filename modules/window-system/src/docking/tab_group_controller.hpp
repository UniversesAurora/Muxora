// SPDX-License-Identifier: MIT
#pragma once

#include "tab_strip_controller.hpp"
#include "tab_group_geometry.hpp"
#include "tab_group_motion.hpp"
#include "tab_viewport_state.hpp"
#include "drag_start.hpp"

#include <optional>

namespace window_system {

// What the view knows about the group. Coordinates of `originInWindow` and
// `windowClientBounds` belong to the containing top-level client.
struct TabGroupEnvironment {
    QSize size;
    QPoint originInWindow;
    QRect windowClientBounds;
    // The widest minimum any page of the group declares.
    int contentMinimumWidth = 0;
    bool visible = false;
    bool windowActive = false;
    bool animations = true;
};

// Where the view places its parts, all in group coordinates. A vertical
// strip's bar and footer are already offset by the column panel.
struct TabGroupPlacement {
    TabGroupGeometry geometry;
    QPoint barOrigin;
    bool vertical = false;
    bool iconMode = false;
    bool nameShown = false;
};

// One tab group's own state beside its strip: appearance and placement of
// the strip, the card and focus motion, the vertical column (width, icon
// mode, resizing), viewport anchoring across layout changes, and the
// strip-side half of dragging. Widgets and Quick views pass their
// environment and apply the placement; neither keeps a copy of these rules.
// Calls that change the strip must run between the strip view's prepare and
// synchronize steps.
class TabGroupController final {
public:
    TabGroupController(const Theme& theme, TabStripController& strip);

    struct Changes {
        bool layout = false;
        bool surface = false;
        // A motion (re)started: drive frames until needsFrame() is false.
        bool motion = false;
        // Run applyViewportReflow() once the view's layout pass is over.
        bool reflow = false;
        // Run finishScrollOffsetRestore() once the view's layout pass is over.
        bool scrollRestore = false;
        // The width or icon mode the user chose for the column, to save.
        std::optional<std::pair<int, bool>> columnState;
    };
    [[nodiscard]] Changes takeChanges() { return std::exchange(changes_, {}); }

    void setEnvironment(const TabGroupEnvironment& environment);
    [[nodiscard]] const TabGroupEnvironment& environment() const { return environment_; }
    void setTheme(const Theme& theme);
    void setTimestampNs(qint64 timestampNs) { timestampNs_ = timestampNs; }
    void advance(qint64 timestampNs);
    [[nodiscard]] bool needsFrame() const { return motion_.needsFrame(); }
    [[nodiscard]] bool layoutRunning() const { return motion_.layoutRunning(); }
    // How far an icon column's hover overlay is shown, 0 to 1.
    [[nodiscard]] qreal columnPeek() const { return motion_.peek(); }
    [[nodiscard]] int motionDurationMs() const { return motion_.durationMs(); }

    // Configuration.
    void setAppearance(const TabAppearance& appearance, bool pinned);
    [[nodiscard]] const TabAppearance& appearance() const { return appearance_; }
    [[nodiscard]] bool pinned() const { return pinned_; }
    void setGroupName(const QString& name);
    [[nodiscard]] const QString& groupName() const { return groupName_; }
    void setVerticalState(int width, bool iconMode);
    void toggleIconMode();
    [[nodiscard]] bool iconMode() const { return iconMode_; }
    void setTabBarVisible(bool visible);
    [[nodiscard]] bool tabBarVisible() const { return barVisible_; }
    void setContentEdgeToEdge(bool enabled);
    [[nodiscard]] bool contentEdgeToEdge() const { return contentEdgeToEdge_; }
    [[nodiscard]] bool compactPresentation() const { return compactPresentation_; }
    [[nodiscard]] bool vertical() const { return strip_.vertical(); }

    // Layout. place() also gives the strip its available size and reveal.
    [[nodiscard]] TabGroupPlacement place();
    // After the view applied the placement and the strip knows its new place.
    void placed();
    [[nodiscard]] const TabGroupPlacement& placement() const { return placement_; }
    [[nodiscard]] int columnWidth() const;
    // The column's width at its current reveal, for the footer's controls.
    [[nodiscard]] int shownColumnWidth() const;
    [[nodiscard]] QSize minimumSize(QSize contentMinimum) const;
    [[nodiscard]] QSize preferredSize(QSize contentPreferred) const;

    // Surface and focus.
    void setGroupFocused(bool focused);
    struct SurfaceState { qreal focus; bool focused; TabStripController::SurfaceState tabs; };
    [[nodiscard]] SurfaceState captureSurfaceState() const;
    void inheritSurface(const SurfaceState& previous, bool animate);
    // The card body seen from the strip, after a placement or motion change.
    void updateCardJoin();
    // With `extension`, also the strip's surface extension, for painting.
    [[nodiscard]] TabGroupSurfaceFrame surfaceFrame(bool extension = false) const;
    void restoreColumnPeek();

    // Viewport anchoring (tab strip design 3.5).
    [[nodiscard]] TabViewportState captureViewportState() const;
    void restoreViewportState(TabViewportState state);
    void restoreScrollOffset(int offset);
    // Called by the view after the deferred pass restoreScrollOffset() asks for.
    void finishScrollOffsetRestore();
    [[nodiscard]] int scrollOffset() const { return pendingScrollOffset_.value_or(strip_.scrollOffset()); }
    void queueViewportReflow();
    void cancelViewportReflow();
    void applyViewportReflow();
    // The strip's offsets or layout changed outside a reflow.
    void viewChanged();
    void setCurrentIndex(int index, bool reveal);

    // The vertical column's resize gesture, in global x.
    void beginColumnResize(int globalX);
    [[nodiscard]] bool columnResizing() const { return resizeOrigin_.has_value(); }
    void moveColumnResize(int globalX);
    void endColumnResize();
    void resetColumnWidth();

    // Dragging, in global coordinates.
    void setDragPresentation(bool compact, bool edge);
    bool setDragTabStyle(const TabStripController& target, bool sticky);
    void setLiftedTab(QPoint global, const DragAnchor& grab);
    void clearLiftedTab();
    [[nodiscard]] int insertionIndex(QPoint global, TabLayoutInput::Carried carried);
    void setInsertionPreview(int index, const QVector<CarriedLabel>& carried, bool carriedSticky, TabLayoutInput::Carried kind);
    [[nodiscard]] DragAnchor folderGrabAnchor(QUuid folder, QPoint global) const;
    // Updates the drag probe; returns the direction to scroll the strip by
    // while the pointer rests near a lane's end, null for none.
    QPoint setDragHover(QPoint global);
    void clearDragHover(bool overColumnPanel);
    // One auto-scroll step; false when the strip can move no further.
    bool autoScrollStep(QPoint direction);
    [[nodiscard]] QRect stripRectGlobal() const;

private:
    [[nodiscard]] TabGroupGeometryInput geometryInput() const;
    [[nodiscard]] int maximumColumnWidth() const;
    [[nodiscard]] int stickyInset(int index) const;
    [[nodiscard]] TabViewportState currentViewportState() const;
    bool applyPendingViewportState();
    [[nodiscard]] QPoint toStrip(QPoint global) const { return global - strip_.environment().globalOrigin; }
    void syncMotion(bool restarted);

    Theme theme_;
    TabStripController& strip_;
    TabGroupEnvironment environment_;
    TabGroupMotion motion_;
    TabGroupPlacement placement_;
    Changes changes_;
    qint64 timestampNs_ = 0;
    TabAppearance appearance_;
    bool pinned_ = false;
    QString groupName_;
    bool barVisible_ = true;
    bool compactPresentation_ = false;
    bool contentEdgeToEdge_ = false;
    int verticalWidth_ = 0;
    bool iconMode_ = false;
    std::optional<int> resizeOrigin_;
    int resizeStartWidth_ = 0;
    struct DragStyle {
        TabAppearance appearance;
        TabBarPlacement placement;
        int width;
        bool iconMode;
        int stickyCount;
    };
    std::optional<DragStyle> normalDragStyle_;
    QSize dragTabArea_;
    bool lifting_ = false;
    QPoint liftedGlobal_;
    DragAnchor liftedGrab_;
    QPoint dragProbe_;
    std::optional<int> pendingScrollOffset_;
    std::optional<TabViewportState> settledViewport_;
    std::optional<TabViewportState> pendingViewportState_;
    bool applyingViewportState_ = false;
};

} // namespace window_system
