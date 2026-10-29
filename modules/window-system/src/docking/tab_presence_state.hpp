// SPDX-License-Identifier: MIT
#pragma once

#include "style/label_presence.hpp"
#include "style/frame_motion.hpp"
#include <QHash>
#include <QSet>
#include <QUuid>

namespace window_system {

// The adapter supplies monotonic frame timestamps. Redirecting the
// shared transition first captures every label's displayed value, including labels
// that are not part of the new operation.
class TabPresenceState final {
public:
    [[nodiscard]] qreal progress() const { return progress_.value(); }
    [[nodiscard]] bool needsFrame() const { return progress_.needsFrame(); }
    [[nodiscard]] int durationMs() const { return progress_.durationMs(); }
    bool advance(qint64 timestampNs) { return progress_.advance(timestampNs); }

    bool begin(const QSet<QUuid>& ids, bool visible, Motion motion, bool enabled, qint64 timestampNs)
    {
        const qreal progress = progress_.value();
        for (auto& value : values_) value.from += (value.to - value.from) * progress;
        for (const auto& id : ids) {
            const auto previous = values_.constFind(id);
            const qreal from = previous != values_.cend() ? previous->from : visible ? 0 : 1;
            values_.insert(id, {from, visible ? 1.0 : 0.0});
        }
        progress_.jump(0);
        progress_.animateTo(1, motion, enabled, timestampNs);
        return progress_.needsFrame();
    }

    void finish()
    {
        for (auto it = values_.begin(); it != values_.end();)
            if (it->to == 1) it = values_.erase(it); else ++it;
    }
    void remove(const QUuid& id) { values_.remove(id); }
    void setPlanar(const QSet<QUuid>& ids)
    {
        for (const auto& id : ids)
            if (auto value = values_.find(id); value != values_.end()) value->kind = LabelPresenceKind::Planar;
    }

    [[nodiscard]] LabelPresencePose pose(const QUuid& id, QRect area) const
    {
        const auto value = values_.constFind(id);
        return value == values_.cend() ? LabelPresencePose{}
            : labelPresencePose(area, value->from + (value->to - value->from) * progress_.value(), value->kind);
    }
    // Planar transitions already capture their displayed width in the layout
    // record; applying the same progress as opacity would add a separate fade.
    [[nodiscard]] qreal capturedVisibility(const QUuid& id) const
    {
        const auto value = values_.constFind(id);
        return value == values_.cend() || value->kind == LabelPresenceKind::Planar ? 1
            : value->from + (value->to - value->from) * progress_.value();
    }

private:
    struct Value { qreal from; qreal to; LabelPresenceKind kind = LabelPresenceKind::Lift; };
    QHash<QUuid, Value> values_;
    FrameMotion progress_{1};
};

} // namespace window_system
