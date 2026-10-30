// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/content_options.hpp>
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <functional>

class QQuickItem;
class QQuickWindow;
class QWindow;

namespace window_system::quick {

// One workspace's live pane content. The controller decides model membership
// and mounting targets; this host owns actual items and never copies layout
// rules. Content is created at add/restore time, not recreated on each mount.
class ContentHost final : public QObject {
    Q_OBJECT
public:
    using Factory = std::function<QQuickItem*(const TabOptions&, PaneId)>;
    using VisibilityHandler = std::function<void(const QVector<ContentVisibilityChange>&)>;

    explicit ContentHost(QObject* parent = nullptr);
    ~ContentHost() override;
    void setFactory(Factory factory);
    bool create(PaneId pane, const TabOptions& options);
    // Rejected inputs stay caller-owned. Accepted items have stable QObject
    // ownership here; setParentItem() only changes their visual attachment.
    bool adopt(PaneId pane, QQuickItem* item);
    bool mount(PaneId pane, QQuickItem* parent);
    void unmount(PaneId pane);
    void remove(PaneId pane);
    void setVisible(PaneId pane, bool visible);
    [[nodiscard]] QQuickItem* item(PaneId pane) const;
    [[nodiscard]] bool visible(PaneId pane) const;
    [[nodiscard]] PaneId paneForItem(QQuickItem* item) const;
    // The last surviving focus descendant, or the pane root. Focusing does
    // not activate another native window; the workspace controls activation.
    [[nodiscard]] QQuickItem* focusItem(PaneId pane) const;
    bool focus(PaneId pane, Qt::FocusReason reason = Qt::OtherFocusReason);
    // Snapshot on registration, then event-loop-coalesced effective changes.
    // A cross-window mount reports hidden before changing the visual parent.
    void setVisibilityHandler(VisibilityHandler handler);

signals:
    void paneFocused(PaneId pane, QWindow* window);

private:
    struct Entry {
        QPointer<QQuickItem> item;
        QPointer<QQuickItem> focus;
    };
    bool current(PaneId pane, const QQuickItem* expected) const;
    void watchWindow(QQuickWindow* window);
    void rememberFocus(QQuickWindow* window);
    void reportHidden(PaneId pane, bool wasVisible);
    void scheduleVisibility();
    void publishVisibility();
    QHash<PaneId, Entry> entries_;
    QHash<QQuickItem*, PaneId> owners_;
    QVector<PaneId> order_;
    QSet<QQuickWindow*> watchedWindows_;
    Factory factory_;
    VisibilityHandler visibilityHandler_;
    QHash<PaneId, bool> reportedVisibility_;
    QVector<PaneId> reportedOrder_;
    bool visibilityPending_ = false;
    bool visibilitySnapshot_ = false;
    bool destroying_ = false;
};

} // namespace window_system::quick
