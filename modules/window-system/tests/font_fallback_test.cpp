// SPDX-License-Identifier: MIT
// Interface font fallbacks on Windows (platform/windows/font_families.cpp):
// prefilled lists still find each script's glyphs, without Qt loading every
// installed family on the first text, which takes about a second with many
// fonts. See docs/development/lessons/windows-platform.md.
#include "platform/font_families.hpp"

#include <QtGui/private/qfontdatabase_p.h>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QTest>
#include <QTextLayout>

class FontFallbackTest final : public QObject {
    Q_OBJECT
private slots:
    void firstTextFindsGlyphsWithoutLoadingEveryFamily()
    {
        if (!QFontDatabase::families().contains(QStringLiteral("Segoe UI"))) QSKIP("Segoe UI is not installed");
        // The interface font's own list, with a generic name Windows lacks.
        const auto families = window_system::resolvePlatformFontFamilies(
            {QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei"), QStringLiteral("sans-serif")});
        window_system::prefillFontFallbacks(families, QFont::SansSerif);
        QFont font;
        font.setFamilies(families);
        font.setStyleHint(QFont::SansSerif);
        // Latin, Chinese, a symbol and an emoji, which Segoe UI lacks, and
        // Devanagari, which only the system's mapping names.
        for (const char32_t character : {U'A', U'文', U'✓', U'\U0001F600', U'क'}) {
            QTextLayout layout(QString::fromUcs4(&character, 1), font);
            layout.beginLayout(); layout.createLine(); layout.endLayout();
            const auto runs = layout.glyphRuns();
            QVERIFY2(!runs.isEmpty() && runs.first().glyphIndexes().value(0) != 0,
                     qPrintable(QStringLiteral("U+%1").arg(quint32(character), 4, 16, QLatin1Char('0'))));
        }
        // Qt's own search populates every family; the lists name few.
        const auto* database = QFontDatabasePrivate::instance();
        int populated = 0;
        for (int index = 0; index < database->count; ++index) populated += database->families[index]->populated;
        QVERIFY2(populated < database->count / 2,
                 qPrintable(QStringLiteral("%1 of %2 families loaded").arg(populated).arg(database->count)));
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    FontFallbackTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "font_fallback_test.moc"
