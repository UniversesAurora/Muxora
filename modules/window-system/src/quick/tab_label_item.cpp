// SPDX-License-Identifier: MIT
#include "tab_label_item.hpp"

#include "path_item.hpp"
#include "anchor_shadow_item.hpp"
#include "body_shadow_item.hpp"
#include "masked_item.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include "style/tab_label_text.hpp"
#include "style/tab_painter.hpp"

#include <QPainterPath>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <algorithm>

namespace window_system::quick {

// The shared coverage gradient is piecewise linear. Keep its stops as vertex
// colors; sizing and animation do not require a tab-sized image.
class TabCoverageItem final : public QQuickItem {
public:
    explicit TabCoverageItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }
    void setCoverage(const SurfaceGeometry& geometry, const ElevationStyle& style)
    {
        if (hasInput_ && anchor_ == geometry.anchor && edge_ == geometry.edge
            && covered_ == geometry.covered && progress_ == geometry.coveredProgress && style_ == style) {
            setVisible(active_);
            return;
        }
        hasInput_ = true;
        anchor_ = geometry.anchor; edge_ = geometry.edge;
        covered_ = geometry.covered; progress_ = geometry.coveredProgress; style_ = style;
        const auto gradient = anchorCoverageGradient(geometry, style);
        active_ = bool(gradient);
        if (!gradient) { setVisible(false); return; }
        const auto stops = gradient->stops();
        const QRectF area(geometry.anchor.left(), gradient->start().y(), geometry.anchor.width(),
                          gradient->finalStop().y() - gradient->start().y());
        if (area_ != area || stops_ != stops) {
            area_ = area;
            stops_ = stops;
            ++revision_;
            update();
        }
        setVisible(true);
    }

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override
    {
        class Node final : public QSGGeometryNode {
        public:
            Node() : mesh(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)
            {
                mesh.setDrawingMode(QSGGeometry::DrawTriangleStrip);
                setGeometry(&mesh);
                setMaterial(&material);
            }
            QSGGeometry mesh;
            QSGVertexColorMaterial material;
            quint64 revision = 0;
        };
        auto* node = previous ? static_cast<Node*>(previous) : new Node;
        if (node->revision == revision_) return node;
        if (node->mesh.vertexCount() != stops_.size() * 2) node->mesh.allocate(int(stops_.size() * 2));
        auto* vertices = node->mesh.vertexDataAsColoredPoint2D();
        for (const auto& stop : stops_) {
            const auto& color = stop.second;
            const auto alpha = color.alphaF();
            const qreal y = area_.y() + stop.first * area_.height();
            for (const auto x : {area_.left(), area_.right()})
                (vertices++)->set(float(x), float(y), uchar(qRound(color.redF() * alpha * 255)),
                    uchar(qRound(color.greenF() * alpha * 255)), uchar(qRound(color.blueF() * alpha * 255)),
                    uchar(qRound(alpha * 255)));
        }
        node->revision = revision_;
        node->markDirty(QSGNode::DirtyGeometry);
        return node;
    }

private:
    bool hasInput_ = false;
    bool active_ = false;
    QRectF anchor_;
    SurfaceEdge edge_ = SurfaceEdge::Top;
    bool covered_ = false;
    qreal progress_ = -1;
    ElevationStyle style_;
    QRectF area_;
    QGradientStops stops_;
    quint64 revision_ = 0;
};

TabLabelItem::TabLabelItem(QQuickItem* parent)
    : QQuickItem(parent),
      shadow_(new AnchorShadowItem(this)),
      border_(new BodyShadowItem(this)),
      surface_(new PathItem(this, PathAntialiasing::Multisample)),
      content_(new MaskedItem(this)),
      coverage_(new TabCoverageItem(content_->contentItem())),
      actionBackground_(new PathItem(content_->contentItem())),
      text_(new TabLabelTextItem(content_->contentItem())),
      numberText_(new TabLabelTextItem(content_->contentItem())),
      mainIcon_(new TabIconItem(content_->contentItem())),
      actionIcon_(new TabIconItem(content_->contentItem()))
{
    setTransformOrigin(QQuickItem::TopLeft);
    shadow_->setZ(-1);
    surface_->setZ(0);
    border_->setZ(1);
    content_->setZ(2);
    coverage_->setZ(-1);
    actionBackground_->setZ(1);
    numberText_->setZ(2);
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

    setPosition(label.presence.mapRect(QRectF(label.parts.tab)).topLeft());
    setSize(label.parts.tab.size());
    setScale(label.presence.scale);
    setOpacity(label.presence.opacity);

    auto local = label;
    local.parts.tab = QRect(QPoint(), label.parts.tab.size());
    for (auto* part : {&local.parts.number, &local.parts.icon, &local.parts.title, &local.parts.action})
        part->translate(-label.parts.tab.topLeft());
    local.presence = {};
    text_->setLabel(local, style);
    bool accepted = true;
    text_->setPosition({});
    if (!local.parts.number.isEmpty() && !local.state.number.isEmpty()) {
        auto number = local;
        number.parts.title = local.parts.number;
        number.parts.number = {};
        number.parts.icon = {};
        number.parts.action = {};
        number.state.title = local.state.number;
        number.state.iconOnly = false;
        number.state.actionShown = false;
        number.state.actionOpacity = 0;
        number.naturalWidth = number.parts.title.width();
        numberText_->setLabel(number, style, TabTextRole::Number);
        numberText_->setPosition({});
    } else numberText_->clearLabel();
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

    accepted = syncSurface(label, local, style, mainIconVisible) && accepted;
    syncElevation(label, local, style);
    return syncAction(label, local, style) && accepted;
}

bool TabLabelItem::syncSurface(const TabLabelPresentation& label, const TabLabelPresentation& local,
                               const TabStripLabelStyle& style, bool mainIconVisible)
{
    bool accepted = true;
    const auto background = tabLabelBackgroundColor(label, style.palette);
    const bool hasBackground = background.isValid() && background.alpha() > 0;
    surface_->setVisible(hasBackground);
    QPainterPath contour;
    if (label.surface) contour = label.surface->anchorPath.translated(-label.parts.tab.topLeft());
    else {
        const auto key = std::tuple(local.parts.tab.size(), label.state.shape,
            style.metrics.radius, style.metrics.controlRadius, label.state.selected, label.state.lifted);
        if (outlineKey_ != key) {
            outlineKey_ = key;
            const TabPainter painter(style.metrics, style.palette);
            outlinePath_ = painter.outline(QRectF(local.parts.tab), label.state.shape,
                                          label.state.selected, label.state.lifted);
        }
        contour = outlinePath_;
    }
    content_->setSize(local.parts.tab.size());
    // Most slots already lie wholly inside the contour. Avoid an offscreen
    // layer there: apart from its cost, native RGB text needs its destination
    // background for subpixel blending. Keep a real mask for overflowing
    // slots and for the coverage band which reaches the rounded edge.
    std::array<QRect, 4> bounds;
    if (!local.state.title.isEmpty()) bounds[0] = local.parts.title;
    if (!local.state.number.isEmpty()) bounds[1] = local.parts.number;
    if (mainIconVisible) bounds[2] = local.parts.icon;
    if (local.state.actionShown) bounds[3] = local.parts.action;
    if (clipContour_ != contour || contentBounds_ != bounds) {
        clipContour_ = contour;
        contentBounds_ = bounds;
        needsContentClip_ = false;
        for (const auto& rect : bounds)
            if (!rect.isEmpty() && !contour.contains(QRectF(rect))) { needsContentClip_ = true; break; }
    }
    const bool covered = label.surface && (label.state.selected || label.state.lifted)
        && label.surface->covered && !style.palette.elevation.highContrast;
    accepted = content_->setContour(needsContentClip_ || covered ? contour : QPainterPath{}) && accepted;
    if (hasBackground) {
        // Rounded rows/tiles retain coverage AA on ordinary targets. Joined
        // shoulders use the explicitly MSAA-capable path, as the backplate
        // does; their window must be configured for MSAA before creation.
        surface_->setAntialiasingMode(label.surface || label.state.shape == TabShape::Top
            || label.state.shape == TabShape::Bottom ? PathAntialiasing::Multisample
                                                    : PathAntialiasing::Coverage);
        surface_->setFill(background);
        accepted = surface_->setPath(contour) && accepted;
    }

    return accepted;
}

void TabLabelItem::syncElevation(const TabLabelPresentation& label, const TabLabelPresentation& local,
                                 const TabStripLabelStyle& style)
{
    const bool showShadow = !label.state.elevationManaged && (label.state.lifted
        || label.state.selected && label.state.shape != TabShape::Tile);
    shadow_->setVisible(showShadow);
    if (showShadow) {
        const bool focused = label.state.groupFocused && label.state.windowActive;
        auto elevationStyle = style.palette.elevation;
        elevationStyle.focused = focused;
        const auto resting = elevationProfile(focused ? ElevationLevel::AnchorRaised : ElevationLevel::AnchorRest);
        const auto elevation = label.state.lifted
            ? interpolateElevation(resting, elevationProfile(ElevationLevel::Lifted), label.state.shadowOpacity) : resting;
        const auto body = label.bodyElevation.value_or(elevationProfile(focused
            ? ElevationLevel::BodyRaised : ElevationLevel::BodyRest));
        SurfaceGeometry geometry;
        if (label.surface) geometry = translatedSurfaceGeometry(*label.surface, -label.parts.tab.topLeft());
        else {
            geometry.anchor = QRectF(local.parts.tab);
            geometry.anchorPath = clipContour_;
            geometry.mergedPath = clipContour_;
            geometry.compact = label.state.shape == TabShape::Tile;
        }
        shadow_->setShadow(geometry, label.surface ? body : elevationProfile(ElevationLevel::Flat),
                           elevation, elevationStyle, label.surface && label.surface->joined);
    }

    const bool showBorder = label.state.shape == TabShape::Tile && !label.state.lifted;
    border_->setVisible(showBorder);
    if (showBorder) {
        const auto tab = QRectF(local.parts.tab);
        const qreal radius = std::max(qreal(0), std::min({qreal(style.metrics.radius),
            tab.width() / 2, tab.height() / 2}) - 0.5);
        border_->setPosition(QPointF(0.5, 0.5));
        border_->setSize(tab.size() - QSizeF(1, 1));
        ElevationStyle stroke;
        stroke.highContrast = true;
        stroke.contrastBorder = style.palette.tileBorder.resolve(label.state.windowActive);
        border_->setShadow(radius, {{}, {}, 1}, stroke);
    }
    if (label.surface && (label.state.selected || label.state.lifted))
        coverage_->setCoverage(translatedSurfaceGeometry(*label.surface, -label.parts.tab.topLeft()),
                               style.palette.elevation);
    else coverage_->setVisible(false);
}

bool TabLabelItem::syncAction(const TabLabelPresentation& label, const TabLabelPresentation& local,
                              const TabStripLabelStyle& style)
{
    bool accepted = true;
    const auto tint = tabLabelIconTint(label);
    const bool actionVisible = label.state.actionShown && label.state.actionOpacity > 0
        && !local.parts.action.isEmpty();
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
        wash.setAlphaF(wash.alphaF() * (label.state.actionPressed && label.state.actionHovered ? 0.24 : 0.14));
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
    shadow_->setVisible(false);
    border_->setVisible(false);
    content_->clearContour();
    coverage_->setVisible(false);
    surface_->setVisible(false);
    actionBackground_->setVisible(false);
    mainIcon_->setVisible(false);
    actionIcon_->setVisible(false);
    mainIcon_->clearIcon();
    actionIcon_->clearIcon();
    text_->clearLabel();
    numberText_->clearLabel();
}

const TabLabelTextItem* TabLabelItem::textItem() const { return text_; }
const TabLabelTextItem* TabLabelItem::numberItem() const { return numberText_; }
quint64 TabLabelItem::iconTextureUploads() const
{
    return mainIcon_->textureUploads() + actionIcon_->textureUploads();
}

} // namespace window_system::quick
