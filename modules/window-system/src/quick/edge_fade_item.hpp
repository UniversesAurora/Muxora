// SPDX-License-Identifier: MIT
#pragma once

#include "style/edge_fade.hpp"

#include <QImage>
#include <QQuickItem>
#include <atomic>
#include <memory>

namespace window_system::quick {

// Blends scrolling lane content into the strip background with a retained
// bounded gradient texture. The image changes only with geometry, ramp or
// background color; scrolling moves the item without another upload.
class EdgeFadeItem final : public QQuickItem {
public:
    explicit EdgeFadeItem(QQuickItem* parent = nullptr);
    ~EdgeFadeItem() override;

    bool setFade(const EdgeFade& fade, const QColor& background);
    void clearFade();
    [[nodiscard]] quint64 textureUploads() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    struct Prepared;
    void prepareImage();
    EdgeFade fade_;
    QColor background_;
    quint64 resourceRevision_ = 0;
    std::unique_ptr<Prepared> prepared_;
    std::atomic<quint64> textureUploads_{0};
};

} // namespace window_system::quick
