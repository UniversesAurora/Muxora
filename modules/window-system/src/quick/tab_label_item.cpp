// SPDX-License-Identifier: MIT
#include "tab_label_item.hpp"

#include "path_item.hpp"
#include "tab_label_text_item.hpp"
#include "style/tab_label_text.hpp"
#include "style/tab_painter.hpp"

#include <QPainterPath>

namespace window_system::quick {

TabLabelItem::TabLabelItem(QQuickItem* parent)
    : QQuickItem(parent),
      surface_(new PathItem(this)),
      text_(new TabLabelTextItem(this))
{
    surface_->setZ(0);
    text_->setZ(1);
}

bool TabLabelItem::setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style)
{
    if (label.parts.tab.isEmpty()) {
        clearLabel();
        return true;
    }

    setPosition(QPointF(label.parts.tab.topLeft()) + label.presence.offset);
    setSize(label.parts.tab.size());
    setScale(label.presence.scale > 0 ? label.presence.scale : 1);
    setOpacity(label.presence.opacity);

    auto local = label;
    local.parts.tab = QRect(QPoint(), label.parts.tab.size());
    for (auto* part : {&local.parts.number, &local.parts.icon, &local.parts.title, &local.parts.action})
        part->translate(-label.parts.tab.topLeft());
    local.presence = {};
    text_->setLabel(local, style);
    text_->setPosition({});
    surface_->setPosition({});
    surface_->setSize(local.parts.tab.size());

    const auto background = tabLabelBackgroundColor(label, style.palette);
    const bool hasBackground = background.isValid() && background.alpha() > 0;
    surface_->setVisible(hasBackground);
    if (!hasBackground) return true;

    QPainterPath contour;
    if (label.surface && !label.surface->anchorPath.isEmpty())
        contour = label.surface->anchorPath.translated(-label.parts.tab.topLeft());
    else {
        const TabPainter painter(style.metrics, style.palette);
        contour = painter.outline(QRectF(local.parts.tab), label.state.shape,
                                  label.state.selected, label.state.lifted);
    }
    surface_->setFill(background);
    return surface_->setPath(contour);
}

void TabLabelItem::clearLabel()
{
    surface_->setVisible(false);
    text_->clearLabel();
}

PathGeometryError TabLabelItem::surfaceError() const { return surface_->geometryError(); }
const TabLabelTextItem* TabLabelItem::textItem() const { return text_; }
bool TabLabelItem::surfaceVisible() const { return surface_->isVisible(); }

} // namespace window_system::quick
