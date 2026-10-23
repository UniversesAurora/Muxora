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

// Owns only the most recently displayed glyphs, not a complete text layout.
// The source remains implicitly shared with the tab so it can still be used
// by accessibility, tooltips, and a later expansion of the text viewport.
class TabTitleLayout final {
public:
    void prepare(const QString& text, const QFont& font, Qt::LayoutDirection direction,
                 bool mnemonic, const QPaintDevice* device = nullptr);
    [[nodiscard]] int naturalWidth() const { return naturalWidth_; }
    void draw(QPainter& painter, const QRect& area);
    [[nodiscard]] qsizetype retainedGlyphCount() const;
    [[nodiscard]] quint64 shapingCount() const { return shapingCount_; }
private:
    void rebuild(QPainter& painter, QSize viewport);

    QString source_;
    QFont font_;
    Qt::LayoutDirection direction_ = Qt::LeftToRight;
    bool mnemonic_ = false;
    bool prepared_ = false;
    bool fallback_ = false;
    bool innerClip_ = false;
    int dpiX_ = 0;
    int dpiY_ = 0;
    int naturalWidth_ = 0;
    QSize viewport_;
    int renderedDpiX_ = 0;
    int renderedDpiY_ = 0;
    qreal renderedRatio_ = 0;
    QList<QGlyphRun> runs_;
    quint64 shapingCount_ = 0;
};

} // namespace window_system
