// SPDX-License-Identifier: MIT
#pragma once

#include <QFont>
#include <QGlyphRun>
#include <QList>
#include <QRect>
#include <QString>

class QPaintDevice;
class QPainter;

namespace window_system {

// Geometry in an unrotated text line's coordinates. The integer insets and
// size enclose the advance, fallback line metrics, and actual visible ink.
struct TabTitleFittedMetrics {
    qreal advance = 0;
    qreal lineHeight = 0;
    QRectF lineRect;
    QRectF inkBounds;
    QSize size;
    int leftInset = 0;
    qreal rightInset = 0;
    int topInset = 0;
    int bottomInset = 0;
};

// Owns only the most recently displayed glyphs, not a complete text layout.
// The source remains implicitly shared with the tab so it can still be used
// by accessibility, tooltips, and a later expansion of the text viewport.
class TabTitleLayout final {
public:
    void prepare(const QString& text, const QFont& font, Qt::LayoutDirection direction,
                 bool mnemonic, const QPaintDevice* device = nullptr);
    [[nodiscard]] int naturalWidth() const;
    [[nodiscard]] const TabTitleFittedMetrics& fittedMetrics() const;
    void draw(QPainter& painter, const QRect& area);
    // Fits the complete ink at the leading edge, while the supplied viewport
    // remains the exact clipping boundary, including when it is narrower.
    void drawFitted(QPainter& painter, const QRect& viewport);
    [[nodiscard]] qsizetype retainedGlyphCount() const;
    [[nodiscard]] quint64 shapingCount() const { return shapingCount_; }
    [[nodiscard]] quint64 fitMeasurementCount() const { return fitMeasurementCount_; }
private:
    enum class Origin { Painter, Fitted };
    void drawInternal(QPainter& painter, const QRect& area, Origin origin);
    void rebuild(QPainter& painter, QSize viewport, Origin origin);

    QString source_;
    QFont font_;
    QFont measurementFont_;
    Qt::LayoutDirection direction_ = Qt::LeftToRight;
    bool mnemonic_ = false;
    bool prepared_ = false;
    bool fallback_ = false;
    bool innerClip_ = false;
    int dpiX_ = 0;
    int dpiY_ = 0;
    int naturalWidth_ = 0;
    mutable bool fitReady_ = false;
    mutable TabTitleFittedMetrics fit_;
    mutable quint64 fitMeasurementCount_ = 0;
    QSize viewport_;
    Origin renderedOrigin_ = Origin::Painter;
    int renderedDpiX_ = 0;
    int renderedDpiY_ = 0;
    qreal renderedRatio_ = 0;
    QList<QGlyphRun> runs_;
    quint64 shapingCount_ = 0;
};

} // namespace window_system
