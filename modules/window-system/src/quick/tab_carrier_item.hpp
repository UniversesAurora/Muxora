// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_drag_visual.hpp"
#include "style/tab_label_content.hpp"
#include <QHash>
#include <QQuickItem>
#include <vector>

namespace window_system::quick {

class TabLabelItem;

// Consumes an owned external drag descriptor. Piece placement and styles are
// independent, while stable label IDs retain text and icon resources on moves.
class TabCarrierItem final : public QQuickItem {
public:
    explicit TabCarrierItem(QQuickItem* parent = nullptr);
    bool setVisual(const TabDragVisual& visual, const SurfaceGeometry* geometry = nullptr);
    void clearVisual();
    [[nodiscard]] qsizetype labelCount() const;

private:
    struct Piece {
        QQuickItem* item = nullptr;
        std::shared_ptr<const TabCarrierLabels> content;
        TabStripLabelStyle style;
    };
    struct Label {
        TabLabelItem* item = nullptr;
        quint64 used = 0;
    };
    void resizePieces(qsizetype count);
    bool syncPiece(Piece& target, const TabCarrierPiece& source, const SurfaceGeometry* geometry);
    std::vector<Piece> pieces_;
    QHash<QUuid, Label> labels_;
    quint64 pass_ = 0;
};

} // namespace window_system::quick
