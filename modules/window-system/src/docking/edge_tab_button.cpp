// SPDX-License-Identifier: MIT
#include "edge_tab_button.hpp"
#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantMap>

namespace window_system {
namespace {
QColor edgeTabColor(const QWidget* widget, const char* token, QPalette::ColorRole fallback)
{
    for (auto* root = widget; root; root = root->parentWidget()) {
        const auto tokens = root->property("windowSystemThemeColors");
        if (!tokens.isValid()) continue;
        const auto value = tokens.toMap().value(QString::fromLatin1(token));
        if (value.canConvert<QColor>()) return value.value<QColor>();
    }
    return widget->palette().color(fallback);
}
}
EdgeTabButton::EdgeTabButton(QString title, bool vertical, int thickness, QWidget* parent)
    : QAbstractButton(parent), vertical_(vertical), thickness_(thickness)
{
    setText(title); setToolTip(title); setAccessibleName(title);
    setObjectName("EdgeButton"); setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}
QSize EdgeTabButton::sizeHint() const
{
    const auto extent = qBound(64, fontMetrics().horizontalAdvance(text()) + 24, 180);
    return vertical_ ? QSize(thickness_, extent) : QSize(extent, thickness_);
}
void EdgeTabButton::mousePressEvent(QMouseEvent* event)
{
    start_ = event->position().toPoint(); QAbstractButton::mousePressEvent(event);
}
void EdgeTabButton::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::LeftButton) &&
        (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance()) {
        setDown(false); const auto callback = drag;
        if (callback) callback(event->globalPosition().toPoint());
        return;
    }
    QAbstractButton::mouseMoveEvent(event);
}
void EdgeTabButton::contextMenuEvent(QContextMenuEvent* event)
{
    const auto callback = menu;
    if (callback) { event->accept(); callback(event->globalPos()); }
    else QAbstractButton::contextMenuEvent(event);
}
void EdgeTabButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), edgeTabColor(this, underMouse() || hasFocus() ? "buttonHover" : "inactiveTab", QPalette::Button));
    painter.setPen(edgeTabColor(this, "text", QPalette::ButtonText));
    if (vertical_) {
        painter.translate(width(), 0); painter.rotate(90);
        painter.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, text());
    } else painter.drawText(rect(), Qt::AlignCenter, text());
}
} // namespace window_system
