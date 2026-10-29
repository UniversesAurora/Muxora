// SPDX-License-Identifier: MIT
#include "tab_label_item.hpp"

#include "path_item.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include "style/tab_label_text.hpp"
#include "style/tab_painter.hpp"

#include <QPainterPath>
#include <algorithm>

namespace window_system::quick {

TabLabelItem::TabLabelItem(QQuickItem* parent)
    : QQuickItem(parent),
      surface_(new PathItem(this)),
      actionBackground_(new PathItem(this)),
      text_(new TabLabelTextItem(this)),
      mainIcon_(new TabIconItem(this)),
      actionIcon_(new TabIconItem(this))
{
    surface_->setZ(0);
    actionBackground_->setZ(1);
    text_->setZ(2);
    mainIcon_->setZ(2);
    actionIcon_->setZ(3);
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
    const auto tint = tabLabelIconTint(label);
    const bool mainIconVisible = !label.state.icon.isNull() && !local.parts.icon.isEmpty();
    mainIcon_->setVisible(mainIconVisible);
    if (mainIconVisible) {
        mainIcon_->setPosition(local.parts.icon.topLeft());
        mainIcon_->setIcon(label.state.icon, local.parts.icon.size(), tint, label.state.enabled,
                           label.state.selected
                               && (label.state.shape != TabShape::Tile || label.state.windowActive));
    } else mainIcon_->clearIcon();

    const auto background = tabLabelBackgroundColor(label, style.palette);
    const bool hasBackground = background.isValid() && background.alpha() > 0;
    surface_->setVisible(hasBackground);
    bool accepted = true;
    if (hasBackground) {
        QPainterPath contour;
        if (label.surface && !label.surface->anchorPath.isEmpty())
            contour = label.surface->anchorPath.translated(-label.parts.tab.topLeft());
        else {
            const TabPainter painter(style.metrics, style.palette);
            contour = painter.outline(QRectF(local.parts.tab), label.state.shape,
                                      label.state.selected, label.state.lifted);
        }
        surface_->setFill(background);
        accepted = surface_->setPath(contour);
    }

    const bool actionVisible = label.state.actionShown && label.state.actionOpacity > 0
        && !local.parts.action.isEmpty() && !label.state.actionIcon.isNull();
    actionIcon_->setVisible(actionVisible);
    actionBackground_->setVisible(actionVisible
        && (label.state.actionHovered || label.state.actionPressed));
    if (actionVisible) {
        const int glyphSize = std::min({local.parts.action.width(), local.parts.action.height(),
                                        style.metrics.iconSize});
        const QRect glyph(QPoint(), QSize(glyphSize, glyphSize));
        actionIcon_->setPosition(local.parts.action.center() - glyph.center());
        actionIcon_->setOpacity(label.state.actionOpacity);
        actionIcon_->setIcon(label.state.actionIcon, glyph.size(), tint, label.state.enabled, false);

        actionBackground_->setPosition({});
        actionBackground_->setSize(local.parts.tab.size());
        actionBackground_->setOpacity(label.state.actionOpacity);
        auto wash = tabLabelTextColor(label, style.palette);
        wash.setAlphaF(label.state.actionPressed && label.state.actionHovered ? 0.24 : 0.14);
        actionBackground_->setFill(wash);
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(local.parts.action), style.metrics.controlRadius,
                               style.metrics.controlRadius);
        accepted = actionBackground_->setPath(rounded) && accepted;
    }
    return accepted;
}

void TabLabelItem::clearLabel()
{
    surface_->setVisible(false);
    actionBackground_->setVisible(false);
    mainIcon_->setVisible(false);
    actionIcon_->setVisible(false);
    mainIcon_->clearIcon();
    actionIcon_->clearIcon();
    text_->clearLabel();
}

PathGeometryError TabLabelItem::surfaceError() const { return surface_->geometryError(); }
const TabLabelTextItem* TabLabelItem::textItem() const { return text_; }
bool TabLabelItem::surfaceVisible() const { return surface_->isVisible(); }
bool TabLabelItem::mainIconVisible() const { return mainIcon_->isVisible(); }
bool TabLabelItem::actionVisible() const { return actionIcon_->isVisible(); }
quint64 TabLabelItem::iconTextureUploads() const
{
    return mainIcon_->textureUploads() + actionIcon_->textureUploads();
}

} // namespace window_system::quick
