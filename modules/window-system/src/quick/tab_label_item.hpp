// SPDX-License-Identifier: MIT
#pragma once

#include "path_geometry.hpp"
#include "style/tab_label_content.hpp"
#include "style/tab_label_presentation.hpp"

#include <QQuickItem>

namespace window_system::quick {

class PathItem;
class TabIconItem;
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
    [[nodiscard]] const TabLabelTextItem* numberItem() const;
    [[nodiscard]] bool surfaceVisible() const;
    [[nodiscard]] bool mainIconVisible() const;
    [[nodiscard]] bool actionVisible() const;
    [[nodiscard]] quint64 iconTextureUploads() const;

private:
    PathItem* surface_ = nullptr;
    PathItem* actionBackground_ = nullptr;
    TabLabelTextItem* text_ = nullptr;
    TabLabelTextItem* numberText_ = nullptr;
    TabIconItem* mainIcon_ = nullptr;
    TabIconItem* actionIcon_ = nullptr;
};

} // namespace window_system::quick
