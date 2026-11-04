// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <array>

class QQuickItem;

namespace window_system::quick {

// Owns the optional bars independently of the pages that display them.
// A page changes only the visual parent; a model owner closing retires a bar.
class ContentBars final : public QObject {
    Q_OBJECT
public:
    explicit ContentBars(QObject* parent = nullptr);
    ~ContentBars() override;
    bool setTab(TabId tab, QQuickItem* item, bool top);
    bool setPane(PaneId pane, QQuickItem* item, bool top);
    [[nodiscard]] QQuickItem* tab(TabId tab, bool top) const;
    [[nodiscard]] QQuickItem* pane(PaneId pane, bool top) const;
    [[nodiscard]] PaneId paneForItem(const LayoutModel& model, QQuickItem* item) const;
    void prune(const LayoutModel& model);

signals:
    void changed();

private:
    enum Kind { TabTop, TabBottom, PaneTop, PaneBottom };
    struct Owner { Kind kind; QUuid id; };
    bool set(Kind kind, QUuid id, QQuickItem* item);
    std::array<QHash<QUuid, QPointer<QQuickItem>>, 4> bars_;
    QHash<QQuickItem*, Owner> owners_;
    bool destroying_ = false;
};

} // namespace window_system::quick
