// SPDX-License-Identifier: MIT
#include "style/tab_title_layout.hpp"

#include <QApplication>
#include <QColor>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextLayout>

using namespace window_system;

namespace {
QImage render(const QString& title, QFont font, Qt::LayoutDirection direction,
              bool mnemonic, int width, qreal ratio, bool rotated, TabTitleLayout* cached,
              int logicalDpi = 0, QColor ink = QColor("#19202c"))
{
    constexpr int height = 39;
    const QSize logical = rotated ? QSize(height + 10, width + 10) : QSize(width + 10, height + 10);
    QImage image(QSize(qRound(logical.width() * ratio), qRound(logical.height() * ratio)),
                 QImage::Format_ARGB32_Premultiplied);
    if (logicalDpi > 0) {
        image.setDotsPerMeterX(qRound(logicalDpi / 0.0254));
        image.setDotsPerMeterY(qRound(logicalDpi / 0.0254));
    }
    image.setDevicePixelRatio(ratio); image.fill(QColor("#ffffff"));
    QPainter painter(&image); painter.setFont(font); painter.setPen(ink);
    painter.setLayoutDirection(direction);
    if (rotated) { painter.translate(height + 5, 5); painter.rotate(90); }
    else painter.translate(5, 5);
    const QRect area(0, 0, width, height);
    if (cached) {
        cached->prepare(title, font, direction, mnemonic, &image);
        cached->draw(painter, area);
    } else {
        painter.setClipRect(area);
        painter.drawText(area, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine
            | (mnemonic ? Qt::TextShowMnemonic : 0), title);
    }
    painter.end(); return image;
}
}

class TabTitleLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void keepsOnlyVisibleGlyphsAndReusesThem();
    void cacheInvalidationRemeasuresChangedInputs();
    void pointSizeFontFollowsLogicalDpiChanges();
    void colorChangesReuseTheShapedGlyphs();
    void fittedMetricsEncloseInkAndCacheMeasurements();
};

void TabTitleLayoutTest::fittedMetricsEncloseInkAndCacheMeasurements()
{
    QFont font = qApp->font(); font.setPixelSize(31); font.setItalic(true);
    for (const auto& text : {QStringLiteral("ffff jjjj"), QStringLiteral("工作区 17"),
                            QString::fromUtf8("A 😀 👩🏽‍💻 Ω العربية")}) {
        for (auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
            TabTitleLayout cached; cached.prepare(text, font, direction, false);
            const auto fit = cached.fittedMetrics();
            QTextLayout reference(text, font); QTextOption option;
            option.setWrapMode(QTextOption::NoWrap); option.setTextDirection(direction);
            reference.setTextOption(option); reference.beginLayout(); auto line = reference.createLine();
            line.setLineWidth(0x01000000); reference.endLayout();
            const QRectF viewport(QPointF(), fit.size);
            const QPointF shift(fit.leftInset, fit.topInset);
            for (const auto& run : line.glyphRuns()) {
                const auto glyphs = run.glyphIndexes(); const auto positions = run.positions();
                for (qsizetype i = 0; i < glyphs.size(); ++i) {
                    const auto ink = run.rawFont().boundingRect(glyphs[i]).translated(positions[i] + shift);
                    if (!ink.isEmpty()) QVERIFY2(viewport.contains(ink), qPrintable(text));
                }
            }
            QImage image(fit.size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
            QPainter painter(&image); painter.setFont(font);
            cached.drawFitted(painter, image.rect());
            const auto shaping = cached.shapingCount();
            for (int i = 0; i < 8; ++i) {
                cached.prepare(text, font, direction, false);
                QCOMPARE(cached.fittedMetrics().size, fit.size);
                cached.drawFitted(painter, image.rect());
            }
            QCOMPARE(cached.fitMeasurementCount(), quint64(1));
            QCOMPARE(cached.shapingCount(), shaping);
            painter.end();
        }
    }
}

void TabTitleLayoutTest::keepsOnlyVisibleGlyphsAndReusesThem()
{
    const QString title(100000, QLatin1Char('W'));
    QFont font = qApp->font(); font.setPixelSize(17);
    TabTitleLayout cached;
    render(title, font, Qt::LeftToRight, true, 83, 1, false, &cached);
    QVERIFY(cached.retainedGlyphCount() > 0); QVERIFY(cached.retainedGlyphCount() < 20);
    QCOMPARE(cached.shapingCount(), quint64(1));
    for (int repeat = 0; repeat < 10; ++repeat)
        render(title, font, Qt::LeftToRight, true, 83, 1, false, &cached);
    QCOMPARE(cached.shapingCount(), quint64(1));
    QCOMPARE(title.size(), qsizetype(100000));
}

void TabTitleLayoutTest::cacheInvalidationRemeasuresChangedInputs()
{
    QFont font = qApp->font(); font.setPixelSize(17);
    QString title = QStringLiteral("A wide title with 中文 and العربية content");
    TabTitleLayout cached;
    render(title, font, Qt::LeftToRight, false, 83, 1, false, &cached);
    QCOMPARE(cached.naturalWidth(), QFontMetrics(font).horizontalAdvance(title));
    QCOMPARE(cached.shapingCount(), quint64(1));
    const auto compare = [&](Qt::LayoutDirection direction, bool mnemonic, int width, qreal ratio) {
        render(title, font, direction, mnemonic, width, ratio, false, &cached);
    };
    compare(Qt::LeftToRight, false, 137, 1); QCOMPARE(cached.shapingCount(), quint64(2));
    compare(Qt::RightToLeft, false, 137, 1); QCOMPARE(cached.shapingCount(), quint64(3));
    compare(Qt::RightToLeft, true, 137, 1); QCOMPARE(cached.shapingCount(), quint64(4));
    font.setPixelSize(23); compare(Qt::RightToLeft, true, 137, 1);
    QCOMPARE(cached.shapingCount(), quint64(5));
    title.append(" changed"); compare(Qt::RightToLeft, true, 137, 1);
    QCOMPARE(cached.shapingCount(), quint64(6));
    compare(Qt::RightToLeft, true, 137, 2); QCOMPARE(cached.shapingCount(), quint64(7));
    QCOMPARE(cached.naturalWidth(), QFontMetrics(font).size(Qt::TextShowMnemonic | Qt::TextSingleLine, title).width());
}

void TabTitleLayoutTest::pointSizeFontFollowsLogicalDpiChanges()
{
    const auto title = QStringLiteral("Point-size 中文 العربية title");
    QFont font = qApp->font(); font.setPointSizeF(12.5);
    TabTitleLayout cached;
    quint64 expectedShapes = 0;
    for (const int dpi : {96, 144, 192, 96}) {
        const auto actual = render(title, font, Qt::RightToLeft, true, 137, 1, false, &cached, dpi);
        QCOMPARE(actual.logicalDpiX(), dpi); QCOMPARE(actual.logicalDpiY(), dpi);
        QCOMPARE(cached.naturalWidth(), QFontMetrics(font, &actual)
            .size(Qt::TextShowMnemonic | Qt::TextSingleLine, title).width());
        QCOMPARE(cached.shapingCount(), ++expectedShapes);
        render(title, font, Qt::RightToLeft, true, 137, 1, false, &cached, dpi);
        QCOMPARE(cached.shapingCount(), expectedShapes);
    }
}

void TabTitleLayoutTest::colorChangesReuseTheShapedGlyphs()
{
    const auto title = QStringLiteral("Colored 中文 title");
    QFont font = qApp->font(); font.setPixelSize(19);
    TabTitleLayout cached;
    for (const auto ink : {QColor("#19202c"), QColor("#d02030"), QColor("#2050c0")}) {
        render(title, font, Qt::LeftToRight, true, 83, 1.5, false, &cached, 96, ink);
        QCOMPARE(cached.shapingCount(), quint64(1));
    }
}

QTEST_MAIN(TabTitleLayoutTest)
#include "tab_title_layout_test.moc"
