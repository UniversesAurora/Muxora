// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>

class QWidget;

namespace window_system {

/// All dimensions are device-independent pixels; Qt supplies the screen scale.
struct ThemeMetrics {
    int controlHeight = 24;
    int headerHeight = 32;
    int tabHeight = 27;
    int statusHeight = 22; // Minimum; larger fonts still expand the global status bar.
    int edgeThickness = 22;
    int paneHeaderHeight = 22;
    int splitterWidth = 1;
    int cornerRadius = 2;
    int padding = 6;
    int spacing = 4;
    int iconSize = 16;
    int groupSplitterWidth = 1;
};

/// Value tokens also available to hosts that paint their own content.
struct Theme {
    using Metrics = ThemeMetrics;

    QString name = QStringLiteral("Violet Light");
    QColor background = QColor(QStringLiteral("#756487"));
    QColor surface = QColor(QStringLiteral("#ffffff"));
    QColor raisedSurface = QColor(QStringLiteral("#f0ebf7"));
    QColor headerBackground = QColor(QStringLiteral("#ddd5ed"));
    QColor headerText = QColor(QStringLiteral("#2c2338"));
    QColor text = QColor(QStringLiteral("#2b2633"));
    QColor mutedText = QColor(QStringLiteral("#716779"));
    QColor border = QColor(QStringLiteral("#aa9cb9"));
    QColor accent = QColor(QStringLiteral("#7545a7"));
    QColor accentHover = QColor(QStringLiteral("#653693"));
    QColor accentSoft = QColor(QStringLiteral("#e8dcf6"));
    QColor buttonHover = QColor(QStringLiteral("#f1e8fb"));
    QColor activeTab = QColor(QStringLiteral("#ead9fa"));
    QColor inactiveTab = QColor(QStringLiteral("#c8bad8"));
    QColor edgeTabBackground = QColor(QStringLiteral("#6A568C"));
    QColor edgeTabHover = QColor(QStringLiteral("#8970AF"));
    QColor edgeTabText = QColor(QStringLiteral("#ffffff"));
    QColor statusBackground = QColor(QStringLiteral("#51366b"));
    QColor statusText = QColor(QStringLiteral("#ffffff"));
    QColor selection = QColor(QStringLiteral("#e0cbf2"));
    QColor selectionText = QColor(QStringLiteral("#2c173d"));
    QColor focus = QColor(QStringLiteral("#8e4fc4"));
    QColor danger = QColor(QStringLiteral("#c42b42"));
    QColor checkedIndicatorText = QColor(QStringLiteral("#ffffff"));
    QColor widgetShadow = Qt::black;
    QColor surfaceEdge = QColor::fromRgbF(0, 0, 0, 0.07f);
    qreal surfaceShadowMultiplier = 1;
    bool highContrast = false;
    Metrics metrics;
};

[[nodiscard]] Theme defaultTheme();
[[nodiscard]] QString styleSheet(const Theme& theme = defaultTheme());

/// Styles this root and its descendants without changing QApplication style.
/// Must be called on the GUI thread, after QApplication has been constructed.
void applyTheme(QWidget* root, const Theme& theme = defaultTheme());

/// Vector icons render at the exact requested device pixel ratio.
/// Names: app, close, minimize, maximize, restore, add, terminal, files, settings,
/// search, pin, unpin, pin-all, unpin-all, lock, unlock, split-horizontal, split-vertical, detach,
/// window, return, save, command, help, chevron-left/right/up/down.
/// Unknown names return a null icon. No resources or SVG dependency are needed.
[[nodiscard]] QIcon iconFor(QString name, const Theme& theme = defaultTheme(),
                            QSize size = QSize(16, 16));

} // namespace window_system
