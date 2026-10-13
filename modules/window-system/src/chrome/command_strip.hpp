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
    // Rows count from the outer edge: top rows grow down, bottom rows grow up.
    enum class Area { Top, Bottom };
    enum class StripSizing { Content, Elastic };
    struct StripConstraints {
        bool compressible = true;
        bool elasticAllowed = true;
        int minimumContentWidth = 12;
        // A required elastic strip owns its row's spare space. Moving an
        // ordinary elastic strip into that row makes the latter content-sized.
        bool elasticRequired = false;
    };
    explicit CommandStripHost(QWidget* parent = nullptr);
    ~CommandStripHost() override;

    void addStrip(QString id, QWidget* content, int row);
    void addStrip(QString id, QString title, QWidget* content, int row,
                  StripSizing sizing = StripSizing::Content);
    void addActionStrip(QString id, QString title, const QList<QAction*>& actions, int row,
                        StripSizing sizing = StripSizing::Content);
    bool setStripSizing(const QString& id, StripSizing sizing);
    [[nodiscard]] StripSizing stripSizing(const QString& id) const;
    [[nodiscard]] bool isStripSizingEditable(const QString& id) const;
    // Presentation policy supplied by the host, independent of saved user
    // layout. Disabled strips reserve no options-button space.
    bool setStripOptionsEnabled(const QString& id, bool enabled);
    [[nodiscard]] bool isStripOptionsEnabled(const QString& id) const;
    // Host constraints are not user layout. A protected strip can grow but
    // never shrink below its complete content or opt into elastic sizing.
    bool setStripConstraints(const QString& id, StripConstraints constraints);
    [[nodiscard]] StripConstraints stripConstraints(const QString& id) const;
    void setLocked(bool locked);
    [[nodiscard]] bool isLocked() const;
    bool setStripVisible(const QString& id, bool visible);
    [[nodiscard]] bool isStripVisible(const QString& id) const;
    bool setStripItemVisible(const QString& id, const QString& item, bool visible);
    [[nodiscard]] bool isStripItemVisible(const QString& id, const QString& item) const;
    bool moveStrip(const QString& id, int row, int index);
    bool moveStrip(const QString& id, Area area, int row, int index);
    // Insert this owned view below workspace content and above global status.
    [[nodiscard]] QWidget* bottomArea() const;
    [[nodiscard]] Area stripArea(const QString& id) const;
    // Visible occupied rows. Saved row slots can retain empty positions so a
    // hidden strip returns to its previous row without leaving a visual gap.
    [[nodiscard]] int rowCount(Area area) const;
    bool setStripWidth(const QString& id, int width);
    bool resetStripWidth(const QString& id);
    void refreshLayout();
    [[nodiscard]] QWidget* stripWidget(const QString& id) const;
    [[nodiscard]] QStringList stripIds() const;
    [[nodiscard]] QString stripTitle(const QString& id) const;
    [[nodiscard]] QStringList stripOrder(int row) const;
    [[nodiscard]] QStringList stripOrder(Area area, int row) const;
    [[nodiscard]] QStringList overflowItems(const QString& id) const;
    [[nodiscard]] bool isDragging() const;

    [[nodiscard]] QMenu* createContextMenu(QWidget* parent);
    [[nodiscard]] QMenu* createOverflowMenu(const QString& id, QWidget* parent);
    [[nodiscard]] QList<QWidget*> windowDragAreas() const;
    void setContextMenuCustomizer(std::function<void(QMenu*)> customizer);
    void setLayoutChangedCallback(std::function<void()> callback);
    void setSizingChangeRequest(std::function<void(const QString&, StripSizing)> request);
    void setWidthResetRequest(std::function<void(const QString&)> request);

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
    void populateOverflowMenu(const QString& id, QMenu* menu);
    void beginDrag(const QString& id, QPoint globalPosition, QPoint offset);
    void updateDrag(QPoint globalPosition);
    void finishDrag(bool commit);
    void removeFromRows(const QString& id);
    void normalizeElastic(int row, const QString& preferred = {});
    [[nodiscard]] QString requiredElasticInRow(int row, const QString& except = {}, bool committed = false) const;
    bool resizeDragBoundary(int row, int requestedLeft);
    void insertDraggedStrip(int row, QPoint position);
    void clearWidthPriority(int row);
    void normalizeWidthPriority(int row);
    [[nodiscard]] int rowIndex(Area area, int row) const;
    int ensureRow(Area area, int row);
    QWidget* ensureRowWidget(int row);
    void destroyRowWidget(int row);
    [[nodiscard]] int rowExtent(int row) const;
    [[nodiscard]] QWidget* areaWidget(Area area) const;
    void prepareDragRows();
    [[nodiscard]] int dragRowAt(QPoint globalPosition);
    [[nodiscard]] QSize areaSizeHint(Area area, bool minimum) const;
    void replaceRows(QVector<QStringList> rows, QVector<Area> areas, bool retainSpareRows = false);
    [[nodiscard]] QVector<int> rowWidths(const QStringList& visible, int available) const;
};

} // namespace window_system
