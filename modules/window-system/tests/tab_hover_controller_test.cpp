// SPDX-License-Identifier: MIT
#include "docking/tab_hover_controller.hpp"
#include "docking/tab_column_peek.hpp"
#include <QTest>

using namespace window_system;
using Fade = TabHoverTransition::Fade;

class TabHoverControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void columnDwellDoesNotRestartWhileWaitingOrAlreadyExpanded()
    {
        using Timer = TabColumnPeekAction::Timer;
        TabColumnPeek column;
        QCOMPARE(column.hover(true, false, false).timer, Timer::Keep);
        QVERIFY(!column.timeout().relayout);
        QVERIFY(column.setIconsOnly(true));
        QCOMPARE(column.hover(true, false, false).timer, Timer::Start);
        QCOMPARE(column.hover(true, true, false).timer, Timer::Keep);
        QCOMPARE(column.hover(false, true, false).timer, Timer::Stop);
        QCOMPARE(column.hover(true, false, false).timer, Timer::Start);
        const auto opened = column.timeout();
        QVERIFY(opened.relayout); QVERIFY(opened.animate); QVERIFY(column.expanded());
        QVERIFY(!column.timeout().relayout);
        QCOMPARE(column.hover(true, false, false).timer, Timer::Keep);
        QVERIFY(!column.setIconsOnly(true)); QVERIFY(column.expanded());
    }

    void columnPopupAndScrollbarCaptureHoldTheExpansion()
    {
        for (bool popup : {false, true}) {
            TabColumnPeek column; column.setIconsOnly(true); (void)column.timeout();
            column.setPopupOpen(popup);
            const auto held = column.hover(false, false, !popup);
            QCOMPARE(held.timer, TabColumnPeekAction::Timer::Stop);
            QVERIFY(!held.relayout); QVERIFY(column.expanded());
            column.setPopupOpen(false);
            const auto closed = column.hover(false, false, false);
            QVERIFY(closed.relayout); QVERIFY(closed.animate); QVERIFY(!column.expanded());
            QVERIFY(!column.hover(false, false, false).relayout);
        }
    }

    void restoredColumnExpansionUsesNoSecondAnimation()
    {
        TabColumnPeek column;
        QVERIFY(!column.restore(true).relayout);
        column.setIconsOnly(true);
        QVERIFY(!column.restore(false).relayout);
        const auto restored = column.restore(true);
        QCOMPARE(restored.timer, TabColumnPeekAction::Timer::Stop);
        QVERIFY(restored.relayout); QVERIFY(!restored.animate); QVERIFY(column.expanded());
        QVERIFY(!column.restore(true).relayout);
        column.setIconsOnly(false);
        QVERIFY(!column.expanded()); QVERIFY(!column.iconsOnly());
        QVERIFY(!column.timeout().relayout);
    }

    void oneFadePerSessionEvenAcrossGaps()
    {
        TabHoverController hover;
        QCOMPARE(hover.move({2}).fade, Fade::In);
        QCOMPARE(hover.move({3}).fade, Fade::Show);
        QCOMPARE(hover.move({}).fade, Fade::Out);
        QVERIFY(hover.hasSession());
        QCOMPARE(hover.move({5}).fade, Fade::Show);
        hover.leave();
        QCOMPARE(hover.hit(), TabHoverHit{});
        QVERIFY(!hover.hasSession());
        QCOMPARE(hover.move({5}).fade, Fade::In);
    }

    void controlChangesRepaintWithoutReplayingTheFade()
    {
        TabHoverController hover;
        (void)hover.move({2, false, 0});
        const auto action = hover.move({2, true, 0});
        QVERIFY(action.changed); QCOMPARE(action.fade, Fade::None);
        QVERIFY(!hover.move({2, true, 0}).changed);
        const auto folder = hover.move({-1, false, 1, 3, true});
        QVERIFY(folder.changed); QCOMPARE(folder.fade, Fade::Out);
        QCOMPARE(hover.hit().folder, 3); QVERIFY(hover.hit().folderAdd);
        QCOMPARE(hover.move({-1, false, 1, 3, false}).fade, Fade::None);
    }

    void reconstructionAndRemovalKeepSessionIdentity()
    {
        TabHoverController hover;
        hover.beginSession();
        QCOMPARE(hover.move({4, true, 0}).fade, Fade::Show);
        hover.removedTab(2);
        QCOMPARE(hover.hit().tab, -1);
        QCOMPARE(hover.move({3}).fade, Fade::Show);
        hover.removedTab(5); QCOMPARE(hover.hit().tab, 3);
        hover.endSession();
        (void)hover.move({});
        QCOMPARE(hover.move({3}).fade, Fade::In);
    }
};

QTEST_APPLESS_MAIN(TabHoverControllerTest)
#include "tab_hover_controller_test.moc"
