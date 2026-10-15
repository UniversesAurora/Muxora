// SPDX-License-Identifier: MIT
#pragma once
#include <window_system/window_manager.hpp>
#include <window_system/view_state.hpp>
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
class ApplicationCommandMenus;
class DockSurface;
class WorkspaceWindow final : public QWidget {
public:
    enum class DragPresentation { Window, Tab, Left, Right, Top, Bottom };
    WorkspaceWindow(WindowManager* manager, WindowId id, const WindowOptions& options);
    [[nodiscard]] WindowId id() const;
    [[nodiscard]] DockSurface* surface() const;
    [[nodiscard]] CommandStripHost* strips() const;
    [[nodiscard]] QJsonObject saveStripLayout() const;
    [[nodiscard]] QJsonObject saveInactiveStripLayouts() const;
    void restoreStripLayout(const QJsonObject& layout);
    void restoreStripLayouts(const QJsonObject& inactive, const QJsonObject& current);
    void syncKind();
    void setOptions(const WindowOptions& options);
    void updateCaption();
    void setStatus(const QString& text);
    [[nodiscard]] static QStringList statusItemIds();
    [[nodiscard]] bool statusItemVisible(const QString& id) const;
    void applyViewState(const WindowViewState& state);
    [[nodiscard]] QRect normalPlacementGeometry() const;
    [[nodiscard]] bool placementMaximized() const;
    void restorePlacement(const QRect& geometry, bool maximized);
    void setDragPresentation(DragPresentation presentation);
    [[nodiscard]] DragPresentation dragPresentation() const { return dragPresentation_; }
    [[nodiscard]] QPoint dragLabelAnchor() const;
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
    static constexpr int titleMinimumWidth = 96;
    static constexpr int captionButtonWidth = 42;
    void createChrome();
    void addWindowMenuActions(QMenu* menu);
    void updateCommandStates();
    void updateWindowControlState();
    void updateFrameMargins();
    void showTabPresentationMenu(const QPoint& global);
    void queuePlacementNotification();
    void notifyPlacementChanged();
    void updateDragMask();
    [[nodiscard]] bool hasNormalPlacement() const;
    QPointer<WindowManager> manager_;
    WindowId id_;
    WindowKind kind_ = WindowKind::Floating;
    WindowOptions options_;
    QVBoxLayout* column_;
    QPointer<QWidget> chrome_;
    QPointer<QWidget> bottomChrome_;
    QPointer<CommandStripHost> strips_;
    QPointer<QLabel> caption_;
    QPointer<QLabel> status_;
    QPointer<QToolButton> maximizeButton_;
    QJsonObject savedStripLayout_;
    QJsonObject committedStripLayout_;
    QHash<WindowKind, QJsonObject> savedStripLayouts_;
    QList<QPointer<QAction>> commandActions_;
    QPointer<ApplicationCommandMenus> menus_;
    DockSurface* surface_;
    QPoint press_;
    bool pendingTitleDrag_ = false;
    QRect normalPlacement_;
    QRect notifiedPlacement_;
    bool maximizedPlacement_ = false;
    bool notifiedMaximized_ = false;
    bool restoringPlacement_ = false;
    QTimer* placementTimer_;
    DragPresentation dragPresentation_ = DragPresentation::Window;
    bool changingDragPresentation_ = false;
    WindowViewState viewState_;
    QRect viewNormalPlacement_;
    bool viewNormalMaximized_ = false;
    bool applyingViewState_ = false;
    QPointer<QWidget> dragPresentationHost_;
};
} // namespace window_system
