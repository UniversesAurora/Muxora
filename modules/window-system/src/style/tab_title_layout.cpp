// SPDX-License-Identifier: MIT
#include "tab_title_layout.hpp"

#include <QFontMetrics>
#include <QHash>
#include <QPaintDevice>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <cmath>

namespace window_system {
namespace {
bool needsNativeFormatting(const QString& text, const QFont& font, bool mnemonic)
{
    // These paths have additional formatting or decoration semantics. Keep
    // Qt's original formatter instead of approximating them in a glyph cache.
    if (font.underline() || font.overline() || font.strikeOut()) return true;
    for (const auto character : text) {
        const auto value = character.unicode();
        if ((mnemonic && value == '&') || value == '\r' || value == '\n' || value == '\t'
            || value == 0x009c || value == 0x2028 || value == 0x2029) return true;
    }
    return false;
}
}

void TabTitleLayout::prepare(const QString& text, const QFont& font, Qt::LayoutDirection direction,
                             bool mnemonic, const QPaintDevice* device)
{
    const QFontMetrics metrics(font, device);
    const int dpiX = device ? device->logicalDpiX() : qRound(metrics.fontDpi());
    const int dpiY = device ? device->logicalDpiY() : qRound(metrics.fontDpi());
    if (prepared_ && source_ == text && font_ == font && direction_ == direction
        && mnemonic_ == mnemonic && dpiX_ == dpiX && dpiY_ == dpiY) return;
    source_ = text; font_ = font; measurementFont_ = device ? QFont(font, device) : font;
    direction_ = direction; mnemonic_ = mnemonic;
    dpiX_ = dpiX; dpiY_ = dpiY; prepared_ = true;
    naturalWidth_ = mnemonic
        ? metrics.size(Qt::TextShowMnemonic | Qt::TextSingleLine, text).width()
        : metrics.horizontalAdvance(text);
    fallback_ = needsNativeFormatting(text, font, mnemonic);
    fitReady_ = false;
    runs_.clear(); viewport_ = QSize(); renderedRatio_ = 0;
}

int TabTitleLayout::naturalWidth() const { return naturalWidth_; }

const TabTitleFittedMetrics& TabTitleLayout::fittedMetrics() const
{
    if (fitReady_) return fit_;
    fitReady_ = true; ++fitMeasurementCount_; fit_ = {};
    // QPainter's single-line path replaces these separators with spaces.
    // Length variants remain on the native formatter path; reserve enough
    // space for every variant without discarding their selection semantics.
    auto text = source_;
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    if (mnemonic_) {
        QString visible;
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text[i] == QLatin1Char('&')) {
                if (i + 1 >= text.size() || text[i + 1] != QLatin1Char('&')) continue;
                ++i;
            }
            visible += text[i];
        }
        text = visible;
    }
    for (const auto& variant : text.split(QChar(0x009c))) {
        QTextLayout layout(variant, measurementFont_);
        QTextOption option; option.setWrapMode(QTextOption::NoWrap);
        option.setTextDirection(direction_); option.setAlignment(Qt::AlignLeft);
        option.setFlags(QTextOption::IncludeTrailingSpaces); layout.setTextOption(option);
        layout.beginLayout(); auto line = layout.createLine();
        if (!line.isValid()) { layout.endLayout(); continue; }
        line.setLineWidth(0x01000000); line.setPosition({}); layout.endLayout();
        fit_.advance = std::max(fit_.advance, line.horizontalAdvance());
        fit_.lineHeight = std::max(fit_.lineHeight, line.ascent() + line.descent());
        for (const auto& run : line.glyphRuns()) {
            const auto raw = run.rawFont(); const auto glyphs = run.glyphIndexes(); const auto positions = run.positions();
            QHash<quint32, QRectF> bounds;
            for (qsizetype i = 0; i < glyphs.size(); ++i) {
                const auto glyph = glyphs[i];
                if (!bounds.contains(glyph)) bounds.insert(glyph, raw.boundingRect(glyph));
                fit_.inkBounds |= bounds.value(glyph).translated(positions[i]);
            }
        }
    }
    fit_.lineRect = QRectF(0, 0, fit_.advance, fit_.lineHeight);
    const auto bounds = fit_.lineRect.united(fit_.inkBounds);
    const int left = int(std::floor(std::min(qreal(0), bounds.left())));
    const int top = int(std::floor(std::min(qreal(0), bounds.top())));
    const int right = int(std::ceil(std::max(fit_.advance, bounds.right())));
    const int bottom = int(std::ceil(std::max(fit_.lineHeight, bounds.bottom())));
    fit_.leftInset = -left; fit_.topInset = -top;
    fit_.rightInset = right - fit_.advance;
    fit_.bottomInset = bottom - int(std::ceil(fit_.lineHeight));
    fit_.size = QSize(right - left, bottom - top);
    return fit_;
}

void TabTitleLayout::draw(QPainter& painter, const QRect& area) { drawInternal(painter, area, Origin::Painter); }
void TabTitleLayout::drawFitted(QPainter& painter, const QRect& area) { drawInternal(painter, area, Origin::Fitted); }

void TabTitleLayout::drawInternal(QPainter& painter, const QRect& area, Origin origin)
{
    if (!prepared_ || source_.isEmpty() || area.isEmpty()) return;
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    if (fallback_) {
        painter.setFont(font_); painter.setLayoutDirection(direction_);
        QRectF textArea(area);
        if (origin == Origin::Fitted) {
            const auto& fit = fittedMetrics();
            textArea.adjust(direction_ == Qt::LeftToRight ? fit.leftInset : 0,
                            fit.topInset, direction_ == Qt::RightToLeft ? -fit.rightInset : 0, -fit.bottomInset);
        }
        painter.drawText(textArea, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine
            | (mnemonic_ ? Qt::TextShowMnemonic : 0), source_);
    } else {
        const auto* device = painter.device();
        if (viewport_ != area.size() || renderedOrigin_ != origin || renderedDpiX_ != device->logicalDpiX()
            || renderedDpiY_ != device->logicalDpiY() || renderedRatio_ != device->devicePixelRatioF())
            rebuild(painter, area.size(), origin);
        // Match drawText's own overflow clip as well as the caller's clip.
        // Fractionally transformed ARGB emoji apply both coverages when Qt
        // temporarily draws their cached image with an identity transform.
        if (innerClip_) painter.setClipRect(area, Qt::IntersectClip);
        const auto matrix = painter.combinedTransform();
        if (!painter.testRenderHint(QPainter::Antialiasing) && matrix.type() >= QTransform::TxScale) {
            const bool plainQuarterTurn = matrix.type() < QTransform::TxShear
                && qFuzzyIsNull(matrix.m11()) && qFuzzyIsNull(matrix.m22())
                && ((qFuzzyIsNull(matrix.m12() - 1) && qFuzzyIsNull(matrix.m21() + 1))
                    || (qFuzzyIsNull(matrix.m12() + 1) && qFuzzyIsNull(matrix.m21() - 1)));
            // drawTextItem enables this for transformed text. It also affects
            // clipping at fractional screen scales, not only glyph outlines.
            if (!plainQuarterTurn) painter.setRenderHint(QPainter::Antialiasing, true);
        }
        for (const auto& run : runs_) painter.drawGlyphRun(area.topLeft(), run);
    }
    painter.restore();
}

void TabTitleLayout::rebuild(QPainter& painter, QSize viewport, Origin origin)
{
    ++shapingCount_;
    runs_.clear(); viewport_ = viewport; innerClip_ = false;
    renderedOrigin_ = origin;
    renderedDpiX_ = painter.device()->logicalDpiX();
    renderedDpiY_ = painter.device()->logicalDpiY();
    renderedRatio_ = painter.device()->devicePixelRatioF();
    QTextLayout layout(source_, font_, painter.device());
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    option.setTextDirection(direction_);
    option.setAlignment(Qt::AlignLeft);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    layout.setTextOption(option);
    layout.setCacheEnabled(true);
    layout.beginLayout();
    auto line = layout.createLine();
    if (!line.isValid()) { layout.endLayout(); return; }
    // Match QPainter's single-line formatter, including contextual shaping
    // beyond the viewport. Truncating Unicode first changes visible glyphs.
    line.setLineWidth(0x01000000);
    line.setPosition(QPointF());
    layout.endLayout();
    innerClip_ = line.naturalTextWidth() > viewport.width()
        || line.ascent() + line.descent() > viewport.height();
    // QTextLine truncates its origin to Qt's 26.6 fixed point before adding
    // the baseline. Do that before the device transform too; combining first
    // can move native glyphs across a fractional-scale raster boundary.
    const auto fixed = [](qreal coordinate) { return qreal(qint64(coordinate * 64)) / 64; };
    auto x = direction_ == Qt::RightToLeft ? viewport.width() - line.horizontalAdvance() : 0;
    auto y = (viewport.height() - line.ascent() - line.descent()) / 2;
    if (origin == Origin::Fitted) {
        const auto& fit = fittedMetrics();
        x += direction_ == Qt::RightToLeft ? -fit.rightInset : fit.leftInset;
        y = (viewport.height() - fit.size.height()) / 2.0 + fit.topInset;
    }
    const QPointF offset(fixed(x), fixed(y));
    const QRectF visible{QPointF(), QSizeF(viewport)};
    // QFontEngine ink bounds do not include the antialiasing fringe. Keep a
    // conservative extra pixel; the actual viewport still clips precisely.
    const auto sampleArea = visible.adjusted(-1, -1, 1, 1);
    for (const auto& original : line.glyphRuns()) {
        const auto font = original.rawFont();
        const auto indexes = original.glyphIndexes();
        const auto positions = original.positions();
        QHash<quint32, QRectF> inkBounds;
        QList<quint32> keptIndexes;
        QList<QPointF> keptPositions;
        for (qsizetype index = 0; index < indexes.size(); ++index) {
            const auto glyph = indexes[index];
            auto found = inkBounds.constFind(glyph);
            if (found == inkBounds.cend()) {
                inkBounds.insert(glyph, font.boundingRect(glyph));
                found = inkBounds.constFind(glyph);
            }
            const auto position = positions[index] + offset;
            if (!found->translated(position).intersects(sampleArea)) continue;
            keptIndexes.append(glyph); keptPositions.append(position);
        }
        if (keptIndexes.isEmpty()) continue;
        QGlyphRun kept;
        kept.setRawFont(font);
        kept.setFlags(original.flags());
        kept.setGlyphIndexes(keptIndexes);
        kept.setPositions(keptPositions);
        runs_.append(kept);
    }
}

qsizetype TabTitleLayout::retainedGlyphCount() const
{
    qsizetype result = 0;
    for (const auto& run : runs_) result += run.glyphIndexes().size();
    return result;
}

} // namespace window_system
