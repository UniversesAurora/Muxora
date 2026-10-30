// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/content_options.hpp>

#include <QObject>
#include <functional>
#include <memory>

class QQuickItem;
class QQuickWindow;

namespace window_system {

class WindowManager;

/// The Qt Quick presentation of a WindowManager created with
/// Presentation::External. It builds a Quick window for every model window
/// from layoutChanged and turns user input into calls on the manager.
/// Pane content is created once per pane by the content factory and keeps
/// its identity while the pane moves between groups and windows.
class QuickWorkspace final : public QObject {
    Q_OBJECT
public:
    using ContentFactory = std::function<QQuickItem*(const TabOptions&, PaneId)>;

    explicit QuickWorkspace(WindowManager& manager, QObject* parent = nullptr);
    ~QuickWorkspace() override;
    /// Set before the first pane needs content.
    void setContentFactory(ContentFactory factory);
    [[nodiscard]] QQuickWindow* window(WindowId window) const;
    [[nodiscard]] QQuickItem* content(PaneId pane) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
    void sync();
};

} // namespace window_system
