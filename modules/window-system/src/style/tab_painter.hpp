// SPDX-License-Identifier: MIT
#pragma once

#include "elevation.hpp"
#include "tab_label_presentation.hpp"
#include "tab_label_geometry.hpp"
#include "tab_title_layout.hpp"
#include "tab_visual_style.hpp"
#include <window_system/tab_appearance.hpp>
#include <window_system/theme.hpp>

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QRect>
#include <QString>

class QPainter;

namespace window_system {

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

struct TabPaintState : TabLabelState {
    // These describe only this paint call. Retained label snapshots do not
    // keep pointers into the current layout or surface animation.
    const SurfaceGeometry* surface = nullptr;
    const ElevationProfile* bodyElevation = nullptr;
    const TabColorPreset* preset = nullptr;
    TabTitleLayout* titleLayout = nullptr;
};

class TabPainter final {
public:
    TabPainter(TabMetrics metrics, TabPalette palette) : metrics_(metrics), palette_(palette) {}
    [[nodiscard]] const TabMetrics& metrics() const { return metrics_; }
    [[nodiscard]] const TabPalette& palette() const { return palette_; }

    void paintTab(QPainter& painter, const TabParts& parts, const TabPaintState& state) const;
    [[nodiscard]] static TabLabelPresentation captureLabel(const TabParts& parts, const TabPaintState& state,
                                                           const QFont& font, Qt::LayoutDirection direction);
    // The caller explicitly supplies a persistent, prepared cache or nullptr.
    // This adapter never prepares or replaces the caller's text cache.
    void paintLabel(QPainter& painter, const TabLabelPresentation& label, TabTitleLayout* widgetCache) const;
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
    [[nodiscard]] QColor backgroundOf(const TabLabelPresentation& label) const;
    [[nodiscard]] QColor textOf(const TabLabelPresentation& label) const;
    [[nodiscard]] ElevationRenderer& elevationRenderer() const;
    TabMetrics metrics_;
    TabPalette palette_;
    mutable std::unique_ptr<ElevationRenderer> elevation_;
};

} // namespace window_system
