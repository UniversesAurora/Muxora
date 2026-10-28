// SPDX-License-Identifier: MIT
#pragma once

#include "tab_painter.hpp"
#include <QHash>
#include <QUuid>
#include <memory>

namespace window_system {

struct TabRetiredLabel {
    TabLabelPresentation label;
    qreal visibility = 1;
};

struct TabLabelVisual {
    QRect global;
    TabShape shape;
    bool sticky;
    bool selected;
    bool iconOnly;
    TabParts parts;
    std::shared_ptr<const TabRetiredLabel> retired;
    bool singleTileRow = false;
    std::optional<QRect> collapse;
};
using TabVisualState = QHash<QUuid, TabLabelVisual>;

struct TabDepartureSample {
    QUuid id;
    TabLabelPresentation label;
};

// Global transition records are sampled into the receiving plane's coordinates.
// Neither records nor samples retain a source control or its text cache.
void sampleTabDepartures(const TabVisualState& labels, QPoint globalOrigin, qreal visibility,
                        const TabPainter& style, QVector<TabDepartureSample>& samples);

// GUI-thread raster adaptation; text caches live only as long as their labels
// remain in the current departure plane, independently of source lifetime.
class TabDeparturePainter final {
public:
    void paint(QPainter& painter, const TabPainter& style, const QVector<TabDepartureSample>& samples);
    [[nodiscard]] qsizetype retainedLabelCount() const { return texts_.size(); }
    [[nodiscard]] quint64 shapingCount() const { return shapingCount_; }
private:
    struct Text { TabTitleLayout layout; quint64 used = 0; };
    QHash<QUuid, Text> texts_;
    quint64 pass_ = 0;
    quint64 shapingCount_ = 0;
};

} // namespace window_system
