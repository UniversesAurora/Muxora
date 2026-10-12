// SPDX-License-Identifier: MIT
#pragma once
#include <window_system/window_manager.hpp>
#include <QWidget>
#include <QPointer>
#include <QList>

class QAction;
class QLabel;
class QMenu;
class QMoveEvent;
class QResizeEvent;
class QTimer;
class QToolButton;
class QVBoxLayout;
namespace window_system {
class CommandStripHost;
class DockSurface;
class WorkspaceWindow final : public QWidget {
public:
    WorkspaceWindow(WindowManager* manager, WindowId id, const WindowOptions& options);
    [[nodiscard]] WindowId id() const;
    [[nodiscard]] DockSurface* surface() const;
    [[nodiscard]] CommandStripHost* strips() const;
    [[nodiscard]] QJsonObject saveStripLayout() const;
    void restoreStripLayout(const QJsonObject& layout);
    void syncKind();
    void setOptions(const WindowOptions& options);
    void updateCaption();
    void setStatus(const QString& text);
    [[nodiscard]] QRect normalPlacementGeometry() const;
    [[nodiscard]] bool placementMaximized() const;
    void restorePlacement(const QRect& geometry, bool maximized);
    bool allowClose = false;
protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void createChrome();
    void addWindowMenuActions(QMenu* menu);
    void updateCommandStates();
    void updateWindowControlState();
    void queuePlacementNotification();
    void notifyPlacementChanged();
    WindowManager* manager_;
    WindowId id_;
    WindowKind kind_ = WindowKind::Floating;
    WindowOptions options_;
    QVBoxLayout* column_;
    QPointer<QWidget> chrome_;
    QPointer<CommandStripHost> strips_;
    QPointer<QLabel> caption_;
    QPointer<QLabel> status_;
    QPointer<QToolButton> maximizeButton_;
    QJsonObject savedStripLayout_;
    QList<QPointer<QAction>> commandActions_;
    DockSurface* surface_;
    QPoint press_;
    bool pendingTitleDrag_ = false;
    QRect normalPlacement_;
    QRect notifiedPlacement_;
    bool maximizedPlacement_ = false;
    bool notifiedMaximized_ = false;
    bool restoringPlacement_ = false;
    QTimer* placementTimer_;
};
} // namespace window_system
