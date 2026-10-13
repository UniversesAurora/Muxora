// SPDX-License-Identifier: MIT
#pragma once
#include <window_system/theme.hpp>
#include <window_system/layout_model.hpp>
#include <QFrame>
#include <QRegion>

namespace window_system {
// A transient view of the same live DockSurface. It owns only its decorations;
// the caller reparents the surface out before deleting this host.
class EdgeDragPresentation final : public QFrame {
public:
    EdgeDragPresentation(QString title, Edge edge, const Theme& theme, QWidget* content, QWidget* parent);
    [[nodiscard]] QRegion visibleRegion() const;
    [[nodiscard]] QPoint labelAnchor() const;
private:
    QWidget* label_;
    QWidget* panel_;
};
} // namespace window_system
