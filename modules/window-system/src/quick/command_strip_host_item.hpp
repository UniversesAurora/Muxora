// SPDX-License-Identifier: MIT
#pragma once

#include "chrome/strip_layout_state.hpp"
#include "chrome/strip_constraints.hpp"
#include "chrome/toolbar_description.hpp"
#include "chrome/toolbar_editor.hpp"
#include <window_system/theme.hpp>
#include <QQuickItem>
#include "window_drag_area_item.hpp"
#include <QPointer>
#include <functional>

namespace window_system::quick {
class CommandStripItem;

// Command strips in both dock areas share one registry and row allocator.
// The saved layout is decoded when it or the registered strips change; an
// edit is applied here and committed as the layout before and after it.
class CommandStripHostItem final : public WindowDragAreaItem, public ToolbarEditor {
    Q_OBJECT
public:
    explicit CommandStripHostItem(QQuickItem* parent);
    ~CommandStripHostItem() override;
    void sync(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved, const Theme& theme, bool hideOptionsWhenLocked);
    // Content with items (a menu bar's roots) hides them one by one; other
    // content shows or hides as a whole.
    void setContent(QString id, QString title, QQuickItem* content, StripConstraints constraints,
                    StripSizing sizing = StripSizing::Content, std::function<QStringList()> items = {});
    QQuickItem* bottomArea() const { return bottom_; }
    int minimumWidth() const { return minimumWidth_; }
    [[nodiscard]] QStringList excludedItems(const QString& id) const { return layout_.strips.value(id).excluded; }
    std::function<void(const QString&)> triggered;
    std::function<void(const QJsonObject& before, const QJsonObject& after)> committed;

    [[nodiscard]] QStringList toolbarIds() const override;
    [[nodiscard]] QString toolbarTitle(const QString& id) const override { return titles_.value(id); }
    [[nodiscard]] std::optional<ToolbarState> toolbarState(const QString& id) const override;
    [[nodiscard]] QVector<QStringList> toolbarRows(ToolbarArea area) const override;
    [[nodiscard]] bool isToolbarSizingEditable(const QString& id) const override;
    bool setToolbarVisible(const QString& id, bool visible) override;
    bool setToolbarWidth(const QString& id, int width) override;
    bool resetToolbarWidth(const QString& id) override;
    bool setToolbarElastic(const QString& id, bool elastic) override;
    bool setToolbarItemVisible(const QString& id, const QString& item, bool visible) override;
    bool moveToolbar(const QString& id, ToolbarArea area, int row, int index) override;
    void setToolbarsLocked(bool locked) override;
    [[nodiscard]] bool toolbarsLocked() const override { return layout_.locked; }
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
private:
    void restoreLayout(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved);
    void normalizeLayout(const QHash<QString, StripLayoutPreferences>& defaults);
    void syncViews(const QVector<CommandToolbarDescription>& descriptions, const Theme& theme, bool hideOptionsWhenLocked);
    void relayout();
    void requestLayout();
    StripConstraints constraintsFor(const QString& id) const;
    [[nodiscard]] int rowOf(const QString& id) const;
    // The least stored width the layout file accepts for the strip.
    [[nodiscard]] int minimumSavedWidth(const QString& id) const;
    [[nodiscard]] QString requiredElasticInRow(int row, const QString& except) const;
    void clearWidthPriority(int row);
    void normalizeElastic(int row, const QString& preferred);
    void removeFromRows(const QString& id);
    [[nodiscard]] QJsonObject currentLayout() const;
    void commit();
    struct Content {
        QString id;
        QPointer<QQuickItem> item;
        StripConstraints constraints;
        StripSizing sizing;
        std::function<QStringList()> items;
    };
    const Content* content(const QString& id) const;
    void watchStrip(CommandStripItem* strip);
    QHash<QString, QPointer<CommandStripItem>> strips_;
    QVector<Content> contents_;
    StripLayoutSnapshot layout_;
    QHash<QString, QString> titles_;
    // The saved layout last decoded or committed, the defaults it was decoded
    // against, and the complete layout an edit starts from.
    QJsonObject applied_;
    QVector<QStringList> defaultRows_;
    QVector<ToolbarArea> defaultAreas_;
    QJsonObject committed_;
    QQuickItem* bottom_;
    int minimumWidth_ = 0;
    bool arranging_ = false;
    bool layoutPending_ = false;
};

} // namespace window_system::quick
