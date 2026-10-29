// SPDX-License-Identifier: MIT
#include "elevation.hpp"
#include <window_system/ui_scale.hpp>

#include <QCache>
#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>
#include <QRegion>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace window_system {
namespace {
constexpr int progressSteps = 32;
constexpr int cacheBudgetKiB = int(elevationImageBudgetBytes / 1024);

qreal mix(qreal from, qreal to, qreal progress) { return from + (to - from) * progress; }
ElevationLayer mix(const ElevationLayer& from, const ElevationLayer& to, qreal progress)
{
    return {mix(from.offset, to.offset, progress), mix(from.blur, to.blur, progress),
            mix(from.opacity, to.opacity, progress)};
}

struct Patch : ElevationBodyPatch {
    int cost() const { return std::max(1, int((image.sizeInBytes() + 1023) / 1024)); }
};

QImage mask(const QPainterPath& path, const QRectF& bounds, qreal ratio, bool contour = false)
{
    QImage result(QSize(qCeil(bounds.width() * ratio), qCeil(bounds.height() * ratio)), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.scale(ratio, ratio);
    painter.translate(-bounds.topLeft());
    painter.setRenderHint(QPainter::Antialiasing);
    if (contour) {
        QPainterPathStroker stroker;
        stroker.setWidth(2);
        stroker.setJoinStyle(Qt::RoundJoin);
        painter.fillPath(stroker.createStroke(path).subtracted(path), Qt::white);
    } else painter.fillPath(path, Qt::white);
    return result;
}

QVector<float> alpha(const QImage& image)
{
    QVector<float> result(image.width() * image.height());
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) result[y * image.width() + x] = qAlpha(row[x]) / 255.0f;
    }
    return result;
}

QVector<float> gaussian(const QVector<float>& source, QSize size, qreal blur)
{
    if (blur <= 0.01) return source;
    const int extent = qCeil(blur);
    const qreal sigma = blur / 2;
    QVector<qreal> weights(2 * extent + 1);
    qreal total = 0;
    for (int index = -extent; index <= extent; ++index) {
        const qreal weight = std::exp(-qreal(index * index) / (2 * sigma * sigma));
        weights[index + extent] = weight;
        total += weight;
    }
    for (auto& weight : weights) weight /= total;
    QVector<float> horizontal(source.size()), result(source.size());
    struct Support { int begin = 0; int end = 0; };
    QVector<Support> support(size.height());
    int top = size.height(), bottom = 0;
    QVector<qreal> accumulated(size.width());
    for (int y = 0; y < size.height(); ++y) {
        const int row = y * size.width();
        int first = 0, last = size.width();
        while (first < last && source[row + first] == 0) ++first;
        while (last > first && source[row + last - 1] == 0) --last;
        if (first == last) continue;
        top = std::min(top, y); bottom = y + 1;
        const int left = std::max(0, first - extent), right = std::min(size.width(), last + extent);
        support[y] = {left, right};
        std::fill(accumulated.begin(), accumulated.end(), qreal(0));
        for (int tap = -extent; tap <= extent; ++tap) {
            const qreal weight = weights[tap + extent];
            const float* input = source.constData() + row;
            qreal* output = accumulated.data();
            const int begin = std::max(left, first - tap), end = std::min(right, last - tap);
            // Consecutive pixels share this coefficient. Keeping the pixel
            // loop innermost permits SIMD without changing coefficients,
            // accumulation precision, kernel extent or addition order.
            for (int x = begin; x < end; ++x) output[x] += input[x + tap] * weight;
        }
        for (int x = left; x < right; ++x) horizontal[row + x] = float(accumulated[x]);
    }
    // Padding and the transparent corner bands have exactly zero support.
    // Skipping their zero products preserves the kernel and addition order.
    for (int y = std::max(0, top - extent); y < std::min(size.height(), bottom + extent); ++y) {
        std::fill(accumulated.begin(), accumulated.end(), qreal(0));
        for (int tap = std::max(-extent, top - y); tap <= std::min(extent, bottom - y - 1); ++tap) {
            const qreal weight = weights[tap + extent];
            const float* input = horizontal.constData() + (y + tap) * size.width();
            qreal* output = accumulated.data();
            const auto band = support[y + tap];
            for (int x = band.begin; x < band.end; ++x) output[x] += input[x] * weight;
        }
        for (int x = 0; x < size.width(); ++x) result[y * size.width() + x] = float(accumulated[x]);
    }
    return result;
}

qreal sample(const QVector<float>& values, QSize size, int x, qreal y)
{
    const int first = qFloor(y);
    const qreal fraction = y - first;
    const qreal a = first >= 0 && first < size.height() ? values[first * size.width() + x] : 0;
    const qreal b = first + 1 >= 0 && first + 1 < size.height() ? values[(first + 1) * size.width() + x] : 0;
    return mix(a, b, fraction);
}

struct Coverage {
    QVector<float> inside;
    QVector<float> shadow;
    QVector<float> edge;
};

Coverage coverage(const QPainterPath& path, const QRectF& bounds, qreal ratio,
                  const ElevationProfile& profile, const ElevationStyle& style)
{
    Coverage result;
    if (path.isEmpty()) {
        const int count = qCeil(bounds.width() * ratio) * qCeil(bounds.height() * ratio);
        result.inside.resize(count); result.shadow.resize(count); result.edge.resize(count);
        return result;
    }
    const QImage silhouette = mask(path, bounds, ratio);
    result.inside = alpha(silhouette);
    result.shadow.resize(result.inside.size());
    result.edge = alpha(mask(path, bounds, ratio, true));
    for (auto& value : result.edge) value *= float(profile.edgeOpacity);
    const qreal influence = std::max(profile.key.blur + std::abs(profile.key.offset),
                                     profile.ambient.blur + std::abs(profile.ambient.offset));
    const int padding = qCeil(influence * ratio) + 1;
    const QRectF paddedBounds(bounds.topLeft() - QPointF(padding / ratio, padding / ratio),
                             QSizeF((silhouette.width() + 2 * padding) / ratio,
                                    (silhouette.height() + 2 * padding) / ratio));
    // A large body's edge crosses this local image. Keep convolution input
    // beyond its crop so the crop does not create an artificial shadow edge.
    const auto paddedMask = mask(path, paddedBounds, ratio);
    const auto paddedInside = alpha(paddedMask);
    for (const auto& layer : {profile.key, profile.ambient}) {
        if (layer.opacity <= 0) continue;
        const auto blurred = gaussian(paddedInside, paddedMask.size(), layer.blur * ratio);
        const qreal opacity = std::min(qreal(0.5), layer.opacity * style.shadowMultiplier);
        for (int y = 0; y < silhouette.height(); ++y) {
            for (int x = 0; x < silhouette.width(); ++x) {
                const int at = y * silhouette.width() + x;
                const qreal added = sample(blurred, paddedMask.size(), x + padding, y + padding - layer.offset * ratio) * opacity;
                result.shadow[at] = float(1 - (1 - result.shadow[at]) * (1 - added));
            }
        }
    }
    return result;
}

QRgb colored(qreal shadow, qreal edge, const ElevationStyle& style)
{
    const qreal a = shadow * style.shadow.alphaF();
    const qreal b = edge * style.edge.alphaF();
    const qreal total = b + a * (1 - b);
    const auto channel = [a, b](qreal lower, qreal upper) { return qRound(255 * (upper * b + lower * a * (1 - b))); };
    return qRgba(channel(style.shadow.redF(), style.edge.redF()), channel(style.shadow.greenF(), style.edge.greenF()),
                 channel(style.shadow.blueF(), style.edge.blueF()), qRound(255 * total));
}

QRgb missing(QRgb desired, QRgb existing)
{
    const qreal targetAlpha = qAlpha(desired) / 255.0, baseAlpha = qAlpha(existing) / 255.0;
    if (targetAlpha <= baseAlpha) return 0;
    qreal addedAlpha = (targetAlpha - baseAlpha) / (1 - baseAlpha);
    const std::array<int, 3> target{qRed(desired), qGreen(desired), qBlue(desired)};
    const std::array<int, 3> base{qRed(existing), qGreen(existing), qBlue(existing)};
    for (int index = 0; index < 3; ++index) {
        const qreal a = target[index] / 255.0, b = base[index] / 255.0;
        if (b > 0) addedAlpha = std::max(addedAlpha, 1 - a / b);
        if (b < 1) addedAlpha = std::max(addedAlpha, (a - b) / (1 - b));
    }
    addedAlpha = std::clamp(addedAlpha, qreal(0), qreal(1));
    const auto channel = [addedAlpha](int targetChannel, int baseChannel) {
        return std::clamp(qRound(targetChannel - baseChannel * (1 - addedAlpha)), 0, qRound(255 * addedAlpha));
    };
    return qRgba(channel(target[0], base[0]), channel(target[1], base[1]), channel(target[2], base[2]), qRound(255 * addedAlpha));
}

QString keyFor(const ElevationProfile& profile, const ElevationStyle& style, qreal ratio)
{
    return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11")
        .arg(profile.key.offset).arg(profile.key.blur).arg(profile.key.opacity)
        .arg(profile.ambient.offset).arg(profile.ambient.blur).arg(profile.ambient.opacity)
        .arg(profile.edgeOpacity).arg(style.shadow.rgba()).arg(style.edge.rgba())
        .arg(style.shadowMultiplier).arg(ratio);
}

void highContrast(QPainter& painter, const QPainterPath& path, const ElevationStyle& style)
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(style.focused ? style.focusBorder : style.contrastBorder, style.focused ? 2 : 1));
    painter.drawPath(path);
}

qreal anchorExtent(const ElevationProfile& body, const ElevationProfile& anchor)
{
    const auto a = elevationMargins(anchor), b = elevationMargins(body);
    return qCeil(std::max({a.left(), a.top(), a.bottom(), b.left(), b.top(), b.bottom()})) + 1;
}

void anchorBorder(QPainter& painter, const SurfaceGeometry& geometry,
                  const ElevationStyle& style, const QRectF& clip)
{
    if (geometry.covered && geometry.coveredProgress >= 1) {
        auto borderClip = clip;
        const qreal width = style.focused ? 2 : 1;
        switch (geometry.edge) {
        case SurfaceEdge::Top: borderClip.setBottom(geometry.anchor.bottom() - width / 2); break;
        case SurfaceEdge::Bottom: borderClip.setTop(geometry.anchor.top() + width / 2); break;
        case SurfaceEdge::Left: borderClip.setRight(geometry.anchor.right() - width / 2); break;
        case SurfaceEdge::Right: borderClip.setLeft(geometry.anchor.left() + width / 2); break;
        }
        painter.setClipRect(borderClip, Qt::IntersectClip);
    }
    highContrast(painter, geometry.mergedPath, style);
}
} // namespace

ElevationProfile elevationProfile(ElevationLevel level)
{
    auto profile = [level]() -> ElevationProfile {
    switch (level) {
    case ElevationLevel::Flat: return {{}, {}, 0};
    case ElevationLevel::AnchorRest: return {{1, 2, 0.10}, {1, 3, 0.06}, 1};
    case ElevationLevel::AnchorRaised: return {{1, 3, 0.14}, {2, 5, 0.07}, 1};
    case ElevationLevel::BodyRest: return {{1, 3, 0.10}, {2, 8, 0.06}, 1};
    case ElevationLevel::BodyRaised: return {{2, 6, 0.14}, {4, 12, 0.08}, 1};
    case ElevationLevel::Lifted: return {{2, 4, 0.16}, {3, 6, 0.08}, 1};
    case ElevationLevel::RowsOverlay: return {{0, 8, 50.0 / 255}, {}, 0};
    case ElevationLevel::ColumnOverlay: return {{0, 2, 0.14}, {}, 0};
    }
    return {};
    }();
    for (auto* layer : {&profile.key, &profile.ambient}) { layer->offset = uiPixelsF(layer->offset); layer->blur = uiPixelsF(layer->blur); }
    return profile;
}

int elevationStep(qreal progress) { return qRound(std::clamp(progress, qreal(0), qreal(1)) * (progressSteps - 1)); }
qreal quantizedElevation(qreal progress) { return qreal(elevationStep(progress)) / (progressSteps - 1); }
ElevationProfile interpolateElevation(const ElevationProfile& from, const ElevationProfile& to, qreal progress)
{
    const qreal amount = quantizedElevation(progress);
    return {mix(from.key, to.key, amount), mix(from.ambient, to.ambient, amount), mix(from.edgeOpacity, to.edgeOpacity, amount)};
}

QMarginsF elevationMargins(const ElevationProfile& profile)
{
    qreal sides = profile.edgeOpacity > 0 ? 1 : 0, top = sides, bottom = sides;
    for (const auto& layer : {profile.key, profile.ambient}) {
        if (layer.opacity <= 0) continue;
        sides = std::max(sides, layer.blur);
        top = std::max(top, layer.blur - layer.offset);
        bottom = std::max(bottom, layer.blur + layer.offset);
    }
    return {sides, top, sides, bottom};
}

struct ElevationRenderer::Cache {
    QCache<QString, Patch> patches{cacheBudgetKiB};
    quint64 generatedImages = 0;
    qsizetype largestImageBytes = 0;
    void generated(const QImage& image)
    {
        ++generatedImages;
        largestImageBytes = std::max(largestImageBytes, image.sizeInBytes());
    }
};
ElevationRenderer::ElevationRenderer()
{
    // A window or group does not receive a separate shadow-cache budget.
    // Paint devices on one GUI thread reuse the same bounded cache, which
    // disappears when its last renderer is destroyed.
    static thread_local std::weak_ptr<Cache> shared;
    cache_ = shared.lock();
    if (!cache_) { cache_ = std::make_shared<Cache>(); shared = cache_; }
}
ElevationRenderer::~ElevationRenderer() = default;
void ElevationRenderer::clear() { cache_->patches.clear(); }
ElevationCacheStats ElevationRenderer::cacheStats() const
{
    return {int(cache_->patches.size()), qsizetype(cache_->patches.totalCost()) * 1024,
            cache_->generatedImages, cache_->largestImageBytes};
}

void ElevationRenderer::paintBody(QPainter& painter, const QRectF& body, qreal radius,
                                  const ElevationProfile& elevation, const ElevationStyle& style,
                                  const QRectF& excluded)
{
    if (body.isEmpty()) return;
    if (elevation.edgeOpacity <= 0 && elevation.key.opacity <= 0 && elevation.ambient.opacity <= 0) return;
    radius = std::max(qreal(0), std::min({radius, body.width() / 2, body.height() / 2}));
    painter.save();
    if (!excluded.isEmpty()) {
        const auto margins = elevationMargins(elevation);
        QPainterPath surrounding, local;
        surrounding.addRect(body.marginsAdded(margins));
        local.addRect(excluded);
        painter.setClipPath(surrounding.subtracted(local), Qt::IntersectClip);
    }
    if (style.highContrast) {
        QPainterPath path;
        path.addRoundedRect(body, radius, radius);
        highContrast(painter, path, style);
        painter.restore();
        return;
    }
    const auto patch = bodyPatch(radius, elevation, style, painter.device()->devicePixelRatioF());
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto& quad : patch.quads(body, radius))
        painter.drawImage(quad.target, patch.image, quad.sourcePixels);
    painter.restore();
}

ElevationBodyPatch ElevationRenderer::bodyPatch(qreal radius, const ElevationProfile& elevation,
                                               const ElevationStyle& style, qreal ratio)
{
    const QString key = QStringLiteral("body:%1:%2:%3:").arg(radius)
        .arg(style.highContrast ? (style.focused ? 2 : 1) : 0)
        .arg(style.highContrast ? (style.focused ? style.focusBorder : style.contrastBorder).rgba() : 0)
        + keyFor(elevation, style, ratio);
    const Patch* patch = cache_->patches.object(key);
    std::unique_ptr<Patch> owned;
    if (!patch) {
        const auto margins = elevationMargins(elevation);
        const qreal spread = qCeil(std::max({margins.left(), margins.top(), margins.bottom()})) + 1;
        const qreal inset = qCeil(radius + spread);
        const QRectF spriteBody(spread, spread, 2 * inset + 1, 2 * inset + 1);
        const QRectF bounds(0, 0, spriteBody.width() + 2 * spread, spriteBody.height() + 2 * spread);
        QPainterPath path;
        path.addRoundedRect(spriteBody, radius, radius);
        owned = std::make_unique<Patch>();
        owned->bounds = bounds; owned->bodyInset = spread; owned->devicePixelRatio = ratio;
        owned->image = QImage(QSize(qCeil(bounds.width() * ratio), qCeil(bounds.height() * ratio)), QImage::Format_ARGB32_Premultiplied);
        if (style.highContrast) {
            owned->image.fill(Qt::transparent);
            QPainter painter(&owned->image);
            painter.scale(ratio, ratio);
            highContrast(painter, path, style);
        } else {
            const auto values = coverage(path, bounds, ratio, elevation, style);
            for (int y = 0; y < owned->image.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(owned->image.scanLine(y));
                for (int x = 0; x < owned->image.width(); ++x) {
                    const int at = y * owned->image.width() + x;
                    row[x] = colored(values.shadow[at] * (1 - values.inside[at]), values.edge[at], style);
                }
            }
        }
        patch = owned.get();
        cache_->generated(owned->image);
        if (owned->cost() <= cacheBudgetKiB) cache_->patches.insert(key, owned.release(), patch->cost());
    }
    return *patch;
}

std::array<ElevationPatchQuad, 8> ElevationBodyPatch::quads(const QRectF& body, qreal radius) const
{
    const qreal ratio = devicePixelRatio;
    const qreal margin = bodyInset;
    const qreal edge = std::min({radius + margin, body.width() / 2, body.height() / 2});
    const qreal sourceEdge = radius + margin;
    const std::array<qreal, 4> sx{0, margin + sourceEdge, bounds.width() - margin - sourceEdge, bounds.width()};
    const std::array<qreal, 4> sy{0, margin + sourceEdge, bounds.height() - margin - sourceEdge, bounds.height()};
    const std::array<qreal, 4> dx{body.left() - margin, body.left() + edge, body.right() - edge, body.right() + margin};
    const std::array<qreal, 4> dy{body.top() - margin, body.top() + edge, body.bottom() - edge, body.bottom() + margin};
    std::array<ElevationPatchQuad, 8> result;
    int at = 0;
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
        if (x == 1 && y == 1) continue;
        result[at++] = {QRectF(dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]),
                       QRectF(sx[x] * ratio, sy[y] * ratio, (sx[x + 1] - sx[x]) * ratio, (sy[y + 1] - sy[y]) * ratio)};
    }
    return result;
}

void ElevationRenderer::paintAnchor(QPainter& painter, const SurfaceGeometry& geometry,
                                    const ElevationProfile& bodyElevation, const ElevationProfile& anchorElevation,
                                    const ElevationStyle& style, bool incremental)
{
    if (geometry.anchorPath.isEmpty() || (geometry.compact && anchorElevation.key.opacity <= 0)) return;
    if (style.highContrast) {
        const qreal extent = anchorExtent(bodyElevation, anchorElevation);
        const auto clip = surfaceShadowClip(geometry, extent);
        painter.save(); painter.setClipRect(clip, Qt::IntersectClip);
        anchorBorder(painter, geometry, style, clip);
        painter.restore();
        return;
    }
    const auto patch = anchorPatch(geometry, bodyElevation, anchorElevation, style,
                                   painter.device()->devicePixelRatioF(), incremental);
    painter.save(); painter.setClipRect(patch.clip, Qt::IntersectClip);
    painter.drawImage(patch.bounds, patch.image);
    painter.restore();
}

ElevationAnchorPatch ElevationRenderer::anchorPatch(const SurfaceGeometry& geometry,
    const ElevationProfile& bodyElevation, const ElevationProfile& anchorElevation,
    const ElevationStyle& style, qreal ratio, bool incremental)
{
    if (geometry.anchorPath.isEmpty() || (geometry.compact && anchorElevation.key.opacity <= 0)) return {};
    const qreal extent = anchorExtent(bodyElevation, anchorElevation);
    const QRectF bounds = geometry.anchorPath.boundingRect().adjusted(-extent, -extent, extent, extent);
    const QRectF clip = surfaceShadowClip(geometry, extent);
    const QPointF origin = bounds.topLeft();
    const auto localPath = geometry.anchorPath.translated(-origin);
    QString key = QStringLiteral("anchor:%1:%2:%3:%4:").arg(incremental).arg(geometry.joined).arg(geometry.covered).arg(geometry.compact)
        + keyFor(bodyElevation, style, ratio) + QLatin1Char(':') + keyFor(anchorElevation, style, ratio);
    if (style.highContrast) key += QStringLiteral(":contrast:%1:%2:%3:%4")
        .arg(style.focused).arg((style.focused ? style.focusBorder : style.contrastBorder).rgba())
        .arg(geometry.covered && geometry.coveredProgress >= 1).arg(int(geometry.edge));
    // Remote body corners cannot influence this small raster. Normalize
    // them out of the key so sliding an anchor along a straight body edge
    // reuses its sprite instead of blurring on every pointer move.
    QRectF localBody = geometry.body.isEmpty() ? QRectF{} : geometry.body.translated(-origin);
    if (!geometry.body.isEmpty()) {
        if (localBody.left() + geometry.bodyRadius < -extent) localBody.setLeft(-extent - geometry.bodyRadius - 1);
        if (localBody.right() - geometry.bodyRadius > bounds.width() + extent)
            localBody.setRight(bounds.width() + extent + geometry.bodyRadius + 1);
        if (localBody.top() + geometry.bodyRadius < -extent) localBody.setTop(-extent - geometry.bodyRadius - 1);
        if (localBody.bottom() - geometry.bodyRadius > bounds.height() + extent)
            localBody.setBottom(bounds.height() + extent + geometry.bodyRadius + 1);
    }
    for (const auto& rect : {localBody, bounds.translated(-origin)})
        key += QStringLiteral(":%1,%2,%3,%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    key += QStringLiteral(":%1").arg(geometry.bodyRadius);
    for (int index = 0; index < localPath.elementCount(); ++index) {
        const auto element = localPath.elementAt(index);
        key += QStringLiteral(":%1,%2,%3").arg(element.type).arg(element.x).arg(element.y);
    }
    const Patch* patch = cache_->patches.object(key);
    std::unique_ptr<Patch> owned;
    if (!patch) {
        owned = std::make_unique<Patch>();
        owned->bounds = bounds.translated(-origin);
        owned->image = QImage(QSize(qCeil(bounds.width() * ratio), qCeil(bounds.height() * ratio)), QImage::Format_ARGB32_Premultiplied);
        if (style.highContrast) {
            owned->image.fill(Qt::transparent);
            QPainter painter(&owned->image);
            painter.scale(ratio, ratio); painter.translate(-origin);
            anchorBorder(painter, geometry, style, clip);
        } else {
            const auto body = coverage(geometry.bodyPath, bounds, ratio, bodyElevation, style);
            const auto anchor = coverage(geometry.anchorPath, bounds, ratio, anchorElevation, style);
            const auto mergedInside = alpha(mask(geometry.mergedPath, bounds, ratio));
            const auto mergedEdge = alpha(mask(geometry.mergedPath, bounds, ratio, true));
            for (int y = 0; y < owned->image.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(owned->image.scanLine(y));
                for (int x = 0; x < owned->image.width(); ++x) {
                    const int at = y * owned->image.width() + x;
                    const qreal shadow = std::max(body.shadow[at], anchor.shadow[at]) * (1 - mergedInside[at]);
                    const qreal edge = mergedEdge[at] * std::max(bodyElevation.edgeOpacity, anchorElevation.edgeOpacity);
                    const QRgb desired = colored(shadow, edge, style);
                    row[x] = incremental ? missing(desired, colored(body.shadow[at] * (1 - body.inside[at]), body.edge[at], style)) : desired;
                }
            }
        }
        patch = owned.get();
        cache_->generated(owned->image);
        if (owned->cost() <= cacheBudgetKiB) cache_->patches.insert(key, owned.release(), patch->cost());
    }
    return {patch->image, bounds, clip};
}

void ElevationRenderer::paintAnchors(QPainter& painter, const QVector<ElevatedAnchor>& anchors,
                                      const ElevationProfile& bodyElevation, const ElevationStyle& style)
{
    if (anchors.isEmpty()) return;
    if (anchors.size() == 1) {
        paintAnchor(painter, anchors.front().geometry, bodyElevation, anchors.front().elevation, style);
        return;
    }
    for (const auto& patch : anchorPatches(anchors, bodyElevation, style, painter.device()->devicePixelRatioF())) {
        painter.save(); painter.setClipRect(patch.clip, Qt::IntersectClip);
        painter.drawImage(patch.bounds.topLeft(), patch.image); painter.restore();
    }
}

QVector<ElevationAnchorPatch> ElevationRenderer::anchorPatches(const QVector<ElevatedAnchor>& anchors,
    const ElevationProfile& bodyElevation, const ElevationStyle& style, qreal ratio)
{
    if (anchors.isEmpty()) return {};
    if (anchors.size() == 1)
        return {anchorPatch(anchors.front().geometry, bodyElevation, anchors.front().elevation, style, ratio)};
    QVector<ElevationAnchorPatch> patches;
    // Several simultaneously elevated anchors may cast nearby shadows.
    // Their local patches both contain the same body shadow: source-over
    // would darken it twice. Merge only their disjoint local regions by max.
    QRegion regions;
    QVector<QRect> bounds;
    const auto bodyMargins = elevationMargins(bodyElevation);
    for (const auto& anchor : anchors) {
        const auto margins = elevationMargins(anchor.elevation);
        const qreal extent = qCeil(std::max({margins.left(), margins.top(), margins.bottom(),
            bodyMargins.left(), bodyMargins.top(), bodyMargins.bottom()})) + 1;
        const QRect area = surfaceShadowClip(anchor.geometry, extent).toAlignedRect();
        bounds.append(area); regions |= area;
    }
    for (const QRect& band : regions) {
        const QSize pixels(qCeil(band.width() * ratio), qCeil(band.height() * ratio));
        QImage merged(pixels, QImage::Format_ARGB32_Premultiplied);
        merged.fill(Qt::transparent); merged.setDevicePixelRatio(ratio);
        QImage layer(pixels, QImage::Format_ARGB32_Premultiplied);
        layer.setDevicePixelRatio(ratio);
        for (int index = 0; index < anchors.size(); ++index) {
            if (!bounds.at(index).intersects(band)) continue;
            layer.fill(Qt::transparent);
            QPainter local(&layer); local.translate(-band.topLeft());
            paintAnchor(local, anchors.at(index).geometry, bodyElevation, anchors.at(index).elevation, style);
            local.end();
            for (int y = 0; y < pixels.height(); ++y) {
                auto* output = reinterpret_cast<QRgb*>(merged.scanLine(y));
                const auto* input = reinterpret_cast<const QRgb*>(layer.constScanLine(y));
                for (int x = 0; x < pixels.width(); ++x)
                    if (qAlpha(input[x]) > qAlpha(output[x])) output[x] = input[x];
            }
        }
        const QRectF naturalBounds(QPointF(band.topLeft()), QSizeF(pixels) / ratio);
        patches.append({std::move(merged), naturalBounds, QRectF(band)});
    }
    return patches;
}

void ElevationRenderer::paintOverlayBand(QPainter& painter, const QRect& band, bool above,
                                          const ElevationStyle& style, qreal progress)
{
    if (band.isEmpty() || progress <= 0 || style.highContrast) return;
    QLinearGradient shade(above ? band.bottomLeft() : band.topLeft(), above ? band.topLeft() : band.bottomLeft());
    auto edge = style.shadow;
    edge.setAlphaF(edge.alphaF() * elevationProfile(ElevationLevel::RowsOverlay).key.opacity * std::clamp(progress, qreal(0), qreal(1)));
    auto clear = edge; clear.setAlpha(0);
    shade.setColorAt(0, edge); shade.setColorAt(1, clear);
    painter.fillRect(band, shade);
}

std::optional<QLinearGradient> anchorCoverageGradient(const SurfaceGeometry& geometry,
    const ElevationStyle& style, qreal progress)
{
    if (!geometry.covered || style.highContrast || progress <= 0
        || (geometry.edge != SurfaceEdge::Top && geometry.edge != SurfaceEdge::Bottom)) return {};
    const bool upperCover = geometry.edge == SurfaceEdge::Bottom;
    const qreal depth = upperCover ? 3 : 2;
    const qreal opacity = (upperCover ? 0.10 : 0.05) * style.shadowMultiplier * geometry.coveredProgress
        * std::clamp(progress, qreal(0), qreal(1));
    const auto& rect = geometry.anchor;
    const QPointF start(rect.center().x(), upperCover ? rect.top() : rect.bottom());
    const QPointF end = start + QPointF(0, upperCover ? depth : -depth);
    QLinearGradient gradient(start, end);
    for (int step = 0; step <= progressSteps; ++step) {
        const qreal distance = qreal(step) / progressSteps;
        QColor color = style.shadow;
        color.setAlphaF(step == progressSteps ? 0 : std::min(qreal(0.5), opacity * std::exp(-8 * distance * distance)));
        gradient.setColorAt(distance, color);
    }
    return gradient;
}

void ElevationRenderer::paintCoverage(QPainter& painter, const SurfaceGeometry& geometry,
                                      const ElevationStyle& style, qreal progress)
{
    const auto gradient = anchorCoverageGradient(geometry, style, progress);
    if (!gradient) return;
    painter.save(); painter.setClipPath(geometry.anchorPath, Qt::IntersectClip);
    painter.fillRect(geometry.anchor, *gradient); painter.restore();
}

} // namespace window_system
