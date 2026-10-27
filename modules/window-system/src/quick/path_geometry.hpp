// SPDX-License-Identifier: MIT
#pragma once

#include <QPainterPath>
#include <QVector>
#include <QtGlobal>

namespace window_system::quick {

enum class PathGeometryError {
    None,
    InvalidOptions,
    ElementBudgetExceeded,
    CoordinateBudgetExceeded,
    NonFiniteCoordinate,
    VertexBudgetExceeded,
    WorkBudgetExceeded,
    DegenerateContour,
    SelfIntersection,
    IntersectingContours,
    NestedContours,
    TriangulationFailed,
    AntialiasingTopology,
};

struct PathGeometryOptions {
    qreal devicePixelRatio = 1;
    // Qt's curve-flattening tolerance in physical pixels.
    qreal curveTolerancePixels = 0.25;
    bool antialiasing = true;
    int maximumElements = 1024;
    int maximumVertices = 2048; // Flattened contour vertices; AA uses three per point.
    quint64 maximumWork = 8 * 1024 * 1024;
    // Also bounds Qt's curve flattening before its result can be inspected.
    qreal maximumPhysicalExtent = 32768;

    bool operator==(const PathGeometryOptions&) const = default;
};

struct PathVertex {
    float x = 0;
    float y = 0;
    float coverage = 1;
};

struct PathMesh {
    QVector<PathVertex> vertices;
    QVector<quint32> indices;
};

struct PathGeometry {
    PathMesh fill;
    PathMesh fringe;
    PathGeometryError error = PathGeometryError::None;
    int contourCount = 0;
    int flattenedVertexCount = 0;
    quint64 work = 0;
    qreal area = 0;

    [[nodiscard]] bool valid() const { return error == PathGeometryError::None; }
};

// Supports simple contours and disjoint components. Shared boundaries are
// normalized once with the original fill rule. Nested contours (including
// holes), crossings and degenerate outlines fail and return no mesh.
// With AA enabled the one-physical-pixel fringe is centered on the outline;
// sharp tips, narrow features and folded offset correspondence fail instead
// of emitting an overlapping fringe. MSAA targets can use fill-only meshes.
[[nodiscard]] PathGeometry pathGeometry(const QPainterPath& path,
                                        const PathGeometryOptions& options = {});

// One retained entry, with a monotonic update ID rather than a content hash.
// A renderer only uploads when generation changes; identical inputs are free.
class PathGeometryCache final {
public:
    bool update(const QPainterPath& path, const PathGeometryOptions& options = {});
    [[nodiscard]] const PathGeometry& geometry() const { return geometry_; }
    [[nodiscard]] quint64 generation() const { return generation_; }

private:
    QPainterPath path_;
    PathGeometryOptions options_;
    PathGeometry geometry_;
    quint64 generation_ = 0;
    bool hasInput_ = false;
};

} // namespace window_system::quick
