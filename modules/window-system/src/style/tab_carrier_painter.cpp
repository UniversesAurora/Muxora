// SPDX-License-Identifier: MIT
#include "tab_carrier_painter.hpp"

#include "docking/tab_drag_visual.hpp"
#include "tab_painter.hpp"
#include <QHash>
#include <QPainter>
#include <algorithm>
#include <vector>

namespace window_system {

struct TabCarrierPainter::Cache {
    struct Piece {
        std::shared_ptr<const TabCarrierLabels> content;
        std::unique_ptr<TabPainter> painter;
    };
    struct Text {
        TabTitleLayout layout;
        quint64 used = 0;
    };
    std::vector<Piece> pieces;
    QHash<QUuid, Text> texts;
    quint64 pass = 0;
    quint64 shapingCount = 0;
};

TabCarrierPainter::TabCarrierPainter() = default;
TabCarrierPainter::~TabCarrierPainter() = default;

void TabCarrierPainter::paint(QPainter& painter, const TabDragVisual& visual, const SurfaceGeometry* geometry)
{
    if (visual.empty()) { clear(); return; }
    if (!cache_) cache_ = std::make_unique<Cache>();
    ++cache_->pass;
    cache_->pieces.resize(size_t(visual.pieces.size()));
    for (qsizetype pieceIndex = 0; pieceIndex < visual.pieces.size(); ++pieceIndex) {
        const auto& piece = visual.pieces.at(pieceIndex);
        auto& cached = cache_->pieces[size_t(pieceIndex)];
        if (!piece.frame || !piece.frame->content) {
            cached.content.reset(); cached.painter.reset(); continue;
        }
        const auto& frame = *piece.frame;
        const auto& content = *frame.content;
        if (cached.content != frame.content) {
            cached.content = frame.content;
            cached.painter = std::make_unique<TabPainter>(content.metrics, content.palette);
        }
        const auto localGeometry = geometry ? translatedSurfaceGeometry(*geometry, -piece.offset) : SurfaceGeometry{};
        painter.save(); painter.translate(piece.offset);
        const auto count = std::min(content.labels.size(), frame.samples.size());
        for (qsizetype index = 0; index < count; ++index) {
            const auto& source = content.labels.at(index);
            const auto& sample = frame.samples.at(index);
            auto label = source.label;
            label.parts = sample.parts;
            label.state.titleOpacity = sample.titleOpacity;
            label.state.actionOpacity = sample.actionOpacity;
            label.state.lifted = geometry != nullptr;
            label.state.elevationManaged = true;
            label.surface = label.state.selected && geometry ? std::optional{localGeometry} : std::nullopt;
            auto& text = cache_->texts[source.id];
            text.used = cache_->pass;
            text.layout.prepare(label.state.title, label.font, label.direction, true, painter.device());
            const auto before = text.layout.shapingCount();
            cached.painter->paintLabel(painter, label, &text.layout);
            cache_->shapingCount += text.layout.shapingCount() - before;
        }
        painter.restore();
    }
    for (auto it = cache_->texts.begin(); it != cache_->texts.end();)
        if (it->used != cache_->pass) it = cache_->texts.erase(it);
        else ++it;
}

void TabCarrierPainter::clear()
{
    cache_.reset();
}

TabCarrierCacheStats TabCarrierPainter::cacheStats() const
{
    if (!cache_) return {};
    qsizetype pieces = 0;
    for (const auto& piece : cache_->pieces) if (piece.painter) ++pieces;
    return {pieces, cache_->texts.size(), cache_->shapingCount};
}

} // namespace window_system
