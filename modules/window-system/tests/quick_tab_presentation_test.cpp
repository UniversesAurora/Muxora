// SPDX-License-Identifier: MIT
// Quick rendering design sections 2 and 4: an actual, non-painting TabBar
// publishes both planes; the Quick consumer outlives its source safely.
// Dimensions: horizontal/right placement x base/lift x source lifetime.
#include "docking/tab_bar.hpp"
#include "quick/tab_foreground_item.hpp"
#include "quick/tab_label_item.hpp"
#include "quick/tab_label_text_item.hpp"
#include "quick/tab_strip_item.hpp"
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

class QuickTabPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { TabBar::setAnimationsEnabled(false); }

    void stoppedWidgetPublishesToQuickAndCanBeDestroyed_data()
    {
        QTest::addColumn<TabBarPlacement>("placement");
        QTest::newRow("horizontal") << TabBarPlacement::Top;
        QTest::newRow("right-mirrored") << TabBarPlacement::Right;
    }

    void stoppedWidgetPublishesToQuickAndCanBeDestroyed()
    {
        QFETCH(TabBarPlacement, placement);
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*(ShaderEffect|QML|shader|texture).*")));
        QQuickWindow window;
        auto format = window.format();
        format.setSamples(4);
        window.setFormat(format);
        window.resize(600, 420);
        auto* strip = new TabStripItem(window.contentItem());
        auto* foreground = new TabForegroundItem(window.contentItem());
        foreground->setZ(1);

        auto source = std::make_unique<TabBar>(defaultTheme(), nullptr);
        source->setAppearance({}, placement);
        for (int index = 0; index < 3; ++index) {
            const auto tab = source->addTab({}, QStringLiteral("Workspace %1").arg(index));
            source->setTabData(tab, QUuid::createUuid());
        }
        source->setAvailableSize({600, 420});
        source->resize(source->displaySize({600, 420}));
        source->setSurfaceContext(placement == TabBarPlacement::Top
            ? QRectF(0, source->height(), 600, 300) : QRectF(-400, 0, 400, 420), 1);
        source->setCurrentIndex(0);
        source->setUpdatesEnabled(false);
        const QPointer<TabBar> lifetime = source.get();
        std::shared_ptr<const TabPresentationFrame> published;
        bool accepted = true;
        connect(source.get(), &TabBar::presentationInvalidated, &window, [&] {
            published = lifetime->presentationFrame();
            strip->setSize(published->size);
            const bool baseAccepted = strip->setFrame(published->base);
            const bool foregroundAccepted = foreground->setFrame(published->foreground);
            accepted = accepted && baseAccepted && foregroundAccepted;
        });
        source->setPresentationPublishing(true);
        QCoreApplication::sendPostedEvents(source.get(), QEvent::MetaCall);
        QVERIFY(accepted);
        QCOMPARE(strip->labelCount(), 3);
        const auto initial = published;
        source->setTabText(0, QStringLiteral("Updated before lift العربية"));
        const auto id = source->tabData(0).toUuid();
        source->setDraggedTab(id);
        source->setLiftedWithAnchor(QPoint(80, 100), DragAnchor{{10, 8}, source->displayTabRect(0).size()});
        QCoreApplication::sendPostedEvents(source.get(), QEvent::MetaCall);
        QVERIFY(accepted);
        QVERIFY(published != initial);
        QVERIFY(initial->foreground->carried.isEmpty());
        QCOMPARE(published->foreground->carried.size(), 1);
        QCOMPARE(published->foreground->carried[0].id, id);
        QCOMPARE(published->foreground->carried[0].label.state.title, QStringLiteral("Updated before lift العربية"));
        QVERIFY(!published->foreground->shadows.isEmpty());
        TabLabelItem* lifted = nullptr;
        for (auto* item : foreground->childItems())
            if (auto* label = dynamic_cast<TabLabelItem*>(item)) lifted = label;
        QVERIFY(lifted);
        QVERIFY(!source->updatesEnabled());
        QVERIFY(!source->isVisible());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(lifted->textItem()->renderStats().nativeTextBuilds > 0);
        QTRY_VERIFY(foreground->shadowStats().textureUploads > 0);

        // Destruction cancels a queued update, but does not invalidate the
        // already published values or their window-owned native resources.
        source->setTabText(0, QStringLiteral("Never delivered"));
        source.reset();
        QVERIFY(lifetime.isNull());
        QSignalSpy rendered(&window, &QQuickWindow::frameSwapped);
        window.requestUpdate();
        QTRY_VERIFY(!rendered.isEmpty());
        QCOMPARE(published->foreground->carried[0].label.state.title, QStringLiteral("Updated before lift العربية"));
    }
};

QTEST_MAIN(QuickTabPresentationTest)
#include "quick_tab_presentation_test.moc"
