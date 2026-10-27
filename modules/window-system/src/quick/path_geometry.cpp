// SPDX-License-Identifier: MIT
#include "path_geometry.hpp"

#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace window_system::quick {
namespace {
using Contour = QVector<QPointF>;

struct Budget {
    quint64 limit;
    quint64 used = 0;
    bool take() { return ++used <= limit; }
};

qreal cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
qreal cross(QPointF a, QPointF b, QPointF c) { return cross(b - a, c - a); }
qreal squared(QPointF point) { return QPointF::dotProduct(point, point); }

qreal signedArea(const Contour& points)
{
    // Translation avoids cancellation for paths positioned far from zero.
    qreal result = 0;
    const QPointF origin = points.front();
    for (int i = 1; i + 1 < points.size(); ++i)
        result += cross(points[i] - origin, points[i + 1] - origin);
    return result / 2;
}

bool onSegment(QPointF point, QPointF a, QPointF b, qreal epsilon)
{
    const QPointF edge = b - a;
    const qreal length = std::sqrt(squared(edge));
    if (std::abs(cross(edge, point - a)) > epsilon * length) return false;
    return point.x() >= std::min(a.x(), b.x()) - epsilon
        && point.x() <= std::max(a.x(), b.x()) + epsilon
        && point.y() >= std::min(a.y(), b.y()) - epsilon
        && point.y() <= std::max(a.y(), b.y()) + epsilon;
}

bool intersects(QPointF a, QPointF b, QPointF c, QPointF d, qreal epsilon)
{
    if (std::max(a.x(), b.x()) + epsilon < std::min(c.x(), d.x())
        || std::max(c.x(), d.x()) + epsilon < std::min(a.x(), b.x())
        || std::max(a.y(), b.y()) + epsilon < std::min(c.y(), d.y())
        || std::max(c.y(), d.y()) + epsilon < std::min(a.y(), b.y())) return false;
    if (onSegment(a, c, d, epsilon) || onSegment(b, c, d, epsilon)
        || onSegment(c, a, b, epsilon) || onSegment(d, a, b, epsilon)) return true;
    const qreal abC = cross(a, b, c), abD = cross(a, b, d);
    const qreal cdA = cross(c, d, a), cdB = cross(c, d, b);
    return (abC > 0) != (abD > 0) && (cdA > 0) != (cdB > 0);
}

bool contains(const Contour& polygon, QPointF point)
{
    bool inside = false;
    for (int i = 0, previous = int(polygon.size()) - 1; i < polygon.size(); previous = i++) {
        const auto a = polygon[previous], b = polygon[i];
        if ((a.y() > point.y()) != (b.y() > point.y())
            && point.x() < a.x() + (point.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y()))
            inside = !inside;
    }
    return inside;
}

PathGeometryError checkSimple(const Contour& points, qreal epsilon, Budget& budget)
{
    for (int i = 0; i < points.size(); ++i) {
        const int next = (i + 1) % points.size();
        const QPointF incoming = points[i] - points[(i + points.size() - 1) % points.size()];
        const QPointF outgoing = points[next] - points[i];
        if (std::abs(cross(incoming, outgoing)) <= epsilon * std::sqrt(squared(incoming))
            && QPointF::dotProduct(incoming, outgoing) < 0)
            return PathGeometryError::SelfIntersection;
        for (int j = i + 1; j < points.size(); ++j) {
            if (!budget.take()) return PathGeometryError::WorkBudgetExceeded;
            const int otherNext = (j + 1) % points.size();
            if (j == next || otherNext == i) continue;
            if (intersects(points[i], points[next], points[j], points[otherNext], epsilon))
                return PathGeometryError::SelfIntersection;
        }
    }
    return PathGeometryError::None;
}

PathGeometryError checkSeparate(const Contour& a, const Contour& b, qreal epsilon,
                                Budget& budget, bool rejectNested, bool* onlyTouches = nullptr)
{
    bool touched = false;
    if (onlyTouches) *onlyTouches = false;
    for (int i = 0; i < a.size(); ++i) for (int j = 0; j < b.size(); ++j) {
        if (!budget.take()) return PathGeometryError::WorkBudgetExceeded;
        const auto a0 = a[i], a1 = a[(i + 1) % a.size()];
        const auto b0 = b[j], b1 = b[(j + 1) % b.size()];
        if (!intersects(a0, a1, b0, b1, epsilon)) continue;
        if (!onlyTouches) return PathGeometryError::IntersectingContours;
        const bool endpointContact = onSegment(a0, b0, b1, epsilon) || onSegment(a1, b0, b1, epsilon)
            || onSegment(b0, a0, a1, epsilon) || onSegment(b1, a0, a1, epsilon);
        if (!endpointContact) return PathGeometryError::IntersectingContours;
        touched = true;
    }
    if (touched) {
        if (onlyTouches) *onlyTouches = true;
        return PathGeometryError::IntersectingContours;
    }
    if (rejectNested && (contains(a, b.front()) || contains(b, a.front())))
        return PathGeometryError::NestedContours;
    return PathGeometryError::None;
}

bool inTriangle(QPointF point, QPointF a, QPointF b, QPointF c, qreal epsilon)
{
    return cross(a, b, point) >= -epsilon
        && cross(b, c, point) >= -epsilon && cross(c, a, point) >= -epsilon;
}

PathGeometryError triangulate(const Contour& contour, PathMesh& mesh, qreal epsilon, Budget& budget)
{
    const quint32 base = quint32(mesh.vertices.size());
    for (const auto point : contour) mesh.vertices.append({float(point.x()), float(point.y()), 1});
    QVector<int> remaining(contour.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    while (remaining.size() > 3) {
        bool removed = false;
        for (int i = 0; i < remaining.size(); ++i) {
            if (!budget.take()) return PathGeometryError::WorkBudgetExceeded;
            const int a = remaining[(i + remaining.size() - 1) % remaining.size()];
            const int b = remaining[i], c = remaining[(i + 1) % remaining.size()];
            if (cross(contour[a], contour[b], contour[c]) <= epsilon) continue;
            bool occupied = false;
            for (const int candidate : remaining) {
                if (!budget.take()) return PathGeometryError::WorkBudgetExceeded;
                if (candidate != a && candidate != b && candidate != c
                    && inTriangle(contour[candidate], contour[a], contour[b], contour[c], epsilon)) {
                    occupied = true; break;
                }
            }
            if (occupied) continue;
            mesh.indices.append({base + quint32(a), base + quint32(b), base + quint32(c)});
            remaining.removeAt(i); removed = true; break;
        }
        if (!removed) return PathGeometryError::TriangulationFailed;
    }
    if (cross(contour[remaining[0]], contour[remaining[1]], contour[remaining[2]]) <= epsilon)
        return PathGeometryError::TriangulationFailed;
    for (const int index : remaining) mesh.indices.append(base + quint32(index));
    return PathGeometryError::None;
}

// Miter offsets preserve the polygon's correspondence, so fill/fringe nodes
// can share stable topology while positions change. Extreme corners and thin
// features are rejected rather than emitting folded or overlapping triangles.
bool offsetContour(const Contour& points, qreal distance, qreal epsilon, Contour& result)
{
    result.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const QPointF before = points[i] - points[(i + points.size() - 1) % points.size()];
        const QPointF after = points[(i + 1) % points.size()] - points[i];
        const qreal beforeLength = std::sqrt(squared(before)), afterLength = std::sqrt(squared(after));
        const QPointF a(before.y() / beforeLength, -before.x() / beforeLength);
        const QPointF b(after.y() / afterLength, -after.x() / afterLength);
        const QPointF sum = a + b;
        const qreal denominator = QPointF::dotProduct(sum, b);
        if (denominator <= epsilon) return false;
        const QPointF shift = sum * (distance / denominator);
        if (squared(shift) > 16 * distance * distance) return false;
        result.append(points[i] + shift);
    }
    return signedArea(result) > epsilon;
}

void appendFringe(const Contour& inner, const Contour& outer, PathMesh& mesh)
{
    const quint32 base = quint32(mesh.vertices.size());
    for (int i = 0; i < inner.size(); ++i) {
        mesh.vertices.append({float(inner[i].x()), float(inner[i].y()), 1});
        mesh.vertices.append({float(outer[i].x()), float(outer[i].y()), 0});
    }
    for (int i = 0; i < inner.size(); ++i) {
        const quint32 current = base + quint32(2 * i);
        const quint32 next = base + quint32(2 * ((i + 1) % inner.size()));
        mesh.indices.append({current, current + 1, next, next, current + 1, next + 1});
    }
}
PathGeometry buildPathGeometry(const QPainterPath& path, const PathGeometryOptions& options,
                                Budget& budget, bool normalizeTouches)
{
    PathGeometry result;
    const auto fail = [&](PathGeometryError error) {
        result.fill = {}; result.fringe = {}; result.error = error; result.work = budget.used;
        return result;
    };
    if (!std::isfinite(options.devicePixelRatio) || options.devicePixelRatio <= 0 || options.devicePixelRatio > 16
        || !std::isfinite(options.curveTolerancePixels) || options.curveTolerancePixels < 0.0625
        || options.curveTolerancePixels > 1 || options.maximumElements < 1 || options.maximumElements > 4096
        || options.maximumVertices < 3 || options.maximumVertices > 8192 || options.maximumWork < 1
        || options.maximumWork > 64 * 1024 * 1024 || !std::isfinite(options.maximumPhysicalExtent)
        || options.maximumPhysicalExtent <= 0 || options.maximumPhysicalExtent > 65536)
        return fail(PathGeometryError::InvalidOptions);
    if (path.elementCount() > options.maximumElements) return fail(PathGeometryError::ElementBudgetExceeded);
    if (path.isEmpty()) return result;

    for (int i = 0; i < path.elementCount(); ++i) {
        const auto point = path.elementAt(i);
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return fail(PathGeometryError::NonFiniteCoordinate);
        // Float GPU vertices must retain subpixel precision at this position.
        if (std::abs(point.x) * options.devicePixelRatio > options.maximumPhysicalExtent
            || std::abs(point.y) * options.devicePixelRatio > options.maximumPhysicalExtent)
            return fail(PathGeometryError::CoordinateBudgetExceeded);
    }
    // Qt's public flattener uses a 0.5-unit curve tolerance. Flatten in
    // physical/error-scaled space, then restore the caller's logical units.
    const qreal scale = options.devicePixelRatio * 0.5 / options.curveTolerancePixels;
    const auto polygons = path.toSubpathPolygons(QTransform::fromScale(scale, scale));
    const qreal epsilon = 0.00001 / options.devicePixelRatio;
    const qreal areaEpsilon = epsilon * epsilon;
    QVector<Contour> contours;
    bool needsNormalization = false;
    for (const auto& polygon : polygons) {
        Contour contour;
        contour.reserve(std::min(polygon.size(), qsizetype(options.maximumVertices)));
        for (const auto scaled : polygon) {
            const QPointF point = scaled / scale;
            if (contour.isEmpty() || squared(point - contour.back()) > areaEpsilon) contour.append(point);
            if (result.flattenedVertexCount + contour.size() > options.maximumVertices + 1)
                return fail(PathGeometryError::VertexBudgetExceeded);
        }
        if (contour.size() > 1 && squared(contour.front() - contour.back()) <= areaEpsilon) contour.removeLast();
        // Straight subdivisions carry no shape and make ear clipping costly.
        bool changed = true;
        while (changed && contour.size() >= 3) {
            changed = false;
            for (int i = 0; i < contour.size(); ++i) {
                if (!budget.take()) return fail(PathGeometryError::WorkBudgetExceeded);
                const auto previous = contour[(i + contour.size() - 1) % contour.size()];
                const auto next = contour[(i + 1) % contour.size()];
                if (onSegment(contour[i], previous, next, epsilon)) {
                    contour.removeAt(i); changed = true; break;
                }
            }
        }
        if (contour.size() < 3) return fail(PathGeometryError::DegenerateContour);
        result.flattenedVertexCount += int(contour.size());
        if (result.flattenedVertexCount > options.maximumVertices) return fail(PathGeometryError::VertexBudgetExceeded);
        auto error = checkSimple(contour, epsilon, budget);
        if (error != PathGeometryError::None) return fail(error);
        const qreal area = signedArea(contour);
        if (std::abs(area) <= areaEpsilon) return fail(PathGeometryError::DegenerateContour);
        if (area < 0) std::reverse(contour.begin(), contour.end());
        for (const auto& previous : contours) {
            bool onlyTouches = false;
            error = checkSeparate(previous, contour, epsilon, budget, true, &onlyTouches);
            if (error == PathGeometryError::IntersectingContours && onlyTouches && normalizeTouches) {
                needsNormalization = true; continue;
            }
            if (error != PathGeometryError::None) return fail(error);
        }
        result.area += std::abs(area);
        contours.append(std::move(contour));
    }
    if (needsNormalization) {
        // Surface unions can retain subpaths sharing an exact boundary at an
        // intermediate join value. Simplification applies the original fill
        // rule once; it does not turn an arbitrary crossing into a solid fill.
        const QPainterPath normalized = path.simplified();
        return buildPathGeometry(normalized, options, budget, false);
    }
    result.contourCount = int(contours.size());
    QVector<Contour> expandedContours;
    for (const auto& contour : contours) {
        Contour inner, outer;
        if (options.antialiasing) {
            const qreal halfPixel = 0.5 / options.devicePixelRatio;
            if (!offsetContour(contour, -halfPixel, epsilon, inner)
                || !offsetContour(contour, halfPixel, epsilon, outer)) return fail(PathGeometryError::AntialiasingTopology);
            auto error = checkSimple(inner, epsilon, budget);
            if (error == PathGeometryError::WorkBudgetExceeded) return fail(error);
            if (error != PathGeometryError::None) return fail(PathGeometryError::AntialiasingTopology);
            error = checkSimple(outer, epsilon, budget);
            if (error == PathGeometryError::WorkBudgetExceeded) return fail(error);
            if (error != PathGeometryError::None) return fail(PathGeometryError::AntialiasingTopology);
            error = checkSeparate(inner, outer, epsilon, budget, false);
            if (error == PathGeometryError::WorkBudgetExceeded) return fail(error);
            if (error != PathGeometryError::None || !contains(outer, inner.front())
                || signedArea(inner) >= signedArea(contour) || signedArea(outer) <= signedArea(contour))
                return fail(PathGeometryError::AntialiasingTopology);
            for (int i = 0; i < inner.size(); ++i) {
                const int next = (i + 1) % inner.size();
                if (cross(inner[i], outer[i], inner[next]) <= areaEpsilon
                    || cross(inner[next], outer[i], outer[next]) <= areaEpsilon)
                    return fail(PathGeometryError::AntialiasingTopology);
            }
            for (const auto& previous : expandedContours) {
                error = checkSeparate(previous, outer, epsilon, budget, true);
                if (error == PathGeometryError::WorkBudgetExceeded) return fail(error);
                if (error != PathGeometryError::None) return fail(PathGeometryError::AntialiasingTopology);
            }
            expandedContours.append(outer);
            appendFringe(inner, outer, result.fringe);
        }
        const auto error = triangulate(options.antialiasing ? inner : contour, result.fill, areaEpsilon, budget);
        if (error != PathGeometryError::None) return fail(error);
    }
    result.work = budget.used;
    return result;
}
} // namespace

PathGeometry pathGeometry(const QPainterPath& path, const PathGeometryOptions& options)
{
    Budget budget{options.maximumWork};
    return buildPathGeometry(path, options, budget, true);
}

bool PathGeometryCache::update(const QPainterPath& path, const PathGeometryOptions& options)
{
    if (hasInput_ && path == path_ && options == options_) return false;
    geometry_ = pathGeometry(path, options); ++generation_;
    // Ordinary rejected contours are still bounded and should not be
    // tessellated again every frame. Oversized sources are never retained.
    hasInput_ = path.elementCount() <= 4096;
    path_ = hasInput_ ? path : QPainterPath{}; options_ = options;
    return true;
}

} // namespace window_system::quick
