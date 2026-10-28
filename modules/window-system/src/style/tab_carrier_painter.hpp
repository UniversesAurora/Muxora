// SPDX-License-Identifier: MIT
#pragma once

#include <QtTypes>
#include <memory>

class QPainter;

namespace window_system {
struct SurfaceGeometry;
struct TabDragVisual;

struct TabCarrierCacheStats {
    qsizetype pieces = 0;
    qsizetype labels = 0;
    quint64 shapingCount = 0;
};

// GUI-thread raster renderer. Retains the styles and text caches for the
// currently presented carrier; moving it does not recreate either resource.
class TabCarrierPainter final {
public:
    TabCarrierPainter();
    ~TabCarrierPainter();
    TabCarrierPainter(const TabCarrierPainter&) = delete;
    TabCarrierPainter& operator=(const TabCarrierPainter&) = delete;

    void paint(QPainter& painter, const TabDragVisual& visual, const SurfaceGeometry* geometry = nullptr);
    void clear();
    [[nodiscard]] TabCarrierCacheStats cacheStats() const;

private:
    struct Cache;
    std::unique_ptr<Cache> cache_;
};

} // namespace window_system
