// SPDX-License-Identifier: MIT
// Phase 10.3: a manager whose windows are built elsewhere (the Quick
// workspace). The model, commands and notifications work as usual; the
// manager builds no Widgets view and adopts no widget content.
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;

class ExternalPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void tabsAndPanesChangeTheModelWithoutViews()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QCOMPARE(manager.presentation(), WindowManager::Presentation::External);
        QSignalSpy changed(&manager, &WindowManager::layoutChanged);
        const auto window = manager.createWindow();
        QVERIFY(!window.isNull());
        TabOptions terminal; terminal.title = QStringLiteral("Shell"); terminal.contentType = QStringLiteral("terminal");
        const auto first = manager.addTab(window, terminal);
        const auto second = manager.addTab(window, {QStringLiteral("Files"), QStringLiteral("files"), {}});
        QVERIFY(!first.isNull()); QVERIFY(!second.isNull());
        QVERIFY(!manager.windowWidget(window)); QVERIFY(QApplication::topLevelWidgets().isEmpty());
        QVERIFY(changed.count() >= 3);
        const auto pane = manager.model().tab(first)->activePane;
        QCOMPARE(manager.paneOptions(pane).contentType, QStringLiteral("terminal"));
        QCOMPARE(manager.model().group(manager.model().groupForTab(second))->activeTab, second);

        changed.clear();
        QVERIFY(manager.activateTab(first));
        QCOMPARE(manager.context(window).tab, first); QCOMPARE(changed.count(), 1);
        const auto split = manager.splitPane(first, pane, Placement::Right);
        QVERIFY(!split.isNull());
        QCOMPARE(manager.model().panes(first).size(), 2);
        QCOMPARE(manager.paneOptions(split).contentType, QStringLiteral("terminal"));
        QCOMPARE(manager.context(window).pane, split);
        QVERIFY(manager.focusPane(first, pane)); QCOMPARE(manager.context(window).pane, pane);

        QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), Placement::Right));
        QCOMPARE(manager.model().centralGroups(window).size(), 2);
        QVERIFY(manager.closePane(first, split));
        QVERIFY(manager.paneOptions(split).title.isEmpty());
        QVERIFY(manager.closeTab(second));
        QVERIFY(!manager.model().tab(second)); QVERIFY(manager.model().validate());
        QVERIFY(!manager.windowWidget(window));
    }

    void widgetContentIsRefused()
    {
        WindowManager manager(WindowManager::Presentation::External);
        const auto window = manager.createWindow();
        QPointer<QLabel> label = new QLabel;
        QVERIFY(manager.addTab(window, label, {QStringLiteral("Widget"), {}, {}}).isNull());
        QVERIFY(label); QVERIFY(!label->parent());
        const auto tab = manager.addTab(window, {QStringLiteral("Quick"), {}, {}});
        QVERIFY(manager.splitPane(tab, manager.model().tab(tab)->activePane, label, Placement::Right).isNull());
        QVERIFY(label); QVERIFY(!label->parent());
        delete label.data();
        // And the Widgets presentation keeps requiring its widget content.
        WindowManager widgets;
        const auto host = widgets.createWindow();
        QVERIFY(widgets.addTab(host, {QStringLiteral("No widget"), {}, {}}).isNull());
    }

    void closingTheLastWindowStillNotifiesTheHost()
    {
        WindowManager manager(WindowManager::Presentation::External);
        const auto window = manager.createWindow();
        manager.addTab(window, {QStringLiteral("Only"), {}, {}});
        QSignalSpy closed(&manager, &WindowManager::allWindowsClosed);
        QVERIFY(manager.requestCloseWindow(window));
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(manager.model().windows().isEmpty());
    }
};

QTEST_MAIN(ExternalPresentationTest)
#include "external_presentation_test.moc"
