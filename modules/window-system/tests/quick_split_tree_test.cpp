// SPDX-License-Identifier: MIT
// Workspace design 6: nested pane/group splits retain content identity and
// obey content minima. Dimensions: horizontal/vertical x resize/projection
// x synchronous model edits during geometry delivery. No appearance checks.
#include "quick/split_tree_item.hpp"
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

class QuickSplitTreeTest final : public QObject {
    Q_OBJECT
private slots:
    void dragCommitsModelRatioWithoutReplacingContent_data()
    {
        QTest::addColumn<bool>("horizontal");
        QTest::newRow("horizontal") << true;
        QTest::newRow("vertical") << false;
    }

    void dragCommitsModelRatioWithoutReplacingContent()
    {
        QFETCH(bool, horizontal);
        LayoutModel model;
        const auto windowId = model.createWindow();
        const auto tab = model.addTab(windowId, QStringLiteral("Workspace"));
        const auto firstId = model.tab(tab)->activePane;
        const auto secondId = model.splitPane(tab, firstId, horizontal ? Placement::Right : Placement::Bottom);
        const auto originalTree = model.tab(tab)->panes;
        QVERIFY(!secondId.isNull());
        QQuickWindow window;
        window.resize(600, 480);
        QObject contentOwner;
        auto* first = new QQuickItem;
        auto* second = new QQuickItem;
        first->setParent(&contentOwner);
        second->setParent(&contentOwner);
        QPointer<QQuickItem> firstIdentity = first, secondIdentity = second;
        auto* view = new SplitTreeItem(window.contentItem());
        view->setSize({600, 480});
        view->setMinimumSizes({{firstId, QSize(100, 80)}, {secondId, QSize(120, 90)}});
        view->setLeafItems({{firstId, first}, {secondId, second}});
        view->setTree(originalTree);
        bool committed = true;
        connect(view, &SplitTreeItem::ratioRequested, &window, [&](QUuid node, double ratio) {
            committed = bool(model.setSplitRatio(node, ratio)) && committed;
            view->setTree(model.tab(tab)->panes);
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto initial = view->leafRect(firstId);
        const auto grab = view->dividerRect(originalTree->id).center();
        QSignalSpy requests(view, &SplitTreeItem::ratioRequested);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, grab);
        QTest::mouseMove(&window, grab + (horizontal ? QPoint(80, 0) : QPoint(0, 80)));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier,
            grab + (horizontal ? QPoint(80, 0) : QPoint(0, 80)));
        QVERIFY(!requests.isEmpty());
        QVERIFY(committed);
        const auto changed = view->leafRect(firstId);
        QVERIFY(horizontal ? changed.width() > initial.width() : changed.height() > initial.height());
        QVERIFY(model.tab(tab)->panes->ratio > originalTree->ratio);
        QVERIFY(!view->leafRect(firstId).intersects(view->leafRect(secondId)));
        QCOMPARE(first->parent(), &contentOwner);
        QCOMPARE(second->parent(), &contentOwner);
        QCOMPARE(firstIdentity.data(), first);
        QCOMPARE(secondIdentity.data(), second);
        const auto savedTree = model.tab(tab)->panes;
        view->setFocusedItem(firstId);
        QVERIFY(first->isVisible());
        QVERIFY(!second->isVisible());
        QCOMPARE(model.tab(tab)->panes, savedTree);
        view->setFocusedItem({});
        QVERIFY(second->isVisible());
        view->setSize({30, 30});
        QVERIFY(first->width() >= 100 && first->height() >= 80);
        QVERIFY(second->width() >= 120 && second->height() >= 90);
        delete view;
        QVERIFY(firstIdentity && secondIdentity);
        QVERIFY(!first->parentItem() && !second->parentItem());
    }

    void geometryCallbacksCanReplaceOrDestroyTheView()
    {
        auto leaf = std::make_shared<LayoutNode>();
        leaf->id = QUuid::createUuid(); leaf->itemId = QUuid::createUuid();
        auto other = std::make_shared<LayoutNode>();
        other->id = QUuid::createUuid(); other->itemId = QUuid::createUuid();
        auto split = std::make_shared<LayoutNode>();
        split->id = QUuid::createUuid(); split->first = leaf; split->second = other;
        QQuickItem content, second;
        auto* view = new SplitTreeItem;
        view->setLeafItems({{leaf->itemId, &content}, {other->itemId, &second}});
        view->setTree(split);
        bool replaced = false;
        connect(&content, &QQuickItem::widthChanged, &content, [&] {
            if (!replaced) { replaced = true; view->setTree(leaf); }
        });
        view->setSize({600, 400});
        QVERIFY(replaced);
        QVERIFY(view->dividerRect(split->id).isEmpty());
        QVERIFY(!second.isVisible());
        QPointer<SplitTreeItem> lifetime = view;
        connect(&content, &QQuickItem::heightChanged, &content, [&] { delete view; view = nullptr; });
        view->setSize({600, 450});
        QVERIFY(lifetime.isNull());
        QVERIFY(!content.parentItem());
    }
};

QTEST_MAIN(QuickSplitTreeTest)
#include "quick_split_tree_test.moc"
