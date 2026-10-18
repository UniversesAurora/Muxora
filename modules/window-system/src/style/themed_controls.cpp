// SPDX-License-Identifier: MIT
#include "themed_controls.hpp"
#include "commands/popup_menu.hpp"
#include <window_system/theme.hpp>

#include <QAbstractItemView>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>

#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStylePainter>
#include <QVariantMap>

namespace window_system {
namespace {

QColor controlColor(const QWidget* widget, const char* token, QPalette::ColorRole fallback)
{
    for (auto* root = widget; root; root = root->parentWidget()) {
        const auto tokens = root->property("windowSystemThemeColors");
        if (!tokens.isValid()) continue;
        const auto value = tokens.toMap().value(QString::fromLatin1(token));
        if (value.canConvert<QColor>()) return value.value<QColor>();
    }
    return widget->palette().color(widget->isEnabled() ? QPalette::Active : QPalette::Disabled, fallback);
}

void drawChevron(QPainter& painter, const QRect& rect, const QColor& color, bool up)
{
    const auto center = QRectF(rect).center();
    const qreal halfWidth = qMin(3.5, rect.width() / 4.0);
    const qreal halfHeight = qMin(2.0, rect.height() / 4.0);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal direction = up ? -1.0 : 1.0;
    QPainterPath path;
    path.moveTo(center + QPointF(-halfWidth, -direction * halfHeight));
    path.lineTo(center + QPointF(0, direction * halfHeight));
    path.lineTo(center + QPointF(halfWidth, -direction * halfHeight));
    painter.drawPath(path);
    painter.restore();
}

} // namespace

QString overlayScrollbarStyle(const Theme& theme)
{
    const auto rgba = [](QColor color, int alpha) {
        color.setAlpha(alpha);
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };
    const int gray = qGray(theme.border.rgb());
    const QColor handle(gray, gray, gray);
    return QStringLiteral(
        "QScrollBar { background: transparent; border: none; margin: 0; }"
        "QScrollBar:vertical { width: 4px; } QScrollBar:horizontal { height: 4px; }"
        "QScrollBar::handle { background: %1; border-radius: 0; margin: 0; }"
        "QScrollBar::handle:vertical { min-height: 18px; }"
        "QScrollBar::handle:horizontal { min-width: 18px; }"
        "QScrollBar::handle:hover, QScrollBar::handle:pressed { background: %2; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }")
        .arg(rgba(handle, 144), rgba(handle, 192));
}

void ThemedComboBox::showPopup()
{
    auto* popup = view()->window();
    if (popup != window() && popup_ != popup) {
        if (popup_) popup_->removeEventFilter(this);
        popup_ = popup;
        popup_->installEventFilter(this);
    }
    if (popup_) preparePopupFrame(popup_);
    const QPointer<ThemedComboBox> self(this);
    QComboBox::showPopup();
    // QComboBox may set its container geometry again after Show when the
    // final item-view scrollbar requirement becomes known. Its native-style
    // position uses the inclusive bottom pixel. Popup animation can defer Show,
    // so anchor the completed geometry even while its container is still hidden.
    if (self && popup_) alignPopup();
}

void ThemedComboBox::alignPopup()
{
    if (!popup_) return;
    preparePopupFrame(popup_);
    const QRect anchor(mapToGlobal(QPoint()), size());
    auto* target = QGuiApplication::screenAt(anchor.center());
    if (!target) target = screen();
    if (target) {
        const auto desired = popupMenuPosition(anchor, popup_->size(), target->availableGeometry());
        popup_->move(desired);
    }
}

bool ThemedComboBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == popup_) {
        if (event->type() == QEvent::WinIdChange && popup_->testAttribute(Qt::WA_WState_Created)
            && popup_->effectiveWinId()) preparePopupFrame(popup_);
        if (event->type() == QEvent::Show) alignPopup();
    }
    return QComboBox::eventFilter(watched, event);
}

void ThemedComboBox::paintEvent(QPaintEvent*)
{
    QStyleOptionComboBox option;
    initStyleOption(&option);
    option.subControls = QStyle::SC_ComboBoxFrame | QStyle::SC_ComboBoxEditField;
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ComboBox, option);
    painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    const auto area = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
    drawChevron(painter, area, controlColor(this, isEnabled() ? "text" : "mutedText", QPalette::Text), false);
}

ThemedSpinBox::ThemedSpinBox(QWidget* parent) : QSpinBox(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

void ThemedSpinBox::paintEvent(QPaintEvent*)
{
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField;
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_SpinBox, option);
    const QPoint pointer = mapFromGlobal(QCursor::pos());
    for (const auto control : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown}) {
        const auto area = style()->subControlRect(QStyle::CC_SpinBox, &option, control, this);
        const bool up = control == QStyle::SC_SpinBoxUp;
        const bool enabled = isEnabled() && option.stepEnabled.testFlag(up ? StepUpEnabled : StepDownEnabled);
        if (enabled && area.contains(pointer)) {
            const bool pressed = option.state.testFlag(QStyle::State_Sunken) && option.activeSubControls.testFlag(control);
            painter.fillRect(area.adjusted(1, 1, -1, -1), controlColor(this, pressed ? "selection" : "buttonHover", QPalette::Midlight));
            painter.setPen(controlColor(this, "accent", QPalette::Link));
            painter.drawRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        drawChevron(painter, area, controlColor(this, enabled ? "text" : "mutedText", QPalette::Text), up);
    }
}

void ThemedCheckBox::paintEvent(QPaintEvent* event)
{
    QCheckBox::paintEvent(event);
    if (checkState() == Qt::Unchecked) return;
    QStyleOptionButton option;
    initStyleOption(&option);
    const auto area = QRectF(style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this));
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(controlColor(this, "checkedIndicatorText", QPalette::BrightText), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (checkState() == Qt::PartiallyChecked) {
        painter.drawLine(area.topLeft() + QPointF(area.width() * 0.25, area.height() * 0.5),
                         area.topLeft() + QPointF(area.width() * 0.75, area.height() * 0.5));
        return;
    }
    QPainterPath tick;
    tick.moveTo(area.topLeft() + QPointF(area.width() * 0.23, area.height() * 0.52));
    tick.lineTo(area.topLeft() + QPointF(area.width() * 0.43, area.height() * 0.72));
    tick.lineTo(area.topLeft() + QPointF(area.width() * 0.78, area.height() * 0.30));
    painter.drawPath(tick);
}

} // namespace window_system
