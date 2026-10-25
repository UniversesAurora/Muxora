// SPDX-License-Identifier: MIT
#include "tab_painter.hpp"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>

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
    metrics.iconSize = theme.metrics.iconSize;
    metrics.rowHeight = std::max(metrics.rowHeight, QFontMetrics(font).height() + 10);
    metrics.rowHeight = std::max(metrics.rowHeight, metrics.iconSize + 10);
    return metrics;
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
    palette.shadow = QColor(0, 0, 0);
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
    auto rects = layoutTabLabel(area, label, direction, tabPartGap);
    result.number = rects.number; result.icon = rects.icon; result.title = rects.title;
    // A title slot keeps the label area's full extent when it is the last
    // part, so the fade lands at the label's real end.
    return result;
}

QColor TabPainter::backgroundOf(const TabPaintState& state) const
{
    if (state.preset) return (state.selected || state.lifted ? state.preset->groupActive : state.preset->groupInactive).background;
    // Outside the focused group the active tab is quieter, like Chrome's
    // inactive window; the focused group needs no line of its own.
    if (state.lifted || (state.selected && state.groupFocused)) return palette_.active;
    const auto mix = [this](qreal toward) {
        const auto& from = palette_.strip; const auto& to = palette_.active;
        return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * toward, from.greenF() + (to.greenF() - from.greenF()) * toward,
                                from.blueF() + (to.blueF() - from.blueF()) * toward);
    };
    if (state.selected) return mix(2.0 / 3);
    // Sticky tiles rest as small cards, half way to the content color.
    if (state.shape == TabShape::Tile) return state.hovered ? mix(0.75) : mix(0.5);
    if (state.pressed) return palette_.pressed;
    if (state.hovered) return palette_.hover;
    return {};
}

QColor TabPainter::textOf(const TabPaintState& state) const
{
    if (state.preset) return (state.selected ? state.preset->groupActive : state.preset->groupInactive).text;
    return state.selected || state.hovered || state.lifted ? palette_.text : palette_.mutedText;
}

void TabPainter::paintTab(QPainter& painter, const TabParts& parts, const TabPaintState& state) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF tab(parts.tab);
    const auto background = backgroundOf(state);
    const bool horizontal = state.shape == TabShape::Top || state.shape == TabShape::Bottom;
    if (state.lifted) {
        painter.save();
        painter.setOpacity(painter.opacity() * std::clamp(state.shadowOpacity, 0.0, 1.0));
        paintShadow(painter, tab, metrics_.radius);
        painter.restore();
    }
    if (background.isValid()) {
        painter.setPen(Qt::NoPen); painter.setBrush(background);
        if (horizontal) {
            const qreal flare = state.selected && !state.lifted ? metrics_.controlRadius : 0;
            painter.drawPath(tabPath(tab, metrics_.radius, state.shape == TabShape::Top, flare));
        } else {
            if (state.shape == TabShape::Tile && !state.lifted) {
                // A card: a fine outline and a one-pixel shadow under it.
                painter.setBrush(QColor(0, 0, 0, 16));
                painter.drawRoundedRect(tab.translated(0, 1), metrics_.radius, metrics_.radius);
                painter.setBrush(background);
                painter.setPen(QPen(withAlpha(palette_.border, 0.45), 1));
                painter.drawRoundedRect(tab.adjusted(0.5, 0.5, -0.5, -0.5), metrics_.radius, metrics_.radius);
                painter.setPen(Qt::NoPen);
            } else painter.drawRoundedRect(tab, metrics_.radius, metrics_.radius);
        }
    }
    // A tab's color is its background alone; lines belong to tab folders.
    const auto* colors = state.preset ? (state.selected ? &state.preset->groupActive : &state.preset->groupInactive) : nullptr;
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor text = textOf(state);
    if (!parts.number.isEmpty()) {
        painter.setPen(text);
        // Digits sit on the baseline up to the cap height; center that span.
        const QFontMetricsF metrics(painter.font());
        painter.drawText(QPointF(parts.number.left(), parts.number.top() + (parts.number.height() + metrics.capHeight()) / 2.0),
                         state.number);
    }
    paintIcon(painter, state.icon, parts.icon, colors ? colors->icon : QColor{}, state.enabled, state.selected);
    if (!parts.title.isEmpty()) {
        if (state.iconOnly || state.shape == TabShape::Tile) {
            painter.setPen(text);
            painter.drawText(parts.title, Qt::AlignCenter | Qt::TextSingleLine, state.title.left(1).toUpper());
        } else if (state.titleLayout) {
            const bool cut = state.titleLayout->naturalWidth() > parts.title.width();
            if (cut && parts.title.width() > metrics_.titleFade) {
                // The cut end fades out instead of ending in half a glyph.
                QLinearGradient fade(parts.title.right() - metrics_.titleFade, 0, parts.title.right() + 1, 0);
                fade.setColorAt(0, text); fade.setColorAt(1, withAlpha(text, 0));
                painter.setPen(QPen(QBrush(fade), 1));
            } else painter.setPen(text);
            state.titleLayout->draw(painter, parts.title);
        }
    }
    if (!parts.action.isEmpty() && state.actionShown) {
        painter.save();
        painter.setOpacity(painter.opacity() * std::clamp(state.actionOpacity, 0.0, 1.0));
        if (!state.actionReserved) {
            // The title fades out under a covering action: the tab's own
            // color, as composited over the strip, slides in from the left.
            auto under = background.isValid() ? background : palette_.strip;
            if (under.alphaF() < 1.0) {
                const qreal alpha = under.alphaF();
                const auto base = palette_.strip;
                under = QColor::fromRgbF(base.redF() + (under.redF() - base.redF()) * alpha,
                                         base.greenF() + (under.greenF() - base.greenF()) * alpha,
                                         base.blueF() + (under.blueF() - base.blueF()) * alpha);
            }
            const bool rightToLeft = parts.action.left() < parts.tab.center().x();
            const int fade = metrics_.edgeFade;
            const QRect cover = rightToLeft ? QRect(parts.tab.left(), parts.tab.top(), parts.action.right() + 1 + fade - parts.tab.left(), parts.tab.height())
                                            : QRect(parts.action.left() - fade, parts.tab.top(), parts.tab.right() + 1 - (parts.action.left() - fade), parts.tab.height());
            QLinearGradient gradient(rightToLeft ? cover.right() + 1 : cover.left(), 0, rightToLeft ? cover.right() + 1 - fade : cover.left() + fade, 0);
            auto clear = under; clear.setAlphaF(0);
            gradient.setColorAt(0, clear); gradient.setColorAt(1, under);
            painter.fillRect(cover.intersected(parts.tab.adjusted(0, metrics_.accentLine, 0, 0)), gradient);
        }
        if (state.actionHovered || state.actionPressed) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(text, state.actionPressed && state.actionHovered ? 0.24 : 0.14));
            painter.drawRoundedRect(QRectF(parts.action), metrics_.controlRadius, metrics_.controlRadius);
        }
        const QRect glyph(QPoint(), QSize(16, 16));
        paintIcon(painter, state.actionIcon, glyph.translated(parts.action.center() - glyph.center()),
                  colors ? colors->icon : QColor{}, state.enabled, false);
        painter.restore();
    }
    painter.restore();
}

void TabPainter::paintSeparator(QPainter& painter, const QRect& line) const
{
    painter.fillRect(line, palette_.separator);
}

void TabPainter::paintFade(QPainter& painter, const QRect& area, Qt::Edge edge) const
{
    if (area.isEmpty()) return;
    const bool horizontal = edge == Qt::LeftEdge || edge == Qt::RightEdge;
    QLinearGradient gradient(horizontal ? QPointF(area.left(), 0) : QPointF(0, area.top()),
                             horizontal ? QPointF(area.right() + 1, 0) : QPointF(0, area.bottom() + 1));
    const auto solid = palette_.strip, clear = withAlpha(palette_.strip, 0);
    const bool fromStart = edge == Qt::LeftEdge || edge == Qt::TopEdge;
    gradient.setColorAt(0, fromStart ? solid : clear);
    gradient.setColorAt(1, fromStart ? clear : solid);
    painter.fillRect(area, gradient);
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

void TabPainter::paintShadow(QPainter& painter, const QRectF& area, qreal radius) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    // A few widening, fading layers approximate a blurred drop shadow.
    const int layers = std::max(1, metrics_.shadowBlur / 2);
    for (int layer = layers; layer >= 1; --layer) {
        const qreal spread = layer * 2.0 * metrics_.shadowBlur / (layers * 2.0);
        auto color = palette_.shadow;
        color.setAlphaF(metrics_.shadowOpacity / layers);
        painter.setBrush(color);
        painter.drawRoundedRect(area.translated(0, metrics_.shadowOffset).adjusted(-spread, -spread / 2, spread, spread),
                                radius + spread, radius + spread);
    }
    painter.restore();
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
        const QRect glyph(QPoint(), QSize(16, 16));
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
