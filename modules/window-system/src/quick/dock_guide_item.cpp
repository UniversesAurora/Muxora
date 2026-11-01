// SPDX-License-Identifier: MIT
#include "dock_guide_item.hpp"
#include "path_item.hpp"
#include "path_stroke_item.hpp"
#include "tab_label_text_item.hpp"

#include <QFontMetrics>
#include <QGuiApplication>
#include <algorithm>

namespace window_system::quick {
namespace {
// Rectangles on whole pixels stay crisp in the window's multisampling.
PathItem* fill(QQuickItem* parent, QRgb color)
{
    auto* item = new PathItem(parent, PathAntialiasing::Multisample);
    item->setFill(QColor::fromRgba(color));
    return item;
}

void show(PathItem* item, const QPainterPath& path) { item->setVisible(!path.isEmpty() && item->setPath(path)); }
} // namespace

DockGuideItem::DockGuideItem(QQuickItem* parent)
    : QQuickItem(parent), previewFill_(fill(this, dock_overlay::previewFill)), previewBorder_(new PathStrokeItem(this)),
      guideFill_(fill(this, dock_overlay::guideFill)), selectedFill_(fill(this, dock_overlay::selectedFill)),
      guideBorder_(new PathStrokeItem(this)), marks_(fill(this, dock_overlay::accent)),
      captionFill_(fill(this, dock_overlay::labelFill)), caption_(new TabLabelTextItem(this))
{
    clear();
}

void DockGuideItem::clear()
{
    for (auto* child : childItems()) child->setVisible(false);
}

void DockGuideItem::setTarget(const DropTarget& target, int count, bool whole)
{
    if (target.window.isNull()) return clear();
    const auto origin = mapToGlobal(QPointF()).toPoint();
    for (auto* stroke : {previewBorder_, guideBorder_}) stroke->setSize(size());
    QPainterPath preview, previewEdge;
    if (target.accepted && !target.preview.isEmpty()) {
        const auto rect = target.preview.translated(-origin);
        preview.addRect(rect);
        // The Widgets pen runs a pixel inside the preview's edge.
        previewEdge.addRect(QRectF(rect).adjusted(1, 1, -1, -1));
    }
    show(previewFill_, preview);
    previewBorder_->setVisible(!previewEdge.isEmpty()
        && previewBorder_->setStroke(previewEdge, QColor(dock_overlay::accent), dock_overlay::previewBorder));
    QPainterPath plain, selected, edges, marks;
    for (const auto& guide : target.guides) {
        const auto square = QRectF(guide.rect.translated(-origin));
        (selectsGuide(target, guide) ? selected : plain).addRoundedRect(square, dock_overlay::guideRadius, dock_overlay::guideRadius);
        edges.addRoundedRect(square, dock_overlay::guideRadius, dock_overlay::guideRadius);
        marks.addRect(guideMark(guide).translated(-origin));
    }
    show(guideFill_, plain);
    show(selectedFill_, selected);
    guideBorder_->setVisible(!edges.isEmpty() && guideBorder_->setStroke(edges, QColor(dock_overlay::guideBorder), 1));
    show(marks_, marks);
    if (!target.accepted) {
        captionFill_->setVisible(false);
        caption_->setVisible(false);
        return;
    }
    const auto text = dropCaption(target, count, whole);
    const auto font = QGuiApplication::font();
    const int natural = QFontMetrics(font).horizontalAdvance(text);
    const QRect label(target.preview.translated(-origin).topLeft() + QPoint(8, 8), QSize(std::max(180, natural + 20), 28));
    QPainterPath background;
    background.addRect(label);
    show(captionFill_, background);
    TabLabelPresentation presentation;
    const QRect area(0, 0, natural, label.height());
    presentation.parts.tab = area; presentation.parts.title = area;
    presentation.state.shape = TabShape::Row; presentation.state.selected = true; presentation.state.title = text;
    presentation.font = font; presentation.naturalWidth = natural;
    TabStripLabelStyle style;
    style.palette.text = Qt::white;
    caption_->setLabel(presentation, style, TabTextRole::Literal);
    caption_->setPosition(QPointF(label.left() + (label.width() - natural) / 2, label.top()));
    caption_->setVisible(true);
}

} // namespace window_system::quick
