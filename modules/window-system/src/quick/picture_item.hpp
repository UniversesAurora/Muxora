// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QImage>
#include <QQuickItem>

namespace window_system::quick {

// A still picture of content, as the Widgets content preview draws it (tab
// strip design 6.4): at its own size from the top left, the rest of the
// item filled with the picture's bottom-right color. A null picture draws
// nothing.
class PictureItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit PictureItem(QQuickItem* parent = nullptr);
    void setPicture(QImage picture);
    [[nodiscard]] const QImage& picture() const { return picture_; }

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& now, const QRectF& previous) override;

private:
    QImage picture_;
    QColor fill_;
    bool pictureChanged_ = false;
};

} // namespace window_system::quick
