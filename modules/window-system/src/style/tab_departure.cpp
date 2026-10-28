// SPDX-License-Identifier: MIT
#include "tab_departure.hpp"
#include "tab_painter.hpp"
#include "label_presence.hpp"
#include <QPainter>

namespace window_system {

void sampleTabDepartures(const TabVisualState& labels, QPoint globalOrigin, qreal visibility,
                        QVector<TabDepartureSample>& samples)
{
    samples.clear();
    for (auto it = labels.cbegin(); it != labels.cend(); ++it) {
        const auto& label = it.value();
        if (!label.retired) continue;
        const auto& retired = *label.retired;
        auto presentation = retired.label;
        if (presentation.surface) presentation.surface = translatedSurfaceGeometry(*presentation.surface, -globalOrigin);
        presentation.state.elevationManaged = false;
        presentation.bodyElevation = elevationProfile(ElevationLevel::Flat);
        auto& parts = presentation.parts;
        parts = label.parts;
        for (auto* part : {&parts.tab, &parts.icon, &parts.number, &parts.title, &parts.action}) part->translate(-globalOrigin);
        if (label.collapse) {
            const auto area = planarLabelRect(label.global, *label.collapse, visibility).translated(-globalOrigin);
            if (area.isEmpty()) continue;
            TabLabelParts sizes; sizes.icon = parts.icon.width();
            parts = layoutTabParts(area, presentation.state.shape, sizes, true, false, false, Qt::LeftToRight);
            presentation.surface.reset(); presentation.state.elevationManaged = true; presentation.state.lifted = false;
        } else presentation.presence = labelPresencePose(parts.tab, visibility * retired.visibility);
        samples.append({it.key(), std::move(presentation)});
    }
}

void TabDeparturePainter::paint(QPainter& painter, const TabPainter& style, const QVector<TabDepartureSample>& samples)
{
    ++pass_;
    for (const auto& sample : samples) {
        auto& text = texts_[sample.id];
        text.used = pass_;
        text.layout.prepare(sample.label.state.title, sample.label.font, sample.label.direction, true, painter.device());
        const auto before = text.layout.shapingCount();
        style.paintLabel(painter, sample.label, &text.layout);
        shapingCount_ += text.layout.shapingCount() - before;
    }
    for (auto it = texts_.begin(); it != texts_.end();)
        if (it->used != pass_) it = texts_.erase(it);
        else ++it;
}

} // namespace window_system
