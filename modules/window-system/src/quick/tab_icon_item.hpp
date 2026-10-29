// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QIcon>
#include <QQuickItem>
#include <QSize>
#include <atomic>
#include <memory>

namespace window_system::quick {

// Owns one GUI-prepared icon image and its texture for the current window.
// Icon engines stay on the GUI thread; scene-graph recreation is detected by
// replacing the texture with the currently retained image.
class TabIconItem final : public QQuickItem {
public:
    explicit TabIconItem(QQuickItem* parent = nullptr);
    ~TabIconItem() override;

    bool setIcon(QIcon icon, QSize logicalSize, QColor tint, bool enabled, bool selected);
    void clearIcon();
    [[nodiscard]] quint64 textureUploads() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    struct Prepared;
    void prepare();
    QIcon icon_;
    QColor tint_;
    QSize logicalSize_;
    bool enabled_ = true;
    bool selected_ = false;
    quint64 resourceRevision_ = 0;
    std::unique_ptr<Prepared> prepared_;
    std::atomic<quint64> textureUploads_{0};
};

} // namespace window_system::quick
