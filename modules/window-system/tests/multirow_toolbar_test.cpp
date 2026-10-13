// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>
#include <window_system/theme.hpp>

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <limits>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

using namespace window_system;
using Area = CommandStripHost::Area;

namespace {

struct Fixture {
    QWidget window;
    CommandStripHost* host = new CommandStripHost(&window);
    QWidget* body = new QWidget(&window);
    Fixture()
    {
        auto* layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
        layout->addWidget(host); layout->addWidget(body, 1);
        host->bottomArea()->setParent(&window);
        layout->addWidget(host->bottomArea()); host->bottomArea()->show();
        auto* status = new QLabel("Status", &window); status->setFixedHeight(20);
        layout->addWidget(status);
        window.resize(800, 460);
        applyTheme(&window, defaultTheme());
    }
    void settle() { window.layout()->activate(); QCoreApplication::processEvents(); }
};

void mouse(QWidget* target, QEvent::Type type, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, target->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}
void begin(CommandStripHost* host, const QString& id, QPoint destination)
{
    auto* handle = host->stripWidget(id)->findChild<QWidget*>("StripHandle");
    const auto start = handle->mapToGlobal(handle->rect().center());
    mouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
}
QList<QWidget*> visibleRows(QWidget* area)
{
    QList<QWidget*> result;
    for (auto* row : area->findChildren<QWidget*>("CommandStripRow", Qt::FindDirectChildrenOnly))
        if (!row->isHidden() && row->height()) result.append(row);
    return result;
}
void addAreas()
{
    QTest::addColumn<bool>("bottom");
    QTest::newRow("top") << false;
    QTest::newRow("bottom") << true;
}

} // namespace

class MultirowToolbarTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void arbitraryRowsGrowFromTheOuterEdge_data() { addAreas(); }
    void arbitraryRowsGrowFromTheOuterEdge();
    void vacantRowsDoNotLeaveBlankSpace_data() { addAreas(); }
    void vacantRowsDoNotLeaveBlankSpace();
    void crossAreaMovementPreservesPresentationAndCustomization();
    void compactRowsExpandForContentAndFonts();
    void publicMovementPreservesIdentityAndAppendsRows();
    void publicMovementRejectsUnboundedAndInvalidRequestsWithoutMutation();
    void bothAreasRoundTripAndRejectAmbiguousLayouts();
    void sparseLayoutsKeepLogicalRowsWithoutAllocatingEmptyWidgets_data();
    void sparseLayoutsKeepLogicalRowsWithoutAllocatingEmptyWidgets();
    void draggingOverEarlierRowsDoesNotReserveAPlaceholder_data() { addAreas(); }
    void draggingOverEarlierRowsDoesNotReserveAPlaceholder();
    void emptyBottomIsDiscoveredWithoutPreallocatingSpace();
    void dragCreatesOnlyTemporaryInnermostRows_data() { addAreas(); }
    void dragCreatesOnlyTemporaryInnermostRows();
    void draggingAcrossAreasUsesTheSameLiveStrip_data() { addAreas(); }
    void draggingAcrossAreasUsesTheSameLiveStrip();
    void aNewRowCanBeCommittedBeyondTheFormerSecondRow_data() { addAreas(); }
    void aNewRowCanBeCommittedBeyondTheFormerSecondRow();
};

void MultirowToolbarTest::arbitraryRowsGrowFromTheOuterEdge()
{
    QFETCH(bool, bottom);
    Fixture fixture; auto* host = fixture.host;
    const auto area = bottom ? Area::Bottom : Area::Top;
    for (int index = 0; index < 6; ++index) {
        const auto id = QString::number(index);
        host->addStrip(id, new QLabel(id), index);
        if (bottom) QVERIFY(host->moveStrip(id, area, index, 0));
    }
    fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    QCOMPARE(host->rowCount(area), 6);
    auto* dock = bottom ? host->bottomArea() : host;
    const auto rows = visibleRows(dock); QCOMPARE(rows.size(), 6);
    QCOMPARE(dock->height(), 168);
    for (int index = 0; index < 6; ++index) {
        QCOMPARE(host->stripOrder(area, index), QStringList{QString::number(index)});
        const auto* strip = host->stripWidget(QString::number(index));
        QCOMPARE(strip->height(), 28);
        QCOMPARE(strip->parentWidget()->y(), bottom ? (5 - index) * 28 : index * 28);
    }
}
void MultirowToolbarTest::vacantRowsDoNotLeaveBlankSpace()
{
    QFETCH(bool, bottom);
    Fixture fixture; auto* host = fixture.host;
    const auto area = bottom ? Area::Bottom : Area::Top;
    for (int index = 0; index < 4; ++index) {
        const auto id = QString::number(index);
        host->addStrip(id, new QLabel(id), index);
        if (bottom) QVERIFY(host->moveStrip(id, area, index, 0));
    }
    fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    auto* dock = bottom ? host->bottomArea() : host;
    QCOMPARE(dock->height(), 112);
    QVERIFY(host->moveStrip("1", area, 0, 1)); fixture.settle();
    QCOMPARE(dock->height(), 84); QCOMPARE(visibleRows(dock).size(), 3);
    QVERIFY(host->setStripVisible("2", false)); fixture.settle();
    QCOMPARE(dock->height(), 56); QCOMPARE(visibleRows(dock).size(), 2);
    QVERIFY(host->moveStrip("3", bottom ? Area::Top : Area::Bottom, 0, 0)); fixture.settle();
    QCOMPARE(dock->height(), 28); QCOMPARE(visibleRows(dock).size(), 1);
    QVERIFY(host->setStripVisible("0", false)); QVERIFY(host->setStripVisible("1", false)); fixture.settle();
    QCOMPARE(dock->height(), 0); QVERIFY(visibleRows(dock).isEmpty());
    QVERIFY(host->setStripVisible("2", true)); fixture.settle();
    QCOMPARE(dock->height(), 28); QCOMPARE(visibleRows(dock).size(), 1);
}
void MultirowToolbarTest::crossAreaMovementPreservesPresentationAndCustomization()
{
    Fixture fixture; auto* host = fixture.host;
    QList<QAction*> actions;
    for (int index = 0; index < 4; ++index) {
        auto* action = new QAction(QString::number(index), host);
        action->setObjectName(QString::number(index)); actions.append(action);
    }
    host->addActionStrip("tools", "Tools", actions, 0);
    host->setLocked(false); QVERIFY(host->setStripItemVisible("tools", "1", false));
    QVERIFY(host->setStripWidth("tools", 65));
    QPointer<QWidget> original = host->stripWidget("tools");
    QPointer<QWidget> viewport = original->findChild<QWidget*>("StripViewport");
    const auto originalSettings = host->saveLayout().value("strips").toObject().value("tools");
    QVERIFY(host->moveStrip("tools", Area::Bottom, 3, 0)); fixture.settle();
    QCOMPARE(host->stripWidget("tools"), original.data());
    QCOMPARE(original->findChild<QWidget*>("StripViewport"), viewport.data());
    QCOMPARE(host->stripArea("tools"), Area::Bottom);
    QVERIFY(!host->isStripItemVisible("tools", "1"));
    QCOMPARE(host->saveLayout().value("strips").toObject().value("tools"), originalSettings);
    QVERIFY(host->moveStrip("tools", Area::Top, 4, 0)); fixture.settle();
    QCOMPARE(host->stripWidget("tools"), original.data());
    QCOMPARE(host->stripArea("tools"), Area::Top);
    QCOMPARE(host->bottomArea()->height(), 0);
}
void MultirowToolbarTest::compactRowsExpandForContentAndFonts()
{
    Fixture fixture; auto* host = fixture.host;
    host->addStrip("ordinary", new QLabel("Text"), 0);
    QCOMPARE(host->stripWidget("ordinary")->height(), 28);
    auto* tall = new QWidget;
    tall->setMinimumHeight(32);
    auto* row = new QHBoxLayout(tall); row->setContentsMargins(0, 0, 0, 0);
    auto* control = new QToolButton(tall); control->setFixedSize(42, 32); row->addWidget(control);
    host->addStrip("protected", tall, 1);
    QCOMPARE(host->stripWidget("protected")->height(), 32);
    QVERIFY(host->moveStrip("ordinary", Area::Bottom, 0, 0));
    auto font = host->font(); font.setPointSize(24); host->setFont(font); fixture.settle();
    QVERIFY(host->stripWidget("ordinary")->height() >= host->fontMetrics().height() + 8);
    QVERIFY(host->stripWidget("protected")->height() >= 32);
}
void MultirowToolbarTest::publicMovementPreservesIdentityAndAppendsRows()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* content = new QLabel("Content");
    const auto tab = manager.addTab(id, content, {"Content", "test.label", {}});
    QVERIFY(!tab.isNull());
    const auto pane = manager.model().tab(tab)->activePane;
    auto* view = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(view);
    auto* host = view->strips(); QVERIFY(host);
    QPointer<QWidget> strip = host->stripWidget("workspace"); QVERIFY(strip);
    QPointer<QWidget> viewport = strip->findChild<QWidget*>("StripViewport"); QVERIFY(viewport);
    const auto settings = host->saveLayout().value("strips").toObject().value("workspace");
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QVERIFY(manager.moveToolbar(id, "workspace", ToolbarArea::Bottom, 0));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(host->stripOrder(Area::Bottom, 0), QStringList{"workspace"});
    QCOMPARE(host->stripWidget("workspace"), strip.data());
    QCOMPARE(strip->findChild<QWidget*>("StripViewport"), viewport.data());
    QCOMPARE(host->saveLayout().value("strips").toObject().value("workspace"), settings);
    QCOMPARE(manager.paneWidget(pane), content);
    changes.clear();
    QVERIFY(manager.moveToolbar(id, "menu", ToolbarArea::Bottom, 0, 0));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(host->stripOrder(Area::Bottom, 0), QStringList({"menu", "workspace"}));
    QVERIFY(manager.moveToolbar(id, "workspace", ToolbarArea::Top, 2));
    QCOMPARE(host->stripOrder(Area::Top, 2), QStringList{"workspace"});
    QVERIFY(manager.moveToolbar(id, "menu", ToolbarArea::Top, 2, 0));
    QCOMPARE(host->stripOrder(Area::Top, 2), QStringList({"menu", "workspace"}));
    QCOMPARE(host->bottomArea()->height(), 0);
    QCOMPARE(host->stripWidget("workspace"), strip.data());
    QCOMPARE(manager.paneWidget(pane), content);
}
void MultirowToolbarTest::publicMovementRejectsUnboundedAndInvalidRequestsWithoutMutation()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* view = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(view);
    auto* host = view->strips(); QVERIFY(host);
    host->addStrip("required", new QLabel("Other required owner"), 1);
    QVERIFY(host->setStripConstraints("required", {true, true, 96, true}));
    const auto saved = manager.saveLayout();
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    const auto rejected = [&](WindowId window, const QString& strip, ToolbarArea area, int row, int index) {
        QVERIFY(!manager.moveToolbar(window, strip, area, row, index));
        QCOMPARE(manager.saveLayout(), saved);
        QCOMPARE(changes.count(), 0);
    };
    rejected({}, "workspace", ToolbarArea::Top, 0, -1);
    rejected(id, "missing", ToolbarArea::Top, 0, -1);
    rejected(id, "workspace", ToolbarArea(123), 0, -1);
    rejected(id, "workspace", ToolbarArea::Top, -1, -1);
    rejected(id, "workspace", ToolbarArea::Top, 0, -2);
    rejected(id, "workspace", ToolbarArea::Top, std::numeric_limits<int>::max(), -1);
    rejected(id, "workspace", ToolbarArea::Top, 3, -1);
    rejected(id, "workspace", ToolbarArea::Bottom, 1, -1);
    rejected(id, "workspace", ToolbarArea::Top, 0, std::numeric_limits<int>::max());
    rejected(id, "workspace", ToolbarArea::Bottom, 0, 1);
    rejected(id, "required", ToolbarArea::Top, 0, -1);
}
void MultirowToolbarTest::bothAreasRoundTripAndRejectAmbiguousLayouts()
{
    CommandStripHost source;
    for (int index = 0; index < 6; ++index) source.addStrip(QString::number(index), new QLabel("Text"), index);
    QVERIFY(source.moveStrip("1", Area::Bottom, 0, 0));
    QVERIFY(source.moveStrip("3", Area::Bottom, 2, 0));
    QVERIFY(source.moveStrip("5", Area::Bottom, 4, 0));
    source.setLocked(false); QVERIFY(source.setStripVisible("3", false));
    const auto saved = source.saveLayout();
    QCOMPARE(saved.value("rows").toArray().size(), 5);
    QCOMPARE(saved.value("bottomRows").toArray().size(), 5);
    CommandStripHost target;
    for (int index = 0; index < 6; ++index) target.addStrip(QString::number(index), new QLabel("Text"), 0);
    target.restoreLayout(saved); QCOMPARE(target.saveLayout(), saved);
    QCOMPARE(target.stripArea("5"), Area::Bottom);
    auto bad = saved; bad.insert("bottomRows", QJsonArray{QJsonArray{"0"}});
    target.restoreLayout(bad); QCOMPARE(target.saveLayout(), saved);
    bad = saved; bad.insert("bottomRows", QJsonArray{42});
    target.restoreLayout(bad); QCOMPARE(target.saveLayout(), saved);
    auto legacy = saved; legacy.remove("bottomRows");
    legacy.insert("rows", QJsonArray{QJsonArray{"0", "1", "2"}, QJsonArray{"3", "4", "5"}});
    target.restoreLayout(legacy);
    QCOMPARE(target.stripOrder(0), QStringList({"0", "1", "2"}));
    QCOMPARE(target.stripOrder(1), QStringList({"3", "4", "5"}));
    QCOMPARE(target.rowCount(Area::Bottom), 0);
}
void MultirowToolbarTest::sparseLayoutsKeepLogicalRowsWithoutAllocatingEmptyWidgets_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("unknown");
    for (bool bottom : {false, true}) for (bool unknown : {false, true})
        QTest::newRow(qPrintable(QString("%1-%2").arg(bottom ? "bottom" : "top", unknown ? "unknown" : "empty")))
            << bottom << unknown;
}
void MultirowToolbarTest::sparseLayoutsKeepLogicalRowsWithoutAllocatingEmptyWidgets()
{
    QFETCH(bool, bottom); QFETCH(bool, unknown);
    constexpr int count = 100000;
    Fixture fixture; auto* host = fixture.host;
    host->addStrip("early", new QLabel("Early"), 0);
    host->addStrip("late", new QLabel("Late"), 1);
    host->setLocked(false);
    QPointer<QWidget> early = host->stripWidget("early");
    QPointer<QWidget> late = host->stripWidget("late");
    auto layout = host->saveLayout();
    QJsonArray rows;
    for (int index = 0; index < count; ++index)
        rows.append(unknown ? QJsonArray{QString("not-installed.%1").arg(index)} : QJsonArray{});
    rows[5] = QJsonArray{"early"}; rows[count - 1] = QJsonArray{"late"};
    layout.insert("rows", bottom ? QJsonArray{QJsonArray{}, QJsonArray{}} : rows);
    if (bottom) layout.insert("bottomRows", rows);
    else layout.remove("bottomRows");
    host->restoreLayout(layout);
    fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    const Area area = bottom ? Area::Bottom : Area::Top;
    const Area opposite = bottom ? Area::Top : Area::Bottom;
    auto* dock = bottom ? host->bottomArea() : host;
    QCOMPARE(host->stripOrder(area, 5), QStringList{"early"});
    QCOMPARE(host->stripOrder(area, count - 1), QStringList{"late"});
    QCOMPARE(host->rowCount(area), 2); QCOMPARE(dock->height(), 56);
    QCOMPARE(fixture.window.findChildren<QWidget*>("CommandStripRow").size(), 2);
    QCOMPARE(early->parentWidget()->y(), bottom ? 28 : 0);
    QCOMPARE(late->parentWidget()->y(), bottom ? 0 : 28);
    int exposedRows = 0;
    for (auto* widget : host->windowDragAreas()) {
        QVERIFY(widget);
        if (widget->objectName() == "CommandStripRow") ++exposedRows;
    }
    QCOMPARE(exposedRows, 2);
    QVERIFY(host->moveStrip("late", area, count - 2, 0)); fixture.settle();
    QCOMPARE(host->stripOrder(area, count - 2), QStringList{"late"});
    QVERIFY(host->stripOrder(area, count - 1).isEmpty());
    QCOMPARE(host->stripWidget("late"), late.data());
    QCOMPARE(fixture.window.findChildren<QWidget*>("CommandStripRow").size(), 2);
    QVERIFY(host->moveStrip("early", opposite, 0, 0)); fixture.settle();
    QCOMPARE(host->stripWidget("early"), early.data());
    QCOMPARE(host->rowCount(area), 1); QCOMPARE(host->rowCount(opposite), 1);
    QCOMPARE(fixture.window.findChildren<QWidget*>("CommandStripRow").size(), 2);
    QVERIFY(host->setStripVisible("late", false)); fixture.settle();
    QCOMPARE(fixture.window.findChildren<QWidget*>("CommandStripRow").size(), 1);
    QVERIFY(host->setStripVisible("late", true)); fixture.settle();
    QCOMPARE(host->stripWidget("late"), late.data());
    QCOMPARE(fixture.window.findChildren<QWidget*>("CommandStripRow").size(), 2);
    const auto saved = host->saveLayout();
    const auto target = late->parentWidget()->mapToGlobal(QPoint(80, 14));
    begin(host, "early", target); fixture.settle(); QVERIFY(host->isDragging());
    // Sparse slots do not turn into controls even while a new drop row is
    // exposed. The original content widgets survive source-row disposal.
    QVERIFY(fixture.window.findChildren<QWidget*>("CommandStripRow").size() <= 3);
    QCOMPARE(host->stripWidget("early"), early.data()); QCOMPARE(host->stripWidget("late"), late.data());
    for (auto* widget : host->windowDragAreas()) QVERIFY(widget);
    QTest::keyClick(host, Qt::Key_Escape); fixture.settle();
    QCOMPARE(host->saveLayout(), saved);
    QCOMPARE(fixture.window.findChildren<QWidget*>("CommandStripRow").size(), 2);
    QCOMPARE(host->stripOrder(area, count - 2), QStringList{"late"});
}
void MultirowToolbarTest::draggingOverEarlierRowsDoesNotReserveAPlaceholder()
{
    QFETCH(bool, bottom);
    Fixture fixture; auto* host = fixture.host;
    const Area area = bottom ? Area::Bottom : Area::Top;
    for (int index = 0; index < 3; ++index) {
        const auto id = QString::number(index); host->addStrip(id, new QLabel(id), index);
        if (bottom) QVERIFY(host->moveStrip(id, area, index, 0));
    }
    host->addStrip("moving", new QLabel("Moving"), 0);
    if (bottom) QVERIFY(host->moveStrip("moving", area, 0, 1));
    host->setLocked(false); fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    auto* dock = bottom ? host->bottomArea() : host;
    auto* opposite = bottom ? static_cast<QWidget*>(host) : host->bottomArea();
    const auto saved = host->saveLayout();
    const auto earlier = host->stripWidget("0")->parentWidget()->mapToGlobal(QPoint(80, 14));
    begin(host, "moving", earlier); fixture.settle();
    QVERIFY(host->isDragging()); QCOMPARE(dock->height(), 84); QCOMPARE(visibleRows(dock).size(), 3);
    QCOMPARE(opposite->height(), 0); QCOMPARE(host->saveLayout(), saved);
    const auto last = host->stripWidget("2")->parentWidget()->mapToGlobal(QPoint(80, 14));
    mouse(host, QEvent::MouseMove, last, Qt::NoButton, Qt::LeftButton); fixture.settle();
    QCOMPARE(dock->height(), 112); QCOMPARE(visibleRows(dock).size(), 4);
    QCOMPARE(opposite->height(), 0);
    QTest::keyClick(host, Qt::Key_Escape); fixture.settle();
    QCOMPARE(dock->height(), 84); QCOMPARE(visibleRows(dock).size(), 3); QCOMPARE(host->saveLayout(), saved);
}
void MultirowToolbarTest::emptyBottomIsDiscoveredWithoutPreallocatingSpace()
{
    Fixture fixture; auto* host = fixture.host;
    host->addStrip("moving", new QLabel("Moving"), 0);
    host->addStrip("stays", new QLabel("Stays"), 0);
    host->setLocked(false); fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    const auto saved = host->saveLayout();
    QCOMPARE(host->bottomArea()->height(), 0);
    begin(host, "moving", fixture.body->mapToGlobal(fixture.body->rect().center())); fixture.settle();
    QVERIFY(host->isDragging()); QCOMPARE(host->height(), 28); QCOMPARE(host->bottomArea()->height(), 0);
    const auto target = host->bottomArea()->mapToGlobal(QPoint(8, -14));
    mouse(host, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton); fixture.settle();
    QCOMPARE(host->bottomArea()->height(), 28); QCOMPARE(visibleRows(host->bottomArea()).size(), 1);
    QCOMPARE(host->height(), 28); QCOMPARE(visibleRows(host).size(), 1);
    QCOMPARE(host->saveLayout(), saved);
    QTest::keyClick(host, Qt::Key_Escape); fixture.settle();
    QCOMPARE(host->bottomArea()->height(), 0); QVERIFY(visibleRows(host->bottomArea()).isEmpty());
    QCOMPARE(host->saveLayout(), saved); QVERIFY(host->stripWidget("moving")->isVisible());
}
void MultirowToolbarTest::dragCreatesOnlyTemporaryInnermostRows()
{
    QFETCH(bool, bottom);
    Fixture fixture; auto* host = fixture.host;
    host->addStrip("first", new QLabel("First"), 0);
    host->addStrip("second", new QLabel("Second"), 1);
    if (bottom) {
        QVERIFY(host->moveStrip("first", Area::Bottom, 0, 0));
        QVERIFY(host->moveStrip("second", Area::Bottom, 1, 0));
    }
    host->setLocked(false); fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    const auto saved = host->saveLayout();
    auto* dock = bottom ? host->bottomArea() : host;
    auto* opposite = bottom ? static_cast<QWidget*>(host) : host->bottomArea();
    const int before = dock->height();
    const auto outside = fixture.body->mapToGlobal(fixture.body->rect().center());
    begin(host, "first", outside); fixture.settle();
    QVERIFY(host->isDragging()); QVERIFY(host->stripWidget("first")->isHidden());
    QCOMPARE(host->saveLayout(), saved);
    // Leaving the source removes its vacant row without allocating any ghost.
    QCOMPARE(dock->height(), before - 28); QCOMPARE(visibleRows(dock).size(), 1);
    QCOMPARE(opposite->height(), 0);
    const auto last = dock->mapToGlobal(QPoint(80, 14));
    mouse(host, QEvent::MouseMove, last, Qt::NoButton, Qt::LeftButton); fixture.settle();
    QCOMPARE(dock->height(), before); QCOMPARE(visibleRows(dock).size(), 2);
    QCOMPARE(opposite->height(), 0);
    int blank = 0;
    for (auto* row : visibleRows(dock))
        if (row->findChildren<QWidget*>("CommandStrip", Qt::FindDirectChildrenOnly).isEmpty()) ++blank;
    QCOMPARE(blank, 1);
    mouse(host, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton); fixture.settle();
    QCOMPARE(dock->height(), before - 28); QCOMPARE(visibleRows(dock).size(), 1);
    QTest::keyClick(host, Qt::Key_Escape); fixture.settle();
    QVERIFY(!host->isDragging()); QCOMPARE(host->saveLayout(), saved);
    QCOMPARE(dock->height(), before); QCOMPARE(visibleRows(dock).size(), 2);
    QCOMPARE(host->bottomArea()->height(), bottom ? before : 0);
}
void MultirowToolbarTest::draggingAcrossAreasUsesTheSameLiveStrip()
{
    QFETCH(bool, bottom);
    Fixture fixture; auto* host = fixture.host;
    host->addStrip("moving", new QLabel("Moving"), 0);
    host->addStrip("stays", new QLabel("Stays"), 0);
    if (bottom) QVERIFY(host->moveStrip("moving", Area::Bottom, 0, 0));
    host->setLocked(false); fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    const Area destinationArea = bottom ? Area::Top : Area::Bottom;
    auto* destination = bottom ? static_cast<QWidget*>(host) : host->bottomArea();
    QPointer<QWidget> original = host->stripWidget("moving");
    begin(host, "moving", fixture.body->mapToGlobal(fixture.body->rect().center())); fixture.settle(); QVERIFY(host->isDragging());
    const auto point = destination->mapToGlobal(QPoint(8, destination->height() ? destination->height() / 2 : -14));
    mouse(host, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton); fixture.settle();
    mouse(host, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton); fixture.settle();
    QVERIFY(!host->isDragging()); QCOMPARE(host->stripArea("moving"), destinationArea);
    QCOMPARE(host->stripWidget("moving"), original.data()); QVERIFY(original->isVisible());
    QCOMPARE(host->rowCount(Area::Bottom), bottom ? 0 : 1);
    for (auto* row : visibleRows(host)) QVERIFY(!row->findChildren<QWidget*>("CommandStrip", Qt::FindDirectChildrenOnly).isEmpty());
    for (auto* row : visibleRows(host->bottomArea())) QVERIFY(!row->findChildren<QWidget*>("CommandStrip", Qt::FindDirectChildrenOnly).isEmpty());
}
void MultirowToolbarTest::aNewRowCanBeCommittedBeyondTheFormerSecondRow()
{
    QFETCH(bool, bottom);
    Fixture fixture; auto* host = fixture.host;
    for (int index = 0; index < 4; ++index) {
        const auto id = QString::number(index); host->addStrip(id, new QLabel(id), index);
        if (bottom) QVERIFY(host->moveStrip(id, Area::Bottom, index, 0));
    }
    host->addStrip("moving", new QLabel("Moving"), 0);
    host->setLocked(false); fixture.window.show(); QVERIFY(QTest::qWaitForWindowExposed(&fixture.window)); fixture.settle();
    begin(host, "moving", fixture.body->mapToGlobal(fixture.body->rect().center())); fixture.settle();
    auto* dock = bottom ? host->bottomArea() : host;
    const int targetY = bottom ? -14 : dock->height() + 14;
    const auto point = dock->mapToGlobal(QPoint(8, targetY));
    mouse(host, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton); fixture.settle();
    mouse(host, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton); fixture.settle();
    QVERIFY(!host->isDragging());
    QCOMPARE(host->stripOrder(bottom ? Area::Bottom : Area::Top, 4), QStringList{"moving"});
    QCOMPARE(host->rowCount(bottom ? Area::Bottom : Area::Top), 5);
    QCOMPARE(dock->height(), 140); QCOMPARE(visibleRows(dock).size(), 5);
}

QTEST_MAIN(MultirowToolbarTest)
#include "multirow_toolbar_test.moc"
