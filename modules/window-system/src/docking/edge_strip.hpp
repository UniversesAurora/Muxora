// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QSet>
#include <functional>
#include <optional>

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
    void setDraggedTab(TabId tab);
    [[nodiscard]] QRect insertionPreviewRect() const;
    void setDragHover(QPoint global);
    [[nodiscard]] int scrollOffset() const;
    void restoreScrollOffset(int offset);
    // The tab a rebuilt strip keeps in place when label lengths change: the
    // checked tab while fully shown, otherwise the tab at the leading edge.
    struct ScrollAnchor {
        int offset = 0;
        TabId tab;
        int shift = 0;
        bool active = false;
    };
    [[nodiscard]] ScrollAnchor scrollAnchor() const;
    void restoreScrollAnchor(const ScrollAnchor& anchor);
    void scrollByWheel(QPoint delta);
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
    void synchronizeScrollBar();
    void updateScrollBarVisibility();
    void queueTrackLayout();
    void queueHoverGeometryChanged();
    void observe(QWidget* widget);
    void applyPendingScrollOffset();

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
    QPointer<QAbstractButton> draggedButton_;
    QSet<QObject*> hoveredSurfaces_;
    int previewIndex_ = -1;
    int previewExtent_ = 0;
    int scrollDirection_ = 0;
    bool arranging_ = false;
    bool hoverNotificationQueued_ = false;
    bool trackLayoutQueued_ = false;
    std::optional<int> pendingScrollOffset_;
};

} // namespace window_system
