// SPDX-License-Identifier: MIT
#pragma once

#include "elevation.hpp"
#include "tab_label_geometry.hpp"
#include "tab_title_layout.hpp"
#include "window_state_color.hpp"
#include <window_system/tab_appearance.hpp>
#include <window_system/theme.hpp>

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QRect>
#include <QString>

class QPainter;

namespace window_system {

// Every dimension of a tab strip in one place (tab strip design 2.1). The
// layout, the painter and the widget take their numbers from here, so a
// change of sizes is one edit. Values are logical pixels.
struct TabMetrics {
    int rowHeight = 28;
    int rowGap = 2;
    // The strip's own background shows above (or below) horizontal tabs.
    int outerMargin = 3;
    int radius = 6;
    int controlRadius = 4;
    int accentLine = 2;
    int separator = 1;
    int edgeFade = 16;
    int titleFade = 12;
    int scrollbar = 5;
    int newTabWidth = 26;
    int typeWidth = 12;
    int listWidth = 24;
    int controlGap = 1;
    int countMinimumWidth = 26;
    int verticalInset = 4;
    int columnGap = 1;
    int columnOverlayOutset = 4;
    QSize tile{26, 26};
    int verticalAccent = 3;
    int verticalAccentLength = 16;
    int verticalIconWidth = 44;
    int resizeBand = 10;
    int iconCollapseTravel = 12;
    int shadowBlur = int(elevationProfile(ElevationLevel::RowsOverlay).key.blur);
    int iconSize = 16;
    // Tab group cards (window interaction design: cards and focus): the
    // space a group keeps around its card and the card's corner radius.
    int cardMargin = 2;
    int cardRadius = 8;
    // Tab folders (tab strip design 12): a horizontal header is a chip this
    // much shorter than a tab, padded inside and spaced outside; without a
    // name it is a dot. The folder's line, the active tab's outline in the
    // folder color, and the indent of vertical folder rows.
    int folderChipInset = 3;
    int folderChipPadding = 8;
    int folderChipMargin = 4;
    int folderDot = 12;
    int folderLine = 2;
    qreal folderOutline = 1.5;
    int folderIndent = 12;
    int folderButton = 20;

    // Rows grow with a large font instead of clipping their text.
    [[nodiscard]] static TabMetrics forTheme(const Theme& theme, const QFont& font);
};

// The colors a tab strip uses, taken from the theme. When the theme becomes
// a color registry (phase 2), only this mapping changes.
struct TabPalette {
    QColor strip;
    QColor active;
    QColor hover;
    QColor pressed;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor separator;
    QColor border;
    QColor shadow;
    ElevationStyle elevation;
    WindowStateColor tileBorder;
    WindowStateColor tileHover;
    WindowStateColor tilePressed;
    WindowStateColor sectionSeparator;
    [[nodiscard]] static TabPalette forTheme(const Theme& theme);
};

[[nodiscard]] ElevationStyle elevationStyleForTheme(const Theme& theme);

enum class TabShape {
    // A horizontal tab whose outer corners are rounded; `Top` means the strip
    // is above the content.
    Top,
    Bottom,
    // A row or icon tile of a vertical strip.
    Row,
    Tile,
};

// A tab folder header as painted (tab strip design 12).
struct TabFolderPaintState {
    QString name;
    int count = 0;
    bool collapsed = false;
    bool hovered = false;
    bool pressed = false;
    // Vertical: the hovered header's "new tab in folder" button.
    bool addHovered = false;
    // The folder color, its light wash and the text on each.
    QColor color;
    QColor wash;
    QColor washText;
    QIcon addIcon;
};

// The positions of a tab's slots. A slot that is not shown is null. Later
// features fill these slots (pane activity in the icon or action slot,
// inline rename over the title) without changing the layout.
struct TabParts {
    QRect tab;
    QRect number;
    QRect icon;
    QRect title;
    QRect action;
};

struct TabPaintState {
    TabShape shape = TabShape::Top;
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool groupFocused = false;
    bool windowActive = true;
    qreal titleOpacity = 1.0;
    bool sticky = false;
    bool compactSticky = false;
    // Only the icon, or the title's initial without one, centered.
    bool iconOnly = false;
    bool lifted = false;
    // These describe only this paint call. Retained label snapshots do not
    // keep pointers into the current layout or surface animation.
    const SurfaceGeometry* surface = nullptr;
    const ElevationProfile* bodyElevation = nullptr;
    bool elevationManaged = false;
    // 0..1 while a lifted tab's shadow fades in.
    qreal shadowOpacity = 1.0;
    // A row of a strip right of its content: the outer side is the right.
    bool outerRight = false;
    bool enabled = true;
    bool actionShown = false;
    bool actionHovered = false;
    bool actionPressed = false;
    // 0..1 while a covering action fades in or out.
    qreal actionOpacity = 1.0;
    // The action has room of its own and covers nothing.
    bool actionReserved = false;
    const TabColorPreset* preset = nullptr;
    QString number;
    QIcon icon;
    QString title;
    TabTitleLayout* titleLayout = nullptr;
    QIcon actionIcon;
};

class TabPainter final {
public:
    TabPainter(TabMetrics metrics, TabPalette palette) : metrics_(metrics), palette_(palette) {}
    [[nodiscard]] const TabMetrics& metrics() const { return metrics_; }
    [[nodiscard]] const TabPalette& palette() const { return palette_; }

    // The slots of a tab with these label parts.
    // `reserveAction` keeps room for an action that always shows; otherwise
    // the action covers the title's end while it shows.
    [[nodiscard]] TabParts parts(const QRect& tab, TabShape shape, TabLabelParts label, bool iconOnly,
                                 bool action, bool reserveAction, Qt::LayoutDirection direction) const;
    void paintTab(QPainter& painter, const TabParts& parts, const TabPaintState& state) const;
    [[nodiscard]] QPainterPath outline(const QRectF& tab, TabShape shape, bool selected, bool lifted = false) const;
    // The short line between two resting tabs.
    void paintSeparator(QPainter& painter, const QRect& line, bool windowActive = true, bool section = false) const;
    void paintScrollbar(QPainter& painter, const QRect& thumb, bool emphasized) const;
    // A soft shadow under a lifted tab or an overlay panel.
    void paintPanelShadow(QPainter& painter, const QRectF& area, qreal radius, bool column) const;
    // The background of a trailing control or chip in its state.
    void paintControl(QPainter& painter, const QRect& area, bool hovered, bool pressed) const;
    // Tab folders: a horizontal chip or a vertical header row; the line that
    // joins a folder's tabs; the outline of its active tab, open toward the
    // content so it meets the line.
    void paintFolderHeader(QPainter& painter, const QRect& area, bool vertical, const TabFolderPaintState& state) const;
    [[nodiscard]] QRect folderChipRect(const QRect& header) const;
    [[nodiscard]] QRect folderAddRect(const QRect& header) const;
    void paintFolderLine(QPainter& painter, const QRect& line, const QColor& color) const;
    void paintFolderOutline(QPainter& painter, const QRect& tab, TabShape shape, const QColor& color) const;
    // The text that reads on `background`: white or the palette's text.
    [[nodiscard]] QColor textOn(const QColor& background) const;

private:
    [[nodiscard]] QColor backgroundOf(const TabPaintState& state) const;
    [[nodiscard]] QColor textOf(const TabPaintState& state) const;
    [[nodiscard]] ElevationRenderer& elevationRenderer() const;
    TabMetrics metrics_;
    TabPalette palette_;
    mutable std::unique_ptr<ElevationRenderer> elevation_;
};

} // namespace window_system
