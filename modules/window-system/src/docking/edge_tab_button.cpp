// SPDX-License-Identifier: MIT
#include "edge_tab_button.hpp"
#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
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
    QObject::connect(qApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
        titleLayout_ = TabTitleLayout{}; updateGeometry(); update();
    });
}
QSize EdgeTabButton::sizeHint() const
{
    titleLayout_.prepare(text(), font(), layoutDirection(), false, this);
    const auto extent = edgeTabExtent(fontMetrics(), titleLayout_.naturalWidth(), !icon().isNull(), number_, thickness_,
                                      clipTitles_, maximumTitleWidth_);
    return vertical_ ? QSize(thickness_, extent) : QSize(extent, thickness_);
}
void EdgeTabButton::setPresentation(QIcon image, int number)
{
    setIcon(std::move(image)); number_=std::max(0,number);
    setProperty("tabNumber",number_); updateGeometry(); update();
}
void EdgeTabButton::setTitleClipping(bool enabled, int maximumWidth)
{
    if (clipTitles_ == enabled && maximumTitleWidth_ == maximumWidth) return;
    clipTitles_ = enabled; maximumTitleWidth_ = maximumWidth;
    updateGeometry(); update();
}
void EdgeTabButton::setPreset(std::optional<TabColorPreset> preset)
{
    if (preset_ == preset) return;
    preset_ = std::move(preset); update();
}
void EdgeTabButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->accept(); return;
    }
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
    painter.setLayoutDirection(layoutDirection());
    // Keyboard focus can remain on the old rail button after programmatic
    // activation. Selection alone owns the active color state.
    const bool active = isChecked();
    const auto colors = preset_ ? std::optional(active ? preset_->autoHideActive : preset_->autoHideInactive) : std::nullopt;
    painter.fillRect(rect(), colors ? colors->background
        : edgeTabColor(this, underMouse() || hasFocus() ? "edgeTabHover" : "edgeTabBackground", QPalette::Button));
    painter.setPen(colors ? colors->text : edgeTabColor(this, "edgeTabText", QPalette::ButtonText));
    if (colors) painter.fillRect(vertical_ ? QRect(width() - 2, 0, 2, height()) : QRect(0, height() - 2, width(), 2), colors->line);
    if (vertical_) {
        painter.translate(width(), 0); painter.rotate(90);
    }
    const QRect area=vertical_ ? QRect(0,0,height(),width()) : rect();
    titleLayout_.prepare(text(), font(), layoutDirection(), false, this);
    const auto parts = edgeTabParts(fontMetrics(), titleLayout_.naturalWidth(), !icon().isNull(), number_,
                                    clipTitles_, maximumTitleWidth_);
    // The extent matches the label exactly; a longer button (an empty label
    // on a thick rail) centers it, and a shorter one cuts the title.
    const int span = int(std::min<qint64>(std::max(0, area.width() - 2 * edgeTabPadding), tabLabelWidth(parts)));
    const QRect label(std::max(edgeTabPadding, (area.width() - span) / 2), area.top(), span, area.height());
    const auto rects = layoutTabLabel(label, parts, layoutDirection());
    if (!rects.number.isEmpty())
        painter.drawText(rects.number, Qt::AlignVCenter | Qt::AlignLeading, QString::number(number_));
    if (!rects.icon.isEmpty()) {
        auto image=icon().pixmap(QSize(edgeTabIconSize,edgeTabIconSize));
        QPainter tint(&image); tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(image.rect(),colors ? colors->icon : edgeTabColor(this,"edgeTabText",QPalette::ButtonText)); tint.end();
        painter.save(); painter.setClipRect(rects.icon, Qt::IntersectClip);
        painter.drawPixmap(QRect(rects.icon.topLeft(), QSize(edgeTabIconSize, edgeTabIconSize)), image);
        painter.restore();
    }
    titleLayout_.draw(painter, rects.title);
}
} // namespace window_system
