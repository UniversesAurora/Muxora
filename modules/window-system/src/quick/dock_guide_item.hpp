// SPDX-License-Identifier: MIT
#pragma once

#include "docking/drop_target.hpp"

#include <QQuickItem>

namespace window_system::quick {

class PathItem;
class PathStrokeItem;
class TabLabelTextItem;

// The drag's guide squares and drop preview over one window's dock, with
// the look of the Widgets overlay. Fills, outlines and marks each take one
// path for all guides, so a pointer move rebuilds a few paths at most.
class DockGuideItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit DockGuideItem(QQuickItem* parent = nullptr);
    // Global coordinates; a target without a window hides everything.
    void setTarget(const DropTarget& target, int count, bool whole);
    void clear();

private:
    PathItem* previewFill_;
    PathStrokeItem* previewBorder_;
    PathItem* guideFill_;
    PathItem* selectedFill_;
    PathStrokeItem* guideBorder_;
    PathItem* marks_;
    PathItem* captionFill_;
    TabLabelTextItem* caption_;
};

} // namespace window_system::quick
