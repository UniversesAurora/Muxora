// SPDX-License-Identifier: MIT
#pragma once

#include "../docking/surface_geometry.hpp"
#include <QColor>
#include <QImage>
#include <QLinearGradient>
#include <QMarginsF>
#include <QVector>
#include <memory>
#include <array>
#include <optional>

class QPainter;

namespace window_system {

inline constexpr qsizetype elevationImageBudgetBytes = 16 * 1024 * 1024;

enum class ElevationLevel { Flat, AnchorRest, AnchorRaised, BodyRest, BodyRaised, Lifted, RowsOverlay, ColumnOverlay };

struct ElevationLayer {
    qreal offset = 0;
    qreal blur = 0;
    qreal opacity = 0;
    bool operator==(const ElevationLayer&) const = default;
};

struct ElevationProfile {
    ElevationLayer key;
    ElevationLayer ambient;
    qreal edgeOpacity = 1;
    bool operator==(const ElevationProfile&) const = default;
};

struct ElevationStyle {
    QColor shadow = Qt::black;
    // This includes the neutral edge opacity (normally 7%).
    QColor edge = QColor::fromRgbF(0, 0, 0, 0.07f);
    qreal shadowMultiplier = 1;
    bool highContrast = false;
    bool focused = false;
    QColor contrastBorder;
    QColor focusBorder;
    bool operator==(const ElevationStyle&) const = default;
};

struct ElevationCacheStats {
    int entries = 0;
    qsizetype bytes = 0;
    quint64 generatedImages = 0;
    qsizetype largestImageBytes = 0;
};

struct ElevatedAnchor {
    SurfaceGeometry geometry;
    ElevationProfile elevation;
};

struct ElevationPatchQuad {
    QRectF target;
    QRectF sourcePixels;
};

// Shared cold resource, independent of the destination body's dimensions.
// Renderers retain this value; the image's pixels are implicitly shared.
struct ElevationBodyPatch {
    QImage image;
    QRectF bounds;
    qreal bodyInset = 0;
    qreal devicePixelRatio = 1;
    [[nodiscard]] std::array<ElevationPatchQuad, 8> quads(const QRectF& body, qreal radius) const;
};

// Only the anchor's local neighborhood, including its share of the body
// shadow. Bounds and clip move without copying the cached image's pixels.
struct ElevationAnchorPatch {
    QImage image;
    QRectF bounds;
    QRectF clip;
};

[[nodiscard]] ElevationProfile elevationProfile(ElevationLevel level);
[[nodiscard]] int elevationStep(qreal progress);
[[nodiscard]] qreal quantizedElevation(qreal progress);
[[nodiscard]] ElevationProfile interpolateElevation(const ElevationProfile& from,
                                                    const ElevationProfile& to, qreal progress);
[[nodiscard]] QMarginsF elevationMargins(const ElevationProfile& profile);
[[nodiscard]] std::optional<QLinearGradient> anchorCoverageGradient(const SurfaceGeometry& geometry,
    const ElevationStyle& style, qreal progress = 1);

// Small rounded-rectangle sprites are stretched as nine patches. Joined
// anchors use a bounded local image, never a body-sized intermediate.
class ElevationRenderer final {
public:
    ElevationRenderer();
    ~ElevationRenderer();
    ElevationRenderer(const ElevationRenderer&) = delete;
    ElevationRenderer& operator=(const ElevationRenderer&) = delete;

    void paintBody(QPainter& painter, const QRectF& body, qreal radius,
                   const ElevationProfile& elevation, const ElevationStyle& style,
                   const QRectF& excluded = {});
    [[nodiscard]] ElevationBodyPatch bodyPatch(qreal radius, const ElevationProfile& elevation,
                                             const ElevationStyle& style, qreal devicePixelRatio);
    [[nodiscard]] ElevationAnchorPatch anchorPatch(const SurfaceGeometry& geometry,
        const ElevationProfile& bodyElevation, const ElevationProfile& anchorElevation,
        const ElevationStyle& style, qreal devicePixelRatio, bool incremental = false);
    // When incremental, source-over contributes only the opacity missing
    // above an already painted body shadow, rather than adding two shadows.
    void paintAnchor(QPainter& painter, const SurfaceGeometry& geometry,
                     const ElevationProfile& bodyElevation,
                     const ElevationProfile& anchorElevation,
                     const ElevationStyle& style, bool incremental = false);
    void paintAnchors(QPainter& painter, const QVector<ElevatedAnchor>& anchors,
                      const ElevationProfile& bodyElevation, const ElevationStyle& style);
    // Multiple anchors share merged local bands, preserving the max-alpha
    // composition without a body-sized image or source-over darkening.
    [[nodiscard]] QVector<ElevationAnchorPatch> anchorPatches(const QVector<ElevatedAnchor>& anchors,
        const ElevationProfile& bodyElevation, const ElevationStyle& style, qreal devicePixelRatio);
    void paintCoverage(QPainter& painter, const SurfaceGeometry& geometry,
                       const ElevationStyle& style, qreal progress = 1);
    void paintOverlayBand(QPainter& painter, const QRect& band, bool above,
                          const ElevationStyle& style, qreal progress);
    void clear();
    [[nodiscard]] ElevationCacheStats cacheStats() const;

private:
    struct Cache;
    std::shared_ptr<Cache> cache_;
};

} // namespace window_system
