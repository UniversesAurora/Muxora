// SPDX-License-Identifier: MIT
#pragma once

#include <QtGlobal>

namespace window_system::quick {

struct ShadowRenderStats {
    quint64 textureUploads = 0;
    quint64 imageBytesUploaded = 0;
    quint64 geometryUpdates = 0;
};

} // namespace window_system::quick
