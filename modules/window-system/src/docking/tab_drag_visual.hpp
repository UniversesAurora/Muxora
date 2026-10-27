// SPDX-License-Identifier: MIT
#pragma once

#include "drag_start.hpp"
#include "style/tab_label_presentation.hpp"
#include "style/tab_visual_style.hpp"
#include <QUuid>
#include <QVector>
#include <memory>

namespace window_system {

struct TabCarrierLabel {
    QUuid id;
    TabLabelPresentation label;
};

// Content changes independently of geometry and opacity. Published frames
// share this immutable data; neither renderer stores a source widget here.
struct TabCarrierLabels {
    TabMetrics metrics;
    TabPalette palette;
    QVector<TabCarrierLabel> labels;
};

struct TabCarrierSample {
    TabParts parts;
    qreal titleOpacity = 1;
    qreal actionOpacity = 1;
};

struct TabCarrierFrame {
    std::shared_ptr<const TabCarrierLabels> content;
    QVector<TabCarrierSample> samples;
};

struct TabCarrierPiece {
    QPoint offset;
    std::shared_ptr<const TabCarrierFrame> frame;
};

// A GUI-produced descriptor. Icons still require GUI-thread resource
// preparation before a Quick scene graph can consume them. No renderer calls
// back into the source to paint, measure text or query release positions.
struct TabDragVisual {
    QSize size;
    QPoint anchor;
    QRect active;
    QUuid identity;
    std::optional<DragAnchor> sourceAnchor;
    QVector<TabCarrierPiece> pieces;

    [[nodiscard]] bool empty() const { return pieces.isEmpty(); }
};

} // namespace window_system
