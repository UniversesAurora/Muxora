// SPDX-License-Identifier: MIT
// Tab strip design 5, 6, 8.3 and 12: release targets and press anchors.
// Dimensions: left/middle/right, label/action/folder, sticky close policy,
// matching/moved release, drag threshold and model-driven cancellation.
#include "docking/tab_pointer_controller.hpp"
#include <QTest>

using namespace window_system;
using Action = TabPointerDecision::Action;

class TabPointerControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void labelPressActivatesWhileRightClickLeavesSelectionAlone()
    {
        TabPointerController input;
        auto decision = input.press(Qt::LeftButton, {2}, {});
        QCOMPARE(decision.action, Action::ActivateTab);
        QCOMPARE(decision.index, 2);
        QCOMPARE(input.press(Qt::RightButton, {4}, {}).action, Action::None);
        QCOMPARE(input.release(Qt::LeftButton, {4}, 0, true).action, Action::None);
        QCOMPARE(input.move(Qt::LeftButton, {100, 100}, 10).action, Action::None);
    }

    void actionReleaseRequiresThePressedButtonAndUsesCurrentStickyState()
    {
        TabPointerController input;
        const TabPointerHit action{2, 2};
        for (const int stickyCount : {0, 3}) {
            QCOMPARE(input.press(Qt::LeftButton, action, {}).action, Action::None);
            QCOMPARE(input.release(Qt::LeftButton, {3, 3}, stickyCount, true).action, Action::None);
            (void)input.press(Qt::LeftButton, action, {});
            const auto decision = input.release(Qt::LeftButton, action, stickyCount, true);
            QCOMPARE(decision.action, stickyCount ? Action::UnstickTab : Action::CloseTab);
            QCOMPARE(decision.index, 2);
            QCOMPARE(input.release(Qt::LeftButton, action, stickyCount, true).action, Action::None);
        }
    }

    void folderReleaseUsesTheReleaseTargetAndDoesNotActivateATab()
    {
        TabPointerController input;
        for (bool add : {false, true}) {
            QCOMPARE(input.press(Qt::LeftButton, {-1, -1, 1}, {}).action, Action::None);
            const auto decision = input.release(Qt::LeftButton, {-1, -1, 1, add}, 0, true);
            QCOMPARE(decision.action, add ? Action::AddTabToFolder : Action::ToggleFolder);
            QCOMPARE(decision.index, 1);
            QCOMPARE(input.release(Qt::LeftButton, {-1, -1, 1, add}, 0, true).action, Action::None);
        }
        (void)input.press(Qt::LeftButton, {-1, -1, 1}, {});
        QCOMPARE(input.release(Qt::LeftButton, {-1, -1, 2}, 0, true).action, Action::None);
    }

    void dragThresholdPreservesThePressGrabAndStartsOnlyOnce()
    {
        const auto start = DragStart::capture({300, 400}, {87, 29}, {20, 20, 100, 30});
        for (bool folder : {false, true}) {
            TabPointerController input;
            const TabPointerHit target = folder ? TabPointerHit{-1, -1, 2} : TabPointerHit{2};
            (void)input.press(Qt::LeftButton, target, start);
            QCOMPARE(input.move(Qt::LeftButton, {304, 405}, 10).action, Action::None);
            QCOMPARE(input.move(Qt::NoButton, {310, 400}, 10).action, Action::None);
            const auto decision = input.move(Qt::LeftButton, {304, 406}, 10);
            QCOMPARE(decision.action, folder ? Action::DragFolder : Action::DragTab);
            QCOMPARE(decision.index, 2);
            QCOMPARE(decision.start.pressGlobal, QPoint(300, 400));
            QCOMPARE(decision.start.currentGlobal, QPoint(304, 406));
            QCOMPARE(decision.start.anchor.offset, QPoint(67, 9));
            QCOMPARE(decision.start.anchor.sourceSize, QSize(100, 30));
            QCOMPARE(input.move(Qt::LeftButton, {350, 450}, 10).action, Action::None);
            QCOMPARE(input.release(Qt::LeftButton, target, 0, true).action, Action::None);
        }
    }

    void middleReleaseClosesOnlyTheSameAllowedTab()
    {
        TabPointerController input;
        for (bool protect : {false, true}) {
            (void)input.press(Qt::MiddleButton, {1}, {});
            QCOMPARE(input.release(Qt::MiddleButton, {2}, 2, protect).action, Action::None);
            (void)input.press(Qt::MiddleButton, {1}, {});
            QCOMPARE(input.release(Qt::MiddleButton, {1}, 2, protect).action,
                     protect ? Action::None : Action::CloseTab);
            QCOMPARE(input.release(Qt::MiddleButton, {1}, 2, protect).action, Action::None);
        }
        (void)input.press(Qt::MiddleButton, {2}, {});
        QCOMPARE(input.release(Qt::MiddleButton, {2}, 2, true).action, Action::CloseTab);
    }

    void modelChangesCancelOnlyTheirOwnPressTargets()
    {
        TabPointerController input;
        for (const TabPointerHit tab : {TabPointerHit{2}, TabPointerHit{2, 2}}) {
            (void)input.press(Qt::LeftButton, tab, {});
            (void)input.press(Qt::MiddleButton, {2}, {});
            input.cancelTabs();
            QCOMPARE(input.release(Qt::MiddleButton, {2}, 0, true).action, Action::None);
            QCOMPARE(input.move(Qt::LeftButton, {100, 100}, 10).action, Action::None);
            QCOMPARE(input.release(Qt::LeftButton, tab, 0, true).action, Action::None);
        }
        (void)input.press(Qt::LeftButton, {-1, -1, 1}, {});
        input.cancelTabs();
        QCOMPARE(input.release(Qt::LeftButton, {-1, -1, 1}, 0, true).action, Action::ToggleFolder);
        (void)input.press(Qt::MiddleButton, {2}, {});
        (void)input.press(Qt::LeftButton, {-1, -1, 1}, {});
        input.cancelFolders();
        QCOMPARE(input.release(Qt::LeftButton, {-1, -1, 1}, 0, true).action, Action::None);
        QCOMPARE(input.release(Qt::MiddleButton, {2}, 0, true).action, Action::CloseTab);
    }
};

QTEST_APPLESS_MAIN(TabPointerControllerTest)
#include "tab_pointer_controller_test.moc"
