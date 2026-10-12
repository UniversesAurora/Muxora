// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QTest>
#include <memory>

using namespace window_system;

namespace {
void moveMouse(QWidget* widget, QEvent::Type type, QPoint global, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(widget->mapFromGlobal(global)), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}
}

class ToolbarPolicyTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void requiredSizingOverridesApiAndCustomization();
    void dragDemotesIncomingElasticAndCancellationRestoresIt();
    void movingTheOwnerResolvesTheDestinationRow();
    void conflictingHostPoliciesAreRejectedAtomically();
    void policyRequestsDuringDragUseTheCommittedRows();
    void savedContentTitleRecoversMenuResetAndRightAnchoring();
    void windowTitlePolicyCannotBeChanged_data();
    void windowTitlePolicyCannotBeChanged();
};

void ToolbarPolicyTest::requiredSizingOverridesApiAndCustomization()
{
    CommandStripHost host;
    host.addStrip("required", "Required", new QLabel("Owner"), 0);
    host.addStrip("ordinary", "Ordinary", new QLabel("Tools"), 0);
    QVERIFY(host.setStripConstraints("required", {true, true, 96, true}));
    QCOMPARE(host.stripSizing("required"), CommandStripHost::StripSizing::Elastic);
    const auto before = host.saveLayout();
    QVERIFY(!host.setStripSizing("required", CommandStripHost::StripSizing::Content));
    QVERIFY(!host.setStripSizing("ordinary", CommandStripHost::StripSizing::Elastic));
    QCOMPARE(host.saveLayout(), before);
    for (const auto& id : {"required", "ordinary"}) {
        std::unique_ptr<QMenu> menu(host.createOverflowMenu(id, &host));
        QVERIFY(!menu->findChild<QAction*>("StripElasticAction"));
    }
    // Hidden host policy remains authoritative; showing it must not introduce
    // two owners into a row that was customized while the title was hidden.
    QVERIFY(host.setStripVisible("required", false));
    QVERIFY(!host.setStripSizing("ordinary", CommandStripHost::StripSizing::Elastic));
    QVERIFY(host.setStripVisible("required", true));
    QCOMPARE(host.stripSizing("required"), CommandStripHost::StripSizing::Elastic);
}

void ToolbarPolicyTest::dragDemotesIncomingElasticAndCancellationRestoresIt()
{
    CommandStripHost host;
    host.addStrip("required", "Required", new QLabel("Owner"), 0);
    QVERIFY(host.setStripConstraints("required", {true, true, 96, true}));
    host.addStrip("incoming", "Incoming", new QLabel("Menu"), 1, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(800, 64); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto original = host.saveLayout();
    auto* handle = host.stripWidget("incoming")->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const auto start = handle->mapToGlobal(handle->rect().center());
    const auto destination = host.mapToGlobal(QPoint(30, 16));
    moveMouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    moveMouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    QVERIFY(host.isDragging()); QVERIFY(host.stripOrder(0).contains("incoming"));
    QCOMPARE(host.stripSizing("required"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(host.stripSizing("incoming"), CommandStripHost::StripSizing::Content);
    QTest::keyClick(&host, Qt::Key_Escape);
    QVERIFY(!host.isDragging()); QCOMPARE(host.saveLayout(), original);
    QVERIFY(host.moveStrip("incoming", 0, 0));
    QCOMPARE(host.stripSizing("incoming"), CommandStripHost::StripSizing::Content);
    QCOMPARE(host.stripSizing("required"), CommandStripHost::StripSizing::Elastic);
}

void ToolbarPolicyTest::movingTheOwnerResolvesTheDestinationRow()
{
    CommandStripHost host;
    host.addStrip("required", "Required", new QLabel("Owner"), 0);
    QVERIFY(host.setStripConstraints("required", {true, true, 96, true}));
    host.addStrip("elastic", "Elastic", new QLabel("Tools"), 1, CommandStripHost::StripSizing::Elastic);
    QVERIFY(host.moveStrip("required", 1, 0));
    QCOMPARE(host.stripSizing("required"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(host.stripSizing("elastic"), CommandStripHost::StripSizing::Content);
    QVERIFY(host.moveStrip("elastic", 0, 0));
    QVERIFY(host.setStripSizing("elastic", CommandStripHost::StripSizing::Elastic));
}

void ToolbarPolicyTest::conflictingHostPoliciesAreRejectedAtomically()
{
    CommandStripHost host;
    host.addStrip("first", "First", new QLabel("One"), 0);
    host.addStrip("second", "Second", new QLabel("Two"), 1);
    QVERIFY(host.setStripConstraints("first", {true, true, 96, true}));
    const auto before = host.saveLayout();
    QVERIFY(!host.setStripConstraints("second", {true, false, 96, true}));
    QCOMPARE(host.saveLayout(), before);
    QVERIFY(host.setStripConstraints("second", {true, true, 96, true}));
    const auto twoRows = host.saveLayout();
    QVERIFY(!host.moveStrip("second", 0, 1));
    QCOMPARE(host.saveLayout(), twoRows);
    auto malformed = twoRows;
    malformed.insert("rows", QJsonArray{QJsonArray{"first", "second"}, QJsonArray{}});
    host.restoreLayout(malformed); QCOMPARE(host.saveLayout(), twoRows);
    host.setLocked(false); host.resize(800, 64); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto beforeDrag = host.saveLayout();
    auto* handle = host.stripWidget("second")->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const auto start = handle->mapToGlobal(handle->rect().center());
    const auto destination = host.mapToGlobal(QPoint(30, 16));
    moveMouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    moveMouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    QVERIFY(host.isDragging());
    QCOMPARE(host.stripOrder(0), QStringList{"first"});
    QCOMPARE(host.stripOrder(1), QStringList{"second"});
    moveMouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!host.isDragging()); QCOMPARE(host.saveLayout(), beforeDrag);
    // Installing a second policy in an occupied row is rejected too.
    host.addStrip("third", "Third", new QLabel("Three"), 0);
    const auto three = host.saveLayout();
    QVERIFY(!host.setStripConstraints("third", {true, true, 96, true}));
    QCOMPARE(host.saveLayout(), three);
}

void ToolbarPolicyTest::policyRequestsDuringDragUseTheCommittedRows()
{
    CommandStripHost host;
    host.addStrip("required", "Required", new QLabel("Owner"), 0);
    host.addStrip("ordinary", "Ordinary", new QLabel("Tools"), 0);
    host.addStrip("other-owner", "Other", new QLabel("Other owner"), 1);
    QVERIFY(host.setStripConstraints("required", {true, true, 96, true}));
    host.setLocked(false); host.resize(800, 64); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto before = host.saveLayout();
    auto* handle = host.stripWidget("required")->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const auto start = handle->mapToGlobal(handle->rect().center());
    const auto destination = host.mapToGlobal(QPoint(30, 48));
    moveMouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    moveMouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    QVERIFY(host.isDragging()); QVERIFY(!host.stripOrder(0).contains("required"));
    QVERIFY(!host.isStripSizingEditable("ordinary"));
    // Validation must use the rows that cancellation will restore, not the
    // preview row temporarily vacated by its owner.
    QVERIFY(!host.setStripSizing("ordinary", CommandStripHost::StripSizing::Elastic));
    QVERIFY(!host.setStripConstraints("ordinary", {true, true, 96, true}));
    QVERIFY(host.isDragging()); QCOMPARE(host.saveLayout(), before);
    QTest::keyClick(&host, Qt::Key_Escape);
    QVERIFY(host.setStripConstraints("other-owner", {true, true, 96, true}));
    // Neither the drag preview nor a simultaneous API request may merge two
    // required owners into one row.
    const auto guarded = host.saveLayout();
    moveMouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    moveMouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    QVERIFY(host.isDragging());
    QVERIFY(!host.moveStrip("other-owner", 0, 0));
    QVERIFY(host.isDragging()); QCOMPARE(host.saveLayout(), guarded);
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host.saveLayout(), guarded);
}

void ToolbarPolicyTest::savedContentTitleRecoversMenuResetAndRightAnchoring()
{
    WindowManager manager;
    const auto id = manager.createWindow(WindowKind::Workbench, {"Regression", QSize(1319, 843)});
    manager.addTab(id, new QLabel("Persistent content"), {"Content", "test.label", {}});
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* host = window->strips();
    const int naturalMenu = host->stripWidget("menu")->width();
    // A minimal reproduction of the reported saved strip layout, without
    // importing any private session IDs, content or user configuration.
    auto old = host->saveLayout();
    old.insert("locked", false);
    old.insert("rows", QJsonArray{QJsonArray{"workspace", "title", "menu", "caption-controls"}, QJsonArray{}});
    auto values = old.value("strips").toObject();
    for (const auto& strip : {"workspace", "title", "menu", "caption-controls"}) {
        auto entry = values.value(strip).toObject(); entry.insert("sizing", "content");
        entry.insert("width", strip == QString("title") ? 717 : strip == QString("caption-controls") ? 531 : -1);
        if (strip == QString("menu")) entry.insert("naturalWidthPriority", true);
        values.insert(strip, entry);
    }
    old.insert("strips", values); window->restoreStripLayout(old);
    QCOMPARE(host->stripSizing("title"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(host->stripSizing("menu"), CommandStripHost::StripSizing::Content);
    QVERIFY(host->setStripWidth("menu", 60));
    const int tail = host->stripWidget("menu")->geometry().right();
    const auto controls = host->stripWidget("caption-controls")->geometry();
    QVERIFY(manager.execute("window.toolbar.resetSize", {{"strip", "menu"}}, id));
    // Unlocking adds the 12-pixel handle to the menu's natural width.
    QCOMPARE(host->stripWidget("menu")->width(), naturalMenu + 12);
    QVERIFY(host->overflowItems("menu").isEmpty());
    QCOMPARE(host->stripWidget("menu")->geometry().right(), tail);
    QCOMPARE(host->stripWidget("caption-controls")->geometry(), controls);
    auto* caption = window->findChild<QLabel*>("CaptionTitle"); QVERIFY(caption); QVERIFY(caption->width() >= 96);
    const auto migrated = host->saveLayout();
    QCOMPARE(migrated.value("strips").toObject().value("title").toObject().value("sizing"), QJsonValue("elastic"));
    host->restoreLayout(migrated); QCOMPARE(host->saveLayout(), migrated);
    const int rightInset = host->width() - host->stripWidget("caption-controls")->geometry().right();
    window->resize(window->width() + 180, window->height()); QCoreApplication::processEvents();
    QCOMPARE(host->width() - host->stripWidget("caption-controls")->geometry().right(), rightInset);
}

void ToolbarPolicyTest::windowTitlePolicyCannotBeChanged_data()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false; QTest::newRow("floating") << true;
}
void ToolbarPolicyTest::windowTitlePolicyCannotBeChanged()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto root = manager.createWindow();
    const auto id = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(window->strips()->stripConstraints("title").elasticRequired);
    const auto before = window->saveStripLayout();
    QVERIFY(!manager.execute("window.toolbar.setSizing", {{"strip", "title"}, {"sizing", "content"}}, id));
    QCOMPARE(window->saveStripLayout(), before);
    if (!floating) {
        QVERIFY(!manager.execute("window.toolbar.setSizing", {{"strip", "menu"}, {"sizing", "elastic"}}, id));
        QVERIFY(window->strips()->moveStrip("menu", 1, 0));
        QVERIFY(manager.execute("window.toolbar.setSizing", {{"strip", "menu"}, {"sizing", "elastic"}}, id));
    }
}

QTEST_MAIN(ToolbarPolicyTest)
#include "toolbar_policy_test.moc"
