// SPDX-License-Identifier: MIT
// Label storage, immutable publication and bounded GUI text preparation.
// No Widgets dependency, exposed windows, or native input.
#include "docking/tab_label_store.hpp"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTest>

using namespace window_system;

class TabLabelStoreTest final : public QObject {
    Q_OBJECT
private slots:
    void retainedContentSurvivesRenumberingStyleChangesAndSourceDestruction()
    {
        std::shared_ptr<const TabLabelContent> old, changed;
        auto style = std::make_shared<TabStripLabelStyle>();
        style->font = QGuiApplication::font(); style->showNumbers = true;
        TabColorPreset preset; preset.id = QStringLiteral("accent"); style->colors.append(preset);
        {
            TabLabelStore store;
            store.append({}, QStringLiteral("First")); store.append({}, QStringLiteral("Second"));
            const auto id = QUuid::createUuid(); QVERIFY(store.setData(1, id));
            QVERIFY(store.setColor(1, preset.id));
            old = store.content(1, style);
            QCOMPARE(store.content(1, style).get(), old.get());
            QCOMPARE(old->number, QStringLiteral("2")); QVERIFY(old->preset);
            store.removeAt(0); QCOMPARE(store.indexOf(id), 0);
            const auto renumbered = store.content(0, style);
            QCOMPARE(renumbered->number, QStringLiteral("1")); QVERIFY(renumbered != old);
            store.setToolTip(0, QStringLiteral("Tip"));
            QCOMPARE(store.content(0, style).get(), renumbered.get());
            QVERIFY(store.setTitle(0, QStringLiteral("Changed")));
            auto next = std::make_shared<TabStripLabelStyle>(*style); next->showNumbers = false;
            changed = store.content(0, next);
            QVERIFY(changed->number.isEmpty()); QCOMPARE(changed->style.get(), next.get());
        }
        QCOMPARE(old->title, QStringLiteral("Second")); QCOMPARE(old->number, QStringLiteral("2"));
        QCOMPARE(old->preset->id, QStringLiteral("accent"));
        QCOMPARE(changed->title, QStringLiteral("Changed"));
    }

    void dirtyTitlesReuseOtherTextCachesAndFontChangesRemeasureAll()
    {
        TabLabelStore store; store.append({}, QStringLiteral("First 标题")); store.append({}, QStringLiteral("Second"));
        store.append({}, QStringLiteral("&Mnemonic 标题"));
        QImage image(400, 100, QImage::Format_RGB32); image.fill(Qt::white);
        image.setDotsPerMeterX(qRound(144 / 0.0254)); image.setDotsPerMeterY(qRound(144 / 0.0254));
        QFont font = QGuiApplication::font(); font.setPointSizeF(11);
        store.measureTitles(font, Qt::LeftToRight, &image);
        const QFontMetrics metrics(font, &image);
        QCOMPARE(store.at(0).titleWidth, metrics.size(Qt::TextShowMnemonic | Qt::TextSingleLine, store.title(0)).width());
        QCOMPARE(store.at(2).titleWidth, metrics.size(Qt::TextShowMnemonic | Qt::TextSingleLine, store.title(2)).width());
        const int previousWidth = store.at(0).titleWidth;
        QPainter painter(&image); const QRect viewport(0, 0, 180, 40);
        store.textLayout(0).draw(painter, viewport); store.textLayout(1).draw(painter, viewport);
        const auto firstShapes = store.textLayout(0).shapingCount(), secondShapes = store.textLayout(1).shapingCount();
        QVERIFY(firstShapes > 0); QVERIFY(secondShapes > 0);
        QVERIFY(store.setTitle(1, QStringLiteral("Changed 第二个标题")));
        QCOMPARE(store.at(1).titleWidth, -1);
        store.measureTitles(font, Qt::LeftToRight, &image);
        store.textLayout(0).draw(painter, viewport); store.textLayout(1).draw(painter, viewport);
        QCOMPARE(store.textLayout(0).shapingCount(), firstShapes);
        QVERIFY(store.textLayout(1).shapingCount() > secondShapes);
        font.setPointSizeF(17); store.measureTitles(font, Qt::LeftToRight, &image, true);
        QVERIFY(store.at(0).titleWidth > previousWidth);
        QVERIFY(!store.setTitle(-1, QStringLiteral("Invalid"))); QCOMPARE(store.size(), 3);
    }
};

QTEST_MAIN(TabLabelStoreTest)
#include "tab_label_store_test.moc"
