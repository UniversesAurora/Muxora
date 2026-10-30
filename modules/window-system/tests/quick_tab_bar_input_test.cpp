// SPDX-License-Identifier: MIT
// Quick tab-strip adapter: actual input and frame clock use the shared state.
// Dimensions: horizontal/right placement x click/key/close x timed activation.
// No static appearance checks.
#include "quick/tab_bar_item.hpp"
#include <QContextMenuEvent>
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

class QuickTabBarInputTest final : public QObject {
    Q_OBJECT
private slots:
    void inputsAndFramesDriveTheSharedController_data()
    {
        QTest::addColumn<bool>("vertical");
        QTest::newRow("horizontal") << false;
        QTest::newRow("right") << true;
    }

    void inputsAndFramesDriveTheSharedController()
    {
        QFETCH(bool, vertical);
        QQuickWindow window;
        auto format = window.format(); format.setSamples(4); window.setFormat(format);
        window.resize(600, 420);
        auto* bar = new TabBarItem(defaultTheme(), window.contentItem());
        bar->setAnimationPolicy(false);
        auto& controller = bar->controller();
        controller.setAppearance({}, vertical ? TabBarPlacement::Right : TabBarPlacement::Top);
        controller.setAvailableSize(vertical ? QSize(200, 420) : QSize(600, 420));
        for (int index = 0; index < 3; ++index) {
            const auto tab = controller.addTab({}, QStringLiteral("Workspace %1").arg(index));
            controller.setTabData(tab, QUuid::createUuid());
        }
        bar->setSize(controller.displaySize(controller.availableSize()));
        bar->synchronize();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(bar->renderingAccepted());
        QSignalSpy activated(bar, &TabBarItem::currentChanged);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, controller.titleRect(1).center());
        QCOMPARE(controller.currentIndex(), 1);
        QVERIFY(!activated.isEmpty());
        int menus = 0;
        connect(bar, &TabBarItem::contextMenuRequested, &window, [&](int tab, int folder, QPoint) {
            QCOMPARE(tab, 0); QCOMPARE(folder, -1); ++menus;
        });
        const QPoint point = controller.titleRect(0).center();
        QContextMenuEvent menu(QContextMenuEvent::Mouse, point, bar->mapToGlobal(point).toPoint());
        QCoreApplication::sendEvent(bar, &menu);
        QCOMPARE(menus, 1);
        QCOMPARE(controller.currentIndex(), 1);
        bar->setPopupOpen(false);
        bar->forceActiveFocus();
        QTest::keyClick(&window, vertical ? Qt::Key_Down : Qt::Key_Right);
        QCOMPARE(controller.currentIndex(), 2);
        connect(bar, &TabBarItem::actionRequested, &window, [&](TabPointerDecision decision) {
            if (decision.action != TabPointerDecision::Action::CloseTab) return;
            controller.removeTab(decision.index); bar->synchronize();
        });
        QTest::mouseClick(&window, Qt::MiddleButton, Qt::NoModifier, controller.titleRect(0).center());
        QCOMPARE(controller.count(), 2);
        bar->setAnimationPolicy(true);
        controller.setCurrentIndex(0);
        bar->synchronize();
        QTRY_VERIFY(!controller.needsFrame());
        QVERIFY(bar->renderingAccepted());
        QPointer<TabBarItem> lifetime = bar;
        connect(bar, &TabBarItem::currentChanged, &window, [bar] { delete bar; });
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, controller.titleRect(1).center());
        QVERIFY(lifetime.isNull());
    }
};

QTEST_MAIN(QuickTabBarInputTest)
#include "quick_tab_bar_input_test.moc"
