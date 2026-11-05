// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/theme.hpp>
#include <QFont>
#include <QObject>
#include <QVariantMap>
#include <qqmlintegration.h>

class QQuickItem;

namespace window_system::quick {

// The look the Quick settings and shortcut pages share with their form
// controls: theme colors, the interface font in the sizes the Widgets pages
// use, the unit for one logical pixel and the control height.
class SettingsStyle final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created with a settings page")
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QFont font READ font NOTIFY changed)
    // The page title, category headings, setting titles and the current
    // category, as the Widgets page sizes and weights them.
    Q_PROPERTY(QFont titleFont READ titleFont NOTIFY changed)
    Q_PROPERTY(QFont headingFont READ headingFont NOTIFY changed)
    Q_PROPERTY(QFont strongFont READ strongFont NOTIFY changed)
    Q_PROPERTY(QFont currentFont READ currentFont NOTIFY changed)
    Q_PROPERTY(qreal unit READ unit NOTIFY changed)
    Q_PROPERTY(qreal controlHeight READ controlHeight NOTIFY changed)
    Q_PROPERTY(QString defaultText READ defaultText CONSTANT)
public:
    explicit SettingsStyle(const Theme& theme, QObject* parent = nullptr);

    QVariantMap colors() const;
    QFont font() const { return font_; }
    QFont titleFont() const { return sized(4, QFont::DemiBold); }
    QFont headingFont() const { return sized(2, QFont::Bold); }
    QFont strongFont() const { return sized(0, QFont::DemiBold); }
    QFont currentFont() const { return sized(0, QFont::Bold); }
    qreal unit() const;
    qreal controlHeight() const;
    QString defaultText() const;
    // The theme or the interface font changed.
    void setTheme(const Theme& theme);
    // Hover tips for the controls, as the other Quick controls show them.
    Q_INVOKABLE void showToolTip(QQuickItem* anchor, const QString& text) const;
    Q_INVOKABLE void hideToolTip(QQuickItem* anchor) const;

signals:
    void changed();

private:
    // The page font grown by points, as the Widgets pages' uiFontDelta.
    QFont sized(int points, QFont::Weight weight) const;
    Theme theme_;
    QFont font_;
};

} // namespace window_system::quick
