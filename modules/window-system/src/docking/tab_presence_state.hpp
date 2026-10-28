// SPDX-License-Identifier: MIT
#pragma once

#include "style/label_presence.hpp"
#include <QHash>
#include <QSet>
#include <QUuid>

namespace window_system {

// The adapter supplies progress from its animation clock. Redirecting the
// common clock first captures every label's displayed value, including labels
// that are not part of the new operation.
class TabPresenceState final {
public:
    void retarget(const QSet<QUuid>& ids, bool visible, qreal progress)
    {
        for (auto& value : values_) value.from += (value.to - value.from) * progress;
        for (const auto& id : ids) {
            const auto previous = values_.constFind(id);
            const qreal from = previous != values_.cend() ? previous->from : visible ? 0 : 1;
            values_.insert(id, {from, visible ? 1.0 : 0.0});
        }
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

    [[nodiscard]] LabelPresencePose pose(const QUuid& id, QRect area, qreal progress) const
    {
        const auto value = values_.constFind(id);
        return value == values_.cend() ? LabelPresencePose{}
            : labelPresencePose(area, value->from + (value->to - value->from) * progress, value->kind);
    }
    // Planar transitions already capture their displayed width in the layout
    // record; applying the same progress as opacity would add a separate fade.
    [[nodiscard]] qreal capturedVisibility(const QUuid& id, qreal progress) const
    {
        const auto value = values_.constFind(id);
        return value == values_.cend() || value->kind == LabelPresenceKind::Planar ? 1
            : value->from + (value->to - value->from) * progress;
    }

private:
    struct Value { qreal from; qreal to; LabelPresenceKind kind = LabelPresenceKind::Lift; };
    QHash<QUuid, Value> values_;
};

} // namespace window_system
