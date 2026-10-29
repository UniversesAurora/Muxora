// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_foreground_frame.hpp"
#include <array>

namespace window_system::benchmarks {

// Static metadata and two writable publication buffers for the tab scenes.
// The renderer may retain the current frame; the next sample uses the other.
class TabBenchmarkFrames final {
public:
    enum class Scene { Strip, Foreground };
    static constexpr int foregroundLabels = 28;
    static constexpr int incomingPieces = 2;

    TabBenchmarkFrames(const QColor& background, Scene scene);
    std::shared_ptr<const TabStripFrame> stripFrame(qreal progress);
    std::shared_ptr<const TabForegroundFrame> foregroundFrame(qreal progress);

private:
    struct ForegroundBuffer {
        std::shared_ptr<TabForegroundFrame> frame;
        std::array<std::shared_ptr<TabCarrierFrame>, incomingPieces> carriers;
    };
    void initializeStrip();
    void initializeForeground();
    void initializeIncoming(ForegroundBuffer& buffer, const SurfaceGeometry& geometry);
    void initializeLocalLabels(TabForegroundFrame& frame);
    std::shared_ptr<const TabStripLabelStyle> style_;
    QVector<TabStripLabelSample> labels_;
    QUuid folderId_ = QUuid::createUuid();
    std::array<std::shared_ptr<const TabCarrierLabels>, incomingPieces> carrierContent_;
    std::array<std::shared_ptr<TabStripFrame>, 2> strips_;
    std::array<ForegroundBuffer, 2> foregrounds_;
};

} // namespace window_system::benchmarks
