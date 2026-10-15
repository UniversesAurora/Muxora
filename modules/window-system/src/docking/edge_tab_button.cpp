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
    const auto extent = edgeTabExtent(fontMetrics(),text(),!icon().isNull(),number_);
    return vertical_ ? QSize(thickness_, extent) : QSize(extent, thickness_);
}
void EdgeTabButton::setPresentation(QIcon image, int number)
{
    setIcon(std::move(image)); number_=std::max(0,number);
    setProperty("tabNumber",number_); updateGeometry(); update();
}
void EdgeTabButton::setPreset(std::optional<TabColorPreset> preset)
{
    if (preset_ == preset) return;
    preset_ = std::move(preset); update();
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
    const bool active = isChecked() || hasFocus();
    const auto colors = preset_ ? std::optional(active ? preset_->autoHideActive : preset_->autoHideInactive) : std::nullopt;
    painter.fillRect(rect(), colors ? colors->background
        : edgeTabColor(this, underMouse() || hasFocus() ? "edgeTabHover" : "edgeTabBackground", QPalette::Button));
    painter.setPen(colors ? colors->text : edgeTabColor(this, "edgeTabText", QPalette::ButtonText));
    if (colors) painter.fillRect(vertical_ ? QRect(width() - 2, 0, 2, height()) : QRect(0, height() - 2, width(), 2), colors->line);
    if (vertical_) {
        painter.translate(width(), 0); painter.rotate(90);
    }
    const QRect area=vertical_ ? QRect(0,0,height(),width()) : rect();
    if(icon().isNull() && !number_) { painter.drawText(area,Qt::AlignCenter,text()); return; }
    const int numberWidth=number_>0 ? fontMetrics().horizontalAdvance(QString::number(number_))+5 : 0;
    const int imageWidth=icon().isNull() ? 0 : 20;
    const int span=std::min(area.width()-12,fontMetrics().horizontalAdvance(text())+numberWidth+imageWidth);
    int left=std::max(6,(area.width()-span)/2);
    if(number_) {
        painter.drawText(QRect(left,0,numberWidth,area.height()),Qt::AlignVCenter,QString::number(number_));
        left+=numberWidth;
    }
    if(!icon().isNull()) {
        auto image=icon().pixmap(QSize(16,16));
        QPainter tint(&image); tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(image.rect(),colors ? colors->icon : edgeTabColor(this,"edgeTabText",QPalette::ButtonText)); tint.end();
        painter.drawPixmap(QRect(left,area.center().y()-8,16,16),image); left+=imageWidth;
    }
    painter.drawText(QRect(left,0,std::max(0,area.width()-6-left),area.height()),Qt::AlignVCenter,
                     fontMetrics().elidedText(text(),Qt::ElideRight,std::max(0,area.width()-6-left)));
}
} // namespace window_system
