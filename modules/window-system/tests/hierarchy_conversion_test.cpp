// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "panes/tab_content.hpp"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <utility>

using namespace window_system;
namespace {
LayoutTree nodeById(LayoutTree tree, QUuid id) {
    if (!tree || tree->id == id)
        return tree;
    if (auto found = nodeById(tree->first, id))
        return found;
    return nodeById(tree->second, id);
}
QSet<PaneId> paneSet(const LayoutModel &model) {
    QSet<PaneId> panes;
    for (const auto &tab : model.tabs())
        for (const auto pane : model.panes(tab.id))
            panes.insert(pane);
    return panes;
}
TabContent *page(QWidget *content) {
    for (auto *widget = content->parentWidget(); widget; widget = widget->parentWidget())
        if (auto *found = dynamic_cast<TabContent *>(widget))
            return found;
    return nullptr;
}
class ReparentProbe final : public QObject {
  public:
    std::function<void()> action;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::ParentChange && action) {
            const auto callback = std::exchange(action, {});
            callback();
            return true;
        }
        return false;
    }
};
} // namespace
class HierarchyConversionTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void groupToTabKeepsIdentityAndEveryOriginalPaneSubtree();
    void wholeGroupAndTabMoveIntoTargetPane_data();
    void wholeGroupAndTabMoveIntoTargetPane();
    void tabToGroupKeepsItsSideAndTheOriginalActiveTab_data();
    void tabToGroupKeepsItsSideAndTheOriginalActiveTab();
    void invalidConversionsAreAtomic();
    void widgetConversionKeepsContentBarsAndNeverClosesConnections();
    void extractingPaneCreatesATabInTheCurrentGroup();
    void commandsUseExplicitSourceAndTargetWithoutChangingSelectionFirst();
    void targetPickerOffersEveryPaneAndKeepsCapturedSource();
    void reparentCallbacksCannotRestoreStaleLayoutOrBars_data();
    void reparentCallbacksCannotRestoreStaleLayoutOrBars();
};

void HierarchyConversionTest::groupToTabKeepsIdentityAndEveryOriginalPaneSubtree() {
    LayoutModel model;
    const auto window = model.createWindow();
    const auto first = model.addTab(window, "First");
    const auto group = model.groupForTab(first);
    const auto firstPane = model.tab(first)->activePane;
    QVERIFY(!model.splitPane(first, firstPane, Placement::Bottom).isNull());
    const auto second = model.addTab(window, "Second", group);
    const auto third = model.addTab(window, "Third", group);
    QVERIFY(model.activateTab(second));
    QVERIFY(model.setTabColor(second, "red"));
    const auto firstTree = model.tab(first)->panes, thirdTree = model.tab(third)->panes;
    const auto panes = paneSet(model);
    const auto active = model.tab(second)->activePane;
    const auto revision = model.revision();
    QCOMPARE(model.convertGroupToTab(group), second);
    QCOMPARE(model.revision(), revision + 1);
    QVERIFY(model.validate());
    QCOMPARE(model.group(group)->tabs, QVector<TabId>{second});
    QCOMPARE(model.tab(second)->activePane, active);
    QCOMPARE(model.tab(second)->title, QStringLiteral("Second"));
    QCOMPARE(model.tab(second)->colorId, QStringLiteral("red"));
    QCOMPARE(paneSet(model), panes);
    QCOMPARE(nodeById(model.tab(second)->panes, firstTree->id), firstTree);
    QCOMPARE(nodeById(model.tab(second)->panes, thirdTree->id), thirdTree);
    LayoutModel restored;
    QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.toJson(), model.toJson());
}

void HierarchyConversionTest::wholeGroupAndTabMoveIntoTargetPane_data() {
    QTest::addColumn<bool>("wholeGroup");
    QTest::addColumn<int>("direction");
    for (bool group : {false, true})
        for (auto place : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(group).arg(int(place))))
                << group << int(place);
}
void HierarchyConversionTest::wholeGroupAndTabMoveIntoTargetPane() {
    QFETCH(bool, wholeGroup);
    QFETCH(int, direction);
    LayoutModel model;
    const auto owner = model.createWindow();
    const auto target = model.addTab(owner, "Target");
    const auto targetPane = model.tab(target)->activePane;
    const auto floatWindow = model.createWindow(WindowKind::Floating, owner);
    const auto source = model.addTab(floatWindow, "Source");
    const auto sourceGroup = model.groupForTab(source);
    QVERIFY(!model.splitPane(source, model.tab(source)->activePane, Placement::Bottom).isNull());
    if (wholeGroup)
        QVERIFY(!model.addTab(floatWindow, "Other", sourceGroup).isNull());
    const auto originalTree = model.tab(source)->panes;
    const auto panes = paneSet(model);
    const auto revision = model.revision();
    const auto result = wholeGroup
                            ? model.moveGroupIntoTab(sourceGroup, target, targetPane, Placement(direction))
                            : model.moveTabIntoTab(source, target, targetPane, Placement(direction));
    QVERIFY2(result, qPrintable(result.error));
    QCOMPARE(model.revision(), revision + 1);
    QVERIFY(model.validate());
    QCOMPARE(paneSet(model), panes);
    QVERIFY(!model.window(floatWindow));
    QVERIFY(!model.group(sourceGroup));
    QVERIFY(!model.tab(source));
    QCOMPARE(nodeById(model.tab(target)->panes, originalTree->id), originalTree);
    const auto root = model.tab(target)->panes;
    QCOMPARE(root->orientation, direction <= int(Placement::Right) ? Qt::Horizontal : Qt::Vertical);
    const auto targetLeaf =
        direction == int(Placement::Left) || direction == int(Placement::Top) ? root->second : root->first;
    QVERIFY(targetLeaf->isLeaf());
    QCOMPARE(targetLeaf->itemId, targetPane);
}

void HierarchyConversionTest::tabToGroupKeepsItsSideAndTheOriginalActiveTab_data() {
    QTest::addColumn<int>("edge");
    QTest::addColumn<bool>("otherTab");
    for (int edge = -1; edge < 4; ++edge)
        for (bool other : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(edge).arg(other))) << edge << other;
}
void HierarchyConversionTest::tabToGroupKeepsItsSideAndTheOriginalActiveTab() {
    QFETCH(int, edge);
    QFETCH(bool, otherTab);
    LayoutModel model;
    const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Original");
    if (edge >= 0)
        QVERIFY(model.moveTabToEdge(tab, Edge(edge), true));
    const auto originalGroup = model.groupForTab(tab);
    TabId other;
    if (otherTab)
        other = model.addTab(window, "Other", originalGroup);
    const auto pane = model.tab(tab)->activePane;
    QVERIFY(!model.splitPane(tab, pane, Placement::Right).isNull());
    const auto active = model.tab(tab)->activePane;
    const auto panes = paneSet(model);
    const auto revision = model.revision();
    const auto result = model.convertTabToGroup(tab, Placement::Top);
    QVERIFY(!result.isNull());
    QCOMPARE(model.revision(), revision + 1);
    QVERIFY(model.validate());
    QCOMPARE(paneSet(model), panes);
    QCOMPARE(model.group(result)->tabs.size(), 2);
    QCOMPARE(model.group(result)->activeTab, tab);
    QCOMPARE(model.tab(tab)->activePane, active);
    QCOMPARE(model.panes(tab), QVector<PaneId>{active});
    QCOMPARE(model.group(result)->location, edge < 0 ? GroupLocation::Central : GroupLocation::Pinned);
    if (edge >= 0)
        QCOMPARE(model.group(result)->edge, Edge(edge));
    QCOMPARE(result == originalGroup, !otherTab);
    if (otherTab)
        QCOMPARE(model.group(originalGroup)->tabs, QVector<TabId>{other});
}

void HierarchyConversionTest::invalidConversionsAreAtomic() {
    LayoutModel model;
    const auto window = model.createWindow();
    const auto first = model.addTab(window, "First");
    const auto group = model.groupForTab(first);
    const auto second = model.addTab(window, "Second");
    const auto before = model.toJson();
    const auto revision = model.revision();
    QVERIFY(!model.moveTabIntoTab(first, first));
    QVERIFY(!model.moveTabIntoTab(first, second, QUuid::createUuid()));
    QVERIFY(!model.moveGroupIntoTab(group, second));
    QVERIFY(model.convertGroupToTab(group, Placement::Center).isNull());
    QVERIFY(model.convertTabToGroup(first, Placement::Center).isNull());
    QCOMPARE(model.toJson(), before);
    QCOMPARE(model.revision(), revision);
}

void HierarchyConversionTest::widgetConversionKeepsContentBarsAndNeverClosesConnections() {
    WindowManager manager;
    const auto window = manager.createWindow();
    auto *firstContent = new QLineEdit("Retained text");
    const auto first = manager.addTab(window, firstContent, {"First", "test", {}});
    const auto firstPane = manager.model().tab(first)->activePane;
    auto *secondContent = new QLineEdit("Second text");
    const auto second = manager.addTab(window, secondContent, {"Second", "test", {}});
    const auto secondPane = manager.model().tab(second)->activePane;
    const QPointer<QWidget> outerTop = new QLabel("Outer top"), paneTop = new QLabel("Pane top"),
                            outerBottom = new QLabel("Outer bottom");
    QVERIFY(manager.setTabTopBar(first, outerTop));
    QVERIFY(manager.setPaneTopBar(firstPane, paneTop));
    QVERIFY(manager.setTabBottomBar(first, outerBottom));
    int closeChecks = 0;
    manager.setCloseGuard([&](const QVector<TabId> &) {
        ++closeChecks;
        return false;
    });
    const auto group = manager.model().groupForTab(second);
    QCOMPARE(manager.convertGroupToTab(group), second);
    QCOMPARE(closeChecks, 0);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    QCOMPARE(manager.paneWidget(secondPane), secondContent);
    QCOMPARE(firstContent->text(), QStringLiteral("Retained text"));
    QCOMPARE(secondContent->text(), QStringLiteral("Second text"));
    QVERIFY(outerTop && paneTop && outerBottom);
    QCOMPARE(outerTop->parentWidget(), paneTop->parentWidget());
    QCOMPARE(outerTop->parentWidget()->objectName(), QStringLiteral("ConvertedPaneBars"));
    QVERIFY(page(firstContent));
    QCOMPARE(page(firstContent), page(secondContent));
    const auto converted = manager.convertTabToGroup(second);
    QVERIFY(!converted.isNull());
    QCOMPARE(manager.model().group(converted)->tabs.size(), 2);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    QCOMPARE(manager.paneWidget(secondPane), secondContent);
    QVERIFY(outerTop && paneTop && outerBottom);
    QCOMPARE(closeChecks, 0);
}

void HierarchyConversionTest::extractingPaneCreatesATabInTheCurrentGroup() {
    WindowManager manager;
    const auto window = manager.createWindow();
    auto *first = new QLabel("First");
    const auto tab = manager.addTab(window, first, {"Source", "test", {}});
    const auto firstPane = manager.model().tab(tab)->activePane;
    auto *second = new QLabel("Second");
    const auto secondPane = manager.splitPane(tab, firstPane, second, Placement::Bottom);
    const auto group = manager.model().groupForTab(tab);
    const auto extracted = manager.extractPaneToTab(tab, secondPane);
    QVERIFY(!extracted.isNull());
    QCOMPARE(manager.model().groupForTab(extracted), group);
    QCOMPARE(manager.model().panes(extracted), QVector<PaneId>{secondPane});
    QCOMPARE(manager.paneWidget(secondPane), second);
    QCOMPARE(manager.paneWidget(firstPane), first);
    QCOMPARE(manager.extractPaneToTab(extracted, secondPane), extracted);
}

void HierarchyConversionTest::commandsUseExplicitSourceAndTargetWithoutChangingSelectionFirst() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto source = manager.addTab(window, new QLabel("Source"), {"Source", "test", {}});
    const auto target = manager.addTab(window, new QLabel("Target"), {"Target", "test", {}});
    const auto targetPane = manager.model().tab(target)->activePane;
    auto context = manager.context(window);
    context.tab = source;
    context.pane = manager.model().tab(source)->activePane;
    context.args = {{"targetTab", target}, {"targetPane", targetPane}, {"placement", int(Placement::Bottom)}};
    QVERIFY(manager.executeInContext("tab.moveIntoTab", context));
    QVERIFY(!manager.model().tab(source));
    QCOMPARE(manager.model().panes(target).size(), 2);
    QVERIFY(manager.commands().find("group.convertToTab"));
    QVERIFY(manager.commands().find("group.moveIntoTab"));
    QVERIFY(manager.commands().find("tab.convertToGroup"));
    QVERIFY(manager.commands().find("pane.extractToTab"));
}

void HierarchyConversionTest::targetPickerOffersEveryPaneAndKeepsCapturedSource() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto source = manager.addTab(window, new QLabel("Source"), {"Source", "test", {}});
    const auto sourcePane = manager.model().tab(source)->activePane;
    const auto target = manager.addTab(window, new QLabel("Target"), {"Target", "test", {}});
    const auto firstPane = manager.model().tab(target)->activePane;
    const auto secondPane = manager.splitPane(target, firstPane, new QLabel("Other pane"), Placement::Bottom);
    auto context = manager.context(window);
    context.tab = source;
    context.pane = sourcePane;
    QVERIFY(manager.executeInContext("tab.moveIntoTab", context));
    QPointer<QMenu> picker = manager.windowWidget(window)->findChild<QMenu *>("HierarchyTargetPicker");
    QVERIFY(picker);
    QCOMPARE(manager.model().tab(target)->activePane, secondPane);
    QHash<PaneId, QSet<int>> offered;
    QPointer<QAction> selected;
    for (auto *action : picker->findChildren<QAction *>()) {
        if (action->property("targetTab").toUuid() != target)
            continue;
        const auto pane = action->property("targetPane").toUuid();
        const auto direction = action->property("placement").toInt();
        offered[pane].insert(direction);
        if (pane == firstPane && direction == int(Placement::Top))
            selected = action;
    }
    const QSet<int> directions{int(Placement::Left), int(Placement::Right), int(Placement::Top),
                               int(Placement::Bottom)};
    QCOMPARE(offered.size(), 2);
    QCOMPARE(offered.value(firstPane), directions);
    QCOMPARE(offered.value(secondPane), directions);
    QVERIFY(selected);
    // Selection changes after opening the picker must not replace its source.
    QVERIFY(manager.activateTab(target));
    selected->trigger();
    QVERIFY(!manager.model().tab(source));
    QCOMPARE(manager.model().panes(target).size(), 3);
    auto originalSlot = manager.model().tab(target)->panes->first;
    QVERIFY(!originalSlot->isLeaf());
    QCOMPARE(originalSlot->orientation, Qt::Vertical);
    QCOMPARE(originalSlot->first->itemId, sourcePane);
    QCOMPARE(originalSlot->second->itemId, firstPane);
    if (picker)
        picker->close();
}

void HierarchyConversionTest::reparentCallbacksCannotRestoreStaleLayoutOrBars_data() {
    QTest::addColumn<QString>("operation");
    QTest::addColumn<QString>("mutation");
    QTest::addColumn<bool>("duringCombination");
    for (const auto *operation : {"move-tab", "move-group", "convert-group"})
        for (const auto *mutation :
             {"destroy-manager", "close-target", "close-carried-pane", "close-other-source", "restore-layout",
              "replace-pane-bar", "delete-existing-bar", "delete-outer"})
            for (const bool combined : {false, true}) {
                // Delete a different bar from the widget currently delivering
                // ParentChange; deleting QWidget inside its own setParent is
                // outside Qt's ownership contract.
                if ((combined && QLatin1StringView(mutation) == QLatin1StringView("delete-existing-bar")) ||
                    (!combined && QLatin1StringView(mutation) == QLatin1StringView("delete-outer")))
                    continue;
                QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3").arg(operation, mutation).arg(combined)))
                    << QString::fromLatin1(operation) << QString::fromLatin1(mutation) << combined;
            }
}
void HierarchyConversionTest::reparentCallbacksCannotRestoreStaleLayoutOrBars() {
    QFETCH(QString, operation);
    QFETCH(QString, mutation);
    QFETCH(bool, duringCombination);
    QPointer<WindowManager> manager = new WindowManager;
    manager->setContentFactory([](const TabOptions &options, PaneId) { return new QLabel(options.title); });
    const auto window = manager->createWindow();
    const auto source = manager->addTab(window, new QLabel("Source"), {"Source", "test", {}});
    const auto sourcePane = manager->model().tab(source)->activePane;
    const auto sourceGroup = manager->model().groupForTab(source);
    const auto otherSource =
        manager->addTab(window, new QLabel("Other source"), {"Other source", "test", {}});
    const auto otherPane = manager->model().tab(otherSource)->activePane;
    const auto target = operation == "convert-group"
                            ? otherSource
                            : manager->addTabToNewGroup(window, new QLabel("Target"), {"Target", "test", {}},
                                                        sourceGroup, Placement::Right);
    const auto before = manager->saveLayout();
    QPointer<QWidget> outer = new QLabel("Outer");
    QVERIFY(manager->setTabTopBar(source, outer));
    QPointer<QWidget> previous;
    if (duringCombination || mutation == "delete-existing-bar") {
        previous = new QLabel("Previous");
        QVERIFY(manager->setPaneTopBar(sourcePane, previous));
    }
    QPointer<QWidget> replacement;
    ReparentProbe probe;
    (duringCombination ? previous : outer)->installEventFilter(&probe);
    bool invoked = false;
    probe.action = [&] {
        invoked = true;
        if (mutation == "destroy-manager") {
            delete manager.data();
            return;
        }
        if (mutation == "close-target") {
            QVERIFY(manager->closeTab(target));
            return;
        }
        if (mutation == "close-carried-pane") {
            QVERIFY(manager->closePane(target, sourcePane));
            return;
        }
        if (mutation == "close-other-source") {
            const auto owner = manager->model().tabForPane(otherPane);
            QVERIFY(manager->closePane(owner, otherPane));
            return;
        }
        if (mutation == "replace-pane-bar") {
            replacement = new QLabel("Replacement");
            QVERIFY(manager->setPaneTopBar(sourcePane, replacement));
            return;
        }
        if (mutation == "delete-existing-bar") {
            delete previous.data();
            return;
        }
        if (mutation == "delete-outer") {
            delete outer.data();
            return;
        }
        QVERIFY(manager->restoreLayout(before));
    };
    const bool result = operation == "convert-group" ? !manager->convertGroupToTab(sourceGroup).isNull()
                        : operation == "move-group"  ? manager->moveGroupIntoTab(sourceGroup, target)
                                                     : manager->moveTabIntoTab(source, target);
    QVERIFY(invoked);
    if (mutation == "destroy-manager") {
        QVERIFY(!manager);
        QVERIFY(!result);
    } else {
        QVERIFY(manager);
        QVERIFY(manager->model().validate());
        if (mutation == "close-target") {
            QVERIFY(!manager->model().tab(target));
            QVERIFY(!result);
        } else if (mutation == "close-carried-pane") {
            QVERIFY(manager->model().tabForPane(sourcePane).isNull());
            QVERIFY(result);
        } else if (mutation == "close-other-source") {
            QVERIFY(manager->model().tabForPane(otherPane).isNull());
            QVERIFY(result);
        } else if (mutation == "restore-layout")
            QCOMPARE(manager->model().toJson(), before.value("model").toObject());
        else
            QVERIFY(result);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (mutation == "close-other-source" || mutation == "delete-existing-bar") {
        QVERIFY(outer);
        QVERIFY(manager->model().tabForPane(sourcePane) == target);
        QVERIFY(outer->parentWidget());
        QVERIFY(page(manager->paneWidget(sourcePane)));
    } else
        QVERIFY(outer.isNull());
    if (mutation == "replace-pane-bar") {
        QVERIFY(replacement);
        QVERIFY(page(manager->paneWidget(sourcePane))->isAncestorOf(replacement));
    }
    delete manager.data();
}

QTEST_MAIN(HierarchyConversionTest)
#include "hierarchy_conversion_test.moc"
