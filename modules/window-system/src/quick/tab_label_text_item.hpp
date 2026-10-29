// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_label_content.hpp"
#include "style/tab_label_presentation.hpp"
#include "style/tab_label_text.hpp"

#include <QQuickItem>
#include <atomic>
#include <memory>

namespace window_system::quick {

enum class TabTextRole { Title, Number, Literal };
enum class TabTextError { None, SourceBudgetExceeded, SliceBudgetExceeded, GlyphBudgetExceeded, PixelBudgetExceeded };

struct TabLabelTextStats {
    quint64 sceneGraphBuilds = 0;
    quint64 textLayoutBuilds = 0;
    quint64 clipUpdates = 0;
    quint64 nativeTextBuilds = 0;
};

// The GUI side shapes complete sources and bounds the native glyph work,
// including its fade-slice multiplier. The scene graph owns window-local
// native glyph caches. Opacity and viewport changes reuse the source shaping.
class TabLabelTextItem final : public QQuickItem {
public:
    explicit TabLabelTextItem(QQuickItem* parent = nullptr);
    ~TabLabelTextItem() override;

    bool setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style,
                  TabTextRole role = TabTextRole::Title);
    void clearLabel();
    [[nodiscard]] TabTextError textError() const;
    [[nodiscard]] TabLabelTextStats renderStats() const;
    [[nodiscard]] qsizetype segmentCount() const;
    [[nodiscard]] qsizetype retainedGlyphInstances() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    struct Prepared;
    std::unique_ptr<Prepared> prepared_;
    bool hasFrame_ = false;
    quint64 resourceRevision_ = 0;
    TabTextError error_ = TabTextError::None;
    std::atomic<quint64> sceneGraphBuilds_{0};
    std::atomic<quint64> textLayoutBuilds_{0};
    std::atomic<quint64> clipUpdates_{0};
    std::atomic<quint64> nativeTextBuilds_{0};
};

} // namespace window_system::quick
