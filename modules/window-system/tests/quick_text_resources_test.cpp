// SPDX-License-Identifier: MIT
// Quick rendering design, sections 2-4: retained shaping, bounded resources,
// source roles, and scene-graph recreation. No static appearance assertions.
// Dimensions: source role x action opacity x viewport change x SG lifetime.
#include "quick/tab_label_text_item.hpp"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
TabLabelPresentation labelWithText(const QString& text)
{
    TabLabelPresentation label;
    label.parts.tab = QRect(0, 0, 240, 28);
    label.parts.title = QRect(28, 0, 180, 28);
    label.parts.action = QRect(210, 4, 20, 20);
    label.state.shape = TabShape::Top;
    label.state.title = text;
    label.state.actionShown = true;
    label.state.actionOpacity = 0;
    label.naturalWidth = 600;
    return label;
}
}

class QuickTextResourcesTest final : public QObject {
    Q_OBJECT
private slots:
    void opacityAndViewportChangesReuseShaping()
    {
        QQuickWindow window;
        window.resize(360, 240);
        window.setPersistentGraphics(false);
        window.setPersistentSceneGraph(false);
        auto* item = new TabLabelTextItem(window.contentItem());
        auto label = labelWithText(QStringLiteral("Retained العربية office"));
        TabStripLabelStyle style;
        QVERIFY(item->setLabel(label, style));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item->renderStats().nativeTextBuilds > 0);
        const auto initial = item->renderStats();
        label.state.actionOpacity = 0.5;
        QVERIFY(item->setLabel(label, style));
        QSignalSpy rendered(&window, &QQuickWindow::frameSwapped);
        window.update();
        QTRY_VERIFY(!rendered.isEmpty());
        QCOMPARE(item->renderStats().textLayoutBuilds, initial.textLayoutBuilds);
        QCOMPARE(item->renderStats().nativeTextBuilds, initial.nativeTextBuilds);

        label.parts.title.setWidth(120);
        QVERIFY(item->setLabel(label, style));
        rendered.clear();
        window.update();
        QTRY_VERIFY(!rendered.isEmpty());
        QCOMPARE(item->renderStats().textLayoutBuilds, initial.textLayoutBuilds);
        const auto beforeRelease = item->renderStats();
        window.hide();
        QTRY_VERIFY(!window.isExposed());
        window.releaseResources();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item->renderStats().sceneGraphBuilds > beforeRelease.sceneGraphBuilds);
        QVERIFY(item->renderStats().nativeTextBuilds > beforeRelease.nativeTextBuilds);
        QCOMPARE(item->renderStats().textLayoutBuilds, initial.textLayoutBuilds);
    }

    void rejectedInputReleasesResourcesAndAcceptsNewSource()
    {
        QQuickWindow window;
        window.resize(360, 240);
        auto* item = new TabLabelTextItem(window.contentItem());
        TabStripLabelStyle style;
        auto label = labelWithText(QStringLiteral("A valid source"));
        QVERIFY(item->setLabel(label, style));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item->renderStats().nativeTextBuilds > 0);
        const auto before = item->renderStats();
        label.state.title = QString(100000, QLatin1Char('x'));
        QVERIFY(!item->setLabel(label, style));
        QCOMPARE(item->textError(), TabTextError::SourceBudgetExceeded);
        QCOMPARE(item->segmentCount(), 0);
        QCOMPARE(item->retainedGlyphInstances(), 0);
        QCOMPARE(item->renderStats().textLayoutBuilds, before.textLayoutBuilds);
        label.state.title = QString(12000, QLatin1Char('i'));
        QVERIFY(!item->setLabel(label, style));
        QCOMPARE(item->textError(), TabTextError::GlyphBudgetExceeded);
        QCOMPARE(item->retainedGlyphInstances(), 0);
        label.state.title = QStringLiteral("The replacement source");
        QVERIFY(item->setLabel(label, style));
        QCOMPARE(item->textError(), TabTextError::None);
        QTRY_VERIFY(item->renderStats().nativeTextBuilds > before.nativeTextBuilds);
        QVERIFY(item->retainedGlyphInstances() > 0);
    }

    void sourceRolesPreserveLiteralNamesAndCompleteNumbers()
    {
        TabLabelTextItem item;
        TabStripLabelStyle style;
        auto label = labelWithText(QStringLiteral("A&&B"));
        label.naturalWidth = 0;
        label.state.actionShown = false;
        QVERIFY(item.setLabel(label, style, TabTextRole::Title));
        const auto mnemonicCount = item.retainedGlyphInstances();
        QVERIFY(item.setLabel(label, style, TabTextRole::Literal));
        QVERIFY(item.retainedGlyphInstances() > mnemonicCount);
        label.state.title = QStringLiteral("123");
        label.state.shape = TabShape::Tile;
        QVERIFY(item.setLabel(label, style, TabTextRole::Title));
        QCOMPARE(item.retainedGlyphInstances(), 1);
        QVERIFY(item.setLabel(label, style, TabTextRole::Number));
        QCOMPARE(item.retainedGlyphInstances(), 3);

        label.state.shape = TabShape::Row;
        label.state.title = QStringLiteral("1234567890") + QChar(0x009c) + QStringLiteral("12");
        label.parts.title.setWidth(1);
        QVERIFY(item.setLabel(label, style, TabTextRole::Literal));
        QCOMPARE(item.retainedGlyphInstances(), 2);
        const auto layouts = item.renderStats().textLayoutBuilds;
        label.parts.title.setWidth(4096);
        QVERIFY(item.setLabel(label, style, TabTextRole::Literal));
        QCOMPARE(item.retainedGlyphInstances(), 10);
        QCOMPARE(item.renderStats().textLayoutBuilds, layouts);
    }
};

QTEST_MAIN(QuickTextResourcesTest)
#include "quick_text_resources_test.moc"
