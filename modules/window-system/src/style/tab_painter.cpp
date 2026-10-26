// SPDX-License-Identifier: MIT
#include "tab_painter.hpp"
#include <window_system/interface_appearance.hpp>

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace window_system {
namespace {

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(color.alphaF() * alpha);
    return color;
}

// A rectangle with its two outer corners rounded. Selected tabs also flare
// into the content with small concave corners, so tab and content read as
// one surface.
QPainterPath tabPath(const QRectF& rect, qreal radius, bool top, qreal flare)
{
    QPainterPath path;
    const qreal left = rect.left(), right = rect.right(), outer = top ? rect.top() : rect.bottom();
    const qreal inner = top ? rect.bottom() : rect.top();
    const qreal toward = top ? 1 : -1;
    path.moveTo(left - flare, inner);
    if (flare > 0) path.quadTo(left, inner, left, inner - toward * flare);
    path.lineTo(left, outer + toward * radius);
    path.quadTo(left, outer, left + radius, outer);
    path.lineTo(right - radius, outer);
    path.quadTo(right, outer, right, outer + toward * radius);
    path.lineTo(right, inner - toward * flare);
    if (flare > 0) path.quadTo(right, inner, right + flare, inner);
    path.closeSubpath();
    return path;
}

void paintIcon(QPainter& painter, const QIcon& icon, const QRect& area, const QColor& tint, bool enabled, bool selected)
{
    if (icon.isNull() || area.isEmpty()) return;
    if (tint.isValid()) {
        const qreal ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
        auto pixmap = icon.pixmap(area.size(), ratio);
        QPainter tinted(&pixmap); tinted.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tinted.fillRect(pixmap.rect(), tint); tinted.end();
        painter.drawPixmap(area, pixmap);
    } else icon.paint(&painter, area, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled, selected ? QIcon::On : QIcon::Off);
}

} // namespace

TabMetrics TabMetrics::forTheme(const Theme& theme, const QFont& font)
{
    TabMetrics metrics;
    for (auto* value : {&metrics.rowHeight, &metrics.rowGap, &metrics.outerMargin, &metrics.radius,
         &metrics.controlRadius, &metrics.accentLine, &metrics.separator, &metrics.edgeFade,
         &metrics.titleFade, &metrics.scrollbar, &metrics.newTabWidth, &metrics.typeWidth,
         &metrics.listWidth, &metrics.controlGap, &metrics.countMinimumWidth, &metrics.verticalInset,
         &metrics.columnGap, &metrics.columnOverlayOutset, &metrics.verticalAccent,
         &metrics.verticalAccentLength, &metrics.verticalIconWidth, &metrics.resizeBand,
         &metrics.iconCollapseTravel, &metrics.cardMargin, &metrics.cardRadius,
         &metrics.folderChipInset, &metrics.folderChipPadding, &metrics.folderChipMargin,
         &metrics.folderDot, &metrics.folderLine, &metrics.folderIndent, &metrics.folderButton})
        *value = uiPixels(*value);
    metrics.folderOutline = uiPixelsF(metrics.folderOutline);
    metrics.iconSize = theme.metrics.iconSize;
    metrics.tile = QSize(std::max(uiPixels(26), metrics.iconSize + uiPixels(10)), std::max(uiPixels(26), metrics.iconSize + uiPixels(10)));
    metrics.rowHeight = std::max(metrics.rowHeight, QFontMetrics(font).height() + uiPixels(10));
    metrics.rowHeight = std::max(metrics.rowHeight, metrics.iconSize + uiPixels(10));
    return metrics;
}

ElevationStyle elevationStyleForTheme(const Theme& theme)
{
    ElevationStyle style;
    style.shadow = theme.widgetShadow;
    style.edge = theme.surfaceEdge;
    style.shadowMultiplier = theme.surfaceShadowMultiplier;
    const bool dark = theme.surface.lightnessF() < 0.5;
    const Theme defaults;
    // A host may replace either neutral token independently. The initial
    // light-theme defaults follow the surface when it becomes dark.
    if (dark && style.edge == defaults.surfaceEdge) style.edge = QColor::fromRgbF(1, 1, 1, 0.07f);
    if (dark && style.shadowMultiplier == defaults.surfaceShadowMultiplier) style.shadowMultiplier = 2;
    style.highContrast = theme.highContrast;
    style.contrastBorder = theme.border;
    style.focusBorder = theme.focus;
    return style;
}

TabPalette TabPalette::forTheme(const Theme& theme)
{
    TabPalette palette;
    palette.strip = theme.headerBackground;
    palette.active = theme.surface;
    // A wash of the text color reads on light and dark strips alike.
    palette.hover = withAlpha(theme.text, 0.08);
    palette.pressed = withAlpha(theme.text, 0.14);
    palette.text = theme.text;
    palette.mutedText = theme.mutedText;
    palette.accent = theme.accent;
    palette.separator = withAlpha(theme.border, 0.8);
    palette.border = theme.border;
    palette.elevation = elevationStyleForTheme(theme);
    palette.shadow = palette.elevation.shadow;
    palette.tileBorder = WindowStateColor::neutralInactive(withAlpha(theme.accent, 0.35));
    palette.tileHover = WindowStateColor::neutralInactive(withAlpha(theme.accent, 0.12));
    palette.tilePressed = WindowStateColor::neutralInactive(withAlpha(theme.accent, 0.20));
    palette.sectionSeparator = palette.tileBorder;
    return palette;
}

TabParts TabPainter::parts(const QRect& tab, TabShape shape, TabLabelParts label, bool iconOnly, bool action,
                           bool reserveAction, Qt::LayoutDirection direction) const
{
    TabParts result;
    result.tab = tab;
    if (iconOnly || shape == TabShape::Tile) {
        const QRect icon(QPoint(), QSize(label.icon, label.icon));
        result.icon = label.icon ? icon.translated(tab.center() - icon.center()) : QRect{};
        result.title = label.icon ? QRect{} : tab;
        return result;
    }
    // Slots run along the text direction and sit centered in the tab.
    const QRect content = tab;
    if (action) result.action = tabBarCloseRect(content, direction);
    const auto area = tabBarLabelArea(content, direction, action && reserveAction);
    auto rects = layoutTabLabel(area, label, direction, uiPixels(tabPartGap));
    result.number = rects.number; result.icon = rects.icon; result.title = rects.title;
    // A title slot keeps the label area's full extent when it is the last
    // part, so the fade lands at the label's real end.
    return result;
}

QColor TabPainter::backgroundOf(const TabPaintState& state) const
{
    if (state.surface && state.surface->joined && (state.selected || state.lifted)) return palette_.active;
    if (state.preset) return (state.selected || state.lifted ? state.preset->groupActive : state.preset->groupInactive).background;
    if (state.shape == TabShape::Tile) {
        if (state.selected || state.lifted) return palette_.active;
        if (state.pressed) return palette_.tilePressed.resolve(state.windowActive);
        if (state.hovered) return palette_.tileHover.resolve(state.windowActive);
        return {};
    }
    if (state.lifted || state.selected) return palette_.active;
    if (state.pressed) return palette_.pressed;
    if (state.hovered) return palette_.hover;
    return {};
}

QColor TabPainter::textOf(const TabPaintState& state) const
{
    if (state.preset) return (state.selected ? state.preset->groupActive : state.preset->groupInactive).text;
    return state.selected || state.hovered || state.lifted ? palette_.text : palette_.mutedText;
}

QPainterPath TabPainter::outline(const QRectF& tab, TabShape shape, bool selected, bool lifted) const
{
    if (shape == TabShape::Top || shape == TabShape::Bottom)
        return tabPath(tab, metrics_.radius, shape == TabShape::Top, selected && !lifted ? metrics_.controlRadius : 0);
    QPainterPath path;
    const qreal radius = shape == TabShape::Tile
        ? std::min({qreal(metrics_.radius), tab.width() / 2, tab.height() / 2}) : metrics_.radius;
    path.addRoundedRect(tab, radius, radius);
    return path;
}

void TabPainter::paintTab(QPainter& painter, const TabParts& parts, const TabPaintState& state) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF tab(parts.tab);
    const auto background = backgroundOf(state);
    const auto contour = state.surface ? state.surface->anchorPath : outline(tab, state.shape, state.selected, state.lifted);
    if (!state.elevationManaged && (state.lifted || state.selected && state.shape != TabShape::Tile)) {
        painter.save();
        const bool focused = state.groupFocused && state.windowActive;
        auto style = palette_.elevation; style.focused = focused;
        const auto resting = elevationProfile(focused ? ElevationLevel::AnchorRaised : ElevationLevel::AnchorRest);
        const auto elevation = state.lifted ? interpolateElevation(resting, elevationProfile(ElevationLevel::Lifted), state.shadowOpacity) : resting;
        const auto body = state.bodyElevation ? *state.bodyElevation
            : elevationProfile(focused ? ElevationLevel::BodyRaised : ElevationLevel::BodyRest);
        SurfaceGeometry detached;
        if (!state.surface) {
            detached.anchor = tab;
            detached.anchorPath = contour;
            detached.mergedPath = contour;
            detached.compact = state.shape == TabShape::Tile;
        }
        elevationRenderer().paintAnchor(painter, state.surface ? *state.surface : detached,
            state.surface ? body : elevationProfile(ElevationLevel::Flat), elevation, style, state.surface && state.surface->joined);
        painter.restore();
    }
    if (background.isValid()) {
        painter.setPen(Qt::NoPen); painter.setBrush(background);
        painter.drawPath(contour);
    }
    if (state.surface && (state.selected || state.lifted))
        elevationRenderer().paintCoverage(painter, *state.surface, palette_.elevation);
    if (state.shape == TabShape::Tile && !state.lifted) {
        // Resting tiles have one outline and no elevation. Hover adds a
        // wash inside the same contour instead of a second frame.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette_.tileBorder.resolve(state.windowActive), 1));
        // The stroke's outer edge shares the fill's circle, including its
        // center. Insetting the rectangle also insets the centerline radius.
        const qreal radius = std::max(qreal(0), std::min({qreal(metrics_.radius), tab.width() / 2, tab.height() / 2}) - 0.5);
        painter.drawRoundedRect(tab.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter.setPen(Qt::NoPen);
    }
    // Text and the covering action fade belong to the same rounded surface.
    // In particular, the rectangular fade must not square off either end.
    painter.setClipPath(contour, Qt::IntersectClip);
    // A tab's color is its background alone; lines belong to tab folders.
    const auto* colors = state.preset ? (state.selected ? &state.preset->groupActive : &state.preset->groupInactive) : nullptr;
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor text = textOf(state);
    if (!parts.number.isEmpty()) {
        painter.save(); painter.setOpacity(painter.opacity() * state.titleOpacity);
        painter.setPen(text);
        // Digits sit on the baseline up to the cap height; center that span.
        const QFontMetricsF metrics(painter.font());
        painter.drawText(QPointF(parts.number.left(), parts.number.top() + (parts.number.height() + metrics.capHeight()) / 2.0),
                         state.number);
        painter.restore();
    }
    paintIcon(painter, state.icon, parts.icon, colors ? colors->icon : QColor{}, state.enabled,
        state.selected && (state.shape != TabShape::Tile || state.windowActive));
    if (!parts.title.isEmpty()) {
        painter.save(); painter.setOpacity(painter.opacity() * state.titleOpacity);
        if (state.iconOnly || state.shape == TabShape::Tile) {
            painter.setPen(text);
            painter.drawText(parts.title, Qt::AlignCenter | Qt::TextSingleLine, state.title.left(1).toUpper());
        } else if (state.titleLayout) {
            const bool cut = state.titleLayout->naturalWidth() > parts.title.width();
            const int fadeLength = state.shape == TabShape::Row
                ? std::min(metrics_.titleFade, std::max(0, parts.title.width() - (metrics_.rowHeight - uiPixels(10)))) : metrics_.titleFade;
            const bool covering = state.actionShown && !state.actionReserved && !parts.action.isEmpty()
                && state.actionOpacity > 0;
            if ((cut && fadeLength > 0) || covering) {
                const qreal left = parts.title.left(), end = parts.title.right() + 1;
                const bool reverse = parts.action.left() < parts.tab.center().x();
                const qreal actionEnd = reverse ? parts.action.right() + 1 : parts.action.left();
                const qreal actionFade = std::max(1, metrics_.edgeFade);
                const auto opacityAt = [&](qreal x) {
                    qreal opacity = cut && fadeLength > 0 ? std::clamp((end - x) / fadeLength, 0.0, 1.0) : 1.0;
                    if (covering) {
                        const qreal cover = reverse ? (actionEnd + actionFade - x) / actionFade
                            : (x - actionEnd + actionFade) / actionFade;
                        opacity *= 1 - std::clamp(state.actionOpacity, 0.0, 1.0) * std::clamp(cover, 0.0, 1.0);
                    }
                    return opacity;
                };
                QVector<qreal> stops{left, end};
                if (cut) for (int step = 0; step <= fadeLength; ++step)
                    stops.append(std::clamp(end - step, left, end));
                if (covering) for (int step = 0; step <= metrics_.edgeFade; ++step)
                    stops.append(std::clamp(actionEnd + (reverse ? step : -step), left, end));
                std::sort(stops.begin(), stops.end());
                stops.erase(std::unique(stops.begin(), stops.end()), stops.end());
                QLinearGradient fade(left, 0, end, 0);
                for (const auto x : stops) fade.setColorAt((x - left) / (end - left), withAlpha(text, opacityAt(x)));
                painter.setPen(QPen(QBrush(fade), 1));
            } else painter.setPen(text);
            state.titleLayout->draw(painter, parts.title);
        }
        painter.restore();
    }
    if (!parts.action.isEmpty() && state.actionShown) {
        painter.save();
        painter.setOpacity(painter.opacity() * std::clamp(state.actionOpacity, 0.0, 1.0));

        if (state.actionHovered || state.actionPressed) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(text, state.actionPressed && state.actionHovered ? 0.24 : 0.14));
            painter.drawRoundedRect(QRectF(parts.action), metrics_.controlRadius, metrics_.controlRadius);
        }
        const QRect glyph(QPoint(), QSize(uiPixels(16), uiPixels(16)));
        paintIcon(painter, state.actionIcon, glyph.translated(parts.action.center() - glyph.center()),
                  colors ? colors->icon : QColor{}, state.enabled, false);
        painter.restore();
    }
    painter.restore();
}

void TabPainter::paintSeparator(QPainter& painter, const QRect& line, bool windowActive, bool section) const
{
    painter.fillRect(line, section ? palette_.sectionSeparator.resolve(windowActive) : palette_.separator);
}

void TabPainter::paintScrollbar(QPainter& painter, const QRect& thumb, bool emphasized) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    // Always translucent: darker on light themes, lighter on dark ones.
    painter.setBrush(withAlpha(palette_.text, emphasized ? 0.38 : 0.22));
    const qreal radius = std::min(thumb.width(), thumb.height()) / 2.0;
    painter.drawRoundedRect(QRectF(thumb), radius, radius);
    painter.restore();
}

ElevationRenderer& TabPainter::elevationRenderer() const
{
    if (!elevation_) elevation_ = std::make_unique<ElevationRenderer>();
    return *elevation_;
}

void TabPainter::paintPanelShadow(QPainter& painter, const QRectF& area, qreal radius, bool column) const
{
    elevationRenderer().paintBody(painter, area, radius,
        elevationProfile(column ? ElevationLevel::ColumnOverlay : ElevationLevel::RowsOverlay), palette_.elevation);
}

QColor TabPainter::textOn(const QColor& background) const
{
    // Relative luminance (sRGB weights) decides dark or light text.
    const qreal luminance = 0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
    return luminance > 0.6 ? QColor(0x1f, 0x1f, 0x1f) : QColor(Qt::white);
}
QRect TabPainter::folderChipRect(const QRect& header) const
{
    return header.adjusted(metrics_.folderChipMargin, metrics_.folderChipInset, -metrics_.folderChipMargin, -metrics_.folderChipInset);
}
QRect TabPainter::folderAddRect(const QRect& header) const
{
    // Left of the chevron at the row's end.
    const int size = metrics_.folderButton;
    return QRect(header.right() - 2 * size - 2, header.center().y() - size / 2, size, size);
}
void TabPainter::paintFolderHeader(QPainter& painter, const QRect& area, bool vertical, const TabFolderPaintState& state) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const auto count = QString::number(state.count);
    if (!vertical) {
        const auto chip = folderChipRect(area);
        const QFontMetrics metrics(painter.font());
        if (state.name.isEmpty() && !state.collapsed) {
            // A dot, centered.
            const QRect dot(QPoint(), QSize(metrics_.folderDot, metrics_.folderDot));
            painter.setBrush(state.color);
            painter.drawEllipse(QRectF(dot.translated(area.center() - dot.center())));
        } else {
            auto fill = state.color;
            if (state.hovered || state.pressed) fill = fill.darker(state.pressed ? 118 : 108);
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(chip), metrics_.radius, metrics_.radius);
            painter.setPen(textOn(fill));
            auto text = chip.adjusted(metrics_.folderChipPadding, 0, -metrics_.folderChipPadding, 0);
            // Collapsed, the count follows the name.
            const auto label = state.collapsed ? (state.name.isEmpty() ? count : state.name + QStringLiteral("  ") + count) : state.name;
            painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, metrics.elidedText(label, Qt::ElideRight, text.width()));
        }
        painter.restore();
        return;
    }
    // A row: the wash, the name, the count in a quieter tone, the chevron at
    // the end and, on hover, the add button left of it.
    auto wash = state.wash;
    if (state.hovered || state.pressed) wash = wash.darker(state.pressed ? 112 : 105);
    painter.setBrush(wash);
    painter.drawRoundedRect(QRectF(area), metrics_.radius, metrics_.radius);
    const int size = metrics_.folderButton;
    const QRect chevron(area.right() - size - 2, area.center().y() - size / 2, size, size);
    const QRect add = folderAddRect(area);
    auto font = painter.font(); font.setWeight(QFont::DemiBold);
    const QFontMetrics bold(font), regular(painter.font());
    int x = area.left() + 10;
    const int end = (state.hovered ? add.left() : chevron.left()) - 4;
    const int countWidth = regular.horizontalAdvance(count);
    const auto name = bold.elidedText(state.name, Qt::ElideRight, std::max(0, end - x - countWidth - 8));
    if (state.name.isEmpty()) {
        // Without a name, a dot of the folder's color leads the row.
        const QRect dot(x, area.center().y() - metrics_.folderDot / 2, metrics_.folderDot, metrics_.folderDot);
        painter.setPen(Qt::NoPen); painter.setBrush(state.color); painter.drawEllipse(QRectF(dot));
        x += metrics_.folderDot + 8;
    }
    if (!name.isEmpty()) {
        painter.save(); painter.setFont(font); painter.setPen(state.washText);
        painter.drawText(QRect(x, area.top(), end - x, area.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, name);
        painter.restore();
        x += bold.horizontalAdvance(name) + 8;
    }
    painter.setPen(withAlpha(state.washText, 0.6));
    painter.drawText(QRect(x, area.top(), std::max(0, end - x), area.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, count);
    if (state.hovered) {
        if (state.addHovered) {
            painter.setPen(Qt::NoPen); painter.setBrush(withAlpha(state.washText, 0.14));
            painter.drawRoundedRect(QRectF(add), metrics_.controlRadius, metrics_.controlRadius);
        }
        const QRect glyph(QPoint(), QSize(uiPixels(16), uiPixels(16)));
        paintIcon(painter, state.addIcon, glyph.translated(add.center() - glyph.center()), state.washText, true, false);
    }
    // The chevron points down while open, to the side when collapsed.
    QPainterPath arrow;
    const QPointF c = QRectF(chevron).center();
    if (state.collapsed) { arrow.moveTo(c.x() - 2, c.y() - 4); arrow.lineTo(c.x() + 2, c.y()); arrow.lineTo(c.x() - 2, c.y() + 4); }
    else { arrow.moveTo(c.x() - 4, c.y() - 2); arrow.lineTo(c.x(), c.y() + 2); arrow.lineTo(c.x() + 4, c.y() - 2); }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAlpha(state.washText, 0.75), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(arrow);
    painter.restore();
}
void TabPainter::paintFolderLine(QPainter& painter, const QRect& line, const QColor& color) const
{
    if (line.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(color);
    const qreal radius = std::min(line.width(), line.height()) / 2.0;
    painter.drawRoundedRect(QRectF(line), radius, radius);
    painter.restore();
}
void TabPainter::paintFolderOutline(QPainter& painter, const QRect& tab, TabShape shape, const QColor& color) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, metrics_.folderOutline, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal half = metrics_.folderOutline / 2;
    const QRectF rect = QRectF(tab).adjusted(half, shape == TabShape::Top ? half : 0, -half, shape == TabShape::Bottom ? -half : 0);
    if (shape == TabShape::Row || shape == TabShape::Tile) {
        painter.drawRoundedRect(rect, metrics_.radius, metrics_.radius);
        painter.restore();
        return;
    }
    // The tab's outline without its open edge, flaring into the folder line.
    const bool top = shape == TabShape::Top;
    const qreal left = rect.left(), right = rect.right(), outer = top ? rect.top() : rect.bottom();
    const qreal inner = top ? rect.bottom() + half - metrics_.folderLine / 2.0 : rect.top() - half + metrics_.folderLine / 2.0;
    const qreal toward = top ? 1 : -1, radius = metrics_.radius, flare = metrics_.controlRadius;
    QPainterPath path;
    path.moveTo(left - flare, inner);
    path.quadTo(left, inner, left, inner - toward * flare);
    path.lineTo(left, outer + toward * radius);
    path.quadTo(left, outer, left + radius, outer);
    path.lineTo(right - radius, outer);
    path.quadTo(right, outer, right, outer + toward * radius);
    path.lineTo(right, inner - toward * flare);
    path.quadTo(right, inner, right + flare, inner);
    painter.drawPath(path);
    painter.restore();
}
void TabPainter::paintControl(QPainter& painter, const QRect& area, bool hovered, bool pressed) const
{
    if (!hovered && !pressed) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pressed ? palette_.pressed : palette_.hover);
    painter.drawRoundedRect(QRectF(area).adjusted(1, 2, -1, -2), metrics_.controlRadius, metrics_.controlRadius);
    painter.restore();
}

} // namespace window_system
