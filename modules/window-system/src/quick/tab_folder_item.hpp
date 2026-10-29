// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_label_content.hpp"
#include "style/tab_painter.hpp"

#include <QQuickItem>

namespace window_system::quick {

class PathItem;
class TabIconItem;
class TabLabelTextItem;

// Retained Quick rendering for one horizontal folder chip or vertical folder
// header. The parent strip owns placement and clipping.
class TabFolderItem final : public QQuickItem {
public:
    explicit TabFolderItem(QQuickItem* parent = nullptr);

    bool setFolder(const TabFolderPaintState& state, const TabStripLabelStyle& style, bool vertical);
    void clearFolder();
    [[nodiscard]] bool addButtonVisible() const;

private:
    void setText(TabLabelTextItem* item, const QRect& area, const QString& text,
                 const QFont& font, const QColor& color, const TabStripLabelStyle& style);
    PathItem* background_ = nullptr;
    PathItem* dot_ = nullptr;
    PathItem* addBackground_ = nullptr;
    PathItem* chevron_ = nullptr;
    TabIconItem* addIcon_ = nullptr;
    TabLabelTextItem* label_ = nullptr;
    TabLabelTextItem* name_ = nullptr;
    TabLabelTextItem* count_ = nullptr;
};

} // namespace window_system::quick
