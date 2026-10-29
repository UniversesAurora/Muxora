// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_label_content.hpp"
#include "style/tab_label_presentation.hpp"
#include "style/tab_label_text.hpp"

#include <QQuickItem>
#include <atomic>
#include <memory>

namespace window_system::quick {

struct TabLabelTextStats {
    quint64 sceneGraphBuilds = 0;
    quint64 textLayoutBuilds = 0;
    quint64 clipUpdates = 0;
};

// Retained Qt Quick text for one tab title. The GUI side owns the immutable
// label values and bounded fade slices; the scene graph owns native glyph
// caches and rebuilds them only when the shaped source or origin changes.
class TabLabelTextItem final : public QQuickItem {
public:
    explicit TabLabelTextItem(QQuickItem* parent = nullptr);
    ~TabLabelTextItem() override;

    bool setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style);
    void clearLabel();
    [[nodiscard]] TabLabelTextStats renderStats() const;
    [[nodiscard]] qsizetype segmentCount() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    struct Prepared;
    std::unique_ptr<Prepared> prepared_;
    bool hasFrame_ = false;
    quint64 layoutRevision_ = 0;
    std::atomic<quint64> sceneGraphBuilds_{0};
    std::atomic<quint64> textLayoutBuilds_{0};
    std::atomic<quint64> clipUpdates_{0};
};

} // namespace window_system::quick
