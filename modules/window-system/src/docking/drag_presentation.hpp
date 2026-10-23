// SPDX-License-Identifier: MIT
#pragma once
#include <window_system/theme.hpp>
#include <window_system/layout_model.hpp>
#include <window_system/tab_appearance.hpp>
#include <QFrame>
#include <QRegion>
#include <QPixmap>
#include <optional>

namespace window_system {
// Only the small label is cached. Live content stays in its original tab until
// the pointer first leaves the source strip.
class DragLabelPreview final : public QWidget {
public:
    explicit DragLabelPreview(QPixmap label);
    void follow(QPoint global);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QPixmap label_;
};
// A transient view of the same live DockSurface. It owns only its decorations;
// the caller reparents the surface out before deleting this host.
class EdgeDragPresentation final : public QFrame {
public:
    EdgeDragPresentation(QString title, Edge edge, const Theme& theme, QWidget* content, QWidget* parent);
    [[nodiscard]] QRegion visibleRegion() const;
    [[nodiscard]] QPoint labelAnchor() const;
    void configureLabel(std::optional<TabColorPreset> preset, QIcon icon, int number,
                        const TabAppearance& appearance = {});
private:
    QWidget* label_;
    QWidget* panel_;
};
} // namespace window_system
