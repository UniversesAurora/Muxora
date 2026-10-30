// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/theme.hpp>

#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <functional>

namespace window_system::quick {

class ButtonItem;
class PathItem;
class TabLabelTextItem;

// The panel an auto-hide tab opens over its window, as the Widgets
// PeekPanel: an accent frame, a title row with pin and close actions, and
// the tab's page below it. The page stays owned by the dock surface.
class PeekItem final : public QQuickItem {
    Q_OBJECT
public:
    PeekItem(const Theme& theme, QQuickItem* parent = nullptr);
    void setTitle(const QString& title, const QFont& font);
    void setPage(QQuickItem* page);
    [[nodiscard]] QQuickItem* page() const { return page_; }
    std::function<void()> pinRequested;
    std::function<void()> closeRequested;

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;

private:
    void relayout();
    Theme theme_;
    PathItem* surface_;
    PathItem* frame_;
    TabLabelTextItem* title_;
    ButtonItem* pin_;
    ButtonItem* close_;
    QPointer<QQuickItem> page_;
    QString titleText_;
    QFont font_;
};

} // namespace window_system::quick
