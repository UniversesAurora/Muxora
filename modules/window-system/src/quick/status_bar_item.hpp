// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/custom_chrome.hpp>
#include <window_system/theme.hpp>
#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <functional>

namespace window_system::quick {

class StatusEntryItem;

// The workbench's global status bar, as the Widgets one: the message (or a
// transient one) on the left, host and script items, then the workspace
// information on the right. Items with a command act when clicked.
class StatusBarItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit StatusBarItem(QQuickItem* parent = nullptr);
    void setTheme(const Theme& theme);
    void setMessage(const QString& message, const QString& transient);
    void setWorkspace(const QString& title, int tabCount);
    void setItems(const QVector<StatusItem>& items, const QStringList& hidden);
    // The IDs of the built-in items, in the order the menu lists them.
    [[nodiscard]] static QStringList builtinItemIds();
    std::function<void(const QString& item, const QString& command)> triggered;
    std::function<void(QPoint global)> menuRequested;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    bool event(QEvent* event) override;

private:
    void relayout();
    Theme theme_;
    QFont font_;
    StatusEntryItem* message_;
    StatusEntryItem* workspace_;
    QVector<QPointer<StatusEntryItem>> custom_;
    QStringList hidden_;
};

} // namespace window_system::quick
