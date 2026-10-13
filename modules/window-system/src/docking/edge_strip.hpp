// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QSet>
#include <functional>

class QAbstractButton;
class QBoxLayout;
class QScrollArea;
class QScrollBar;
class QTimer;

namespace window_system {

// A presentation-only strip: real buttons retain their identity and order;
// scrolling and an insertion gap never add a tab to the workspace model.
class EdgeStrip final : public QFrame {
public:
    EdgeStrip(Edge edge, const Theme& theme, QWidget* parent = nullptr);
    ~EdgeStrip() override;

    void addButton(QAbstractButton* button);
    void setTailWidget(QWidget* widget);
    [[nodiscard]] QRect viewportRectGlobal() const;
    [[nodiscard]] int insertionIndex(QPoint global) const;
    [[nodiscard]] QRect insertionMarkerRect(int index) const;
    void setInsertionPreview(int index, int extent);
    [[nodiscard]] QRect insertionPreviewRect() const;
    void setDragHover(QPoint global);
    std::function<void()> hoverGeometryChanged;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    [[nodiscard]] QScrollBar* axisScrollBar() const;
    [[nodiscard]] int axis(QPoint point) const;
    void layoutTrack();
    void updateScrollBarGeometry();
    void updateScrollBarVisibility();
    void queueHoverGeometryChanged();
    void observe(QWidget* widget);

    bool vertical_;
    Theme theme_;
    QScrollArea* scroll_;
    QWidget* track_;
    QBoxLayout* trackLayout_;
    QScrollBar* scrollbar_;
    QWidget* gap_;
    QTimer* autoScroll_;
    QList<QPointer<QAbstractButton>> buttons_;
    QPointer<QWidget> tail_;
    QSet<QObject*> hoveredSurfaces_;
    int previewIndex_ = -1;
    int previewExtent_ = 0;
    int scrollDirection_ = 0;
    bool arranging_ = false;
    bool hoverNotificationQueued_ = false;
};

} // namespace window_system
