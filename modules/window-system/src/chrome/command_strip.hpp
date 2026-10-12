// SPDX-License-Identifier: MIT
#pragma once

#include <QFrame>
#include <QJsonObject>
#include <QList>
#include <QStringList>

#include <functional>
#include <memory>

class QAction;
class QMenu;

namespace window_system {

// The chrome owns its widgets and action presentations; callers retain QAction
// ownership. Layout storage contains stable strip/action IDs, never pointers.
class CommandStripHost final : public QFrame {
public:
    explicit CommandStripHost(QWidget* parent = nullptr);
    ~CommandStripHost() override;

    void addStrip(QString id, QWidget* content, int row);
    void addStrip(QString id, QString title, QWidget* content, int row,
                  bool fillWhenFirst = false);
    void addActionStrip(QString id, QString title, const QList<QAction*>& actions, int row);
    void setLocked(bool locked);
    [[nodiscard]] bool isLocked() const;
    bool setStripVisible(const QString& id, bool visible);
    [[nodiscard]] bool isStripVisible(const QString& id) const;
    bool setStripItemVisible(const QString& id, const QString& item, bool visible);
    [[nodiscard]] bool isStripItemVisible(const QString& id, const QString& item) const;
    bool moveStrip(const QString& id, int row, int index);
    bool setStripWidth(const QString& id, int width);
    void refreshLayout();
    [[nodiscard]] QWidget* stripWidget(const QString& id) const;
    [[nodiscard]] QStringList stripIds() const;
    [[nodiscard]] QStringList stripOrder(int row) const;
    [[nodiscard]] QStringList overflowItems(const QString& id) const;
    [[nodiscard]] bool isDragging() const;

    [[nodiscard]] QMenu* createContextMenu(QWidget* parent);
    [[nodiscard]] QMenu* createOverflowMenu(const QString& id, QWidget* parent);
    [[nodiscard]] QList<QWidget*> windowDragAreas() const;
    void setContextMenuCustomizer(std::function<void(QMenu*)> customizer);
    void setLayoutChangedCallback(std::function<void()> callback);

    [[nodiscard]] QJsonObject saveLayout() const;
    void restoreLayout(const QJsonObject& json);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
    void arrange();
    void changed();
    void beginDrag(const QString& id, QPoint globalPosition, QPoint offset);
    void updateDrag(QPoint globalPosition);
    void finishDrag(bool commit);
};

} // namespace window_system
