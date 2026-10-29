// SPDX-License-Identifier: MIT
// Complete tab-plane publication (Quick rendering design sections 2 and 4).
// Dimensions: publishing on/off x queued state x paint disabled x source
// lifetime. Checks owned data and event delivery, never rendered appearance.
#include "docking/tab_bar.hpp"
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;

namespace {
void arrange(TabBar& bar)
{
    bar.setAppearance({}, TabBarPlacement::Left);
    for (int index = 0; index < 3; ++index) {
        const int added = bar.addTab({}, QStringLiteral("Workspace %1").arg(index));
        bar.setTabData(added, QUuid::createUuid());
    }
    bar.setAvailableSize({200, 400});
    bar.resize(bar.displaySize({200, 400}));
    bar.setAppearance({}, TabBarPlacement::Left);
    bar.setCurrentIndex(0);
}

const TabStripLabelSample& sample(const TabPresentationFrame& frame, QUuid id)
{
    for (const auto& lane : frame.base->lanes) {
        for (const auto& label : lane.regular) if (label.id == id) return label;
        for (const auto& label : lane.frozen) if (label.id == id) return label;
    }
    Q_UNREACHABLE();
}

void mouse(TabBar& bar, QEvent::Type type, QPoint point,
           Qt::MouseButton button = Qt::NoButton, Qt::MouseButtons buttons = Qt::NoButton)
{
    QMouseEvent event(type, QPointF(point), QPointF(bar.mapToGlobal(point)), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &event);
}
}

class TabPresentationSourceTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { TabBar::setAnimationsEnabled(false); }

    void optInNotificationsCoalesceAndReadsShareAnImmutableCapture()
    {
        TabBar bar(defaultTheme(), nullptr);
        arrange(bar);
        QSignalSpy changes(&bar, &TabBar::presentationInvalidated);
        bar.setTabText(0, QStringLiteral("Unobserved change"));
        QCoreApplication::sendPostedEvents(&bar, QEvent::MetaCall);
        QVERIFY(changes.isEmpty());
        bar.setPresentationPublishing(true);
        bar.setTabText(0, QStringLiteral("First title"));
        bar.setGroupFocused(true);
        QCoreApplication::sendPostedEvents(&bar, QEvent::MetaCall);
        QCOMPARE(changes.size(), 1);
        const auto first = bar.presentationFrame();
        QCOMPARE(first->size, bar.size());
        QVERIFY(first->base);
        QVERIFY(first->foreground);
        const auto id = bar.tabData(0).toUuid();
        QCOMPARE(sample(*first, id).label.state.title, QStringLiteral("First title"));
        for (int read = 0; read < 100; ++read) QCOMPARE(bar.presentationFrame().get(), first.get());
        bar.setTabText(0, QStringLiteral("Second title"));
        const auto next = bar.presentationFrame();
        QVERIFY(next != first);
        QCOMPARE(sample(*first, id).label.state.title, QStringLiteral("First title"));
        QCOMPARE(sample(*next, id).label.state.title, QStringLiteral("Second title"));
        bar.setPresentationPublishing(false);
        QCoreApplication::sendPostedEvents(&bar, QEvent::MetaCall);
        QCOMPARE(changes.size(), 1);
    }

    void hoverAndPointerStatePublishWhileWidgetPaintingIsStopped()
    {
        TabBar bar(defaultTheme(), nullptr);
        arrange(bar);
        bar.setUpdatesEnabled(false);
        bar.setPresentationPublishing(true);
        const auto id = bar.tabData(0).toUuid();
        mouse(bar, QEvent::MouseMove, bar.titleRect(0).center());
        const auto titleHover = bar.presentationFrame();
        const auto action = bar.actionRect(0);
        QVERIFY(!action.isEmpty());
        mouse(bar, QEvent::MouseMove, action.center());
        const auto actionHover = bar.presentationFrame();
        QVERIFY(actionHover != titleHover);
        // These are the input states delivered to either renderer, not an
        // assertion about a button's static appearance or a screenshot.
        QVERIFY(!sample(*titleHover, id).label.state.actionHovered);
        QVERIFY(sample(*actionHover, id).label.state.actionHovered);
        mouse(bar, QEvent::MouseButtonPress, action.center(), Qt::LeftButton, Qt::LeftButton);
        const auto pressed = bar.presentationFrame();
        QVERIFY(sample(*pressed, id).label.state.actionPressed);
        mouse(bar, QEvent::MouseButtonRelease, action.center(), Qt::LeftButton);
        QVERIFY(!sample(*bar.presentationFrame(), id).label.state.actionPressed);
        QVERIFY(!bar.updatesEnabled());
        QVERIFY(!bar.isVisible());
    }

    void liftedLabelsAndBaseBelongToTheSameCapture()
    {
        TabBar bar(defaultTheme(), nullptr);
        arrange(bar);
        bar.setSurfaceContext(QRectF(200, 0, 400, 400), 1);
        const auto id = bar.tabData(0).toUuid();
        const auto before = bar.presentationFrame();
        QVERIFY(before->foreground->carried.isEmpty());
        bar.setUpdatesEnabled(false);
        bar.setDraggedTab(id);
        bar.setLiftedWithAnchor(QPoint(80, 100), DragAnchor{{10, 8}, bar.displayTabRect(0).size()});
        const auto lifted = bar.presentationFrame();
        QCOMPARE(lifted->foreground->carried.size(), 1);
        QCOMPARE(lifted->foreground->carried.first().id, id);
        QVERIFY(!lifted->foreground->shadows.isEmpty());
        QVERIFY(before->foreground->carried.isEmpty());
    }

    void oldQueuedSessionsAndDeletedSourcesDoNotNotify()
    {
        auto* bar = new TabBar(defaultTheme(), nullptr);
        arrange(*bar);
        QObject observer;
        int notifications = 0;
        connect(bar, &TabBar::presentationInvalidated, &observer, [&] { ++notifications; });
        bar->setPresentationPublishing(true);
        bar->setPresentationPublishing(false);
        bar->setPresentationPublishing(true);
        QCoreApplication::sendPostedEvents(bar, QEvent::MetaCall);
        QCOMPARE(notifications, 1);
        const auto retained = bar->presentationFrame();
        const auto id = bar->tabData(0).toUuid();
        bar->setTabText(0, QStringLiteral("Queued before deletion"));
        QPointer<TabBar> lifetime = bar;
        delete bar;
        QVERIFY(lifetime.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(notifications, 1);
        QCOMPARE(sample(*retained, id).label.state.title, QStringLiteral("Workspace 0"));
    }
};

QTEST_MAIN(TabPresentationSourceTest)
#include "tab_presentation_source_test.moc"
