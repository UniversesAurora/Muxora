// SPDX-License-Identifier: MIT
// Wheel interpolation contracts: accumulation/reversal, range limits,
// immediate input, inherited opt-out and native touchpad delivery.
#include "style/smooth_scroll.hpp"
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QWheelEvent>
#include <QWidget>
#include <memory>

using namespace window_system;

class SmoothScrollTest final : public QObject {
    Q_OBJECT
    // Advance Qt's public animation clock deterministically. No window is
    // shown, and the assertions observe the scroll area's public position.
    static void advance(QObject& owner, qreal progress)
    {
        for (auto* animation : owner.findChildren<QVariantAnimation*>())
            if (animation->state() == QAbstractAnimation::Running)
                animation->setCurrentTime(qRound(animation->duration() * progress));
    }
    static bool wheel(QScrollArea& area, QPoint pixels = {})
    {
        QWheelEvent event(QPointF(10, 10), QPointF(10, 10), pixels, QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        return filterSmoothScroll(area.viewport(), &event);
    }
    static void prepare(QScrollArea& area)
    {
        area.verticalScrollBar()->setRange(0, 1000);
        area.verticalScrollBar()->setSingleStep(10);
        area.verticalScrollBar()->setPageStep(120);
    }
private slots:
    void successiveStepsAccumulateWithoutAVisibleJump()
    {
        SmoothScrollAnimation motion;
        qreal position = 0;
        motion.applyValue = [&](qreal value) { position = value; };
        motion.scrollBy(100, position, 0, 1000);
        motion.setCurrentTime(motion.duration() / 2);
        QVERIFY(position > 0 && position < 100);
        const auto before = position;
        motion.scrollBy(100, position, 0, 1000);
        QCOMPARE(position, before);
        QCOMPARE(motion.target(), 200);
        motion.setCurrentTime(motion.duration());
        QCOMPARE(position, 200);
    }
    void reversingMovesBackFromTheDisplayedPosition()
    {
        SmoothScrollAnimation motion;
        qreal position = 100;
        motion.applyValue = [&](qreal value) { position = value; };
        motion.scrollBy(200, position, 0, 1000);
        motion.setCurrentTime(motion.duration() / 2);
        const auto before = position;
        motion.scrollBy(-30, position, 0, 1000);
        QCOMPARE(motion.target(), before - 30);
        motion.setCurrentTime(motion.duration() / 2);
        QVERIFY(position < before);
        motion.setCurrentTime(motion.duration());
        QCOMPARE(position, before - 30);
    }
    void aCompletedStepHasNoOverscroll_data()
    {
        QTest::addColumn<qreal>("start");
        QTest::addColumn<qreal>("delta");
        QTest::addColumn<qreal>("maximum");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("lower") << qreal(10) << qreal(-200) << qreal(100) << qreal(0);
        QTest::newRow("upper") << qreal(90) << qreal(200) << qreal(100) << qreal(100);
        QTest::newRow("empty-range") << qreal(0) << qreal(200) << qreal(0) << qreal(0);
    }
    void aCompletedStepHasNoOverscroll()
    {
        QFETCH(qreal, start); QFETCH(qreal, delta); QFETCH(qreal, maximum); QFETCH(qreal, expected);
        SmoothScrollAnimation motion;
        qreal position = start;
        motion.applyValue = [&](qreal value) { position = value; };
        motion.scrollBy(delta, position, 0, maximum);
        for (int time = 0; time <= motion.duration(); time += 10) {
            motion.setCurrentTime(time);
            QVERIFY(position >= 0 && position <= maximum);
        }
        motion.setCurrentTime(motion.duration());
        QCOMPARE(position, expected);
    }
    void directNavigationCancelsAnUnfinishedStep()
    {
        SmoothScrollAnimation motion;
        qreal position = 0;
        motion.applyValue = [&](qreal value) { position = value; };
        motion.scrollBy(100, position, 0, 1000);
        motion.setCurrentTime(motion.duration() / 2);
        position = 33;
        motion.stopAt(position);
        QCOMPARE(motion.state(), QAbstractAnimation::Stopped);
        QCOMPARE(motion.value(), position);
        QCOMPARE(motion.target(), position);
        motion.scrollBy(10, position, 0, 1000, false);
        QCOMPARE(position, 43);
        QCOMPARE(motion.state(), QAbstractAnimation::Stopped);
    }
    void disabledParentsCannotBeOverriddenByAChild()
    {
        QWidget root;
        QWidget child(&root), grandchild(&child);
        QVERIFY(isSmoothScrollingEnabled(&grandchild));
        setSmoothScrollingEnabled(&child, false);
        setSmoothScrollingEnabled(&grandchild, true);
        QVERIFY(!isSmoothScrollingEnabled(&grandchild));
        QVERIFY(isSmoothScrollingEnabled(&root));
        setSmoothScrollingEnabled(&child, true);
        QVERIFY(isSmoothScrollingEnabled(&grandchild));
    }
    void applyingAFrameCanDestroyItsConsumer()
    {
        auto motion = std::make_unique<SmoothScrollAnimation>();
        motion->applyValue = [&](qreal) { motion.reset(); };
        motion->scrollBy(20, 0, 0, 100, false);
        QVERIFY(!motion);
    }
    void nestedComponentsSeeTheirWheelBeforeTheScrollArea()
    {
        QScrollArea area;
        auto* content = new QWidget;
        area.setWidget(content);
        area.verticalScrollBar()->setRange(0, 1000);
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), {}, QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QVERIFY(!filterSmoothScroll(content, &wheel));
        QCOMPARE(area.verticalScrollBar()->value(), 0);
        QVERIFY(!area.isVisible());
    }
    void touchpadInputKeepsItsNativePath()
    {
        QScrollArea area;
        area.verticalScrollBar()->setRange(0, 1000);
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(0, -13), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QVERIFY(!filterSmoothScroll(area.viewport(), &wheel));
        QCOMPARE(area.verticalScrollBar()->value(), 0);
        QVERIFY(!area.isVisible());
    }
    void optOutLeavesTheOriginalEventForTheComponent()
    {
        QScrollArea area;
        area.verticalScrollBar()->setRange(0, 1000);
        setSmoothScrollingEnabled(&area, false);
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), {}, QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QVERIFY(!filterSmoothScroll(area.viewport(), &wheel));
        QCOMPARE(area.verticalScrollBar()->value(), 0);
        QVERIFY(!area.isVisible());
    }
    void aScrollAreaAccumulatesWheelStepsAndClampsAtTheEnd()
    {
        QScrollArea area;
        prepare(area);
        if (!smoothScrollAnimationsAllowed(&area)) QSKIP("System UI animations are disabled");
        QVERIFY(wheel(area));
        QVERIFY(wheel(area));
        advance(area, 1);
        const int step = std::min(area.verticalScrollBar()->pageStep(),
                                 area.verticalScrollBar()->singleStep() * QApplication::wheelScrollLines());
        QCOMPARE(area.verticalScrollBar()->value(), 2 * step);
        area.verticalScrollBar()->setValue(area.verticalScrollBar()->maximum() - 1);
        QVERIFY(wheel(area));
        advance(area, 1);
        QCOMPARE(area.verticalScrollBar()->value(), area.verticalScrollBar()->maximum());
        QVERIFY(!area.isVisible());
    }
    void changingTheRangeCannotResumeAnOldDestination()
    {
        QScrollArea area;
        prepare(area);
        if (!smoothScrollAnimationsAllowed(&area)) QSKIP("System UI animations are disabled");
        QVERIFY(wheel(area));
        advance(area, 0.5);
        const int before = area.verticalScrollBar()->value();
        QVERIFY(before > 0);
        area.verticalScrollBar()->setRange(0, before - 1);
        advance(area, 1);
        QCOMPARE(area.verticalScrollBar()->value(), before - 1);
        QVERIFY(!area.isVisible());
    }
    void aDirectSliderDragCancelsPendingInterpolation()
    {
        QScrollArea area;
        prepare(area);
        if (!smoothScrollAnimationsAllowed(&area)) QSKIP("System UI animations are disabled");
        QVERIFY(wheel(area));
        advance(area, 0.5);
        area.verticalScrollBar()->setSliderDown(true);
        area.verticalScrollBar()->setSliderPosition(17);
        area.verticalScrollBar()->setSliderDown(false);
        advance(area, 1);
        QCOMPARE(area.verticalScrollBar()->value(), 17);
        QVERIFY(!area.isVisible());
    }
    void disablingAParentCancelsAnExistingScroll()
    {
        QWidget root;
        QScrollArea area(&root);
        prepare(area);
        if (!smoothScrollAnimationsAllowed(&area)) QSKIP("System UI animations are disabled");
        QVERIFY(wheel(area));
        advance(area, 0.5);
        const int before = area.verticalScrollBar()->value();
        QVERIFY(before > 0);
        setSmoothScrollingEnabled(&root, false);
        advance(area, 1);
        QCOMPARE(area.verticalScrollBar()->value(), before);
        QVERIFY(!wheel(area));
        QVERIFY(!area.isVisible());
    }
    void nativePixelInputCancelsAnExistingWheelInterpolation()
    {
        QScrollArea area;
        prepare(area);
        if (!smoothScrollAnimationsAllowed(&area)) QSKIP("System UI animations are disabled");
        QVERIFY(wheel(area));
        advance(area, 0.5);
        const int before = area.verticalScrollBar()->value();
        QVERIFY(!wheel(area, QPoint(0, -13)));
        // Native delivery can now set its own position without an old wheel
        // animation moving it again afterwards.
        area.verticalScrollBar()->setValue(before + 13);
        advance(area, 1);
        QCOMPARE(area.verticalScrollBar()->value(), before + 13);
        QVERIFY(!area.isVisible());
    }
};
QTEST_MAIN(SmoothScrollTest)
#include "smooth_scroll_test.moc"
