// SPDX-License-Identifier: MIT
// Existing tab strip navigation: orientation, model-order boundaries,
// no current tab, Home/End, empty-strip acceptance and expanded Escape.
#include "docking/tab_keyboard_controller.hpp"
#include <QTest>

using namespace window_system;
using Action = TabKeyboardDecision::Action;

class TabKeyboardControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void navigationKeepsModelOrderAndStopsAtTheEnds_data()
    {
        QTest::addColumn<int>("key"); QTest::addColumn<int>("current");
        QTest::addColumn<int>("count"); QTest::addColumn<bool>("vertical"); QTest::addColumn<int>("expected");
        QTest::newRow("horizontal-forward") << int(Qt::Key_Right) << 1 << 4 << false << 2;
        QTest::newRow("vertical-backward") << int(Qt::Key_Up) << 2 << 4 << true << 1;
        QTest::newRow("first-tab") << int(Qt::Key_Left) << 0 << 4 << false << 0;
        QTest::newRow("last-tab") << int(Qt::Key_Down) << 3 << 4 << true << 3;
        QTest::newRow("no-current-tab") << int(Qt::Key_Right) << -1 << 4 << false << 0;
        QTest::newRow("home") << int(Qt::Key_Home) << 2 << 4 << true << 0;
        QTest::newRow("end") << int(Qt::Key_End) << 1 << 4 << false << 3;
        QTest::newRow("single-tab") << int(Qt::Key_Up) << 0 << 1 << true << 0;
    }
    void navigationKeepsModelOrderAndStopsAtTheEnds()
    {
        QFETCH(int, key); QFETCH(int, current); QFETCH(int, count);
        QFETCH(bool, vertical); QFETCH(int, expected);
        const auto decision = tabKeyboardDecision(key, current, count, vertical, false);
        QCOMPARE(decision.action, Action::ActivateTab);
        QCOMPARE(decision.index, expected);
    }

    void emptyStripsAndOtherKeysPreserveAcceptanceRules()
    {
        for (bool vertical : {false, true}) {
            const int forward = vertical ? Qt::Key_Down : Qt::Key_Right;
            const int perpendicular = vertical ? Qt::Key_Left : Qt::Key_Up;
            QCOMPARE(tabKeyboardDecision(forward, -1, 0, vertical, false).action, Action::Ignore);
            QCOMPARE(tabKeyboardDecision(perpendicular, 1, 4, vertical, false).action, Action::Ignore);
            for (int key : {Qt::Key_Home, Qt::Key_End})
                QCOMPARE(tabKeyboardDecision(key, -1, 0, vertical, false).action, Action::Accept);
        }
        QCOMPARE(tabKeyboardDecision(Qt::Key_A, 1, 4, false, true).action, Action::Ignore);
        QCOMPARE(tabKeyboardDecision(Qt::Key_Escape, 1, 4, false, false).action, Action::Ignore);
        QCOMPARE(tabKeyboardDecision(Qt::Key_Escape, -1, 0, false, true).action, Action::Collapse);
    }
};

QTEST_APPLESS_MAIN(TabKeyboardControllerTest)
#include "tab_keyboard_controller_test.moc"
