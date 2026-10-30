// SPDX-License-Identifier: MIT
#include "group_surface_item.hpp"

#include "shadow_node.hpp"
#include <QMatrix4x4>
#include <QSGClipNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>
#include "path_node.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <tuple>
#include <utility>

namespace window_system::quick {
namespace {
bool sameGeometry(const SurfaceGeometry& a, const SurfaceGeometry& b)
{
    return std::tie(a.body, a.bodyRadius, a.anchor, a.edge, a.bodyPath, a.anchorPath, a.mergedPath,
                    a.joinStart, a.joinEnd, a.joinProgress, a.joined, a.covered, a.coveredProgress, a.compact)
        == std::tie(b.body, b.bodyRadius, b.anchor, b.edge, b.bodyPath, b.anchorPath, b.mergedPath,
                    b.joinStart, b.joinEnd, b.joinProgress, b.joined, b.covered, b.coveredProgress, b.compact);
}

bool sameAnchors(const QVector<ElevatedAnchor>& a, const QVector<ElevatedAnchor>& b)
{
    return a.size() == b.size() && std::equal(a.cbegin(), a.cend(), b.cbegin(), [](const auto& x, const auto& y) {
        return x.elevation == y.elevation && sameGeometry(x.geometry, y.geometry);
    });
}

bool sameFrame(const TabGroupSurfaceFrame& a, const TabGroupSurfaceFrame& b)
{
    if (std::tie(a.groupBounds, a.body, a.radius, a.elevation, a.style, a.bounds, a.paintRegion,
                 a.extension.path, a.extension.fill, a.extension.opacity)
        != std::tie(b.groupBounds, b.body, b.radius, b.elevation, b.style, b.bounds, b.paintRegion,
                    b.extension.path, b.extension.fill, b.extension.opacity)
        || a.anchors.size() != b.anchors.size() || bool(a.column) != bool(b.column)) return false;
    if (a.column) {
        const auto& x = *a.column; const auto& y = *b.column;
        if (std::tie(x.bounds, x.body, x.radius, x.opacity, x.fill, x.line, x.elevation)
            != std::tie(y.bounds, y.body, y.radius, y.opacity, y.fill, y.line, y.elevation)) return false;
    }
    return std::equal(a.anchors.cbegin(), a.anchors.cend(), b.anchors.cbegin(), [](const auto& x, const auto& y) {
        return x.elevation == y.elevation && x.lifted == y.lifted && x.foreground == y.foreground
            && std::tie(x.presence.opacity, x.presence.scale, x.presence.offset)
                == std::tie(y.presence.opacity, y.presence.scale, y.presence.offset)
            && sameGeometry(x.geometry, y.geometry);
    });
}

void appendMesh(PathMesh& target, const PathMesh& source)
{
    const auto base = quint32(target.vertices.size());
    target.vertices += source.vertices;
    for (const auto index : source.indices) target.indices.append(base + index);
}

void appendRect(PathMesh& mesh, QRectF rect)
{
    if (rect.isEmpty()) return;
    const auto base = quint32(mesh.vertices.size());
    mesh.vertices.append({float(rect.left()), float(rect.top()), 1});
    mesh.vertices.append({float(rect.right()), float(rect.top()), 1});
    mesh.vertices.append({float(rect.left()), float(rect.bottom()), 1});
    mesh.vertices.append({float(rect.right()), float(rect.bottom()), 1});
    mesh.indices.append({base, base + 1, base + 2, base + 2, base + 1, base + 3});
}

// QRegion already provides disjoint integer rectangles. Emitting those
// directly preserves hard clipping without a polygon containing holes.
struct RegionMesh {
    QRegion region;
    PathMesh mesh;
    quint64 revision = 0;
    void update(const QRegion& value, quint64& nextRevision)
    {
        if (revision && region == value) return;
        region = value; mesh = {};
        for (const auto& rect : region) appendRect(mesh, QRectF(rect));
        revision = ++nextRevision;
    }
};

bool subtractQuad(const ElevationPatchQuad& quad, const QRectF& excluded, QVector<ElevationPatchQuad>& output)
{
    const qsizetype maximumQuads = PathGeometryOptions{}.maximumVertices / 6;
    const auto cut = quad.target.intersected(excluded);
    if (cut.isEmpty()) {
        if (output.size() >= maximumQuads) return false;
        output.append(quad); return true;
    }
    const auto& d = quad.target; const auto& s = quad.sourcePixels;
    const auto append = [&](const QRectF& area) {
        if (area.isEmpty()) return true;
        if (output.size() >= maximumQuads) return false;
        output.append({area, {s.x() + (area.x() - d.x()) * s.width() / d.width(),
            s.y() + (area.y() - d.y()) * s.height() / d.height(),
            area.width() * s.width() / d.width(), area.height() * s.height() / d.height()}});
        return true;
    };
    return append({d.left(), d.top(), d.width(), cut.top() - d.top()})
        && append({d.left(), cut.bottom(), d.width(), d.bottom() - cut.bottom()})
        && append({d.left(), cut.top(), cut.left() - d.left(), cut.height()})
        && append({cut.right(), cut.top(), d.right() - cut.right(), cut.height()});
}

struct ShadowSample {
    QImage image;
    QVector<ElevationPatchQuad> quads;
    quint64 resourceRevision = 0;
    quint64 geometryRevision = 0;
    void update(const QImage& nextImage, QVector<ElevationPatchQuad> nextQuads, quint64& nextRevision)
    {
        if (image.constBits() != nextImage.constBits()) { image = nextImage; resourceRevision = ++nextRevision; }
        const bool same = quads.size() == nextQuads.size()
            && std::equal(quads.cbegin(), quads.cend(), nextQuads.cbegin(), [](const auto& a, const auto& b) {
                return a.target == b.target && a.sourcePixels == b.sourcePixels;
            });
        if (!geometryRevision || !same) { quads = std::move(nextQuads); geometryRevision = ++nextRevision; }
    }
};

QVector<ElevationPatchQuad> patchQuad(const ElevationAnchorPatch& patch)
{
    const auto target = patch.bounds.intersected(patch.clip);
    if (patch.image.isNull() || target.isEmpty()) return {};
    const qreal x = patch.image.width() / patch.bounds.width(), y = patch.image.height() / patch.bounds.height();
    return {{target, {(target.left() - patch.bounds.left()) * x, (target.top() - patch.bounds.top()) * y,
                      target.width() * x, target.height() * y}}};
}

struct BodyResource {
    ElevationBodyPatch patch;
    qreal radius = -1;
    qreal ratio = -1;
    ElevationProfile profile;
    ElevationStyle style;
    void prepare(ElevationRenderer& renderer, qreal nextRadius, const ElevationProfile& nextProfile,
                 const ElevationStyle& nextStyle, qreal nextRatio)
    {
        if (radius == nextRadius && ratio == nextRatio && profile == nextProfile && style == nextStyle) return;
        radius = nextRadius; ratio = nextRatio; profile = nextProfile; style = nextStyle;
        patch = profile.edgeOpacity <= 0 && profile.key.opacity <= 0 && profile.ambient.opacity <= 0
            ? ElevationBodyPatch{} : renderer.bodyPatch(radius, profile, style, ratio);
    }
};

struct AnchorResource {
    SurfaceGeometry geometry;
    ElevationProfile body, anchor;
    ElevationStyle style;
    qreal ratio = -1;
    ElevationAnchorPatch patch;
    void prepare(ElevationRenderer& renderer, const TabSurfaceAnchor& input,
                 const ElevationProfile& nextBody, const ElevationStyle& nextStyle, qreal nextRatio)
    {
        if (ratio == nextRatio && body == nextBody && anchor == input.elevation && style == nextStyle
            && sameGeometry(geometry, input.geometry)) return;
        geometry = input.geometry; body = nextBody; anchor = input.elevation; style = nextStyle; ratio = nextRatio;
        patch = renderer.anchorPatch(geometry, body, anchor, style, ratio, true);
    }
};

// A rectangle minus its rounded inner body is four straight bands and four
// simple corner pieces. No clip tessellation needs a hole or an AA fringe.
struct ColumnShadowClip {
    QRect bounds;
    QRectF body;
    qreal radius = -1;
    qreal ratio = -1;
    std::array<PathGeometryCache, 4> corners;
    PathMesh mesh;
    quint64 revision = 0;
    PathGeometryError update(const TabColumnSurface& column, qreal nextRatio, quint64& nextRevision)
    {
        if (revision && bounds == column.bounds && body == column.body && radius == column.radius && ratio == nextRatio)
            return PathGeometryError::None;
        revision = 0;
        bounds = column.bounds; body = column.body; radius = column.radius; ratio = nextRatio; mesh = {};
        QVector<ElevationPatchQuad> outside;
        subtractQuad({QRectF(bounds), QRectF(bounds)}, body, outside);
        for (const auto& quad : outside) appendRect(mesh, quad.target);
        const qreal rx = std::min(radius, body.width() / 2), ry = std::min(radius, body.height() / 2);
        if (rx > 0 && ry > 0) {
            QPainterPath outline; outline.addRoundedRect(body, radius, radius);
            const std::array<QRectF, 4> boxes{{{body.left(), body.top(), rx, ry},
                {body.right() - rx, body.top(), rx, ry}, {body.left(), body.bottom() - ry, rx, ry},
                {body.right() - rx, body.bottom() - ry, rx, ry}}};
            PathGeometryOptions options; options.antialiasing = false; options.devicePixelRatio = ratio;
            for (size_t index = 0; index < boxes.size(); ++index) {
                QPainterPath box; box.addRect(boxes[index]);
                corners[index].update(box.subtracted(outline), options);
                if (!corners[index].geometry().valid()) return corners[index].geometry().error;
                appendMesh(mesh, corners[index].geometry().fill);
            }
        }
        revision = ++nextRevision;
        return PathGeometryError::None;
    }
};

class ClipNode : public QSGClipNode {
public:
    ClipNode() : geometry_(QSGGeometry::defaultAttributes_Point2D(), 0, 0, QSGGeometry::UnsignedIntType)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_.setVertexDataPattern(QSGGeometry::DynamicPattern); geometry_.setIndexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&geometry_); setIsRectangular(false);
    }
    bool sync(const PathMesh& mesh, quint64 revision)
    {
        if (revision_ == revision) return false;
        if (geometry_.vertexCount() != mesh.vertices.size() || geometry_.indexCount() != mesh.indices.size())
            geometry_.allocate(int(mesh.vertices.size()), int(mesh.indices.size()));
        auto* vertices = geometry_.vertexDataAsPoint2D();
        for (const auto& vertex : mesh.vertices) (vertices++)->set(vertex.x, vertex.y);
        std::copy(mesh.indices.cbegin(), mesh.indices.cend(), geometry_.indexDataAsUInt());
        revision_ = revision; markDirty(QSGNode::DirtyGeometry); return true;
    }
private:
    QSGGeometry geometry_;
    quint64 revision_ = 0;
};


class PresenceNode final : public QSGTransformNode {
public:
    PresenceNode()
    {
        opacity = new QSGOpacityNode; shadow = new ShadowNode(1);
        appendChildNode(opacity); opacity->appendChildNode(shadow);
    }
    void syncPose(const LabelPresencePose& pose)
    {
        QMatrix4x4 matrix; matrix.translate(float(pose.offset.x()), float(pose.offset.y())); matrix.scale(float(pose.scale));
        if (matrix != this->matrix()) setMatrix(matrix);
        if (opacity->opacity() != pose.opacity) opacity->setOpacity(pose.opacity);
    }
    QSGOpacityNode* opacity;
    ShadowNode* shadow;
};

class ColumnNode final : public ClipNode {
public:
    ColumnNode()
    {
        shadowClip = new ClipNode; shadowOpacity = new QSGOpacityNode; shadow = new ShadowNode(8);
        fill = new PathNode; lineOpacity = new QSGOpacityNode; line = new ShadowNode(8);
        appendChildNode(shadowClip); shadowClip->appendChildNode(shadowOpacity); shadowOpacity->appendChildNode(shadow);
        appendChildNode(fill); appendChildNode(lineOpacity); lineOpacity->appendChildNode(line);
    }
    ClipNode* shadowClip;
    QSGOpacityNode* shadowOpacity;
    ShadowNode* shadow;
    PathNode* fill;
    QSGOpacityNode* lineOpacity;
    ShadowNode* line;
};

class GroupNode final : public ClipNode {
public:
    GroupNode()
    {
        bodyLayer = new QSGNode; completeLayer = new QSGNode; presenceLayer = new QSGNode;
        extensionOpacity = new QSGOpacityNode; extension = new PathNode;
        columnOpacity = new QSGOpacityNode; column = new ColumnNode;
        appendChildNode(bodyLayer); appendChildNode(completeLayer); appendChildNode(presenceLayer);
        appendChildNode(extensionOpacity); extensionOpacity->appendChildNode(extension);
        appendChildNode(columnOpacity); columnOpacity->appendChildNode(column);
    }
    QSGNode* bodyLayer;
    ShadowNode* body = nullptr;
    int bodyCapacity = 0;
    QSGNode* completeLayer;
    QVector<ShadowNode*> complete;
    QSGNode* presenceLayer;
    QVector<PresenceNode*> presence;
    QSGOpacityNode* extensionOpacity;
    PathNode* extension;
    QSGOpacityNode* columnOpacity;
    ColumnNode* column;
};
} // namespace

struct GroupSurfaceItem::Prepared {
    qreal ratio = -1;
    quint64 revision = 0;
    PathGeometryError error = PathGeometryError::None;
    bool active = false;
    RegionMesh clip;
    BodyResource bodyResource;
    ShadowSample body;
    QVector<ElevatedAnchor> completeInput;
    ElevationProfile completeBody;
    ElevationStyle completeStyle;
    qreal completeRatio = -1;
    QVector<ElevationAnchorPatch> completePatches;
    QVector<ShadowSample> complete;
    QVector<AnchorResource> partialResources;
    QVector<ShadowSample> partial;
    QVector<LabelPresencePose> poses;
    PathGeometryCache extension;
    RegionMesh columnClip;
    ColumnShadowClip columnShadowClip;
    BodyResource columnResource;
    ShadowSample columnShadow;
    PathGeometryCache columnFill;
    BodyResource columnLineResource;
    ShadowSample columnLine;
};

GroupSurfaceItem::GroupSurfaceItem(QQuickItem* parent) : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
}

GroupSurfaceItem::~GroupSurfaceItem() = default;

bool GroupSurfaceItem::setFrame(const TabGroupSurfaceFrame& frame)
{
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    if (frame.body.isEmpty()) {
        frame_ = {}; hasFrame_ = true; clearPrepared(PathGeometryError::None); update();
        return true;
    }
    if (hasFrame_ && prepared_->error == PathGeometryError::None && prepared_->ratio == ratio && sameFrame(frame_, frame)) return true;
    frame_ = frame; hasFrame_ = true;
    const bool accepted = prepareFrame();
    if (!accepted) return rejectFrame(prepared_->error);
    update();
    return true;
}

void GroupSurfaceItem::clearPrepared(PathGeometryError error)
{
    auto next = std::make_unique<Prepared>();
    // A rejected and then accepted input can be coalesced before Qt syncs.
    // Revisions must still differ from the retained previous render nodes.
    next->revision = prepared_->revision; next->error = error;
    prepared_ = std::move(next);
}

bool GroupSurfaceItem::rejectFrame(PathGeometryError error)
{
    frame_ = {}; hasFrame_ = false;
    clearPrepared(error);
    update(); return false;
}

bool GroupSurfaceItem::prepareFrame()
{
    auto& out = *prepared_;
    out.ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    out.error = PathGeometryError::None; out.active = !frame_.body.isEmpty();
    if (!out.active) return true;
    out.clip.update(frame_.paintRegion, out.revision);
    const qreal radius = std::max(qreal(0), std::min({frame_.radius, frame_.body.width() / 2, frame_.body.height() / 2}));
    out.bodyResource.prepare(renderer_, radius, frame_.elevation, frame_.style, out.ratio);
    QVector<ElevationPatchQuad> bodyQuads;
    if (!out.bodyResource.patch.image.isNull()) {
        const auto quads = out.bodyResource.patch.quads(frame_.body, radius);
        bodyQuads = QVector<ElevationPatchQuad>(quads.cbegin(), quads.cend());
    }
    QVector<ElevatedAnchor> complete;
    QVector<const TabSurfaceAnchor*> partial;
    const auto bodyMargins = elevationMargins(frame_.elevation);
    for (const auto& anchor : frame_.anchors) if (!anchor.lifted && !anchor.foreground && !anchor.geometry.compact) {
        if (anchor.presence.complete()) {
            complete.append({anchor.geometry, anchor.elevation});
            const auto margins = elevationMargins(anchor.elevation);
            const qreal extent = std::ceil(std::max({margins.left(), margins.top(), margins.bottom(),
                bodyMargins.left(), bodyMargins.top(), bodyMargins.bottom()})) + 1;
            QVector<ElevationPatchQuad> remaining;
            for (const auto& quad : bodyQuads)
                if (!subtractQuad(quad, surfaceShadowClip(anchor.geometry, extent), remaining)) {
                    out.error = PathGeometryError::VertexBudgetExceeded; return false;
                }
            bodyQuads = std::move(remaining);
        } else if (anchor.presence.opacity > 0) partial.append(&anchor);
    }
    out.body.update(out.bodyResource.patch.image, std::move(bodyQuads), out.revision);
    if (out.completeRatio != out.ratio || out.completeBody != frame_.elevation || out.completeStyle != frame_.style
        || !sameAnchors(out.completeInput, complete)) {
        out.completeInput = complete; out.completeBody = frame_.elevation; out.completeStyle = frame_.style; out.completeRatio = out.ratio;
        out.completePatches = renderer_.anchorPatches(complete, frame_.elevation, frame_.style, out.ratio);
    }
    out.complete.resize(out.completePatches.size());
    for (qsizetype index = 0; index < out.complete.size(); ++index)
        out.complete[index].update(out.completePatches[index].image, patchQuad(out.completePatches[index]), out.revision);
    out.partialResources.resize(partial.size()); out.partial.resize(partial.size()); out.poses.resize(partial.size());
    for (qsizetype index = 0; index < partial.size(); ++index) {
        out.partialResources[index].prepare(renderer_, *partial[index], frame_.elevation, frame_.style, out.ratio);
        const auto& patch = out.partialResources[index].patch;
        out.partial[index].update(patch.image, patchQuad(patch), out.revision); out.poses[index] = partial[index]->presence;
    }
    PathGeometryOptions options; options.devicePixelRatio = out.ratio;
    out.extension.update(frame_.extension.path, options);
    // A vertical strip bridges a one-pixel gap to the card: too thin for an
    // inset antialiasing band. Drawn aliased it covers the same pixels.
    if (out.extension.geometry().error == PathGeometryError::AntialiasingTopology) {
        auto aliased = options; aliased.antialiasing = false;
        out.extension.update(frame_.extension.path, aliased);
    }
    if (!out.extension.geometry().valid()) { out.error = out.extension.geometry().error; return false; }
    if (frame_.column && frame_.column->opacity > 0) {
        const auto& column = *frame_.column;
        out.columnClip.update(QRegion(frame_.bounds) - frame_.groupBounds, out.revision);
        out.error = out.columnShadowClip.update(column, out.ratio, out.revision);
        if (out.error != PathGeometryError::None) return false;
        const qreal columnRadius = std::max(qreal(0), std::min({column.radius, column.body.width() / 2, column.body.height() / 2}));
        out.columnResource.prepare(renderer_, columnRadius, elevationProfile(ElevationLevel::ColumnOverlay), column.elevation, out.ratio);
        const auto shadowQuads = out.columnResource.patch.quads(column.body, columnRadius);
        out.columnShadow.update(out.columnResource.patch.image,
            QVector<ElevationPatchQuad>(shadowQuads.cbegin(), shadowQuads.cend()), out.revision);
        const auto insetBody = column.body.adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath fill; fill.addRoundedRect(insetBody, column.radius, column.radius);
        out.columnFill.update(fill, options);
        if (!out.columnFill.geometry().valid()) { out.error = out.columnFill.geometry().error; return false; }
        // The existing cold stroke rasterizer supplies the exact 1px pen.
        // Keep dynamic alpha on its retained opacity node, not in the image.
        auto line = column.line; line.setAlphaF(1);
        ElevationStyle stroke; stroke.highContrast = true; stroke.contrastBorder = line;
        const qreal lineRadius = std::max(qreal(0), std::min({column.radius, insetBody.width() / 2, insetBody.height() / 2}));
        out.columnLineResource.prepare(renderer_, lineRadius, {{}, {}, 1}, stroke, out.ratio);
        const auto lineQuads = out.columnLineResource.patch.quads(insetBody, lineRadius);
        out.columnLine.update(out.columnLineResource.patch.image,
            QVector<ElevationPatchQuad>(lineQuads.cbegin(), lineQuads.cend()), out.revision);
    }
    return true;
}

void GroupSurfaceItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if ((change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) && hasFrame_) {
        if (!prepareFrame()) { rejectFrame(prepared_->error); return; }
        update();
    }
}

ShadowRenderStats GroupSurfaceItem::renderStats() const
{
    return {textureUploads_.load(std::memory_order_relaxed), imageBytesUploaded_.load(std::memory_order_relaxed),
            geometryUpdates_.load(std::memory_order_relaxed)};
}

PathGeometryError GroupSurfaceItem::geometryError() const { return prepared_->error; }

QSGNode* GroupSurfaceItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    const auto& data = *prepared_;
    if (!data.active || data.error != PathGeometryError::None) { delete previous; return nullptr; }
    auto* root = previous ? static_cast<GroupNode*>(previous) : new GroupNode;
    const auto countGeometry = [&](bool changed) { if (changed) geometryUpdates_.fetch_add(1, std::memory_order_relaxed); };
    const auto syncShadow = [&](ShadowNode& node, const ShadowSample& sample) {
        if (!sample.image.isNull() && node.syncResource(*window(), sample.image, sample.resourceRevision)) {
            textureUploads_.fetch_add(1, std::memory_order_relaxed);
            imageBytesUploaded_.fetch_add(quint64(sample.image.sizeInBytes()), std::memory_order_relaxed);
        }
        countGeometry(node.syncGeometry(std::span<const ElevationPatchQuad>(sample.quads.constData(), size_t(sample.quads.size())),
                                        sample.geometryRevision));
    };
    countGeometry(root->sync(data.clip.mesh, data.clip.revision));
    if (!data.body.image.isNull() && !data.body.quads.isEmpty() && (root->bodyCapacity < data.body.quads.size() || !root->body)) {
        if (root->body) { root->bodyLayer->removeChildNode(root->body); delete root->body; }
        root->bodyCapacity = std::max(8, int(data.body.quads.size())); root->body = new ShadowNode(root->bodyCapacity);
        root->bodyLayer->appendChildNode(root->body);
    }
    if (root->body) syncShadow(*root->body, data.body);
    while (root->complete.size() > data.complete.size()) {
        auto* node = root->complete.takeLast(); root->completeLayer->removeChildNode(node); delete node;
    }
    while (root->complete.size() < data.complete.size()) {
        auto* node = new ShadowNode(1); root->complete.append(node); root->completeLayer->appendChildNode(node);
    }
    for (qsizetype index = 0; index < data.complete.size(); ++index) syncShadow(*root->complete[index], data.complete[index]);
    while (root->presence.size() > data.partial.size()) {
        auto* node = root->presence.takeLast(); root->presenceLayer->removeChildNode(node); delete node;
    }
    while (root->presence.size() < data.partial.size()) {
        auto* node = new PresenceNode; root->presence.append(node); root->presenceLayer->appendChildNode(node);
    }
    for (qsizetype index = 0; index < data.partial.size(); ++index) {
        root->presence[index]->syncPose(data.poses[index]); syncShadow(*root->presence[index]->shadow, data.partial[index]);
    }
    if (root->extensionOpacity->opacity() != frame_.extension.opacity) root->extensionOpacity->setOpacity(frame_.extension.opacity);
    countGeometry(root->extension->sync(data.extension, frame_.extension.fill));
    const qreal columnVisible = frame_.column && frame_.column->opacity > 0 ? 1 : 0;
    if (root->columnOpacity->opacity() != columnVisible) root->columnOpacity->setOpacity(columnVisible);
    if (columnVisible > 0) {
        const auto& column = *frame_.column; auto& nodes = *root->column;
        countGeometry(nodes.sync(data.columnClip.mesh, data.columnClip.revision));
        countGeometry(nodes.shadowClip->sync(data.columnShadowClip.mesh, data.columnShadowClip.revision));
        if (nodes.shadowOpacity->opacity() != column.opacity) nodes.shadowOpacity->setOpacity(column.opacity);
        syncShadow(*nodes.shadow, data.columnShadow);
        countGeometry(nodes.fill->sync(data.columnFill, column.fill));
        if (nodes.lineOpacity->opacity() != column.line.alphaF()) nodes.lineOpacity->setOpacity(column.line.alphaF());
        syncShadow(*nodes.line, data.columnLine);
    }
    return root;
}

} // namespace window_system::quick
