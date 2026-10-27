// SPDX-License-Identifier: MIT
// Cached GUI publication without exposed windows or native input. Covers
// unchanged reads, immutable frames, sample-only motion, metadata/font/layout
// invalidation, hidden paint updates, and coalesced publication generations.
#include "docking/tab_bar.hpp"
#include "docking/dock_surface.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;

namespace {
void arrange(TabBar& bar)
{
    TabAppearance appearance;
    bar.setAppearance(appearance, TabBarPlacement::Left);
    for (int index = 0; index < 5; ++index) {
        const int added = bar.addTab({}, QStringLiteral("Workspace %1").arg(index));
        bar.setTabData(added, QUuid::createUuid());
    }
    bar.setAvailableSize({180, 500});
    bar.resize(bar.displaySize({180, 500}));
    bar.setAppearance(appearance, TabBarPlacement::Left);
    bar.setCurrentIndex(0); bar.setCarriedDisplay(true);
}
}

class TabCarrierSourceTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { TabBar::setAnimationsEnabled(false); }

    void unchangedReadsShareFramesAndSamplesDoNotCopyContent()
    {
        TabBar bar(defaultTheme(), nullptr); arrange(bar);
        const auto first = bar.carrierFrame();
        QCOMPARE(first->samples.size(), 5);
        for (int query = 0; query < 100; ++query) QCOMPARE(bar.carrierFrame().get(), first.get());
        bar.setColumnReveal(0.5);
        const auto next = bar.carrierFrame();
        QVERIFY(next != first);
        QCOMPARE(next->content.get(), first->content.get());
        QCOMPARE(first->samples[0].titleOpacity, qreal(1));
        QCOMPARE(next->samples[0].titleOpacity, qreal(0.5));
        QCOMPARE(first->content->labels[0].label.state.titleOpacity, qreal(1));
        QVERIFY(!bar.isVisible());
    }

    void releasedFrameBuffersAreReused()
    {
        TabBar bar(defaultTheme(), nullptr); arrange(bar);
        std::shared_ptr<const TabCarrierFrame> current;
        QSet<const TabCarrierFrame*> buffers;
        for (int step = 1; step <= 100; ++step) {
            bar.setColumnReveal(step % 2 ? 0.4 : 0.6);
            auto next = bar.carrierFrame();
            buffers.insert(next.get());
            if (current) QCOMPARE(current->samples[0].titleOpacity, step % 2 ? qreal(0.6) : qreal(0.4));
            current = std::move(next);
        }
        QCOMPARE(buffers.size(), 2);
    }

    void metadataFontAndLayoutInvalidateOwnedContent()
    {
        TabBar bar(defaultTheme(), nullptr); arrange(bar);
        const auto before = bar.carrierFrame();
        const auto original = before->content->labels[0].id;
        const auto replacement = QUuid::createUuid();
        bar.setTabData(0, replacement);
        const auto renamed = bar.carrierFrame();
        QVERIFY(renamed->content != before->content);
        QCOMPARE(renamed->content->labels[0].id, replacement);
        QCOMPARE(before->content->labels[0].id, original);
        bar.setTabText(0, QStringLiteral("Changed title"));
        const auto title = bar.carrierFrame();
        QCOMPARE(title->content->labels[0].label.state.title, QStringLiteral("Changed title"));
        bar.setCurrentIndex(1);
        const auto selected = bar.carrierFrame();
        QVERIFY(!selected->content->labels[0].label.state.selected);
        QVERIFY(selected->content->labels[1].label.state.selected);
        auto font = bar.font(); font.setPixelSize(21); bar.setFont(font);
        const auto resized = bar.carrierFrame();
        QCOMPARE(resized->content->labels[0].label.font.pixelSize(), 21);
        QVERIFY(resized->samples[0].parts.tab.height() >= selected->samples[0].parts.tab.height());
        bar.setColumnReveal(0);
        QVERIFY(bar.carrierFrame()->content != resized->content);
    }

    void stoppedWidgetPaintingDoesNotStopStateNotifications()
    {
        TabBar bar(defaultTheme(), nullptr); arrange(bar);
        const auto first = bar.carrierFrame();
        QSignalSpy changes(&bar, &TabBar::carrierInvalidated);
        bar.setUpdatesEnabled(false);
        bar.setColumnReveal(0.6);
        QVERIFY(!changes.isEmpty());
        const auto next = bar.carrierFrame();
        QCOMPARE(next->content.get(), first->content.get());
        QCOMPARE(next->samples[0].titleOpacity, qreal(0.6));
        QVERIFY(!bar.updatesEnabled()); QVERIFY(!bar.isVisible());
    }

    void queuedPublicationsCoalesceAndIgnoreAnOldPresentation()
    {
        WindowManager manager;
        const auto id = manager.createWindow(WindowKind::Workbench, {"Carrier publication", QSize(600, 400)});
        auto* host = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
        auto* source = host->surface();
        QSignalSpy changes(source, &DockSurface::carrierChanged);
        source->setDragPresentation(true, false);
        source->setDragPresentation(true, false);
        source->setDragPresentation(true, false);
        QCoreApplication::sendPostedEvents(source, QEvent::MetaCall);
        QCOMPARE(changes.size(), 1);
        source->setDragPresentation(true, false);
        source->setDragPresentation(false, false);
        source->setDragPresentation(true, false);
        QCoreApplication::sendPostedEvents(source, QEvent::MetaCall);
        QCOMPARE(changes.size(), 2);
        source->setDragPresentation(true, false);
        source->setDragPresentation(false, false);
        QCoreApplication::sendPostedEvents(source, QEvent::MetaCall);
        QCOMPARE(changes.size(), 2);
        QVERIFY(!host->isVisible());
    }
};

QTEST_MAIN(TabCarrierSourceTest)
#include "tab_carrier_source_test.moc"
