// SPDX-License-Identifier: MIT
#pragma once

#include "style/edge_fade.hpp"
#include <QQuickItem>

namespace window_system::quick {

class PathItem;

// A GPU layer is allocated only when the content needs an alpha mask. The
// content remains ordinary retained Quick items; no CPU image/readback is used.
class MaskedItem final : public QQuickItem {
public:
    explicit MaskedItem(QQuickItem* parent = nullptr);
    [[nodiscard]] QQuickItem* contentItem() const;
    bool setFades(const QVector<EdgeFade>& fades);
    bool setContour(const QPainterPath& contour);
    void clearContour();

protected:
    void geometryChange(const QRectF& next, const QRectF& previous) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    bool updateMaskSize();
    QQuickItem* layer_ = nullptr;
    PathItem* contour_ = nullptr;
    QPainterPath contourPath_;
    bool contourValid_ = true;
    bool fadesValid_ = true;
};

} // namespace window_system::quick
