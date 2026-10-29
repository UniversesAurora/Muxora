// SPDX-License-Identifier: MIT
#pragma once

#include "path_geometry.hpp"
#include <QColor>
#include <QQuickItem>

namespace window_system::quick {

enum class PathAntialiasing { Coverage, Multisample };

// Internal presentation primitive. The GUI thread owns the path and prepares
// its bounded mesh; the scene graph owns the uploaded geometry and material.
// No widget, raster image or render-thread callback enters this interface.
class PathItem final : public QQuickItem {
    Q_OBJECT
public:
    // Multisample mode requires a window configured with MSAA before native
    // creation. It is explicit: a rejected coverage mesh never changes mode.
    explicit PathItem(QQuickItem* parent = nullptr, PathAntialiasing mode = PathAntialiasing::Coverage);
    bool setPath(const QPainterPath& path);
    void setAntialiasingMode(PathAntialiasing mode);
    void setFill(const QColor& fill);
    [[nodiscard]] PathGeometryError geometryError() const { return cache_.geometry().error; }

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    bool prepare(const QPainterPath& path);
    QPainterPath path_;
    PathGeometryCache cache_;
    QColor fill_ = Qt::white;
    PathAntialiasing antialiasing_;
};

} // namespace window_system::quick
