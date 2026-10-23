// SPDX-License-Identifier: MIT
#include "style/tab_title_layout.hpp"

#include <QApplication>
#include <QColor>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTest>

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
    void matchesQtPixelForPixel_data();
    void matchesQtPixelForPixel();
    void keepsOnlyVisibleGlyphsAndReusesThem();
    void cacheInvalidationKeepsMeasurementAndPixelsCorrect();
    void pointSizeFontFollowsLogicalDpiChanges();
    void colorChangesReuseTheShapedGlyphs();
};

void TabTitleLayoutTest::matchesQtPixelForPixel_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<bool>("mnemonic");
    QTest::addColumn<bool>("rtl");
    QTest::addColumn<bool>("italic");
    QTest::addColumn<bool>("decorated");
    QTest::addColumn<qreal>("ratio");
    QTest::addColumn<bool>("rotated");
    const QList<std::pair<QString, QString>> titles{
        {"latin", QStringLiteral("Wide titles: office affinity ffi ffl WWiiii")},
        {"chinese", QStringLiteral("工作区域标题应该直接裁切而不是出现省略号")},
        {"arabic", QStringLiteral("مرحبا بالعالم اللغة العربية والعناوين الطويلة")},
        {"bidi", QStringLiteral("Latin العربية 123 עברית more text")},
        {"bidi-isolates", QStringLiteral("A \u2067العربية 123\u2069 B \u2066Latin suffix\u2069 C")},
        {"combining", QStringLiteral("A\u0301e\u0327\u0308o\u0302 x\u0301\u0307 combining title")},
        {"emoji", QString::fromUtf8("A 👩🏽‍💻 👨‍👩‍👧‍👦 🇨🇳 😀 emoji title")},
        {"mnemonic", QStringLiteral("&Window && &Workspace long title")},
        {"singleline-controls", QStringLiteral("Window\tand\rnew\nline title")},
        {"length-variant", QStringLiteral("Long window title\u009cShort title")},
        {"trailing-spaces", QStringLiteral("  العربية Latin    ")}
    };
    for (const auto& [name, title] : titles)
        for (const bool rtl : {false, true})
            for (const qreal ratio : {1.0, 1.25, 1.5, 2.0})
                for (const bool rotated : {false, true}) {
                    const auto row = QString("%1-%2-%3-%4").arg(name).arg(rtl).arg(ratio).arg(rotated).toUtf8();
                    QTest::newRow(row.constData()) << title << true << rtl << false << false << ratio << rotated;
                }
    QTest::newRow("italic-overhang") << QStringLiteral("ffff jjjj WWWW italic")
        << false << false << true << false << 1.0 << false;
    QTest::newRow("literal-ampersand") << QStringLiteral("Window & Workspace && long title")
        << false << false << false << false << 1.5 << false;
    QTest::newRow("decorated-font") << QStringLiteral("underlined long title")
        << false << false << false << true << 2.0 << true;
}

void TabTitleLayoutTest::matchesQtPixelForPixel()
{
    QFETCH(QString, title); QFETCH(bool, mnemonic); QFETCH(bool, rtl);
    QFETCH(bool, italic); QFETCH(bool, decorated); QFETCH(qreal, ratio); QFETCH(bool, rotated);
    QFont font = qApp->font(); font.setPixelSize(19); font.setItalic(italic); font.setUnderline(decorated);
    const auto direction = rtl ? Qt::RightToLeft : Qt::LeftToRight;
    TabTitleLayout cached;
    for (const int width : {16, 37, 83, 137, 480}) {
        const auto expected = render(title, font, direction, mnemonic, width, ratio, rotated, nullptr);
        const auto actual = render(title, font, direction, mnemonic, width, ratio, rotated, &cached);
        if (actual != expected) {
            qsizetype changed = 0; QRect bounds; QPoint first; QColor actualFirst, expectedFirst;
            for (int y = 0; y < actual.height(); ++y) for (int x = 0; x < actual.width(); ++x) {
                if (actual.pixel(x, y) == expected.pixel(x, y)) continue;
                if (!changed) { first = QPoint(x, y); actualFirst = actual.pixelColor(x, y); expectedFirst = expected.pixelColor(x, y); }
                ++changed; bounds = bounds.united(QRect(x, y, 1, 1));
            }
            qWarning() << "pixel mismatch" << "width" << width << "changed" << changed << "bounds" << bounds
                       << "first" << first << actualFirst << expectedFirst << "glyphs" << cached.retainedGlyphCount();
        }
        QCOMPARE(actual, expected);
    }
}

void TabTitleLayoutTest::keepsOnlyVisibleGlyphsAndReusesThem()
{
    const QString title(100000, QLatin1Char('W'));
    QFont font = qApp->font(); font.setPixelSize(17);
    TabTitleLayout cached;
    const auto first = render(title, font, Qt::LeftToRight, true, 83, 1, false, &cached);
    QCOMPARE(first, render(title, font, Qt::LeftToRight, true, 83, 1, false, nullptr));
    QVERIFY(cached.retainedGlyphCount() > 0); QVERIFY(cached.retainedGlyphCount() < 20);
    QCOMPARE(cached.shapingCount(), quint64(1));
    for (int repeat = 0; repeat < 10; ++repeat)
        QCOMPARE(render(title, font, Qt::LeftToRight, true, 83, 1, false, &cached), first);
    QCOMPARE(cached.shapingCount(), quint64(1));
    QCOMPARE(title.size(), qsizetype(100000));
}

void TabTitleLayoutTest::cacheInvalidationKeepsMeasurementAndPixelsCorrect()
{
    QFont font = qApp->font(); font.setPixelSize(17);
    QString title = QStringLiteral("A wide title with 中文 and العربية content");
    TabTitleLayout cached;
    render(title, font, Qt::LeftToRight, false, 83, 1, false, &cached);
    QCOMPARE(cached.naturalWidth(), QFontMetrics(font).horizontalAdvance(title));
    QCOMPARE(cached.shapingCount(), quint64(1));
    const auto compare = [&](Qt::LayoutDirection direction, bool mnemonic, int width, qreal ratio) {
        QCOMPARE(render(title, font, direction, mnemonic, width, ratio, false, &cached),
                 render(title, font, direction, mnemonic, width, ratio, false, nullptr));
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
        const auto expected = render(title, font, Qt::RightToLeft, true, 137, 1, false, nullptr, dpi);
        QCOMPARE(actual, expected);
        QCOMPARE(actual.logicalDpiX(), dpi); QCOMPARE(actual.logicalDpiY(), dpi);
        QCOMPARE(cached.naturalWidth(), QFontMetrics(font, &actual)
            .size(Qt::TextShowMnemonic | Qt::TextSingleLine, title).width());
        QCOMPARE(cached.shapingCount(), ++expectedShapes);
        QCOMPARE(render(title, font, Qt::RightToLeft, true, 137, 1, false, &cached, dpi), expected);
        QCOMPARE(cached.shapingCount(), expectedShapes);
    }
}

void TabTitleLayoutTest::colorChangesReuseTheShapedGlyphs()
{
    const auto title = QStringLiteral("Colored 中文 title");
    QFont font = qApp->font(); font.setPixelSize(19);
    TabTitleLayout cached;
    QImage previous;
    for (const auto ink : {QColor("#19202c"), QColor("#d02030"), QColor("#2050c0")}) {
        const auto actual = render(title, font, Qt::LeftToRight, true, 83, 1.5, false, &cached, 96, ink);
        QCOMPARE(actual, render(title, font, Qt::LeftToRight, true, 83, 1.5, false, nullptr, 96, ink));
        QCOMPARE(cached.shapingCount(), quint64(1));
        if (!previous.isNull()) QVERIFY(actual != previous);
        previous = actual;
    }
}

QTEST_MAIN(TabTitleLayoutTest)
#include "tab_title_layout_test.moc"
