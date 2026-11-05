// SPDX-License-Identifier: MIT
#include "settings_style.hpp"
#include "tool_tip.hpp"

#include <window_system/ui_scale.hpp>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <algorithm>

namespace window_system::quick {

SettingsStyle::SettingsStyle(const Theme& theme, QObject* parent)
    : QObject(parent), theme_(theme), font_(QGuiApplication::font())
{
}

QVariantMap SettingsStyle::colors() const
{
    return {{"surface", theme_.surface}, {"raised", theme_.raisedSurface}, {"text", theme_.text}, {"muted", theme_.mutedText},
        {"border", theme_.border}, {"accent", theme_.accent}, {"soft", theme_.accentSoft}, {"focus", theme_.focus},
        {"selection", theme_.selection}, {"selectionText", theme_.selectionText}, {"buttonHover", theme_.buttonHover},
        {"check", theme_.checkedIndicatorText}};
}

qreal SettingsStyle::unit() const { return uiPixelsF(1); }

qreal SettingsStyle::controlHeight() const
{
    return std::max(uiPixels(std::max(14, theme_.metrics.controlHeight - 6)), QFontMetrics(font_).height()) + uiPixels(6);
}

QString SettingsStyle::defaultText() const { return QCoreApplication::translate("WindowSystem", "Default"); }

QFont SettingsStyle::sized(int points, QFont::Weight weight) const
{
    QFont result = font_;
    if (points) {
        if (result.pixelSize() > 0) result.setPixelSize(result.pixelSize() + qRound(uiPixelsF(points * 96.0 / 72.0)));
        else result.setPointSizeF(result.pointSizeF() + points);
    }
    result.setWeight(weight);
    return result;
}

void SettingsStyle::setTheme(const Theme& theme)
{
    theme_ = theme;
    font_ = QGuiApplication::font();
    emit changed();
}

void SettingsStyle::showToolTip(QQuickItem* anchor, const QString& text) const { quick::showToolTip(anchor, theme_, text); }
void SettingsStyle::hideToolTip(QQuickItem* anchor) const { quick::hideToolTip(anchor); }

} // namespace window_system::quick
