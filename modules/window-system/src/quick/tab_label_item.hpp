// SPDX-License-Identifier: MIT
#pragma once

#include "path_geometry.hpp"
#include "style/tab_label_content.hpp"
#include "style/tab_label_presentation.hpp"

#include <QQuickItem>

namespace window_system::quick {

class PathItem;
class TabLabelTextItem;

// One retained Quick label: its filled surface and native title share the
// presentation pose. The parent strip owns placement and the surrounding
// group shadows; this item owns neither.
class TabLabelItem final : public QQuickItem {
public:
    explicit TabLabelItem(QQuickItem* parent = nullptr);

    bool setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style);
    void clearLabel();
    [[nodiscard]] PathGeometryError surfaceError() const;
    [[nodiscard]] const TabLabelTextItem* textItem() const;
    [[nodiscard]] bool surfaceVisible() const;

private:
    PathItem* surface_ = nullptr;
    TabLabelTextItem* text_ = nullptr;
};

} // namespace window_system::quick
