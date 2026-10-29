// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_label_content.hpp"
#include "style/tab_label_presentation.hpp"

#include <QQuickItem>
#include <array>
#include <tuple>

namespace window_system::quick {

class PathItem;
class TabIconItem;
class TabLabelTextItem;
class AnchorShadowItem;
class BodyShadowItem;
class MaskedItem;
class TabCoverageItem;

// One retained Quick label. All label layers share the published pose;
// elevationManaged leaves the joined shadow to the surrounding group.
class TabLabelItem final : public QQuickItem {
public:
    explicit TabLabelItem(QQuickItem* parent = nullptr);

    bool setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style);
    void clearLabel();
    [[nodiscard]] const TabLabelTextItem* textItem() const;
    [[nodiscard]] const TabLabelTextItem* numberItem() const;
    [[nodiscard]] quint64 iconTextureUploads() const;

private:
    bool syncSurface(const TabLabelPresentation& label, const TabLabelPresentation& local,
                     const TabStripLabelStyle& style, bool mainIconVisible);
    void syncElevation(const TabLabelPresentation& label, const TabLabelPresentation& local,
                       const TabStripLabelStyle& style);
    bool syncAction(const TabLabelPresentation& label, const TabLabelPresentation& local,
                    const TabStripLabelStyle& style);
    std::tuple<QSize, TabShape, int, int, bool, bool> outlineKey_;
    QPainterPath outlinePath_;
    QPainterPath clipContour_;
    std::array<QRect, 4> contentBounds_;
    bool needsContentClip_ = false;
    AnchorShadowItem* shadow_ = nullptr;
    BodyShadowItem* border_ = nullptr;
    PathItem* surface_ = nullptr;
    MaskedItem* content_ = nullptr;
    TabCoverageItem* coverage_ = nullptr;
    PathItem* actionBackground_ = nullptr;
    TabLabelTextItem* text_ = nullptr;
    TabLabelTextItem* numberText_ = nullptr;
    TabIconItem* mainIcon_ = nullptr;
    TabIconItem* actionIcon_ = nullptr;
};

} // namespace window_system::quick
