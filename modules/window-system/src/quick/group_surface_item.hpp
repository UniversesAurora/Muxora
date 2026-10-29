// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_group_surface.hpp"
#include "path_geometry.hpp"
#include "shadow_stats.hpp"
#include <QQuickItem>
#include <atomic>
#include <memory>

namespace window_system::quick {

// The GUI thread prepares the existing backplate's bounded resources. The
// owning window's scene graph retains textures, clips and presence transforms.
class GroupSurfaceItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit GroupSurfaceItem(QQuickItem* parent = nullptr);
    ~GroupSurfaceItem() override;
    bool setFrame(const TabGroupSurfaceFrame& frame);
    [[nodiscard]] ShadowRenderStats renderStats() const;
    [[nodiscard]] PathGeometryError geometryError() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    struct Prepared;
    bool prepareFrame();
    void clearPrepared(PathGeometryError error);
    bool rejectFrame(PathGeometryError error);
    TabGroupSurfaceFrame frame_;
    ElevationRenderer renderer_;
    std::unique_ptr<Prepared> prepared_;
    bool hasFrame_ = false;
    std::atomic<quint64> textureUploads_{0};
    std::atomic<quint64> imageBytesUploaded_{0};
    std::atomic<quint64> geometryUpdates_{0};
};

} // namespace window_system::quick
