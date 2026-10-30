// SPDX-License-Identifier: MIT
// Phase 10.3, Quick tab group: the strip beside its card in every placement,
// one shown page, pages created on first show and handed back on removal.
// Placement rules themselves are covered with the shared controller.
#include "quick/group_item.hpp"
#include "quick/tab_bar_item.hpp"

#include <QPointer>
#include <QQuickWindow>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

class QuickGroupTest final : public QObject {
    Q_OBJECT
private slots:
    void stripJoinsTheCardInEveryPlacement_data()
    {
        QTest::addColumn<int>("placement");
        for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right})
            QTest::newRow(qPrintable(QString::number(int(placement)))) << int(placement);
    }

    void stripJoinsTheCardInEveryPlacement()
    {
        QFETCH(int, placement);
        QQuickWindow window; window.resize(640, 320);
        auto* group = new GroupItem(defaultTheme(), window.contentItem());
        group->setSize(window.size());
        for (const auto title : {"First", "Second", "Third"}) group->addTab(new QQuickItem, {}, title, QUuid::createUuid());
        TabAppearance appearance; appearance.centralPlacement = TabBarPlacement(placement);
        group->setAppearance(appearance);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto strip = group->stripRect(), content = group->contentRect();
        QVERIFY(!strip.isEmpty()); QVERIFY(!content.isEmpty());
        QVERIFY(!strip.intersects(content));
        // The strip lies on its side of the card and spans along it.
        switch (TabBarPlacement(placement)) {
        case TabBarPlacement::Top: QVERIFY(strip.bottom() < content.top()); break;
        case TabBarPlacement::Bottom: QVERIFY(strip.top() > content.bottom()); break;
        case TabBarPlacement::Left: QVERIFY(strip.right() < content.left()); break;
        case TabBarPlacement::Right: QVERIFY(strip.left() > content.right()); break;
        }
    }

    void onlyTheCurrentPageIsShownAndPagesAreCreatedWhenShown()
    {
        QQuickWindow window; window.resize(480, 300);
        auto* group = new GroupItem(defaultTheme(), window.contentItem());
        group->setSize(window.size());
        const auto first = QUuid::createUuid(), second = QUuid::createUuid();
        QPointer<QQuickItem> firstPage = new QQuickItem;
        int created = 0; QPointer<QQuickItem> secondPage;
        group->pageRequested = [&](TabId tab) {
            ++created; if (tab == second) secondPage = new QQuickItem; return secondPage.data();
        };
        group->addTab(firstPage, {}, QStringLiteral("First"), first);
        group->addTab(nullptr, {}, QStringLiteral("Second"), second);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(group->currentIndex(), 0);
        QVERIFY(firstPage->isVisible()); QCOMPARE(created, 0);
        QCOMPARE(firstPage->size(), QSizeF(group->contentRect().size()));
        group->setCurrentIndex(1);
        QCOMPARE(created, 1); QVERIFY(secondPage); QVERIFY(secondPage->isVisible()); QVERIFY(!firstPage->isVisible());
        group->setCurrentIndex(0); group->setCurrentIndex(1);
        QCOMPARE(created, 1);
    }

    void removedPagesAreHandedBackAlive()
    {
        QQuickWindow window;
        auto* group = new GroupItem(defaultTheme(), window.contentItem());
        group->setSize({400, 240});
        const auto tab = QUuid::createUuid();
        QPointer<QQuickItem> page = new QQuickItem;
        group->addTab(page, {}, QStringLiteral("Only"), tab);
        QCOMPARE(group->takePage(tab), page.data());
        QCOMPARE(group->count(), 0); QVERIFY(page); QVERIFY(!page->parentItem());
        group->addTab(page, {}, QStringLiteral("Again"), tab);
        delete group;
        QVERIFY(page); QVERIFY(!page->parentItem());
        delete page.data();
    }
};

QTEST_MAIN(QuickGroupTest)
#include "quick_group_test.moc"
